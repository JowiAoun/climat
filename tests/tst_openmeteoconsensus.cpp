// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Canadian average: three forecasts, six readings, and nothing else moved.
//
// libclimat/providers/openmeteo/openmeteoconsensus.h has the measurement that
// justifies it. What this file holds it to is the shape of the change: which
// readings are averaged and which are left alone, that "now" and the daily
// extremes follow the averaged hours, that it happens in Canada and nowhere
// else, and that every way the second request can fail ends in the forecast
// the first request made on its own.

#include "libclimat/cache/cachestore.h"
#include "libclimat/cache/payloadcache.h"
#include "libclimat/core/clock.h"
#include "libclimat/net/httpclient.h"
#include "libclimat/net/requestkey.h"
#include "libclimat/providers/openmeteo/openmeteoadapter.h"
#include "libclimat/providers/openmeteo/openmeteoconsensus.h"
#include "libclimat/providers/openmeteo/openmeteoforecastprovider.h"
#include "support/httpstub.h"
#include "support/networkguard.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QUrlQuery>

#include <cmath>

using namespace climat;

namespace {

const Coordinate kToronto{ 43.6532, -79.3832 };
const Coordinate kMiami{ 25.7617, -80.1918 };

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(CLIMAT_SOURCE_DIR "/tests/fixtures/openmeteo/") + name);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

Forecast alone(const QByteArray &payload)
{
    return openmeteo::adaptForecast(payload, QStringLiteral("open-meteo")).value();
}

// A two-model answer to buildConsensusRequest(), made from a recorded
// one-model answer: the same hours, and every averaged reading raised by
// `gem` for one model and by `ifs` for the other. With 3 and 6 the average of
// the three is the recording plus 3, which is a number a test can state.
QByteArray consensusFor(const QByteArray &recorded, double gem, double ifs)
{
    const QJsonObject source = QJsonDocument::fromJson(recorded).object();
    const QJsonObject hours  = source.value(QLatin1String("hourly")).toObject();

    QJsonObject columns;
    columns.insert(QLatin1String("time"), hours.value(QLatin1String("time")));
    const QList<QPair<QString, double>> models = { { QStringLiteral("gem_seamless"), gem },
                                                   { QStringLiteral("ecmwf_ifs"), ifs } };
    for (const auto &[model, shift] : models) {
        for (const QLatin1String &name : openmeteo::consensusVariables()) {
            QJsonArray raised;
            for (const QJsonValue &value : hours.value(name).toArray())
                raised.append(value.isDouble() ? QJsonValue(value.toDouble() + shift) : value);
            columns.insert(QString(name) + QLatin1Char('_') + model, raised);
        }
    }

    QJsonObject root;
    for (const QString &key : { QStringLiteral("latitude"), QStringLiteral("longitude"),
                                QStringLiteral("utc_offset_seconds"), QStringLiteral("timezone"),
                                QStringLiteral("elevation") })
        root.insert(key, source.value(key));
    root.insert(QLatin1String("hourly"), columns);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool near(const Reading &value, double expected)
{
    return value.has_value() && std::abs(*value - expected) < 0.051;
}

ForecastRequest canadian()
{
    ForecastRequest request;
    request.coord = kToronto;
    request.days  = 10;
    return request;
}

QUrlQuery queryOf(const StubRequest &sent)
{
    return QUrlQuery(QUrl(QString::fromLatin1(sent.target)).query());
}

} // namespace

class TestOpenMeteoConsensus : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    // ---- the parts ------------------------------------------------------------
    void onlyAPointInCanadaIsAveraged();
    void theSecondRequestAsksForTwoModelsAndSixReadings();
    void aTwoModelAnswerIsReadAsTwoForecasts();
    void sixReadingsAreAveragedAndTheRestAreLeftAlone();
    void nowIsReadOffTheOtherModelsHours();
    void theDailyExtremesAreTheAveragedHoursExtremes();
    void theCreditsSayWhatEachLicenceAsks();

    // ---- the provider -----------------------------------------------------------
    void aCanadianForecastIsTheAverageOfThree();
    void aFailedAverageLeavesTheForecastAsItWas();
    void aCachedReadNeverOpensASocketForTheAverage();
    void anAmericanForecastIsNotAveraged();
    void anExplicitModelChoiceIsNotAveraged();

private:
    HttpStub    m_stub;
    FrozenClock m_clock{ QDateTime(QDate(2026, 7, 31), QTime(9, 0), QTimeZone::UTC) };
};

void TestOpenMeteoConsensus::initTestCase()
{
    NetworkGuard::install();
    QVERIFY2(!fixture(QStringLiteral("toronto-summer.json")).isEmpty(), "fixture missing");
}

void TestOpenMeteoConsensus::init()
{
    m_stub.reset();
    QVERIFY2(m_stub.listen(), "could not listen on loopback");
    NetworkGuard::clearAttempts();
}

void TestOpenMeteoConsensus::cleanup()
{
    QCOMPARE(NetworkGuard::externalAttempts(), QStringList());
}

// ---- the parts ------------------------------------------------------------------------

void TestOpenMeteoConsensus::onlyAPointInCanadaIsAveraged()
{
    QVERIFY(openmeteo::consensusApplies(kToronto));
    QVERIFY(openmeteo::consensusApplies(Coordinate{ 63.7467, -68.5170 }));    // Iqaluit
    QVERIFY(openmeteo::consensusApplies(Coordinate{ 49.2827, -123.1207 }));   // Vancouver
    QVERIFY(openmeteo::consensusApplies(Coordinate{ 47.5615, -52.7126 }));    // St. John's

    QVERIFY(!openmeteo::consensusApplies(kMiami));
    QVERIFY(!openmeteo::consensusApplies(Coordinate{ 52.5200, 13.4050 }));    // Berlin
    QVERIFY(!openmeteo::consensusApplies(Coordinate{ -33.8688, 151.2093 }));  // Sydney
}

void TestOpenMeteoConsensus::theSecondRequestAsksForTwoModelsAndSixReadings()
{
    HttpClient                client(&m_clock);
    OpenMeteoForecastProvider provider(&client, &m_clock);

    const HttpRequest first     = provider.buildRequest(canadian());
    const HttpRequest consensus = provider.buildConsensusRequest(canadian());

    const auto value = [](const HttpRequest &request, const QString &name) {
        for (const auto &[key, v] : request.parameters)
            if (key == name)
                return v;
        return QString();
    };

    QCOMPARE(value(consensus, QStringLiteral("models")), QStringLiteral("gem_seamless,ecmwf_ifs"));
    QCOMPARE(value(consensus, QStringLiteral("hourly")),
             QStringLiteral("temperature_2m,apparent_temperature,dew_point_2m,"
                            "relative_humidity_2m,wind_speed_10m,wind_gusts_10m"));
    QVERIFY(value(consensus, QStringLiteral("current")).isEmpty());
    QVERIFY(value(consensus, QStringLiteral("daily")).isEmpty());

    // The same place and window, or the hours do not pair up.
    for (const QString &name : { QStringLiteral("timezone"), QStringLiteral("forecast_days"),
                                 QStringLiteral("past_days") })
        QCOMPARE(value(consensus, name), value(first, name));
    QVERIFY(consensus.coordinate.has_value() && first.coordinate.has_value());
    QCOMPARE(consensus.coordinate->toKeyString(), first.coordinate->toKeyString());
    QCOMPARE(consensus.url, first.url);

    // And the first request is untouched: no model list, so its cache key and
    // every recorded fixture keyed by it are what they were.
    QVERIFY(value(first, QStringLiteral("models")).isEmpty());
    QVERIFY(RequestKey::forRequest(first).toString()
            != RequestKey::forRequest(consensus).toString());
}

void TestOpenMeteoConsensus::aTwoModelAnswerIsReadAsTwoForecasts()
{
    const QByteArray body = QByteArrayLiteral(R"({
        "latitude": 43.65, "longitude": -79.38, "utc_offset_seconds": -14400,
        "timezone": "America/Toronto",
        "hourly": {
            "time": ["2026-07-31T12:00", "2026-07-31T13:00"],
            "temperature_2m_gem_seamless": [21.0, 22.0],
            "wind_speed_10m_gem_seamless": [10.0, null],
            "temperature_2m_ecmwf_ifs": [23.0, 24.0]
        }
    })");

    const Result<QList<Forecast>> models =
        openmeteo::adaptConsensus(body, QStringLiteral("open-meteo"));
    QVERIFY2(models, qPrintable(models.error().toString()));
    QCOMPARE(models.value().size(), 2);

    const Forecast &gem = models.value().at(0);
    QCOMPARE(gem.hourly.size(), 2);
    QVERIFY(near(gem.hourly.at(1).temperature, 22.0));
    QVERIFY(near(gem.hourly.at(0).windSpeed, 10.0));
    QVERIFY(!gem.hourly.at(1).windSpeed.has_value());
    QCOMPARE(gem.hourly.at(0).time, QDateTime(QDate(2026, 7, 31), QTime(16, 0), QTimeZone::UTC));

    const Forecast &ifs = models.value().at(1);
    QVERIFY(near(ifs.hourly.at(0).temperature, 23.0));
    QVERIFY(!ifs.hourly.at(0).windSpeed.has_value());

    // A model the answer does not carry is an empty forecast, not a failure.
    const QByteArray oneModel = QByteArrayLiteral(R"({
        "latitude": 43.65, "longitude": -79.38, "utc_offset_seconds": 0, "timezone": "GMT",
        "hourly": { "time": ["2026-07-31T12:00"], "temperature_2m_ecmwf_ifs": [23.0] }
    })");
    const Result<QList<Forecast>> partial =
        openmeteo::adaptConsensus(oneModel, QStringLiteral("open-meteo"));
    QVERIFY(partial);
    QVERIFY(partial.value().at(0).hourly.isEmpty());
    QCOMPARE(partial.value().at(1).hourly.size(), 1);

    // Open-Meteo's refusal is an error, so the provider leaves it out.
    QVERIFY(!openmeteo::adaptConsensus(
        QByteArrayLiteral(R"({"error": true, "reason": "Unknown model"})"),
        QStringLiteral("open-meteo")));
}

void TestOpenMeteoConsensus::sixReadingsAreAveragedAndTheRestAreLeftAlone()
{
    const QByteArray recorded = fixture(QStringLiteral("toronto-summer.json"));
    const Forecast   before   = alone(recorded);
    Forecast         after    = before;

    openmeteo::applyConsensus(
        after, openmeteo::adaptConsensus(consensusFor(recorded, 3.0, 6.0),
                                         QStringLiteral("open-meteo")).value());

    QCOMPARE(after.hourly.size(), before.hourly.size());
    int compared = 0;
    for (int i = 0; i < after.hourly.size(); ++i) {
        const HourlyPoint &was = before.hourly.at(i);
        const HourlyPoint &is  = after.hourly.at(i);
        if (!was.temperature.has_value())
            continue;
        ++compared;

        QVERIFY2(near(is.temperature, *was.temperature + 3.0), qPrintable(QString::number(i)));
        QVERIFY(near(is.apparentTemperature, *was.apparentTemperature + 3.0));
        QVERIFY(near(is.dewPoint, *was.dewPoint + 3.0));
        QVERIFY(near(is.windSpeed, *was.windSpeed + 3.0));
        QVERIFY(near(is.windGust, *was.windGust + 3.0));
        // Whole percent, as Open-Meteo serves it.
        QCOMPARE(is.relativeHumidity, Reading(*was.relativeHumidity + 3.0));

        // Everything a reader judges rain by, and everything GEM does not
        // serve, is the first model's alone.
        QCOMPARE(is.precipitation, was.precipitation);
        QCOMPARE(is.rain, was.rain);
        QCOMPARE(is.snowfall, was.snowfall);
        QCOMPARE(is.precipitationProbability, was.precipitationProbability);
        QCOMPARE(is.weatherCode, was.weatherCode);
        QCOMPARE(is.cloudCover, was.cloudCover);
        QCOMPARE(is.pressureMsl, was.pressureMsl);
        QCOMPARE(is.windDirection, was.windDirection);
        QCOMPARE(is.uvIndex, was.uvIndex);
        QCOMPARE(is.visibility, was.visibility);
        QCOMPARE(is.time, was.time);
    }
    QVERIFY(compared > 200);

    // Rounded to the provider's own tenths, so climat-cli's JSON prints 14.1
    // and not 14.100000000000001. Same double as parsing the text gives.
    for (const HourlyPoint &point : after.hourly) {
        if (!point.temperature.has_value())
            continue;
        const QByteArray text =
            QByteArray::number(*point.temperature, 'g', QLocale::FloatingPointShortest);
        QVERIFY2(text.size() <= 6, text.constData());
    }

    // No twins, no change: an hour the other models do not reach keeps its own
    // values rather than being averaged with nothing.
    Forecast lonely = before;
    openmeteo::applyConsensus(lonely, { Forecast{}, Forecast{} });
    QCOMPARE(lonely.hourly.first().temperature, before.hourly.first().temperature);
    QCOMPARE(lonely.daily.first().temperatureMax, before.daily.first().temperatureMax);
}

void TestOpenMeteoConsensus::nowIsReadOffTheOtherModelsHours()
{
    const QByteArray recorded = fixture(QStringLiteral("toronto-summer.json"));
    const Forecast   before   = alone(recorded);
    QVERIFY(before.current.time.isValid());
    QCOMPARE(before.current.time.time().minute(), 15);   // a quarter-hour, between two hours

    Forecast after = before;
    openmeteo::applyConsensus(
        after, openmeteo::adaptConsensus(consensusFor(recorded, 3.0, 6.0),
                                         QStringLiteral("open-meteo")).value());

    // The two models' value a quarter of the way from one hour to the next.
    // Found by timestamp rather than with Forecast::hourAt(), which answers
    // with the hour ENDING after an instant: right for an hour's rain, and one
    // hour late for a temperature, which is a reading at its timestamp.
    const QDateTime    hour = before.current.time.addSecs(-15 * 60);
    const HourlyPoint *from = nullptr;
    const HourlyPoint *to   = nullptr;
    for (const HourlyPoint &point : before.hourly) {
        if (point.time == hour)
            from = &point;
        if (point.time == hour.addSecs(3600))
            to = &point;
    }
    QVERIFY(from != nullptr && to != nullptr);
    const double between =
        *from->temperature + (*to->temperature - *from->temperature) * 0.25;
    const double expected = (*before.current.temperature + (between + 3.0) + (between + 6.0)) / 3.0;
    QVERIFY2(near(after.current.temperature, expected),
             qPrintable(QStringLiteral("%1, wanted %2")
                            .arg(*after.current.temperature)
                            .arg(expected)));

    // And the reading the hero does not average is left as it was.
    QCOMPARE(after.current.weatherCode, before.current.weatherCode);
    QCOMPARE(after.current.precipitation, before.current.precipitation);
}

void TestOpenMeteoConsensus::theDailyExtremesAreTheAveragedHoursExtremes()
{
    const QByteArray recorded = fixture(QStringLiteral("toronto-summer.json"));
    Forecast         after    = alone(recorded);
    openmeteo::applyConsensus(
        after, openmeteo::adaptConsensus(consensusFor(recorded, 3.0, 6.0),
                                         QStringLiteral("open-meteo")).value());

    // The strip's high is the chart's peak, day after day, which is what
    // Open-Meteo's own daily max was before the average and has to stay.
    int checked = 0;
    for (const DailyPoint &day : after.daily) {
        Reading high;
        Reading low;
        int     hours = 0;
        for (const HourlyPoint &point : after.hourly) {
            if (point.time.toTimeZone(after.timeZone).date() != day.date
                || !point.temperature.has_value())
                continue;
            ++hours;
            high = high.has_value() ? std::max(*high, *point.temperature) : *point.temperature;
            low  = low.has_value() ? std::min(*low, *point.temperature) : *point.temperature;
        }
        if (hours < 23)
            continue;
        ++checked;
        QVERIFY2(near(day.temperatureMax, *high), qPrintable(day.date.toString(Qt::ISODate)));
        QVERIFY2(near(day.temperatureMin, *low), qPrintable(day.date.toString(Qt::ISODate)));
    }
    QVERIFY(checked >= 10);
}

void TestOpenMeteoConsensus::theCreditsSayWhatEachLicenceAsks()
{
    const QList<Attribution> credits = openmeteo::consensusAttributions(2026);
    QCOMPARE(credits.size(), 2);
    for (const Attribution &credit : credits)
        QVERIFY2(credit.isComplete(), qPrintable(credit.firstMissingField()));

    // ECCC's sentence for information from several sources, word for word.
    QCOMPARE(credits.at(0).creditLine,
             QStringLiteral("Contains information licenced under the Data Server End-use "
                            "Licence of Environment and Climate Change Canada."));
    QCOMPARE(credits.at(0).licenceUrl.host(), QStringLiteral("eccc-msc.github.io"));

    // ECMWF's copyright line carries the year the data is from.
    QVERIFY(credits.at(1).creditLine.contains(QStringLiteral("© 2026 European Centre")));
    QCOMPARE(credits.at(1).licenceName, QStringLiteral("CC BY 4.0"));

    // Both say what was done to them, which CC BY and ECMWF's terms ask for.
    for (const Attribution &credit : credits)
        QVERIFY(credit.note.contains(QStringLiteral("averages it")));
}

// ---- the provider -------------------------------------------------------------------------

void TestOpenMeteoConsensus::aCanadianForecastIsTheAverageOfThree()
{
    const QByteArray recorded = fixture(QStringLiteral("toronto-summer.json"));
    m_stub.enqueue(StubResponse::ok(recorded));
    m_stub.enqueue(StubResponse::ok(consensusFor(recorded, 3.0, 6.0)));

    HttpClient                client(&m_clock);
    OpenMeteoForecastProvider provider(&client, &m_clock);
    provider.setBaseUrl(QUrl(m_stub.baseUrl() + QStringLiteral("/v1/forecast")));

    auto future = provider.fetchForecast(canadian());
    QVERIFY(QTest::qWaitFor([&future]() { return future.isFinished(); }, 5000));
    QVERIFY2(future.result().hasValue(), qPrintable(future.result().error().toString()));

    QCOMPARE(m_stub.requestCount(), 2);
    QVERIFY(!queryOf(m_stub.requests().at(0)).hasQueryItem(QStringLiteral("models")));
    QCOMPARE(queryOf(m_stub.requests().at(1)).queryItemValue(QStringLiteral("models"),
                                                             QUrl::FullyDecoded),
             QStringLiteral("gem_seamless,ecmwf_ifs"));

    const Forecast before = alone(recorded);
    const Forecast after  = future.result().value();
    const int      noon   = 36;   // any hour; the recording has every one
    QVERIFY(near(after.hourly.at(noon).temperature, *before.hourly.at(noon).temperature + 3.0));
    QCOMPARE(after.hourly.at(noon).precipitation, before.hourly.at(noon).precipitation);
    QCOMPARE(after.providerId, QStringLiteral("open-meteo"));
}

void TestOpenMeteoConsensus::aFailedAverageLeavesTheForecastAsItWas()
{
    const QByteArray recorded = fixture(QStringLiteral("toronto-summer.json"));
    m_stub.enqueue(StubResponse::ok(recorded));
    StubResponse refused = StubResponse::ok(
        QByteArrayLiteral(R"({"error": true, "reason": "Unknown model gem_seamless"})"));
    refused.status = 400;
    m_stub.enqueue(refused);

    HttpClient                client(&m_clock);
    OpenMeteoForecastProvider provider(&client, &m_clock);
    provider.setBaseUrl(QUrl(m_stub.baseUrl() + QStringLiteral("/v1/forecast")));

    auto future = provider.fetchForecast(canadian());
    QVERIFY(QTest::qWaitFor([&future]() { return future.isFinished(); }, 5000));
    QVERIFY2(future.result().hasValue(), qPrintable(future.result().error().toString()));
    QCOMPARE(m_stub.requestCount(), 2);

    const Forecast before = alone(recorded);
    const Forecast after  = future.result().value();
    for (int i = 0; i < before.hourly.size(); ++i)
        QCOMPARE(after.hourly.at(i).temperature, before.hourly.at(i).temperature);
    QCOMPARE(after.current.temperature, before.current.temperature);
    QCOMPARE(after.daily.first().temperatureMax, before.daily.first().temperatureMax);
}

void TestOpenMeteoConsensus::aCachedReadNeverOpensASocketForTheAverage()
{
    const QByteArray recorded = fixture(QStringLiteral("toronto-summer.json"));

    CacheStore cache(&m_clock);
    QVERIFY(cache.open(QStringLiteral(":memory:")).hasValue());

    HttpClient                client(&m_clock);
    OpenMeteoForecastProvider provider(&client, &m_clock);
    provider.setBaseUrl(QUrl(m_stub.baseUrl() + QStringLiteral("/v1/forecast")));
    provider.setCache(&cache);

    ForecastRequest cachedOnly = canadian();
    cachedOnly.cachedOnly      = true;

    // Only the first request's answer on disk, the way an app that last ran
    // before this change leaves it. The read finishes in the same turn, as
    // daemon/snapshotservice.cpp's warm-up needs, and unaveraged.
    const HttpRequest first = provider.buildRequest(canadian());
    HttpResponse      stored;
    stored.status    = 200;
    stored.body      = recorded;
    stored.fetchedAt = m_clock.now();
    payloadcache::store(&cache, RequestKey::forRequest(first).toString(), first.providerId,
                        first.endpoint, first.kind, *first.coordinate, stored);

    auto unaveraged = provider.fetchForecast(cachedOnly);
    QVERIFY(unaveraged.isFinished());
    QVERIFY(unaveraged.result().hasValue());
    QCOMPARE(unaveraged.result().value().hourly.at(36).temperature,
             alone(recorded).hourly.at(36).temperature);

    // Both on disk: averaged, still in the same turn, still without a socket.
    const HttpRequest consensus = provider.buildConsensusRequest(canadian());
    stored.body                 = consensusFor(recorded, 3.0, 6.0);
    payloadcache::store(&cache, RequestKey::forRequest(consensus).toString(),
                        consensus.providerId, consensus.endpoint, consensus.kind,
                        *consensus.coordinate, stored);

    auto averaged = provider.fetchForecast(cachedOnly);
    QVERIFY(averaged.isFinished());
    QVERIFY(averaged.result().hasValue());
    QVERIFY(near(averaged.result().value().hourly.at(36).temperature,
                 *alone(recorded).hourly.at(36).temperature + 3.0));

    QCOMPARE(m_stub.requestCount(), 0);
}

void TestOpenMeteoConsensus::anAmericanForecastIsNotAveraged()
{
    m_stub.enqueue(StubResponse::ok(fixture(QStringLiteral("miami-thunder.json"))));

    HttpClient                client(&m_clock);
    OpenMeteoForecastProvider provider(&client, &m_clock);
    provider.setBaseUrl(QUrl(m_stub.baseUrl() + QStringLiteral("/v1/forecast")));

    ForecastRequest request;
    request.coord = kMiami;
    auto future   = provider.fetchForecast(request);
    QVERIFY(QTest::qWaitFor([&future]() { return future.isFinished(); }, 5000));
    QVERIFY(future.result().hasValue());
    QCOMPARE(m_stub.requestCount(), 1);
}

void TestOpenMeteoConsensus::anExplicitModelChoiceIsNotAveraged()
{
    m_stub.enqueue(StubResponse::ok(fixture(QStringLiteral("toronto-summer.json"))));

    HttpClient                client(&m_clock);
    OpenMeteoForecastProvider provider(&client, &m_clock);
    provider.setBaseUrl(QUrl(m_stub.baseUrl() + QStringLiteral("/v1/forecast")));

    ForecastRequest request = canadian();
    request.models          = { QStringLiteral("gem_seamless") };
    auto future             = provider.fetchForecast(request);
    QVERIFY(QTest::qWaitFor([&future]() { return future.isFinished(); }, 5000));
    QVERIFY(future.result().hasValue());
    QCOMPARE(m_stub.requestCount(), 1);
}

QTEST_GUILESS_MAIN(TestOpenMeteoConsensus)

#include "tst_openmeteoconsensus.moc"
