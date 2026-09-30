// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// climat-cli, run as a script would run it.
//
// The binary, not its functions: every case here starts a process with an
// argv and reads what came out, because that is the interface. A script that
// parses `--json` today should parse the same shape after this file has been
// edited, and the way to promise that is to parse it here.
//
// Fixture mode throughout, so nothing opens a socket and every number is one
// tests/fixtures already carries. The reader's preferences come from an INI
// this test writes into a config directory of its own, because the whole
// point of the text output is that it follows them.

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QtTest>

namespace {

struct Outcome {
    int        exitCode = -1;
    QByteArray stdOut;
    QByteArray stdErr;
};

} // namespace

class TestCli : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void nowPrintsThePlaceAndAReading();
    void aWarningNamesTheDayItEndsAndIsPrintedOnce();
    void nowAsJsonIsCanonicalAndShaped();
    void hourlyCountsHours();
    void dailyCountsDaysAndStartsToday();
    void placesListsTheFixturePlace();
    void theReadersUnitsAreHonouredInTextAndNotInJson();
    void theOverrideBeatsThePreference();
    void theObservationIsNotAStaleCurrentBlock();
    void theHourlySeriesIsReadTheWayTheAppReadsIt();
    void theConditionAgreesWithTheHourEvenOnAFreshBlock();
    void aPlaceCannotBeChosenAgainstAFixture();
    void anUnknownCommandIsAUsageError();
    void jsonAndCsvTogetherIsAUsageError();

private:
    Outcome run(const QStringList &arguments, const QByteArray &ini = {});

    QTemporaryDir m_config;
};

void TestCli::initTestCase()
{
    QVERIFY2(QFile::exists(QStringLiteral(CLIMAT_CLI_BINARY)),
             "climat-cli was not built at " CLIMAT_CLI_BINARY);
    QVERIFY(m_config.isValid());
}

Outcome TestCli::run(const QStringList &arguments, const QByteArray &ini)
{
    // The INI where QSettings will look for it under XDG_CONFIG_HOME: the
    // organisation's directory, the application's file. Written fresh - or
    // removed - for every run so that a case cannot inherit another's.
    const QString directory = m_config.path() + QStringLiteral("/Climat");
    const QString file      = directory + QStringLiteral("/climat.ini");
    QDir().mkpath(directory);
    QFile::remove(file);
    if (!ini.isEmpty()) {
        QFile out(file);
        if (out.open(QIODevice::WriteOnly))
            out.write(ini);
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_config.path());
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("TZ"), QStringLiteral("UTC"));

    QProcess process;
    process.setProcessEnvironment(env);
    process.start(QStringLiteral(CLIMAT_CLI_BINARY), arguments);

    Outcome outcome;
    if (!process.waitForFinished(20000)) {
        outcome.stdErr = QByteArrayLiteral("climat-cli did not finish in 20 s");
        process.kill();
        return outcome;
    }
    outcome.exitCode = process.exitCode();
    outcome.stdOut   = process.readAllStandardOutput();
    outcome.stdErr   = process.readAllStandardError();
    return outcome;
}

void TestCli::nowPrintsThePlaceAndAReading()
{
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"), QStringLiteral("now") });
    QCOMPARE(got.exitCode, 0);

    const QString text = QString::fromUtf8(got.stdOut);
    QVERIFY2(text.contains(QStringLiteral("Toronto")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("°C")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("Updated")), qPrintable(text));
    QVERIFY2(text.contains(QStringLiteral("fixture")), qPrintable(text));
}

// Seattle's heat advisory ends at 10 PM on Thursday and the fixture is recorded
// on Wednesday afternoon. "until 10:00 PM" read as tonight. Its three Air Quality
// Alerts differ only in the counties they cover, which this output does not
// print, so they were three identical lines.
void TestCli::aWarningNamesTheDayItEndsAndIsPrintedOnce()
{
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("seattle"), QStringLiteral("now") });
    QCOMPARE(got.exitCode, 0);
    const QString text = QString::fromUtf8(got.stdOut);

    // Either clock: the day is the point, and this run has no preference file.
    QVERIFY2(text.contains(QStringLiteral("! Heat Advisory (Moderate) · until Thu 10:00 PM\n"))
                 || text.contains(QStringLiteral("! Heat Advisory (Moderate) · until Thu 22:00\n")),
             qPrintable(text));
    QCOMPARE(text.count(QStringLiteral("Air Quality Alert")), 1);
    QVERIFY2(text.contains(QStringLiteral(" · 3 notices\n")), qPrintable(text));
}

void TestCli::nowAsJsonIsCanonicalAndShaped()
{
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"), QStringLiteral("now"),
                              QStringLiteral("--json") });
    QCOMPARE(got.exitCode, 0);

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(got.stdOut, &error);
    QVERIFY2(error.error == QJsonParseError::NoError, qPrintable(error.errorString()));

    const QJsonObject root = document.object();
    QCOMPARE(root.value(QStringLiteral("place")).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("Toronto"));
    QVERIFY(root.contains(QStringLiteral("servedBy")));
    QVERIFY(root.contains(QStringLiteral("fetchedAt")));
    QVERIFY(root.value(QStringLiteral("alerts")).isArray());

    const QJsonObject current = root.value(QStringLiteral("current")).toObject();
    QVERIFY(current.value(QStringLiteral("temperature")).isDouble());
    // Canonical: a Toronto summer afternoon is tens of degrees, not seventies.
    const double temperature = current.value(QStringLiteral("temperature")).toDouble();
    QVERIFY2(temperature > -40 && temperature < 45, qPrintable(QString::number(temperature)));
    QVERIFY(current.value(QStringLiteral("time")).toString().contains(QLatin1Char('T')));
    QVERIFY(!current.value(QStringLiteral("condition")).toString().isEmpty());
}

void TestCli::hourlyCountsHours()
{
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                              QStringLiteral("hourly"), QStringLiteral("3"), QStringLiteral("--csv") });
    QCOMPARE(got.exitCode, 0);

    const QList<QByteArray> lines = got.stdOut.trimmed().split('\n');
    QCOMPARE(lines.size(), 4); // a header and three hours
    QVERIFY(lines.constFirst().startsWith("time,temperature,"));

    const Outcome text = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                               QStringLiteral("hourly"), QStringLiteral("5") });
    QCOMPARE(text.exitCode, 0);
    QCOMPARE(text.stdOut.trimmed().split('\n').size(), 5);
}

void TestCli::dailyCountsDaysAndStartsToday()
{
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                              QStringLiteral("daily"), QStringLiteral("2"), QStringLiteral("--json") });
    QCOMPARE(got.exitCode, 0);

    const QJsonArray days = QJsonDocument::fromJson(got.stdOut).object().value(QStringLiteral("daily")).toArray();
    QCOMPARE(days.size(), 2);

    // The fixture's clock is the recording instant; the first day is that day
    // and not the `past_days` day the series starts on.
    const Outcome text = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                               QStringLiteral("daily"), QStringLiteral("1") });
    QVERIFY2(text.stdOut.startsWith("Today"), text.stdOut.constData());
}

void TestCli::placesListsTheFixturePlace()
{
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"), QStringLiteral("places") });
    QCOMPARE(got.exitCode, 0);
    QVERIFY(got.stdOut.contains("Toronto"));
}

void TestCli::theReadersUnitsAreHonouredInTextAndNotInJson()
{
    const QByteArray fahrenheit = QByteArrayLiteral("[units]\ntemperature=fahrenheit\nwind=mph\n");

    const Outcome text = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"), QStringLiteral("now") },
                             fahrenheit);
    QCOMPARE(text.exitCode, 0);
    QVERIFY2(text.stdOut.contains("\xC2\xB0" "F"), text.stdOut.constData());
    QVERIFY2(text.stdOut.contains("mph"), text.stdOut.constData());

    const Outcome json = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"), QStringLiteral("now"),
                               QStringLiteral("--json") },
                             fahrenheit);
    const double temperature = QJsonDocument::fromJson(json.stdOut).object()
                                   .value(QStringLiteral("current")).toObject()
                                   .value(QStringLiteral("temperature")).toDouble();
    // Still Celsius: a preference in a dialog must not move a number a script
    // reads.
    QVERIFY2(temperature < 45, qPrintable(QString::number(temperature)));
}

void TestCli::theOverrideBeatsThePreference()
{
    const Outcome metric = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                                 QStringLiteral("now"), QStringLiteral("--units"),
                                 QStringLiteral("metric") },
                               QByteArrayLiteral("[units]\ntemperature=fahrenheit\n"));
    QCOMPARE(metric.exitCode, 0);
    QVERIFY2(metric.stdOut.contains("\xC2\xB0" "C"), metric.stdOut.constData());
    QVERIFY(!metric.stdOut.contains("\xC2\xB0" "F"));

    // The other direction, which is the one that cannot pass by accident:
    // metric is already the built-in default, so the row above fails only if
    // the PREFERENCE is read and the override ignored. This one fails if the
    // override is dropped entirely.
    const Outcome imperial = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                                   QStringLiteral("now"), QStringLiteral("--units"),
                                   QStringLiteral("imperial") },
                                 QByteArrayLiteral("[units]\ntemperature=celsius\n"));
    QCOMPARE(imperial.exitCode, 0);
    QVERIFY2(imperial.stdOut.contains("\xC2\xB0" "F"), imperial.stdOut.constData());
    QVERIFY(!imperial.stdOut.contains("\xC2\xB0" "C"));
}

void TestCli::theObservationIsNotAStaleCurrentBlock()
{
    // The bug this repository already fixed once, in the app, and which the
    // first version of this tool reintroduced: Open-Meteo's `current` block is
    // stamped to the quarter hour and a cached response can carry a very old
    // one. toronto's block says 06:30 against a recording at 12:28, so `now`
    // printed 15 °C and "Sunny" while `hourly` led with 23 °C - one process,
    // one file, one instant, eight degrees apart.
    const Outcome now = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                              QStringLiteral("now"), QStringLiteral("--json") });
    QCOMPARE(now.exitCode, 0);

    const QJsonObject current =
        QJsonDocument::fromJson(now.stdOut).object().value(QStringLiteral("current")).toObject();

    const Outcome hourly = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                                 QStringLiteral("hourly"), QStringLiteral("1"),
                                 QStringLiteral("--json") });
    QCOMPARE(hourly.exitCode, 0);

    const QJsonObject standing = QJsonDocument::fromJson(hourly.stdOut).object()
                                     .value(QStringLiteral("hourly")).toArray().at(0).toObject();

    // The same instant described by the same process twice. Not identical -
    // the block is a quarter-hour reading and the row is an hour - but they
    // cannot be a different afternoon.
    const double a = current.value(QStringLiteral("temperature")).toDouble();
    const double b = standing.value(QStringLiteral("temperature")).toDouble();
    QVERIFY2(qAbs(a - b) <= 3.0,
             qPrintable(QStringLiteral("`now` says %1 and `hourly` says %2 for the same "
                                       "instant").arg(a).arg(b)));

    QCOMPARE(current.value(QStringLiteral("condition")).toString(),
             standing.value(QStringLiteral("condition")).toString());
}

void TestCli::theHourlySeriesIsReadTheWayTheAppReadsIt()
{
    // asHourStarting does NOT move a timestamp - it moves the accumulations and
    // the weather code onto the row before, and drops the last point. So a test
    // that compared stamps compared something the conversion never touches and
    // passed either way; this asserts the value that actually moves.
    //
    // kampala is the fixture with drizzle in it. Converted, the 0.4 mm and the
    // "Light drizzle" code sit on 09:00, which is where the app draws them;
    // unconverted they sit on 10:00, an hour later than every screen in the
    // app shows.
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("kampala"),
                              QStringLiteral("hourly"), QStringLiteral("6"),
                              QStringLiteral("--json") });
    QCOMPARE(got.exitCode, 0);

    const QJsonArray hours =
        QJsonDocument::fromJson(got.stdOut).object().value(QStringLiteral("hourly")).toArray();
    QVERIFY(!hours.isEmpty());

    QJsonObject wet;
    for (const QJsonValue &value : hours) {
        const QJsonObject hour = value.toObject();
        if (hour.value(QStringLiteral("precipitation")).toDouble() > 0.0) {
            wet = hour;
            break;
        }
    }
    QVERIFY2(!wet.isEmpty(), "no hour in this window carries any precipitation");

    const QDateTime when =
        QDateTime::fromString(wet.value(QStringLiteral("time")).toString(), Qt::ISODate);
    QVERIFY(when.isValid());
    QCOMPARE(when.time().hour(), 9);
    QCOMPARE(wet.value(QStringLiteral("precipitation")).toDouble(), 0.4);
    QCOMPARE(wet.value(QStringLiteral("condition")).toString(), QStringLiteral("Light drizzle"));
}

void TestCli::theConditionAgreesWithTheHourEvenOnAFreshBlock()
{
    // The other branch of observationAt(), and the one the stale-block test
    // cannot reach. When Open-Meteo's `current` block IS within the hour it is
    // still not the last word on the weather code or the precipitation: the
    // code describes a stretch and the block's rainfall is the PRECEDING hour,
    // where the series is the hour starting. Trusting it whole is how "Mainly
    // sunny" came to sit above a Now column drawing heavy rain.
    //
    // berlin's block is inside the hour of its recording, so this drives the
    // fresh branch; the assertion is that one process does not describe one
    // hour two ways.
    const Outcome now = run({ QStringLiteral("--fixture"), QStringLiteral("berlin"),
                              QStringLiteral("now"), QStringLiteral("--json") });
    QCOMPARE(now.exitCode, 0);

    const QJsonObject current =
        QJsonDocument::fromJson(now.stdOut).object().value(QStringLiteral("current")).toObject();

    const Outcome hourly = run({ QStringLiteral("--fixture"), QStringLiteral("berlin"),
                                 QStringLiteral("hourly"), QStringLiteral("1"),
                                 QStringLiteral("--json") });
    QCOMPARE(hourly.exitCode, 0);

    const QJsonObject standing = QJsonDocument::fromJson(hourly.stdOut).object()
                                     .value(QStringLiteral("hourly")).toArray().at(0).toObject();

    QCOMPARE(current.value(QStringLiteral("weatherCode")).toInt(),
             standing.value(QStringLiteral("weatherCode")).toInt());
    QCOMPARE(current.value(QStringLiteral("condition")).toString(),
             standing.value(QStringLiteral("condition")).toString());
    QCOMPARE(current.value(QStringLiteral("precipitation")).toDouble(),
             standing.value(QStringLiteral("precipitation")).toDouble());
}

void TestCli::aPlaceCannotBeChosenAgainstAFixture()
{
    // It used to be accepted and ignored: `--fixture toronto --place Berlin`
    // printed Toronto and exited 0. A recorded forecast and a named place are
    // two answers to one question, which is the rule app/appoptions.cpp
    // already applies to the same pair of flags.
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"),
                              QStringLiteral("--place"), QStringLiteral("Berlin"),
                              QStringLiteral("now") });
    QCOMPARE(got.exitCode, 2);
    QVERIFY2(!got.stdOut.contains("Toronto"), got.stdOut.constData());
}

void TestCli::anUnknownCommandIsAUsageError()
{
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"), QStringLiteral("tomorrow") });
    QCOMPARE(got.exitCode, 2);
    QVERIFY(got.stdErr.contains("not a command"));

    const Outcome none = run({});
    QCOMPARE(none.exitCode, 2);
}

void TestCli::jsonAndCsvTogetherIsAUsageError()
{
    const Outcome got = run({ QStringLiteral("--fixture"), QStringLiteral("toronto"), QStringLiteral("now"),
                              QStringLiteral("--json"), QStringLiteral("--csv") });
    QCOMPARE(got.exitCode, 2);

    // The message too, not only the code: every other usage rejection returns
    // the same 2, so a code on its own would pass for the wrong reason - a
    // renamed fixture, a bad --units value, an unparsed count.
    QVERIFY2(got.stdErr.contains("pick one"), got.stdErr.constData());
}

QTEST_GUILESS_MAIN(TestCli)
#include "tst_cli.moc"
