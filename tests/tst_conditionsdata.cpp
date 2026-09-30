// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The shape the twelve detail cards read, before there is anything to put in it.
//
// ============================================================================
// WHAT THIS TEST IS ABOUT
//
// `Detail` is a QML singleton whose fifteen blocks are QVariantMaps, and QML
// binds `Detail.wind.gust` while the component holding that binding is being
// constructed. On any start that has to fetch - a first run with a cold cache,
// an expired forecast, a socket that is still opening - construction happens
// before the first snapshot, so those bindings are evaluated against whatever
// the maps hold at that moment.
//
// They used to hold nothing, and an empty map answers `undefined` to every key
// in it. Every launch of both binaries therefore printed several hundred lines
// of
//
//     Unable to assign [undefined] to QString
//     TypeError: Cannot read property 'length' of undefined
//
// before the data landed and it all quietly resolved. The rendering was never
// wrong; the model was simply not well formed until something filled it in.
//
// ============================================================================
// WHY IT IS A KEY-SET COMPARISON AND NOT A LIST OF EXPECTED KEYS
//
// The fix is a neutral shape per block - the same keys the build function
// produces, with every value at its nothing - and its failure mode is drift:
// somebody adds `d.gustDirection` to buildWind() and does not add it to
// neutralWind(). That is invisible. It compiles, the card draws correctly the
// moment data arrives, and the only symptom is one more line of console on
// startup, in a stream that (before this was fixed) nobody was reading anyway.
//
// A test that listed the keys it expected would have to be edited by the same
// person in the same commit, which is exactly the step being missed. So it
// asserts the RELATIONSHIP instead: the key set a block has at construction is
// the key set it has after a real fixture snapshot. Add a key to one side only
// and this fails, naming the block and the key.
//
// ============================================================================
// AND WHY THIS IS THE ONE TEST THAT LINKS THE APP
//
// tests/CMakeLists.txt says the tests link libclimat and Qt Test and that is the
// whole list, because `engine_has_no_gui` reads a test binary's DT_NEEDED
// entries to prove the engine drags in no windowing toolkit. That guarantee is
// asked of tst_httpclient specifically and it is unaffected by this file: what
// is under test here is a VIEW MODEL, which lives in app/, is GPL-3.0-or-later
// rather than MPL-2.0, and is allowed a QML dependency because being read by
// QML is its entire job. It is registered by hand below the loop for that
// reason, without climat_forbid_gui.

#include "conditionsdata.h"
#include "settings.h"

#include "libclimat/domain/hourconvention.h"
#include "libclimat/domain/weathercode.h"
#include "libclimat/providers/fixture/fixtureprovider.h"

#include <QRegularExpression>
#include <QScopeGuard>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QTest>

using namespace climat;

class TestConditionsData : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void everyBlockCarriesItsKeysBeforeTheFirstSnapshot();
    void theConditionComesFromTheHourWeAreStandingIn();
    void aRainAlreadyFallingIsNotAnnouncedAsStartingLater();
    void noAirQualityIsADashWithNoBand();
    void everySentenceEndsInOneFullStop_data();
    void everySentenceEndsInOneFullStop();
    void nothingInTheNeutralShapeIsUndefined();
    void theNeutralShapeInventsNoWeather();

private:
    // The blocks, as (name, getter) so a failure names the one that drifted.
    // `activities` is deliberately absent: it is a QVariantList, and an empty
    // list is already well formed - it answers `.length` with 0 and a Repeater
    // bound to it draws nothing. Only the maps had holes.
    using Block = std::pair<const char *, QVariantMap (ConditionsData::*)() const>;
    static QList<Block> blocks();

    static QStringList keysOf(const QVariantMap &map);
};

QList<TestConditionsData::Block> TestConditionsData::blocks()
{
    return {
        { "current",       &ConditionsData::current },
        { "temperature",   &ConditionsData::temperature },
        { "feelsLike",     &ConditionsData::feelsLike },
        { "cloudCover",    &ConditionsData::cloudCover },
        { "precipitation", &ConditionsData::precipitation },
        { "wind",          &ConditionsData::wind },
        { "humidity",      &ConditionsData::humidity },
        { "uv",            &ConditionsData::uv },
        { "airQuality",    &ConditionsData::airQuality },
        { "visibility",    &ConditionsData::visibility },
        { "pressure",      &ConditionsData::pressure },
        { "sun",           &ConditionsData::sun },
        { "moon",          &ConditionsData::moon },
        { "moonPhase",     &ConditionsData::moonPhase },
        { "pollen",        &ConditionsData::pollen },
        { "location",      &ConditionsData::location },
    };
}

QStringList TestConditionsData::keysOf(const QVariantMap &map)
{
    QStringList names = map.keys();
    names.sort();
    return names;
}

void TestConditionsData::initTestCase()
{
    // Before anything constructs a Settings, which Units does on first use and
    // ConditionsData's constructor triggers. Without this the test writes to
    // the developer's real preferences directory.
    QStandardPaths::setTestModeEnabled(true);
}

// Berlin, because it is the fixture with pollen in it: outside the CAMS
// European domain buildPollen() gates the block off and fills only `available`,
// and a comparison against that would prove nothing about the other five keys.
// ---- one definition of "now" -----------------------------------------------
//
// Two surfaces described the same instant from two sources and contradicted
// each other: the card said "Mainly sunny" while the chart's Now column, a few
// centimetres below, drew heavy rain - and it was raining.
//
// `weatherCode` describes a stretch of weather rather than a moment, which is
// why hourconvention.cpp moves it with the accumulations it belongs to. The
// chart, the hour strip, the precipitation wash and this card's own next-rain
// clause all read that series. The `current` block is a separate product on its
// own convention, so it is trusted for the instants - the temperature at 4:47
// rather than at four - and not for the stretch.
//
// Two things had to be arranged for this to be a test of anything, and both are
// findings in their own right.
//
// The fixture's `current` block is stamped 06:30 against a `recordedAt` of
// 12:28 - six hours stale - so `buildContext` rejects it and rebuilds the
// observation from the standing hour. Every golden image and every other test
// therefore runs the *fallback*, where the card and the chart agree because
// they are already the same value. The branch the live app takes has never been
// exercised, which is most of why this reached a screenshot. So the clock is
// moved to the block's own time here, which is what makes it current.
//
// And no fixture reproduces the disagreement even then: all four carry a
// `current` code equal to the standing hour's. The contradiction is introduced
// rather than recorded, which states the rule more plainly than a fixture could
// - the current block is overruled on this field however loudly it disagrees.
void TestConditionsData::theConditionComesFromTheHourWeAreStandingIn()
{
    const Fixture fixture = fixtures::load(QStringLiteral("toronto"));
    QVERIFY(fixture.isValid());

    ForecastRequest request;
    request.coord = fixture.place.coordinate;

    FixtureForecastProvider   forecasts(fixture);
    FixtureAirQualityProvider air(fixture);

    const QFuture<Result<Forecast>>   forecastFuture = forecasts.fetchForecast(request);
    const QFuture<Result<AirQuality>> airFuture      = air.fetchAirQuality(request);
    QVERIFY(forecastFuture.isFinished());
    QVERIFY(airFuture.isFinished());

    Forecast forecast = forecastFuture.result().value();
    const AirQuality quality = airFuture.result().value();

    // What the standing hour says, taken through the same conversion the app
    // applies - index i carries the code for the hour STARTING at its stamp.
    QVERIFY(forecast.current.time.isValid());
    const QDateTime now = forecast.current.time;

    const QList<HourlyPoint> hours = asHourStarting(forecast.hourly);
    int standing = 0;
    for (int i = 0; i < hours.size(); ++i) {
        if (hours.at(i).time <= now)
            standing = i;
        else
            break;
    }
    QVERIFY(hours.at(standing).weatherCode.has_value());
    const int hourCode = *hours.at(standing).weatherCode;

    // A code the standing hour certainly does not carry. 95 is thunderstorm;
    // 0 is clear sky. One of the two is always a contradiction.
    const int provocation = hourCode == 95 ? 0 : 95;
    forecast.current.weatherCode = provocation;

    // A temperature the hourly series certainly does not carry either, and it
    // is not decoration: it is what proves this test is exercising the branch it
    // claims to. Without moving the clock above, the card falls back to the
    // standing hour, agrees with it for a reason that has nothing to do with the
    // fix, and passes whether or not the fix is there - which is what the first
    // draft of this test did. Asserting the temperature comes back says the
    // block was in play, and states the other half of the rule while it is at
    // it: instants are exactly what the block is kept for.
    forecast.current.temperature = -41.0;

    ConditionsData data(nullptr);
    data.setSnapshot(forecast, quality, now, fixture.place, /*hasPollen=*/false);

    QCOMPARE(data.temperature().value(QStringLiteral("value")).toInt(), -41);

    const QString kind = data.current().value(QStringLiteral("conditionKind")).toString();
    const bool    day  = forecast.current.isDay.value_or(true);

    QCOMPARE(kind, conditionKindName(conditionFor(hourCode, day)));
    QVERIFY2(kind != conditionKindName(conditionFor(provocation, day)),
             "the card is drawing the `current` block's weather code. It describes a "
             "stretch, the hourly series is what every other surface on the page reads, "
             "and the two contradicting each other is what the reader sees.");
}

// The clause says "from", and "from" names a time to act on. A rain that has
// been falling for two hours does not have one - announcing it as starting at
// five is the same sentence a dry afternoon would get, which is what the card
// said while it was raining.
void TestConditionsData::aRainAlreadyFallingIsNotAnnouncedAsStartingLater()
{
    // Toronto, and the rain is put there rather than found: the point is the
    // sentence's arithmetic, not any particular afternoon's weather, and a
    // fixture chosen for being wet would still have to be edited to guarantee
    // an unbroken run through the hour we are standing in.
    const Fixture fixture = fixtures::load(fixtures::defaultName());
    QVERIFY(fixture.isValid());

    ForecastRequest request;
    request.coord = fixture.place.coordinate;

    FixtureForecastProvider   forecasts(fixture);
    FixtureAirQualityProvider air(fixture);

    const QFuture<Result<Forecast>>   forecastFuture = forecasts.fetchForecast(request);
    const QFuture<Result<AirQuality>> airFuture      = air.fetchAirQuality(request);
    QVERIFY(forecastFuture.isFinished());
    QVERIFY(airFuture.isFinished());

    Forecast forecast = forecastFuture.result().value();
    const AirQuality quality = airFuture.result().value();

    // Make it rain from the standing hour onward, so the run the reader is
    // already in continues without a break.
    QList<HourlyPoint> hours = asHourStarting(forecast.hourly);
    int standing = 0;
    for (int i = 0; i < hours.size(); ++i) {
        if (hours.at(i).time <= fixture.recordedAt)
            standing = i;
        else
            break;
    }
    // `forecast.hourly` is hour-ENDING, so the hour starting at index i is the
    // raw sample at i + 1.
    for (int i = standing + 1; i < qMin(int(forecast.hourly.size()), standing + 10); ++i)
        forecast.hourly[i].precipitation = 3.0;

    ConditionsData data(nullptr);
    data.setSnapshot(forecast, quality, fixture.recordedAt, fixture.place, /*hasPollen=*/false);

    const QString summary = data.current().value(QStringLiteral("summary")).toString();
    QVERIFY2(!summary.contains(QStringLiteral(" from ")),
             qPrintable(QStringLiteral(
                 "it is already raining and the card says \"%1\". A run that has "
                 "started has no onset to announce.").arg(summary)));
}

// ---- no air quality ---------------------------------------------------------
//
// Kampala is outside the air-quality product. The hero and the card both read
// `reading` and `band`: a dash in the text, and no band, so no coloured dot and
// no marker. The hero used to put a green "good" dot beside the dash, and the
// card a large "0" over the words "No reading".
void TestConditionsData::noAirQualityIsADashWithNoBand()
{
    const Fixture fixture = fixtures::load(fixtures::defaultName());
    QVERIFY(fixture.isValid());

    ForecastRequest request;
    request.coord = fixture.place.coordinate;
    FixtureForecastProvider forecasts(fixture);
    const Forecast forecast = forecasts.fetchForecast(request).result().value();

    ConditionsData data(nullptr);
    data.setSnapshot(forecast, AirQuality(), fixture.recordedAt, fixture.place, /*hasPollen=*/false);

    const QVariantMap air = data.airQuality();
    QCOMPARE(air.value(QStringLiteral("reading")).toString(), QStringLiteral("\u2014"));
    QCOMPARE(air.value(QStringLiteral("band")).toString(), QString());
}

// ---- one full stop ---------------------------------------------------------
//
// A time inside a sentence is "3:00 p.m.", which ends in a stop of its own, so
// every card whose sentence closed on a time printed "p.m.." - four at once on
// the desktop. A 24-hour clock is the other half: "15:00" carries no stop, so
// the template has to. Both clocks, and every sentence the cards and the
// summary show.
void TestConditionsData::everySentenceEndsInOneFullStop_data()
{
    QTest::addColumn<QString>("clock");
    QTest::newRow("12h") << QStringLiteral("12h");
    QTest::newRow("24h") << QStringLiteral("24h");
}

void TestConditionsData::everySentenceEndsInOneFullStop()
{
    QFETCH(QString, clock);
    const QString before = Settings::instance()->clockFormat();
    const auto restore = qScopeGuard([&before] { Settings::instance()->setClockFormat(before); });
    Settings::instance()->setClockFormat(clock);

    const Fixture fixture = fixtures::load(fixtures::defaultName());
    QVERIFY(fixture.isValid());

    ForecastRequest request;
    request.coord = fixture.place.coordinate;

    FixtureForecastProvider   forecasts(fixture);
    FixtureAirQualityProvider air(fixture);
    Forecast forecast = forecasts.fetchForecast(request).result().value();
    const AirQuality quality = air.fetchAirQuality(request).result().value();

    // Rain that starts a few hours out, so the summary and the precipitation
    // card both have an onset to name. Indices as in the test above.
    const QList<HourlyPoint> hours = asHourStarting(forecast.hourly);
    int standing = 0;
    for (int i = 0; i < hours.size(); ++i) {
        if (hours.at(i).time <= fixture.recordedAt)
            standing = i;
        else
            break;
    }
    for (int i = standing + 4; i < qMin(int(forecast.hourly.size()), standing + 8); ++i)
        forecast.hourly[i].precipitation = 3.0;

    ConditionsData data(nullptr);
    data.setSnapshot(forecast, quality, fixture.recordedAt, fixture.place, /*hasPollen=*/false);

    QStringList sentences{ data.current().value(QStringLiteral("summary")).toString() };
    for (const Block &block : blocks())
        sentences.append((data.*block.second)().value(QStringLiteral("body")).toString());

    static const QRegularExpression doubled(QStringLiteral("(?<!\\.)\\.\\.(?!\\.)"));
    static const QRegularExpression time(QStringLiteral("\\d:\\d\\d"));
    // "from 16:00 The high will be": the summary joins sentences, so a stop
    // missing from one is in the middle of the string, not at its end.
    static const QRegularExpression runOn(QStringLiteral("\\d:\\d\\d\\s+\\p{Lu}"));
    int timed = 0;
    for (const QString &sentence : std::as_const(sentences)) {
        if (sentence.isEmpty())
            continue;
        QVERIFY2(!doubled.match(sentence).hasMatch(),
                 qPrintable(QStringLiteral("two full stops in \"%1\"").arg(sentence)));
        QVERIFY2(sentence.endsWith(QLatin1Char('.')),
                 qPrintable(QStringLiteral("no full stop at the end of \"%1\"").arg(sentence)));
        QVERIFY2(!runOn.match(sentence).hasMatch(),
                 qPrintable(QStringLiteral("a time runs into the next sentence in \"%1\"").arg(sentence)));
        if (time.match(sentence).hasMatch())
            ++timed;
    }

    // Seven of them name a time today. Fewer, and this proved less than it says.
    QVERIFY2(timed >= 7, qPrintable(QStringLiteral("only %1 sentences named a time").arg(timed)));
}

void TestConditionsData::everyBlockCarriesItsKeysBeforeTheFirstSnapshot()
{
    ConditionsData data(nullptr);

    QHash<QString, QStringList> before;
    for (const Block &block : blocks())
        before.insert(QString::fromLatin1(block.first), keysOf((data.*block.second)()));

    const Fixture fixture = fixtures::load(QStringLiteral("berlin"));
    QVERIFY(fixture.isValid());

    ForecastRequest request;
    request.coord = fixture.place.coordinate;

    FixtureForecastProvider   forecasts(fixture);
    FixtureAirQualityProvider air(fixture);

    const QFuture<Result<Forecast>>   forecastFuture = forecasts.fetchForecast(request);
    const QFuture<Result<AirQuality>> airFuture      = air.fetchAirQuality(request);

    // Finished inside the call, which is what lets the app publish a fixture
    // before its QML engine loads. See AppEngine::fetch().
    QVERIFY(forecastFuture.isFinished());
    QVERIFY(airFuture.isFinished());

    const Result<Forecast>   forecast = forecastFuture.result();
    const Result<AirQuality> quality  = airFuture.result();
    QVERIFY(forecast.hasValue());
    QVERIFY(quality.hasValue());

    data.setSnapshot(forecast.value(), quality.value(), fixture.recordedAt, fixture.place,
                     /*hasPollen=*/true);

    for (const Block &block : blocks()) {
        const QString     name  = QString::fromLatin1(block.first);
        const QStringList after = keysOf((data.*block.second)());

        const QSet<QString> missing = QSet<QString>(after.cbegin(), after.cend())
                                    - QSet<QString>(before.value(name).cbegin(),
                                                    before.value(name).cend());
        const QSet<QString> extra = QSet<QString>(before.value(name).cbegin(),
                                                  before.value(name).cend())
                                  - QSet<QString>(after.cbegin(), after.cend());

        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral(
                     "Detail.%1 gains %2 only once a snapshot lands. Add it to the matching "
                     "neutral*() in app/viewmodels/conditionsdata.cpp, or QML reads it as "
                     "undefined on every start that has to fetch.")
                                .arg(name, QStringList(missing.cbegin(), missing.cend())
                                               .join(QStringLiteral(", ")))));

        QVERIFY2(extra.isEmpty(),
                 qPrintable(QStringLiteral(
                     "Detail.%1 carries %2 before a snapshot and never again. A key nothing "
                     "fills is a key nothing should promise.")
                                .arg(name, QStringList(extra.cbegin(), extra.cend())
                                               .join(QStringLiteral(", ")))));
    }
}

// A key that is present but holds an invalid QVariant is the same `undefined`
// with extra steps, so the shape is only well formed if every value in it is a
// value.
void TestConditionsData::nothingInTheNeutralShapeIsUndefined()
{
    ConditionsData data(nullptr);

    for (const Block &block : blocks()) {
        const QVariantMap map = (data.*block.second)();
        QVERIFY2(!map.isEmpty(), block.first);

        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            QVERIFY2(it.value().isValid(),
                     qPrintable(QStringLiteral("Detail.%1.%2 is an invalid QVariant, which "
                                               "reaches QML as undefined.")
                                    .arg(QString::fromLatin1(block.first), it.key())));
        }
    }
}

// docs/README.md's first non-negotiable, asked of the one frame that has no
// data behind it: a card may say nothing, and may not say something.
//
// Every string is empty except `trend`, which is "none" - the word TrendBadge
// hides itself on, and the model's own vocabulary for a reading that is not
// doing anything. Nothing in the shape is a formatted reading: no em dash
// either, because "we asked and there is no value" is a claim that has not been
// earned before anybody has asked.
void TestConditionsData::theNeutralShapeInventsNoWeather()
{
    ConditionsData data(nullptr);

    for (const Block &block : blocks()) {
        const QVariantMap map = (data.*block.second)();

        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            if (it.value().typeId() != QMetaType::QString)
                continue;

            const QString text     = it.value().toString();
            const QString expected = it.key() == QLatin1String("trend")
                                         ? QStringLiteral("none")
                                         : QString();

            QVERIFY2(text == expected,
                     qPrintable(QStringLiteral("Detail.%1.%2 says \"%3\" before there is any "
                                               "weather. It should say nothing.")
                                    .arg(QString::fromLatin1(block.first), it.key(), text)));
        }
    }

    // And the two sentence fields on the block that has to exist before the
    // first fetch even resolves a place.
    QCOMPARE(data.observedAt(), QString());
    QCOMPARE(data.observedOn(), QString());
    QVERIFY(data.activities().isEmpty());
}

// Guiless on purpose. The target links the QML module because that is where the
// view model lives, but nothing under test needs a window or a platform plugin,
// and a test that opens neither cannot hang waiting for one.
QTEST_GUILESS_MAIN(TestConditionsData)
#include "tst_conditionsdata.moc"
