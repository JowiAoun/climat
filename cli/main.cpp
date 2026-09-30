// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// climat-cli - the forecast for a status bar, a script or a terminal.
//
//   climat-cli now                  the current conditions, and any warning
//   climat-cli hourly [N]           the next N hours (12)
//   climat-cli daily [N]            the next N days (7)
//   climat-cli places               the saved places, home marked
//
//   --place <id|name>   a saved place by id, or anywhere by name (a search)
//   --json / --csv      machine output, always in canonical units
//   --units metric|imperial   override the app's preference for this run
//   --fixture <name>    a recorded forecast at a frozen clock; no network
//   --timeout <ms>      how long to wait for the network (15000)
//
// ============================================================================
// TWO OUTPUTS, TWO RULES
//
// Text honours the reader's preferences - the units and the clock format the
// app's Preferences screen wrote to the INI - because a status bar that said
// 72° while the app said 22° would be the app arguing with itself. The keys
// are app/settingskeys.h, read here through a bare QSettings; there is no QML
// engine in this process and no reason to pay for one.
//
// JSON and CSV are canonical: °C, km/h, hPa, km, mm, ISO 8601 in the place's
// own zone. A script reads a number and wants the same number tomorrow after
// somebody flipped a switch in a dialog it never opened. Anything that wants
// the reader's units can convert with the factors in libclimat/domain/units.h
// or ask for text.
//
// ============================================================================
// WHAT IT SHARES WITH THE APP, AND WHAT IT DOES NOT
//
// The engine: the same providers, the same fallback chain, the same cache at
// the same path, the same saved places. `climat-cli now` on a laptop the app
// runs on answers from the app's cache when it is fresh and asks the network
// otherwise, exactly as the app would, and writes what it fetched back for
// the app to find. One cache, one set of requests against the free tier.
//
// Not the settings object, not the view models, not a window. And it does not
// ADD to the saved places: `--place Lisbon` looks Lisbon up and answers, and
// the app's list is the app's.

#include "cliconfig.h"

#include "app/settingskeys.h"
#include "libclimat/cache/cachestore.h"
#include "libclimat/core/clock.h"
#include "libclimat/core/result.h"
#include "libclimat/domain/alert.h"
#include "libclimat/domain/forecast.h"
#include "libclimat/domain/hourconvention.h"
#include "libclimat/domain/place.h"
#include "libclimat/domain/units.h"
#include "libclimat/domain/weathercode.h"
#include "libclimat/net/httpclient.h"
#include "libclimat/places/locationcontroller.h"
#include "libclimat/providers/eccc/ecccalertprovider.h"
#include "libclimat/providers/fixture/fixtureprovider.h"
#include "libclimat/providers/geocoding/openmeteogeocoder.h"
#include "libclimat/providers/metno/metnoforecastprovider.h"
#include "libclimat/providers/nws/nwsalertprovider.h"
#include "libclimat/providers/openmeteo/openmeteoforecastprovider.h"
#include "libclimat/providers/registry.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFuture>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QTextStream>
#include <QTimeZone>
#include <QTimer>

#include <cstdio>
#include <memory>
#include <optional>

using namespace climat;

namespace {

// ---- exit codes ------------------------------------------------------------
//
// Distinct on purpose, so that a script can tell "you asked for something I
// do not have" from "the network is down" without parsing English.
constexpr int kExitUsage    = 2;
constexpr int kExitNoPlace  = 3;
constexpr int kExitFetch    = 4;
constexpr int kExitTimeout  = 5;

constexpr int kDefaultTimeoutMs = 15000;
constexpr int kForecastDays     = 16;

// ---- waiting on a future without holding the thread -------------------------
//
// HttpClient needs the event loop to move bytes, so a blocking wait on a
// future deadlocks. This spins a local loop until the future finishes or the
// timer fires. `std::nullopt` is the timeout.
template <typename T>
std::optional<T> await(QFuture<T> future, int timeoutMs)
{
    if (!future.isFinished()) {
        QEventLoop        loop;
        QFutureWatcher<T> watcher;
        QTimer            timer;
        timer.setSingleShot(true);
        QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        timer.start(timeoutMs);
        loop.exec();
    }
    if (!future.isFinished())
        return std::nullopt;
    return future.result();
}

// ---- the reader's preferences -------------------------------------------------

struct Preferences {
    units::Preset chosen;
    bool          twentyFourHour = false;
};

Preferences readPreferences(const QString &unitsOverride)
{
    // The same file the app writes: same organisation, same application name,
    // same format. main() set the names; this sets the format the app's
    // Settings::prepareStorage sets, before any QSettings exists.
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const QSettings ini;

    Preferences prefs;
    prefs.chosen = units::metric();

    const auto read = [&ini](const char *key, const QString &fallback) {
        return ini.value(QLatin1String(key), fallback).toString();
    };
    prefs.chosen.temperature   = read(settingskeys::temperatureUnit, prefs.chosen.temperature);
    prefs.chosen.wind          = read(settingskeys::windUnit, prefs.chosen.wind);
    prefs.chosen.pressure      = read(settingskeys::pressureUnit, prefs.chosen.pressure);
    prefs.chosen.visibility    = read(settingskeys::visibilityUnit, prefs.chosen.visibility);
    prefs.chosen.precipitation = read(settingskeys::precipitationUnit, prefs.chosen.precipitation);

    // The same default the app falls back to, from the same function, so the
    // two cannot disagree about what time it is on one machine.
    prefs.twentyFourHour = read(settingskeys::clockFormat, settingskeys::defaultClockFormat())
                           == QLatin1String("24h");

    if (unitsOverride == QLatin1String("metric"))
        prefs.chosen = units::metric();
    else if (unitsOverride == QLatin1String("imperial"))
        prefs.chosen = units::imperial();

    return prefs;
}

// ---- formatting ----------------------------------------------------------------

QString unitFor(const Preferences &prefs, units::Quantity quantity)
{
    switch (quantity) {
    case units::Quantity::Temperature:   return prefs.chosen.temperature;
    case units::Quantity::Wind:          return prefs.chosen.wind;
    case units::Quantity::Pressure:      return prefs.chosen.pressure;
    case units::Quantity::Visibility:    return prefs.chosen.visibility;
    case units::Quantity::Precipitation: return prefs.chosen.precipitation;
    default:                             return {};
    }
}

// "23°C", "12 km/h", "-" for a reading that is not there.
QString shown(const Preferences &prefs, units::Quantity quantity, const Reading &reading)
{
    if (!reading.has_value())
        return QStringLiteral("-");
    const QString unit    = unitFor(prefs, quantity);
    const double  display = units::convert(quantity, unit, *reading);
    const QString number  = QString::number(display, 'f', units::decimals(quantity, unit));
    const QString symbol  = units::symbol(quantity, unit);
    if (quantity == units::Quantity::Temperature || quantity == units::Quantity::Percentage
        || quantity == units::Quantity::Direction || symbol.isEmpty())
        return number + symbol;
    return number + QLatin1Char(' ') + symbol;
}

QString percent(const Reading &reading)
{
    return reading.has_value() ? QStringLiteral("%1%").arg(qRound(*reading)) : QStringLiteral("-");
}

// The sixteen points, from the direction the wind blows FROM.
QString compass(const Reading &degrees)
{
    if (!degrees.has_value())
        return {};
    static const char *const names[] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                         "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };
    const int index = int(std::fmod(*degrees + 11.25, 360.0) / 22.5) % 16;
    return QLatin1String(names[index]);
}

QString clock(const Preferences &prefs, const QDateTime &instant, const QTimeZone &zone)
{
    if (!instant.isValid())
        return QStringLiteral("-");
    const QDateTime local = instant.toTimeZone(zone);
    return prefs.twentyFourHour ? local.toString(QStringLiteral("HH:mm"))
                                : local.toString(QStringLiteral("h:mm AP"));
}

// "10:00 PM" today and "Thu 10:00 PM" any other day, as the app's banner says it.
// A bare time for a warning that ends tomorrow night reads as tonight.
QString until(const Preferences &prefs, const QDateTime &instant, const QDateTime &now,
              const QTimeZone &zone)
{
    const QString time = clock(prefs, instant, zone);
    const QDate   day  = instant.toTimeZone(zone).date();
    if (!instant.isValid() || day == now.toTimeZone(zone).date())
        return time;
    return QLocale().toString(day, QStringLiteral("ddd")) + QLatin1Char(' ') + time;
}

QString condition(const WeatherCode &code, const std::optional<bool> &isDay)
{
    return code.has_value() ? conditionText(*code, isDay.value_or(true)) : QString();
}

QString ago(const QDateTime &then, const QDateTime &now)
{
    if (!then.isValid())
        return QStringLiteral("unknown age");
    const qint64 minutes = then.secsTo(now) / 60;
    if (minutes < 1)
        return QStringLiteral("just now");
    if (minutes < 60)
        return QStringLiteral("%1 min ago").arg(minutes);
    return QStringLiteral("%1 h ago").arg(minutes / 60);
}

// ---- JSON --------------------------------------------------------------------

QJsonValue json(const Reading &reading)
{
    return reading.has_value() ? QJsonValue(*reading) : QJsonValue(QJsonValue::Null);
}

QJsonValue json(const std::optional<int> &value)
{
    return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

QJsonValue json(const std::optional<bool> &value)
{
    return value.has_value() ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

QJsonValue json(const QDateTime &instant, const QTimeZone &zone)
{
    if (!instant.isValid())
        return QJsonValue(QJsonValue::Null);
    return instant.toTimeZone(zone).toString(Qt::ISODate);
}

QJsonObject placeJson(const Place &place)
{
    QJsonObject out;
    out.insert(QStringLiteral("id"), double(place.id));
    out.insert(QStringLiteral("name"), place.name);
    out.insert(QStringLiteral("admin1"), place.admin1);
    out.insert(QStringLiteral("country"), place.country);
    out.insert(QStringLiteral("countryCode"), place.countryCode);
    out.insert(QStringLiteral("latitude"), place.coordinate.latitude);
    out.insert(QStringLiteral("longitude"), place.coordinate.longitude);
    out.insert(QStringLiteral("timezone"), place.timezone);
    out.insert(QStringLiteral("home"), place.isHome);
    return out;
}

QJsonObject currentJson(const CurrentConditions &c, const QTimeZone &zone)
{
    QJsonObject out;
    out.insert(QStringLiteral("time"), json(c.time, zone));
    out.insert(QStringLiteral("temperature"), json(c.temperature));
    out.insert(QStringLiteral("apparentTemperature"), json(c.apparentTemperature));
    out.insert(QStringLiteral("relativeHumidity"), json(c.relativeHumidity));
    out.insert(QStringLiteral("dewPoint"), json(c.dewPoint));
    out.insert(QStringLiteral("precipitation"), json(c.precipitation));
    out.insert(QStringLiteral("windSpeed"), json(c.windSpeed));
    out.insert(QStringLiteral("windGust"), json(c.windGust));
    out.insert(QStringLiteral("windDirection"), json(c.windDirection));
    out.insert(QStringLiteral("pressureMsl"), json(c.pressureMsl));
    out.insert(QStringLiteral("cloudCover"), json(c.cloudCover));
    out.insert(QStringLiteral("visibility"), json(c.visibility));
    out.insert(QStringLiteral("uvIndex"), json(c.uvIndex));
    out.insert(QStringLiteral("weatherCode"), json(c.weatherCode));
    out.insert(QStringLiteral("condition"), condition(c.weatherCode, c.isDay));
    out.insert(QStringLiteral("isDay"), json(c.isDay));
    return out;
}

QJsonObject hourJson(const HourlyPoint &h, const QTimeZone &zone)
{
    QJsonObject out;
    out.insert(QStringLiteral("time"), json(h.time, zone));
    out.insert(QStringLiteral("temperature"), json(h.temperature));
    out.insert(QStringLiteral("apparentTemperature"), json(h.apparentTemperature));
    out.insert(QStringLiteral("relativeHumidity"), json(h.relativeHumidity));
    out.insert(QStringLiteral("precipitation"), json(h.precipitation));
    out.insert(QStringLiteral("precipitationProbability"), json(h.precipitationProbability));
    out.insert(QStringLiteral("snowfall"), json(h.snowfall));
    out.insert(QStringLiteral("windSpeed"), json(h.windSpeed));
    out.insert(QStringLiteral("windGust"), json(h.windGust));
    out.insert(QStringLiteral("windDirection"), json(h.windDirection));
    out.insert(QStringLiteral("pressureMsl"), json(h.pressureMsl));
    out.insert(QStringLiteral("cloudCover"), json(h.cloudCover));
    out.insert(QStringLiteral("visibility"), json(h.visibility));
    out.insert(QStringLiteral("uvIndex"), json(h.uvIndex));
    out.insert(QStringLiteral("weatherCode"), json(h.weatherCode));
    out.insert(QStringLiteral("condition"), condition(h.weatherCode, h.isDay));
    out.insert(QStringLiteral("isDay"), json(h.isDay));
    return out;
}

QJsonObject dayJson(const DailyPoint &d, const QTimeZone &zone)
{
    QJsonObject out;
    out.insert(QStringLiteral("date"), d.date.toString(Qt::ISODate));
    out.insert(QStringLiteral("temperatureMax"), json(d.temperatureMax));
    out.insert(QStringLiteral("temperatureMin"), json(d.temperatureMin));
    out.insert(QStringLiteral("precipitationSum"), json(d.precipitationSum));
    out.insert(QStringLiteral("precipitationProbabilityMax"), json(d.precipitationProbabilityMax));
    out.insert(QStringLiteral("snowfallSum"), json(d.snowfallSum));
    out.insert(QStringLiteral("windSpeedMax"), json(d.windSpeedMax));
    out.insert(QStringLiteral("windGustMax"), json(d.windGustMax));
    out.insert(QStringLiteral("windDirectionDominant"), json(d.windDirectionDominant));
    out.insert(QStringLiteral("uvIndexMax"), json(d.uvIndexMax));
    out.insert(QStringLiteral("weatherCode"), json(d.weatherCode));
    out.insert(QStringLiteral("condition"), condition(d.weatherCode, true));
    out.insert(QStringLiteral("sunrise"), json(d.sunrise, zone));
    out.insert(QStringLiteral("sunset"), json(d.sunset, zone));
    return out;
}

QJsonObject alertJson(const Alert &a, const QTimeZone &zone)
{
    QJsonObject out;
    out.insert(QStringLiteral("event"), a.event);
    out.insert(QStringLiteral("headline"), a.headline);
    out.insert(QStringLiteral("severity"), alertSeverityKey(a.severity));
    out.insert(QStringLiteral("sender"), a.senderName);
    out.insert(QStringLiteral("onset"), json(a.onset, zone));
    out.insert(QStringLiteral("ends"), json(a.hazardEnd(), zone));
    return out;
}

// ---- CSV -------------------------------------------------------------------------
//
// One row per point, canonical units, a header naming every column. A cell
// with no reading is empty rather than "null" - that is what a spreadsheet
// reads as missing.

QString cell(const Reading &reading)
{
    return reading.has_value() ? QString::number(*reading, 'f', 1) : QString();
}

// A text cell, quoted the way RFC 4180 says when it has to be.
//
// The numeric and ISO columns can never need this; the text ones can and do.
// A place name out of GeoNames is whatever the dataset holds - "Washington,
// D.C." is a real row - and joined with a bare comma it shifts every later
// column by one, so `places --csv` grew a tenth field and a parser read the
// home flag as the timezone. Condition text and issuer names are the same
// hazard from a different source.
QString text(const QString &value)
{
    if (!value.contains(QLatin1Char(',')) && !value.contains(QLatin1Char('"'))
        && !value.contains(QLatin1Char('\n')) && !value.contains(QLatin1Char('\r')))
        return value;

    QString quoted = value;
    quoted.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + quoted + QLatin1Char('"');
}

QString cell(const std::optional<int> &value)
{
    return value.has_value() ? QString::number(*value) : QString();
}

// ---- the engine, assembled the way the daemon assembles it -------------------

struct Engine {
    std::unique_ptr<Clock>            clock;
    std::unique_ptr<CacheStore>       cache;
    std::unique_ptr<HttpClient>       http;
    std::unique_ptr<ProviderRegistry> registry;
    std::unique_ptr<LocationController> places;
    std::unique_ptr<OpenMeteoGeocoder>  geocoder;

    Fixture fixture;
    bool    fixtureMode = false;

    // Providers are parented to this and die with it.
    QObject owner;
};

std::unique_ptr<Engine> buildEngine(const QString &fixtureName)
{
    auto engine = std::make_unique<Engine>();

    if (!fixtureName.isEmpty()) {
        engine->fixtureMode = true;
        engine->fixture     = fixtures::load(fixtureName);
        engine->clock       = std::make_unique<FrozenClock>(engine->fixture.recordedAt);
        engine->registry    = std::make_unique<ProviderRegistry>();

        // No cache and no network: a fixture is a recording, and recording
        // what it answered would be a cache row from a day that never happened.
        auto *forecast = new FixtureForecastProvider(engine->fixture, &engine->owner);
        auto *alerts   = new FixtureAlertProvider(engine->fixture, &engine->owner);
        engine->registry->addForecastProvider(forecast, 100);
        engine->registry->addAlertProvider(alerts, 0);
        return engine;
    }

    engine->clock = std::make_unique<SystemClock>();
    engine->cache = std::make_unique<CacheStore>(engine->clock.get());
    if (const Status opened = engine->cache->open(CacheStore::defaultDatabasePath()); !opened) {
        // The same posture as the app and the daemon: a cache that will not
        // open is a tool that asks the network, not a tool that cannot answer.
        std::fprintf(stderr, "climat-cli: the cache could not be opened (%s); answering from "
                             "the network\n",
                     qPrintable(opened.error().toString()));
    }

    engine->http = std::make_unique<HttpClient>(engine->clock.get());
    engine->http->setValidatorStore(engine->cache.get());
    engine->registry = std::make_unique<ProviderRegistry>();

    auto *openMeteo = new OpenMeteoForecastProvider(engine->http.get(), engine->clock.get(),
                                                    &engine->owner);
    openMeteo->setCache(engine->cache.get());
    auto *metNo = new MetNoForecastProvider(engine->http.get(), engine->clock.get(), &engine->owner);
    metNo->setCache(engine->cache.get());
    auto *eccc = new EcccAlertProvider(engine->http.get(), engine->clock.get(), &engine->owner);
    eccc->setCache(engine->cache.get());
    auto *nws = new NwsAlertProvider(engine->http.get(), engine->clock.get(), &engine->owner);
    nws->setCache(engine->cache.get());

    engine->registry->addForecastProvider(openMeteo, 100);
    engine->registry->addForecastProvider(metNo, 200);
    engine->registry->addAlertProvider(eccc, 0);
    engine->registry->addAlertProvider(nws, 0);

    engine->places = std::make_unique<LocationController>(engine->cache.get());
    engine->places->load();

    engine->geocoder = std::make_unique<OpenMeteoGeocoder>(engine->http.get(), engine->cache.get(),
                                                           engine->clock.get());
    return engine;
}

// ---- which place --------------------------------------------------------------

// `--place` empty: the app's current place, then its home. A number: a saved
// place by its id. Anything else: a search, whose first answer is used and
// NOT saved - the app's list is the app's.
Result<Place> resolvePlace(Engine &engine, const QString &asked, int timeoutMs)
{
    if (engine.fixtureMode)
        return engine.fixture.place;

    if (asked.isEmpty()) {
        const Place current = engine.places->currentPlace();
        if (current.id != 0)
            return current;
        const int home = engine.places->homeIndex();
        if (home >= 0)
            return engine.places->placeAt(home);
        return Error(ErrorKind::NotFound,
                     QStringLiteral("no saved place: open Climat once and choose one, or pass "
                                    "--place <name>"));
    }

    bool         numeric = false;
    const qint64 id      = asked.toLongLong(&numeric);
    if (numeric) {
        const QList<Place> all = engine.places->places();
        for (const Place &place : all) {
            if (place.id == id)
                return place;
        }
        return Error(ErrorKind::NotFound,
                     QStringLiteral("no saved place has id %1 - `climat-cli places` lists them")
                         .arg(id));
    }

    GeocodeQuery query;
    query.name  = asked;
    query.count = 1;

    const std::optional<Result<QList<Place>>> found =
        await(engine.geocoder->search(query), timeoutMs);
    if (!found.has_value())
        return Error(ErrorKind::Timeout, QStringLiteral("the search for \"%1\" timed out").arg(asked));
    if (!*found)
        return found->error();
    if (found->value().isEmpty())
        return Error(ErrorKind::NotFound, QStringLiteral("nothing is called \"%1\"").arg(asked));
    return found->value().constFirst();
}

// ---- the commands -----------------------------------------------------------------

struct Run {
    QString     command;
    int         count       = 0;
    QString     place;
    QString     unitsOverride;
    QString     fixture;
    bool        json        = false;
    bool        csv         = false;
    int         timeoutMs   = kDefaultTimeoutMs;
};

int printPlaces(Engine &engine, const Run &run, QTextStream &out)
{
    QList<Place> all;
    if (engine.fixtureMode)
        all.append(engine.fixture.place);
    else
        all = engine.places->places();

    if (run.json) {
        QJsonArray array;
        for (const Place &place : all)
            array.append(placeJson(place));
        out << QJsonDocument(array).toJson(QJsonDocument::Indented);
        return 0;
    }

    if (run.csv) {
        out << "id,name,admin1,country,countryCode,latitude,longitude,timezone,home\n";
        for (const Place &place : all) {
            out << place.id << ',' << text(place.name) << ',' << text(place.admin1) << ','
                << text(place.country) << ',' << text(place.countryCode) << ','
                << QString::number(place.coordinate.latitude, 'f', 5) << ','
                << QString::number(place.coordinate.longitude, 'f', 5) << ','
                << text(place.timezone) << ',' << (place.isHome ? "1" : "0") << '\n';
        }
        return 0;
    }

    if (all.isEmpty()) {
        out << "No saved places. Open Climat once and choose one, or pass --place <name>.\n";
        return 0;
    }
    for (const Place &place : all) {
        out << (place.isHome ? "* " : "  ") << place.id << "  " << place.label();
        if (!place.region().isEmpty())
            out << "  (" << place.region() << ')';
        out << '\n';
    }
    return 0;
}

// ---- one definition of "now" ------------------------------------------------
//
// The same rule app/viewmodels/conditionsdata.cpp follows, and for the same
// reason it was written there. Open-Meteo's `current` block is stamped to the
// quarter hour and is the better answer for an INSTANT - the temperature at
// 4:47 rather than at four - but only while it is actually current. A cached
// response is served for as long as its freshness window allows, so the block
// inside it can be hours old, and the CLI was printing that as the conditions
// and labelling it "just now" off the fetch time.
//
// Measured on the recorded fixtures: toronto's block says 06:30 against a
// recording at 12:28, so `climat-cli now` reported 15 °C and "Sunny" while
// `climat-cli hourly` led with 23 °C for the same instant, out of one process
// and one file.
//
// Within the hour, the block. Otherwise the hour the reader is standing in,
// which is what the chart and the hourly list draw.
CurrentConditions observationAt(const Forecast &forecast, const QDateTime &now)
{
    const CurrentConditions &block = forecast.current;
    const QList<HourlyPoint>  hours = asHourStarting(forecast.hourly);

    // The hour the reader is standing in, whether or not the block is usable -
    // because it is needed in both branches.
    const HourlyPoint *standing = nullptr;
    for (int i = hours.size() - 1; i >= 0; --i) {
        const HourlyPoint &hour = hours.at(i);
        if (hour.time.isValid() && hour.time <= now) {
            standing = &hour;
            break;
        }
    }

    if (block.time.isValid() && qAbs(block.time.secsTo(now)) <= 3600) {
        CurrentConditions live = block;

        // Even a current block does not get the last word on these two, and
        // conditionsdata.cpp says why at length: a weather code describes a
        // STRETCH of weather rather than a moment, which is why the hourly
        // series shifts it, and the block's `precipitation` is the PRECEDING
        // hour where the series is the hour starting. Two conventions in one
        // process is how "Mainly sunny" came to sit above a Now column drawing
        // heavy rain - measured on five of twelve cities in one afternoon.
        //
        // So the block is trusted for the instants it is genuinely better at
        // and the interval fields come from the interval.
        if (standing != nullptr) {
            live.weatherCode   = standing->weatherCode;
            live.precipitation = standing->precipitation;
        }
        return live;
    }

    // Not current. Rebuilt from the hour, exactly as the app does.
    if (standing == nullptr) {
        // No hour covers `now` either - a forecast with no usable series at
        // all. Empty, rather than the stale block this function exists to
        // suppress; the app leaves the observation empty here too.
        return {};
    }

    CurrentConditions rebuilt;
    rebuilt.time                = standing->time;
    rebuilt.temperature         = standing->temperature;
    rebuilt.apparentTemperature = standing->apparentTemperature;
    rebuilt.relativeHumidity    = standing->relativeHumidity;
    rebuilt.dewPoint            = standing->dewPoint;
    rebuilt.precipitation       = standing->precipitation;
    rebuilt.windSpeed           = standing->windSpeed;
    rebuilt.windGust            = standing->windGust;
    rebuilt.windDirection       = standing->windDirection;
    rebuilt.pressureMsl         = standing->pressureMsl;
    rebuilt.cloudCover          = standing->cloudCover;
    rebuilt.visibility          = standing->visibility;
    rebuilt.uvIndex             = standing->uvIndex;
    rebuilt.weatherCode         = standing->weatherCode;
    rebuilt.isDay               = standing->isDay;
    return rebuilt;
}

int printNow(const Forecast &forecast, const QString &servedBy, const QList<Alert> &alerts,
             const Place &place, const Preferences &prefs, const Run &run, const QDateTime &now,
             QTextStream &out)
{
    const QTimeZone         zone = forecast.timeZone.isValid() ? forecast.timeZone : QTimeZone::utc();
    const CurrentConditions c    = observationAt(forecast, now);

    if (run.json) {
        QJsonObject root;
        root.insert(QStringLiteral("place"), placeJson(place));
        root.insert(QStringLiteral("servedBy"), servedBy);
        root.insert(QStringLiteral("fetchedAt"), json(forecast.fetchedAt, zone));
        root.insert(QStringLiteral("current"), currentJson(c, zone));
        QJsonArray warnings;
        for (const Alert &alert : alerts)
            warnings.append(alertJson(alert, zone));
        root.insert(QStringLiteral("alerts"), warnings);
        out << QJsonDocument(root).toJson(QJsonDocument::Indented);
        return 0;
    }

    if (run.csv) {
        out << "time,temperature,apparentTemperature,relativeHumidity,precipitation,windSpeed,"
               "windGust,windDirection,pressureMsl,cloudCover,visibility,uvIndex,weatherCode,"
               "condition\n";
        out << json(c.time, zone).toString() << ',' << cell(c.temperature) << ','
            << cell(c.apparentTemperature) << ',' << cell(c.relativeHumidity) << ','
            << cell(c.precipitation) << ',' << cell(c.windSpeed) << ',' << cell(c.windGust) << ','
            << cell(c.windDirection) << ',' << cell(c.pressureMsl) << ',' << cell(c.cloudCover)
            << ',' << cell(c.visibility) << ',' << cell(c.uvIndex) << ',' << cell(c.weatherCode)
            << ',' << text(condition(c.weatherCode, c.isDay)) << '\n';
        return 0;
    }

    using Q = units::Quantity;
    out << place.label();
    const QString sky = condition(c.weatherCode, c.isDay);
    if (!sky.isEmpty())
        out << "  " << sky;
    out << '\n';

    out << shown(prefs, Q::Temperature, c.temperature);
    if (c.apparentTemperature.has_value())
        out << " · feels like " << shown(prefs, Q::Temperature, c.apparentTemperature);
    if (c.windSpeed.has_value()) {
        out << " · wind " << shown(prefs, Q::Wind, c.windSpeed);
        const QString from = compass(c.windDirection);
        if (!from.isEmpty())
            out << ' ' << from;
    }
    if (c.relativeHumidity.has_value())
        out << " · humidity " << percent(c.relativeHumidity);
    if (c.pressureMsl.has_value())
        out << " · " << shown(prefs, Q::Pressure, c.pressureMsl);
    out << '\n';

    // No area is printed, so Seattle's three Air Quality Alerts, one per list of
    // counties, came out as three identical lines. An identical line is printed
    // once, with how many notices it stands for.
    QStringList lines;
    QList<int>  notices;
    for (const Alert &alert : alerts) {
        QString line = alert.event + QStringLiteral(" (") + alertSeverityName(alert.severity)
                       + QLatin1Char(')');
        if (alert.hazardEnd().isValid())
            line += QStringLiteral(" · until ") + until(prefs, alert.hazardEnd(), now, zone);
        const qsizetype seen = lines.indexOf(line);
        if (seen >= 0) {
            ++notices[seen];
        } else {
            lines.append(line);
            notices.append(1);
        }
    }
    for (qsizetype i = 0; i < lines.size(); ++i) {
        out << "! " << lines.at(i);
        if (notices.at(i) > 1)
            out << " · " << notices.at(i) << " notices";
        out << '\n';
    }

    // fetchedAt, which is what the app's own "Updated N minutes ago" reports.
    //
    // This briefly measured the reading instead, to stop a six-hour-old
    // `current` block being called "just now". That was the wrong half of the
    // fix: observationAt() above now makes the reading current by
    // construction, so its stamp is under an hour old whenever any row covers
    // the moment - and a line derived from it could never say a cached
    // response was stale, which is the one thing this line is for.
    out << "Updated " << ago(forecast.fetchedAt, now) << " · " << servedBy << '\n';
    return 0;
}

int printHourly(const Forecast &forecast, const Preferences &prefs, const Run &run,
                const QDateTime &now, QTextStream &out)
{
    const QTimeZone zone = forecast.timeZone.isValid() ? forecast.timeZone : QTimeZone::utc();

    // Converted first, then windowed, and the order is the whole of it.
    // libclimat hands out Open-Meteo's own convention, where the accumulations
    // on the row stamped `t` describe the hour ENDING at `t`; every screen in
    // the app reads asHourStarting(), where they describe the hour beginning
    // there. Windowed without converting, this printed the right times against
    // the wrong rainfall - kampala's 0.4 mm of drizzle sat in the CLI's 10:00
    // row and the app's 09:00 row, from one file.
    const QList<HourlyPoint> hours = asHourStarting(forecast.hourly);

    // From the hour we are standing in, forward. `past_days=1` puts yesterday
    // in the series so the app can draw behind the marker; a status bar wants
    // what is next.
    QList<HourlyPoint> ahead;
    for (const HourlyPoint &hour : hours) {
        if (hour.time.isValid() && hour.time.addSecs(3600) > now)
            ahead.append(hour);
        if (ahead.size() >= run.count)
            break;
    }

    if (ahead.isEmpty()) {
        // Not silence. A status bar cannot tell an empty answer from a calm
        // one, and this forecast has no hours in it at all.
        std::fputs("climat-cli: this forecast carries no hours.\n", stderr);
        return kExitFetch;
    }

    if (run.json) {
        QJsonArray array;
        for (const HourlyPoint &hour : ahead)
            array.append(hourJson(hour, zone));
        QJsonObject root;
        root.insert(QStringLiteral("hourly"), array);
        out << QJsonDocument(root).toJson(QJsonDocument::Indented);
        return 0;
    }

    if (run.csv) {
        out << "time,temperature,apparentTemperature,relativeHumidity,precipitation,"
               "precipitationProbability,snowfall,windSpeed,windGust,windDirection,pressureMsl,"
               "cloudCover,visibility,uvIndex,weatherCode,condition\n";
        for (const HourlyPoint &h : ahead) {
            out << json(h.time, zone).toString() << ',' << cell(h.temperature) << ','
                << cell(h.apparentTemperature) << ',' << cell(h.relativeHumidity) << ','
                << cell(h.precipitation) << ',' << cell(h.precipitationProbability) << ','
                << cell(h.snowfall) << ',' << cell(h.windSpeed) << ',' << cell(h.windGust) << ','
                << cell(h.windDirection) << ',' << cell(h.pressureMsl) << ',' << cell(h.cloudCover)
                << ',' << cell(h.visibility) << ',' << cell(h.uvIndex) << ',' << cell(h.weatherCode)
                << ',' << text(condition(h.weatherCode, h.isDay)) << '\n';
        }
        return 0;
    }

    using Q = units::Quantity;
    for (const HourlyPoint &h : ahead) {
        out << clock(prefs, h.time, zone).rightJustified(8) << "  "
            << shown(prefs, Q::Temperature, h.temperature).rightJustified(6) << "  "
            << condition(h.weatherCode, h.isDay).leftJustified(22);
        if (h.precipitationProbability.has_value())
            out << percent(h.precipitationProbability).rightJustified(5);
        else
            out << "     ";
        out << "  " << shown(prefs, Q::Precipitation, h.precipitation).rightJustified(8)
            << "  wind " << shown(prefs, Q::Wind, h.windSpeed);
        const QString from = compass(h.windDirection);
        if (!from.isEmpty())
            out << ' ' << from;
        out << '\n';
    }
    return 0;
}

int printDaily(const Forecast &forecast, const Preferences &prefs, const Run &run,
               const QDateTime &now, QTextStream &out)
{
    const QTimeZone zone  = forecast.timeZone.isValid() ? forecast.timeZone : QTimeZone::utc();
    const QDate     today = now.toTimeZone(zone).date();

    QList<DailyPoint> ahead;
    for (const DailyPoint &day : forecast.daily) {
        if (day.date.isValid() && day.date >= today)
            ahead.append(day);
        if (ahead.size() >= run.count)
            break;
    }

    if (ahead.isEmpty()) {
        // openmeteoadapter only validates the daily block if it is present, so
        // a response without one parses green and leaves this empty. Saying so
        // is the difference between "no data" and "clear skies".
        std::fputs("climat-cli: this forecast carries no days.\n", stderr);
        return kExitFetch;
    }

    if (run.json) {
        QJsonArray array;
        for (const DailyPoint &day : ahead)
            array.append(dayJson(day, zone));
        QJsonObject root;
        root.insert(QStringLiteral("daily"), array);
        out << QJsonDocument(root).toJson(QJsonDocument::Indented);
        return 0;
    }

    if (run.csv) {
        out << "date,temperatureMax,temperatureMin,precipitationSum,precipitationProbabilityMax,"
               "snowfallSum,windSpeedMax,windGustMax,windDirectionDominant,uvIndexMax,weatherCode,"
               "condition,sunrise,sunset\n";
        for (const DailyPoint &d : ahead) {
            out << d.date.toString(Qt::ISODate) << ',' << cell(d.temperatureMax) << ','
                << cell(d.temperatureMin) << ',' << cell(d.precipitationSum) << ','
                << cell(d.precipitationProbabilityMax) << ',' << cell(d.snowfallSum) << ','
                << cell(d.windSpeedMax) << ',' << cell(d.windGustMax) << ','
                << cell(d.windDirectionDominant) << ',' << cell(d.uvIndexMax) << ','
                << cell(d.weatherCode) << ',' << text(condition(d.weatherCode, true)) << ','
                << json(d.sunrise, zone).toString() << ',' << json(d.sunset, zone).toString() << '\n';
        }
        return 0;
    }

    using Q = units::Quantity;
    const QLocale locale;
    for (const DailyPoint &d : ahead) {
        const QString label = d.date == today ? QStringLiteral("Today")
                                              : locale.toString(d.date, QStringLiteral("ddd d MMM"));
        out << label.leftJustified(12) << "  " << shown(prefs, Q::Temperature, d.temperatureMax).rightJustified(6)
            << " / " << shown(prefs, Q::Temperature, d.temperatureMin).leftJustified(6) << "  "
            << condition(d.weatherCode, true).leftJustified(22);
        if (d.precipitationProbabilityMax.has_value())
            out << percent(d.precipitationProbabilityMax).rightJustified(5);
        else
            out << "     ";
        out << "  " << shown(prefs, Q::Precipitation, d.precipitationSum).rightJustified(8) << '\n';
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // The same names the app, the daemon and the widget host use, because they
    // are what QSettings and QStandardPaths key on: this process has to find
    // the INI the app wrote and the cache the app filled.
    QCoreApplication::setOrganizationName(QStringLiteral("Climat"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("github.io"));
    QCoreApplication::setApplicationName(QStringLiteral(CLIMAT_APP_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(CLIMAT_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("The forecast, for a status bar, a script or a terminal.\n\n"
                       "Commands:\n"
                       "  now          the current conditions and any warning in force\n"
                       "  hourly [N]   the next N hours (default 12)\n"
                       "  daily [N]    the next N days (default 7)\n"
                       "  places       the saved places, home marked with *\n\n"
                       "Text output uses the units and clock the app's preferences chose.\n"
                       "--json and --csv are canonical: °C, km/h, hPa, km, mm, ISO 8601."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("now | hourly | daily | places"));
    parser.addPositionalArgument(QStringLiteral("count"), QStringLiteral("hours or days"), QStringLiteral("[N]"));

    const QCommandLineOption placeOption(
        QStringLiteral("place"),
        QStringLiteral("A saved place by id, or any place by name. Defaults to the app's current place."),
        QStringLiteral("id|name"));
    const QCommandLineOption jsonOption(QStringLiteral("json"), QStringLiteral("JSON, canonical units."));
    const QCommandLineOption csvOption(QStringLiteral("csv"), QStringLiteral("CSV, canonical units."));
    const QCommandLineOption unitsOption(
        QStringLiteral("units"), QStringLiteral("metric or imperial, overriding the app's preference."),
        QStringLiteral("system"));
    const QCommandLineOption fixtureOption(
        QStringLiteral("fixture"),
        QStringLiteral("A recorded forecast at a frozen clock, no network. One of: %1")
            .arg(fixtures::names().join(QStringLiteral(", "))),
        QStringLiteral("name"));
    const QCommandLineOption timeoutOption(
        QStringLiteral("timeout"), QStringLiteral("How long to wait for the network, in ms."),
        QStringLiteral("ms"), QString::number(kDefaultTimeoutMs));

    parser.addOption(placeOption);
    parser.addOption(jsonOption);
    parser.addOption(csvOption);
    parser.addOption(unitsOption);
    parser.addOption(fixtureOption);
    parser.addOption(timeoutOption);
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        std::fputs(qPrintable(parser.helpText()), stderr);
        return kExitUsage;
    }

    Run run;
    run.command       = positional.constFirst();
    run.place         = parser.value(placeOption);
    run.unitsOverride = parser.value(unitsOption);
    run.fixture       = parser.value(fixtureOption);
    run.json          = parser.isSet(jsonOption);
    run.csv           = parser.isSet(csvOption);
    run.timeoutMs     = parser.value(timeoutOption).toInt();

    const bool wantsCount = run.command == QLatin1String("hourly") || run.command == QLatin1String("daily");
    if (run.command != QLatin1String("now") && run.command != QLatin1String("places") && !wantsCount) {
        std::fprintf(stderr, "climat-cli: \"%s\" is not a command. Try: now, hourly, daily, places.\n",
                     qPrintable(run.command));
        return kExitUsage;
    }
    if (run.json && run.csv) {
        std::fputs("climat-cli: --json and --csv are two answers to one question; pick one.\n", stderr);
        return kExitUsage;
    }
    if (!run.unitsOverride.isEmpty() && run.unitsOverride != QLatin1String("metric")
        && run.unitsOverride != QLatin1String("imperial")) {
        std::fprintf(stderr, "climat-cli: --units takes metric or imperial, not \"%s\".\n",
                     qPrintable(run.unitsOverride));
        return kExitUsage;
    }
    if (!run.fixture.isEmpty() && !run.place.isEmpty()) {
        // A fixture is a recording of one place. Honouring --place would mean
        // geocoding a second one and then answering with the first one's
        // weather, which is app/appoptions.cpp's rule about the same pair of
        // flags: a named place and a recorded forecast are two answers to one
        // question.
        std::fputs("climat-cli: --fixture is a recording of one place, so --place cannot "
                   "choose another. Drop one of them.\n", stderr);
        return kExitUsage;
    }
    if (!run.fixture.isEmpty() && !fixtures::exists(run.fixture)) {
        std::fprintf(stderr, "climat-cli: no fixture called \"%s\". Known: %s\n", qPrintable(run.fixture),
                     qPrintable(fixtures::names().join(QStringLiteral(", "))));
        return kExitUsage;
    }
    if (run.timeoutMs <= 0) {
        std::fputs("climat-cli: --timeout wants a positive number of milliseconds.\n", stderr);
        return kExitUsage;
    }

    run.count = run.command == QLatin1String("hourly") ? 12 : 7;
    if (wantsCount && positional.size() > 1) {
        bool      ok  = false;
        const int got = positional.at(1).toInt(&ok);
        if (!ok || got <= 0) {
            std::fprintf(stderr, "climat-cli: \"%s\" is not a count.\n", qPrintable(positional.at(1)));
            return kExitUsage;
        }
        run.count = got;
    }

    QTextStream out(stdout);

    std::unique_ptr<Engine> engine = buildEngine(run.fixture);

    if (run.command == QLatin1String("places"))
        return printPlaces(*engine, run, out);

    const Result<Place> place = resolvePlace(*engine, run.place, run.timeoutMs);
    if (!place) {
        std::fprintf(stderr, "climat-cli: %s\n", qPrintable(place.error().toString()));
        return place.errorKind() == ErrorKind::Timeout ? kExitTimeout : kExitNoPlace;
    }

    ForecastRequest request;
    request.coord    = place.value().coordinate;
    request.days     = kForecastDays;
    request.timeZone = QTimeZone(place.value().timezone.toUtf8());

    const std::optional<Result<ForecastAnswer>> forecast =
        await(engine->registry->fetchForecast(request), run.timeoutMs);
    if (!forecast.has_value()) {
        std::fprintf(stderr, "climat-cli: no answer within %d ms.\n", run.timeoutMs);
        return kExitTimeout;
    }
    if (!*forecast) {
        std::fprintf(stderr, "climat-cli: %s\n", qPrintable(forecast->error().toString()));
        return kExitFetch;
    }

    const Preferences prefs = readPreferences(run.unitsOverride);
    const QDateTime   now   = engine->clock->now();

    if (run.command == QLatin1String("hourly"))
        return printHourly(forecast->value().value, prefs, run, now, out);
    if (run.command == QLatin1String("daily"))
        return printDaily(forecast->value().value, prefs, run, now, out);

    // `now` also asks for the warnings, which fan out rather than fall back and
    // may be absent where nobody covers the place - in which case there is
    // nothing to print and nothing to apologise for.
    AlertRequest alertRequest;
    alertRequest.coord = place.value().coordinate;

    QList<Alert> inForce;
    const std::optional<Result<AlertAnswer>> alerts =
        await(engine->registry->fetchAlerts(alertRequest), run.timeoutMs);

    if (!alerts.has_value()) {
        // Silence here would be indistinguishable from "no warnings", which is
        // the one thing this feature must never say when it does not know.
        std::fputs("climat-cli: the warnings could not be checked (timed out).\n", stderr);
    } else if (*alerts) {
        inForce = alerts->value().value.displayableAt(now);
    } else if (alerts->errorKind() != ErrorKind::Unsupported) {
        // Unsupported is the ordinary case - most of the world has no covering
        // provider and the app hides the feature. Anything else is a service
        // we should have been able to reach and could not.
        std::fprintf(stderr, "climat-cli: the warnings could not be checked: %s\n",
                     qPrintable(alerts->error().toString()));
    }

    return printNow(forecast->value().value, forecast->value().servedBy, inForce, place.value(), prefs,
                    run, now, out);
}
