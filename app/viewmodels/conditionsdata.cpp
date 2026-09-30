// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later

#include "conditionsdata.h"

#include "appengine.h"
#include "forecastdata.h"
#include "timeformat.h"
#include "units.h"

#include "libclimat/domain/hourconvention.h"
#include "libclimat/domain/scales.h"
#include "libclimat/domain/timeaxis.h"
#include "libclimat/domain/weathercode.h"

#include <QLocale>
#include <QRegularExpression>
#include <QStringList>
#include <QQmlEngine>

#include <cmath>
#include <limits>

using namespace climat;

namespace {

// Six behind, five ahead. detaildata.js's window, kept because every card's
// sparkline geometry is built around where index 6 falls.
constexpr int kContextBefore = 6;
constexpr int kContextAfter  = 6;

double value(const Reading &reading)
{
    return reading.has_value() ? *reading : qQNaN();
}

double display(const Reading &reading, Units::Quantity quantity)
{
    if (!reading.has_value())
        return qQNaN();
    return Units::instance()->convert(quantity, *reading);
}

int roundedDisplay(const Reading &reading, Units::Quantity quantity)
{
    const double converted = display(reading, quantity);
    return qIsNaN(converted) ? 0 : int(std::lround(converted));
}

// "12:28 PM", or "12:28" for a reader who asked for a 24-hour clock.
QString clockLabel(const QDateTime &instant, const QTimeZone &zone)
{
    if (!instant.isValid())
        return {};
    return TimeFormat::instance()->clock(instant.toTimeZone(zone).time());
}

// "3:00 p.m." - the reference's spelling for a sentence, as distinct from the
// "3:00 PM" a label uses. detaildata.js used both, in the same two places, and
// TimeFormat keeps the distinction because losing it would mean a body sentence
// shouting its meridiem at the reader mid-paragraph.
QString sentenceTime(const QDateTime &instant, const QTimeZone &zone)
{
    if (!instant.isValid())
        return {};
    return TimeFormat::instance()->sentence(instant.toTimeZone(zone).time());
}

// That spelling ends in a full stop of its own, so a sentence that closes on a
// time, "Clearest around %1.", printed "p.m.." on a 12-hour clock. Dropping the
// template's stop is no fix: a 24-hour clock says "15:00", which needs one. So
// every template keeps its stop and a sentence is passed through here once it is
// built. Exactly two stops become one; an ellipsis is left alone.
QString oneStop(QString sentence)
{
    static const QRegularExpression doubled(QStringLiteral("(?<!\\.)\\.\\.(?!\\.)"));
    return sentence.replace(doubled, QStringLiteral("."));
}

// ---- the published scales ---------------------------------------------------
//
// Each of these is somebody else's table, transcribed. None of them is a
// judgement of ours, and none of them may be adjusted to make a card look
// better balanced.
//
// They used to be defined here, and they moved to libclimat/domain/scales.h the
// day a second thing had to draw weather. A UV dial on the desktop has to put
// the same word under the same number as the card in the app, and the only way
// to guarantee that is for there to be one table. See that header.

using climat::scales::aqiBand;
using climat::scales::beaufortForce;
using climat::scales::beaufortName;
using climat::scales::compassPoint;
using climat::scales::uvBand;
using climat::scales::visibilityBand;

// up | down | steady, from the difference between now and three hours out.
// The badge tracks the *number*; whether that direction is good news is the
// body's job. docs/10-design-system.md §10.5.
QString trendOf(double from, double to, double deadband)
{
    if (qIsNaN(from) || qIsNaN(to))
        return QStringLiteral("steady");
    const double delta = to - from;
    if (delta > deadband)
        return QStringLiteral("up");
    if (delta < -deadband)
        return QStringLiteral("down");
    return QStringLiteral("steady");
}

QString toneFor(bool good, bool caution)
{
    return good ? QStringLiteral("good")
                : (caution ? QStringLiteral("caution") : QStringLiteral("poor"));
}


// ---- absent is not zero, and the screen has to say so ---------------------
//
// libclimat/domain/forecast.h opens with this argument and it lands here: "a
// plain double for a field MET Norway does not carry means the gust row reads
// '0 km/h' during a gale and nothing anywhere goes red." Every block below
// therefore carries a `reading` - the value and its unit, already formatted, or
// an em dash - and every QML file that PRINTS a reading uses that rather than
// gluing `value` to `unit`. The numeric `value` beside it stays, because a bar
// and a colour ramp need something finite to scale against; zero is a harmless
// length and only a harmful sentence.
//
// Seen on screen before it was fixed: MET Norway served as the fallback and the
// hero read "Feels like 0°" on a 28 °C afternoon.
QString readingOf(const Reading &canonical, Units::Quantity quantity)
{
    if (!canonical.has_value())
        return QStringLiteral("\u2014");
    return Units::instance()->format(quantity, *canonical);
}

// The same reading without its unit, for a layout that sets the unit apart.
QString figureOf(const Reading &canonical, Units::Quantity quantity)
{
    if (!canonical.has_value())
        return QStringLiteral("\u2014");
    return QString::number(roundedDisplay(canonical, quantity));
}

QVariantMap activity(const QString &name, const QString &status, const QString &tone)
{
    return QVariantMap{ { QStringLiteral("name"), name },
                        { QStringLiteral("status"), status },
                        { QStringLiteral("tone"), tone } };
}

QVariantMap stop(double p, const QString &colour)
{
    return QVariantMap{ { QStringLiteral("p"), p }, { QStringLiteral("c"), colour } };
}

// ---- the shape a block has before there is any weather in it ----------------
//
// Every map below is published through a Q_PROPERTY that QML binds while its
// component is being constructed, and there are starts where construction
// genuinely precedes the data: a first run with a cold cache, a saved place
// whose forecast has expired, a network still opening its socket. A map with no
// keys in it answers `undefined` to every one of those bindings, so `d.status`
// fails to assign to a string, `d.series.length` throws, and the launch prints
// several hundred lines of console before the snapshot lands and it all
// silently resolves.
//
// The fix is not sixty `|| {}` guards in the QML. docs/10-design-system.md keeps
// the components declarative and a model owes its readers a shape, so it is
// these: the same key set the matching build function produces, with every value
// at its nothing. `clear()` installs them, the constructor calls `clear()`, and
// a binding evaluated before the first snapshot reads a defined value of the
// right type instead of a hole.
//
// ---- what "nothing" is, and what it is not ----------------------------------
//
// Empty strings, empty lists, zeroes. Never a plausible reading. A card that
// says 20° for one frame and 31° for the rest has told the reader something
// false, and docs/README.md ranks not fabricating above everything else this app
// does - a brief lie is still the thing that rule is about.
//
// Nor is it the em dash. `readingOf()` answers "-" and that is a different
// sentence: "we asked the provider and it carries no value here". Before the
// first snapshot nobody has asked, and a card has not earned the right to say
// so. Absent, here, is silent.
//
// Three fields are not zero, and each is the vocabulary something downstream
// already reads:
//
//   trend        "none". The word buildSunMoon() already publishes for a
//                reading that is not doing anything, and the one value
//                TrendBadge.qml makes itself invisible on rather than drawing
//                an arrow that points nowhere.
//
//   riseMin, setMin, nowMin
//                minutesFromLocalMidnight()'s own absent value. Zero is not
//                nothing here, it is midnight - DetailSunCard would draw a day
//                that begins and ends at it, with the sun mark sitting on the
//                crossing.
//
//   directionDeg -1, which is exactly what CurrentConditions.qml's
//                `visible: arrow >= 0` tests for. Zero is due north, and a
//                bearing is a claim.
//
// tests/tst_conditionsdata.cpp asserts that each of these key sets equals the
// set the build function produces from a real fixture, because the failure mode
// of this block is a key added to a builder and forgotten here - which is
// invisible until it is several hundred lines of console again.

// minutesFromLocalMidnight()'s "there is no such instant". See timeaxis.h.
constexpr int kNoMinute = std::numeric_limits<int>::min();

QString noTrend() { return QStringLiteral("none"); }

QVariantMap neutralLocation()
{
    return QVariantMap{
        { QStringLiteral("name"), QString() },
        { QStringLiteral("region"), QString() },
        { QStringLiteral("label"), QString() },
        { QStringLiteral("isHome"), false },
    };
}

QVariantMap neutralCurrent()
{
    return QVariantMap{
        { QStringLiteral("conditionKind"), QString() },
        { QStringLiteral("unitLabel"), QString() },
        { QStringLiteral("summary"), QString() },
    };
}

QVariantMap neutralTemperature()
{
    return QVariantMap{
        { QStringLiteral("value"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("unit"), QString() },
        { QStringLiteral("series"), QVariantList() },
        { QStringLiteral("high"), 0 },
        { QStringLiteral("low"), 0 },
        { QStringLiteral("peakAt"), QString() },
        { QStringLiteral("lowAt"), QString() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralFeelsLike()
{
    return QVariantMap{
        { QStringLiteral("value"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("actual"), 0 },
        { QStringLiteral("unit"), QString() },
        { QStringLiteral("series"), QVariantList() },
        { QStringLiteral("dominantFactor"), QString() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralCloudCover()
{
    return QVariantMap{
        { QStringLiteral("value"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("unit"), QString() },
        { QStringLiteral("condition"), QString() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralPrecipitation()
{
    return QVariantMap{
        { QStringLiteral("value"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("unit"), QString() },
        { QStringLiteral("window"), QString() },
        { QStringLiteral("scaleMax"), 0.0 },
        { QStringLiteral("series"), QVariantList() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralWind()
{
    return QVariantMap{
        { QStringLiteral("speed"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("gust"), 0 },
        { QStringLiteral("speedFigure"), QString() },
        { QStringLiteral("gustFigure"), QString() },
        { QStringLiteral("unit"), QString() },
        { QStringLiteral("scaleMax"), 0 },
        { QStringLiteral("directionDeg"), -1 },
        { QStringLiteral("directionLabel"), QString() },
        { QStringLiteral("beaufort"), 0 },
        { QStringLiteral("beaufortName"), QString() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralHumidity()
{
    return QVariantMap{
        { QStringLiteral("value"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("dewReading"), QString() },
        { QStringLiteral("unit"), QString() },
        { QStringLiteral("dewPoint"), 0 },
        { QStringLiteral("dewUnit"), QString() },
        { QStringLiteral("series"), QVariantList() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralUv()
{
    return QVariantMap{
        { QStringLiteral("value"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("max"), 0 },
        { QStringLiteral("band"), QString() },
        { QStringLiteral("peakAt"), QString() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralAirQuality()
{
    return QVariantMap{
        { QStringLiteral("value"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("max"), 0 },
        { QStringLiteral("band"), QString() },
        { QStringLiteral("pollutant"), QString() },
        { QStringLiteral("pollutantValue"), 0.0 },
        { QStringLiteral("pollutantUnit"), QString() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralVisibility()
{
    return QVariantMap{
        { QStringLiteral("value"), 0 },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("unit"), QString() },
        { QStringLiteral("scaleMax"), 0 },
        { QStringLiteral("band"), QString() },
        { QStringLiteral("peak"), 0 },
        { QStringLiteral("peakAt"), QString() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralPressure()
{
    return QVariantMap{
        // A string, like the built one: buildPressure() formats to the unit's
        // own decimals rather than handing QML a number to round.
        { QStringLiteral("value"), QString() },
        { QStringLiteral("reading"), QString() },
        { QStringLiteral("unit"), QString() },
        { QStringLiteral("at"), QString() },
        { QStringLiteral("series"), QVariantList() },
        { QStringLiteral("min"), 0 },
        { QStringLiteral("max"), 0 },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralSun()
{
    return QVariantMap{
        { QStringLiteral("riseMin"), kNoMinute },
        { QStringLiteral("setMin"), kNoMinute },
        { QStringLiteral("nowMin"), kNoMinute },
        { QStringLiteral("riseLabel"), QString() },
        { QStringLiteral("riseSuffix"), QString() },
        { QStringLiteral("setLabel"), QString() },
        { QStringLiteral("setSuffix"), QString() },
        { QStringLiteral("dayLength"), QString() },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

QVariantMap neutralMoon()
{
    return QVariantMap{
        { QStringLiteral("riseMin"), kNoMinute },
        { QStringLiteral("setMin"), kNoMinute },
        { QStringLiteral("nowMin"), kNoMinute },
        { QStringLiteral("riseLabel"), QString() },
        { QStringLiteral("riseSuffix"), QString() },
        { QStringLiteral("setLabel"), QString() },
        { QStringLiteral("setSuffix"), QString() },
        { QStringLiteral("upLength"), QString() },
        { QStringLiteral("phase"), QString() },
        { QStringLiteral("illumination"), 0.0 },
        { QStringLiteral("waxing"), true },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

// `available` false rather than absent, for the same reason the pollen block
// carries one: MET Norway has no moon product at all, and 0% illuminated is not
// "we were not told" - it is a new moon, which is a reading, and a card is not
// allowed to assert one it does not have.
QVariantMap neutralMoonPhase()
{
    return QVariantMap{
        { QStringLiteral("available"), false },
        { QStringLiteral("illumination"), 0.0 },
        { QStringLiteral("percent"), 0 },
        { QStringLiteral("waxing"), true },
        { QStringLiteral("nextFullLabel"), QString() },
        { QStringLiteral("nextFullDays"), -1 },
        { QStringLiteral("trend"), noTrend() },
        { QStringLiteral("status"), QString() },
        { QStringLiteral("body"), QString() },
    };
}

// `available` false rather than absent, because it is the one field the pollen
// card is gated on and "we do not know yet" and "not a product here" both mean
// the card does not draw. buildPollen() fills the rest in place, so these five
// are what a European coordinate shows between the forecast landing and the air
// quality arriving behind it.
QVariantMap neutralPollen()
{
    return QVariantMap{
        { QStringLiteral("available"), false },
        { QStringLiteral("band"), QString() },
        { QStringLiteral("tone"), QString() },
        { QStringLiteral("main"), QString() },
        { QStringLiteral("items"), QVariantList() },
        { QStringLiteral("body"), QString() },
    };
}

} // namespace

ConditionsData::ConditionsData(QObject *parent)
    : QObject(parent)
{
    // Born with the shape, not with fifteen empty maps. QML binds `Detail.wind`
    // and everything under it while its component is constructed, which on a
    // start with nothing cached happens before any snapshot exists - see the
    // neutral block above for what the alternative sounded like.
    clear();

    const auto rebuild = [this]() {
        if (!m_forecast.isEmpty())
            setSnapshot(m_forecast, m_air, m_now, m_place, m_hasPollen);
    };
    connect(Units::instance(), &Units::changed, this, rebuild);

    // The clock is a preference this class formats with, exactly as the units
    // are: the observation stamp, both sun and moon readings and nine body
    // sentences are strings held in a snapshot, so the format changing has to
    // rebuild it or the switch appears inert until the next fetch.
    connect(TimeFormat::instance(), &TimeFormat::changed, this, rebuild);
}

ConditionsData *ConditionsData::create(QQmlEngine *, QJSEngine *)
{
    ConditionsData *data = AppEngine::instance()->conditionsData();
    QQmlEngine::setObjectOwnership(data, QQmlEngine::CppOwnership);
    return data;
}

void ConditionsData::setPlace(const Place &place)
{
    m_place = place;
    m_location = QVariantMap{
        { QStringLiteral("name"), place.name },
        { QStringLiteral("region"), place.region() },
        { QStringLiteral("label"), place.label() },
        { QStringLiteral("isHome"), place.isHome },
    };
    Q_EMIT changed();
}

void ConditionsData::clear()
{
    m_hours.clear();
    m_hourNow = 0;
    m_from    = 0;
    m_observation = {};
    m_nowIndex = kContextBefore;
    m_observedAt.clear();
    m_observedOn.clear();

    // Reset to the neutral shape rather than emptied. An empty map is a map
    // whose every key is `undefined`, and this function runs at construction and
    // again at the head of every setSnapshot() - which is to say at both of the
    // moments a binding can be evaluated with no weather behind it.
    m_location      = neutralLocation();
    m_current       = neutralCurrent();
    m_temperature   = neutralTemperature();
    m_feelsLike     = neutralFeelsLike();
    m_cloudCover    = neutralCloudCover();
    m_precipitation = neutralPrecipitation();
    m_wind          = neutralWind();
    m_humidity      = neutralHumidity();
    m_uv            = neutralUv();
    m_airQuality    = neutralAirQuality();
    m_visibility    = neutralVisibility();
    m_pressure      = neutralPressure();
    m_sun           = neutralSun();
    m_moon          = neutralMoon();
    m_moonPhase     = neutralMoonPhase();
    m_pollen        = neutralPollen();

    // The one member that needs no neutral shape: a QVariantList is already a
    // JavaScript array, so an empty one answers `.length` with 0 and a Repeater
    // bound to it draws nothing. It is the maps that had holes in them.
    m_activities.clear();
}

void ConditionsData::setSnapshot(const Forecast &forecast, const AirQuality &airQuality,
                                 const QDateTime &now, const Place &place, bool hasPollen)
{
    m_forecast = forecast;
    m_air      = airQuality;
    m_now      = now;

    if (forecast.timeZone.isValid())
        m_zone = forecast.timeZone;
    else if (!place.timezone.isEmpty())
        m_zone = QTimeZone(place.timezone.toUtf8());
    else
        m_zone = QTimeZone::UTC;

    clear();
    setPlace(place);

    if (forecast.isEmpty()) {
        Q_EMIT changed();
        return;
    }

    m_hours = asHourStarting(forecast.hourly);

    buildContext();
    buildTemperature();
    buildFeelsLike();
    buildCloud();
    buildPrecipitation();
    buildWind();
    buildHumidity();
    buildUv();
    buildAirQuality();
    buildVisibility();
    buildPressure();
    buildSunMoon();
    buildPollen(hasPollen);
    buildActivities();
    buildSummary();

    Q_EMIT changed();
}

void ConditionsData::buildContext()
{
    for (int i = 0; i < m_hours.size(); ++i) {
        if (m_hours.at(i).time <= m_now)
            m_hourNow = i;
        else
            break;
    }

    m_from     = qMax(0, m_hourNow - kContextBefore);
    m_nowIndex = m_hourNow - m_from;

    // ---- which reading is "now" -------------------------------------------
    //
    // See the declaration of m_observation. Open-Meteo's `current` block is
    // stamped to the quarter hour, which is why the reference reads 12:28
    // rather than 12:00 - it is a reading and not a slot, and it is the right
    // one to show whenever it is actually current.
    const QDateTime provided = m_forecast.current.time;
    const bool      usable   = provided.isValid() && qAbs(provided.secsTo(m_now)) <= 3600;

    if (usable) {
        m_observation = m_forecast.current;

        // ---- one definition of "now" --------------------------------------
        //
        // The `current` block is the right source for an *instant* - a
        // temperature, a wind, a humidity read at 4:47 rather than on the hour
        // - and the wrong one for anything describing a stretch.
        //
        // `weatherCode` is a stretch. forecast.h says so where it groups the
        // code with the accumulations - it "describes a stretch of weather
        // rather than a moment" - and hourconvention.cpp acts on it: every
        // entry in `m_hours` carries the code for the hour *starting* at its
        // stamp, which is the hour we are standing in. Open-Meteo's `current`
        // block is a separate product on its own convention, and the two
        // disagree often enough to matter: sampled across twelve cities on one
        // afternoon, five of them.
        //
        // When they disagreed the app stated both at once. The card read
        // "Mainly sunny" a few centimetres above a chart whose Now column drew
        // heavy rain, and the sentence under the card announced a rain that had
        // already started - while it was, in fact, raining. Whichever value is
        // nearer the truth, saying both is a defect of ours, and the series the
        // rest of the page is drawn from is the one to keep: the chart, the
        // strip, the precipitation wash and this card's own next-rain clause
        // all come out of `m_hours`.
        //
        // `precipitation` moves with it, for a sharper reason. This struct
        // documents it as the *preceding* hour while `m_hours` has been
        // converted to the hour starting, so reading the two side by side is
        // the exact mistake asHourStarting exists to prevent.
        //
        // Everything else stays: the instants are why the `current` block is
        // preferred in the first place, and they are what makes the reading
        // 4:47's rather than four o'clock's.
        if (m_hourNow < m_hours.size()) {
            const HourlyPoint &standing = m_hours.at(m_hourNow);
            m_observation.weatherCode   = standing.weatherCode;
            m_observation.precipitation = standing.precipitation;
        }
    } else if (m_hourNow < m_hours.size()) {
        // Rebuilt from the hour we are standing in. Field for field rather than
        // by any clever means, because the two structs differ in exactly one
        // way - an HourlyPoint has no dew point on some providers - and a memcpy
        // would not have noticed.
        const HourlyPoint &hour = m_hours.at(m_hourNow);
        m_observation                     = {};
        m_observation.time                = hour.time;
        m_observation.temperature         = hour.temperature;
        m_observation.apparentTemperature = hour.apparentTemperature;
        m_observation.relativeHumidity    = hour.relativeHumidity;
        m_observation.dewPoint            = hour.dewPoint;
        m_observation.precipitation       = hour.precipitation;
        m_observation.windSpeed           = hour.windSpeed;
        m_observation.windGust            = hour.windGust;
        m_observation.windDirection       = hour.windDirection;
        m_observation.pressureMsl         = hour.pressureMsl;
        m_observation.cloudCover          = hour.cloudCover;
        m_observation.visibility          = hour.visibility;
        m_observation.uvIndex             = hour.uvIndex;
        m_observation.weatherCode         = hour.weatherCode;
        m_observation.isDay               = hour.isDay;
    }

    const QDateTime observed = m_observation.time.isValid() ? m_observation.time : m_now;
    m_observedAt = clockLabel(observed, m_zone);
    m_observedOn = QLocale().toString(observed.toTimeZone(m_zone).date(),
                                      QStringLiteral("dddd, d MMMM yyyy"));
}

QVariantList ConditionsData::window(Reading HourlyPoint::*field, int quantity) const
{
    QVariantList out;
    for (int i = m_from; i < qMin(int(m_hours.size()), m_from + kContextBefore + kContextAfter); ++i) {
        const double converted =
            display(m_hours.at(i).*field, static_cast<Units::Quantity>(quantity));
        out.append(qIsNaN(converted) ? QVariant() : QVariant(int(std::lround(converted))));
    }
    return out;
}

DailyPoint ConditionsData::today() const
{
    const QDate date = m_now.toTimeZone(m_zone).date();
    for (const DailyPoint &day : m_forecast.daily) {
        if (day.date == date)
            return day;
    }
    return m_forecast.daily.isEmpty() ? DailyPoint{} : m_forecast.daily.constFirst();
}

// ---- temperature -------------------------------------------------------------------

void ConditionsData::buildTemperature()
{
    const CurrentConditions &now = m_observation;
    const DailyPoint         day = today();

    const int  reading = roundedDisplay(now.temperature, Units::Quantity::Temperature);
    const int  high    = roundedDisplay(day.temperatureMax, Units::Quantity::Temperature);
    const int  low     = roundedDisplay(day.temperatureMin, Units::Quantity::Temperature);

    // The hour today's extremes actually fall in, found rather than assumed.
    // "Peaks at 3 p.m." is true in July in Toronto and not in December in
    // Tromsø, and a card that says it anyway is a card nobody can trust about
    // anything else either.
    QDateTime peakAt;
    QDateTime lowAt;
    double    peak = -1e9;
    double    dip  = 1e9;
    for (const HourlyPoint &hour : m_hours) {
        if (hour.time.toTimeZone(m_zone).date() != day.date || !hour.temperature)
            continue;
        if (*hour.temperature > peak) { peak = *hour.temperature; peakAt = hour.time; }
        if (*hour.temperature < dip)  { dip  = *hour.temperature; lowAt  = hour.time; }
    }

    const QString unit  = Units::instance()->symbol(Units::Quantity::Temperature);
    const double  later = display(m_hours.value(qMin(m_hourNow + 3, int(m_hours.size()) - 1))
                                      .temperature,
                                  Units::Quantity::Temperature);
    const QString trend = trendOf(display(now.temperature, Units::Quantity::Temperature), later, 0.5);

    m_temperature = QVariantMap{
        { QStringLiteral("value"), reading },
        { QStringLiteral("reading"), readingOf(now.temperature, Units::Quantity::Temperature) },
        { QStringLiteral("unit"), unit },
        { QStringLiteral("series"), window(&HourlyPoint::temperature,
                                           int(Units::Quantity::Temperature)) },
        { QStringLiteral("high"), high },
        { QStringLiteral("low"), low },
        { QStringLiteral("peakAt"), sentenceTime(peakAt, m_zone) },
        { QStringLiteral("lowAt"), sentenceTime(lowAt, m_zone) },
        { QStringLiteral("trend"), trend },
        { QStringLiteral("status"), trend == QLatin1String("up")     ? tr("Rising")
                                    : trend == QLatin1String("down") ? tr("Falling")
                                                                     : tr("Steady") },
        { QStringLiteral("body"),
          oneStop(tr("Peaks at %1%2 around %3. Overnight low of %4%2 at %5.")
                      .arg(high).arg(unit).arg(sentenceTime(peakAt, m_zone))
                      .arg(low).arg(sentenceTime(lowAt, m_zone))) },
    };
}

void ConditionsData::buildFeelsLike()
{
    const CurrentConditions &now = m_observation;

    const int    apparent = roundedDisplay(now.apparentTemperature, Units::Quantity::Temperature);
    const int    actual   = roundedDisplay(now.temperature, Units::Quantity::Temperature);
    const double deltaC   = value(now.apparentTemperature) - value(now.temperature);

    // Which factor is doing it, from the numbers rather than from a guess.
    // Humidity makes warm air feel warmer, wind makes cold air feel colder, and
    // the sign of the difference says which of the two is in play.
    const bool    warmer = deltaC > 0;
    const QString factor = warmer ? (value(now.relativeHumidity) >= 60
                                         ? QStringLiteral("humidity")
                                         : QStringLiteral("sun"))
                                  : QStringLiteral("wind");

    m_feelsLike = QVariantMap{
        { QStringLiteral("value"), apparent },
        { QStringLiteral("reading"), readingOf(now.apparentTemperature, Units::Quantity::Temperature) },
        { QStringLiteral("actual"), actual },
        { QStringLiteral("unit"), Units::instance()->symbol(Units::Quantity::Temperature) },
        { QStringLiteral("series"), window(&HourlyPoint::apparentTemperature,
                                           int(Units::Quantity::Temperature)) },
        { QStringLiteral("dominantFactor"), factor },
        { QStringLiteral("trend"), trendOf(0, deltaC, 0.5) },
        { QStringLiteral("status"), std::abs(deltaC) < 1 ? tr("As it looks")
                                    : warmer            ? tr("Warmer than it is")
                                                        : tr("Colder than it is") },
        { QStringLiteral("body"),
          std::abs(deltaC) < 1
              ? tr("It feels about as warm as the thermometer says.")
              : warmer ? tr("Feels warmer than the actual temperature because of the %1.")
                             .arg(factor == QLatin1String("humidity") ? tr("humidity")
                                                                      : tr("sunshine"))
                       : tr("Feels colder than the actual temperature because of the wind.") },
    };
}

void ConditionsData::buildCloud()
{
    const CurrentConditions &now  = m_observation;
    const int                cover = int(std::lround(value(now.cloudCover)));

    const bool day = now.isDay.value_or(true);
    const QString condition = now.weatherCode
        ? climat::conditionText(*now.weatherCode, day)
        : QStringLiteral("-");

    const double later = value(m_hours.value(qMin(m_hourNow + 3, int(m_hours.size()) - 1)).cloudCover);
    const QString trend = trendOf(value(now.cloudCover), later, 5);

    m_cloudCover = QVariantMap{
        { QStringLiteral("value"), qIsNaN(value(now.cloudCover)) ? 0 : cover },
        { QStringLiteral("reading"), readingOf(now.cloudCover, Units::Quantity::Percentage) },
        { QStringLiteral("unit"), QStringLiteral("%") },
        { QStringLiteral("condition"), condition },
        { QStringLiteral("trend"), trend },
        { QStringLiteral("status"), condition },
        { QStringLiteral("body"), trend == QLatin1String("up")
                                      ? tr("Clouding over through the next few hours.")
                                  : trend == QLatin1String("down")
                                      ? tr("Clearing through the next few hours.")
                                      : tr("Little change in cloud cover expected.") },
    };
}

// ---- precipitation -----------------------------------------------------------------

void ConditionsData::buildPrecipitation()
{
    // Twenty-four hours from now, summed in millimetres and converted once.
    double totalMm = 0;
    for (int i = m_hourNow; i < qMin(int(m_hours.size()), m_hourNow + 24); ++i)
        totalMm += m_hours.at(i).precipitation.value_or(0.0);

    const Units *units = Units::instance();
    const auto   kind  = Units::Quantity::Precipitation;

    // The next wet run: when it starts and how hard it gets. This is the
    // sentence's source and the card's, so the two cannot disagree.
    int    startsAt = -1;
    double peakMm   = 0;
    for (int i = m_hourNow; i < qMin(int(m_hours.size()), m_hourNow + 24); ++i) {
        const double mm = m_hours.at(i).precipitation.value_or(0.0);
        if (mm < 0.1)
            continue;
        if (startsAt < 0)
            startsAt = i;
        peakMm = qMax(peakMm, mm);
    }

    QVariantList series;
    for (int i = m_from; i < qMin(int(m_hours.size()), m_from + kContextBefore + kContextAfter); ++i) {
        const Reading probability = m_hours.at(i).precipitationProbability;
        series.append(probability ? int(std::lround(*probability)) : 0);
    }

    const QString body = startsAt < 0
        ? tr("Nothing falling in the next 24 hours.")
        : (startsAt == m_hourNow
               ? tr("Falling now, easing later.")
               : oneStop(tr("Dry now. Starts around %1.")
                             .arg(sentenceTime(m_hours.at(startsAt).time, m_zone))));

    m_precipitation = QVariantMap{
        { QStringLiteral("value"), int(std::lround(units->convert(kind, totalMm))) },
        { QStringLiteral("reading"), units->format(kind, totalMm) },
        { QStringLiteral("unit"), units->bareSymbol(kind) },
        { QStringLiteral("window"), tr("Next 24 hours") },
        // A thoroughly wet day, in the display unit: the ceiling the card draws
        // the amount against. 25 mm, which is detaildata.js's number and the
        // one its comment argues for.
        { QStringLiteral("scaleMax"), units->convert(kind, 25.0) },
        { QStringLiteral("series"), series },
        { QStringLiteral("trend"), startsAt >= 0 ? QStringLiteral("up")
                                                 : QStringLiteral("steady") },
        { QStringLiteral("status"), startsAt < 0 ? tr("Dry")
                                    : peakMm >= 7.6 ? tr("Heavy rain expected")
                                    : peakMm >= 2.5 ? tr("Rain expected")
                                                    : tr("Light rain expected") },
        { QStringLiteral("body"), body },
    };
}

void ConditionsData::buildWind()
{
    const CurrentConditions &now = m_observation;
    const Units             *units = Units::instance();

    const double  kmh   = value(now.windSpeed);
    const int     force = beaufortForce(kmh);
    const double  degrees = value(now.windDirection);

    // Whether there is a wind reading at all, asked once.
    //
    // beaufortForce() is the only function in libclimat/domain/scales.h that
    // returns a number rather than a word, so it has nowhere to put the empty
    // string the others answer NaN with - it returns 0, and beaufortName(0) is
    // "Calm". That made a missing reading indistinguishable from still air:
    // `reading` said "-" and the verdict beside it said Calm, in the same card.
    //
    // ECMWF omits variables at some coordinates (see
    // tests/fixtures/openmeteo/toronto-ecmwf-gaps.json), so this is reachable
    // rather than theoretical. neutralWind() above has always spelled the
    // honest answer - an empty name - and this is what puts the built map back
    // in step with it.
    const bool measured = !qIsNaN(kmh);

    m_wind = QVariantMap{
        { QStringLiteral("speed"), roundedDisplay(now.windSpeed, Units::Quantity::Wind) },
        { QStringLiteral("reading"), readingOf(now.windSpeed, Units::Quantity::Wind) },
        { QStringLiteral("gust"), roundedDisplay(now.windGust, Units::Quantity::Wind) },
        // The card's two figures, which sit beside the unit rather than carry
        // it. `speed` and `gust` above are numbers for the rose, where a missing
        // reading has to be something; here it is a dash, not a calm "0".
        { QStringLiteral("speedFigure"), figureOf(now.windSpeed, Units::Quantity::Wind) },
        { QStringLiteral("gustFigure"), figureOf(now.windGust, Units::Quantity::Wind) },
        { QStringLiteral("unit"), units->bareSymbol(Units::Quantity::Wind) },
        // Beaufort 5 - a fresh breeze, when loose paper starts blowing about.
        // detaildata.js's ceiling, converted rather than re-chosen because it is
        // a wind speed and not an axis.
        { QStringLiteral("scaleMax"), int(std::lround(units->convert(Units::Quantity::Wind, 30.0))) },
        { QStringLiteral("directionDeg"), qIsNaN(degrees) ? 0 : int(std::lround(degrees)) },
        { QStringLiteral("directionLabel"), compassPoint(degrees) },
        { QStringLiteral("beaufort"), force },
        { QStringLiteral("beaufortName"), measured ? beaufortName(force) : QString() },
        { QStringLiteral("trend"),
          trendOf(kmh, value(m_hours.value(qMin(m_hourNow + 3, int(m_hours.size()) - 1)).windSpeed),
                  3) },
        // "No reading" rather than an empty status, matching buildAirQuality
        // three cards up: the status line is the one a reader looks at to find
        // out whether the card has anything to say.
        { QStringLiteral("status"), measured ? beaufortName(force) : tr("No reading") },
        { QStringLiteral("body"),
          measured ? tr("%1 from the %2, gusting to %3 %4.")
                         .arg(beaufortName(force))
                         .arg(compassPoint(degrees))
                         .arg(roundedDisplay(now.windGust, Units::Quantity::Wind))
                         .arg(units->bareSymbol(Units::Quantity::Wind))
                   : QString() },
    };
}

void ConditionsData::buildHumidity()
{
    const CurrentConditions &now = m_observation;

    // Eight columns, matching the reference's bar array - a shorter window than
    // the twelve every other card draws, and deliberately: this one is bars, and
    // twelve bars at this width are stripes.
    QVariantList series;
    for (int i = m_from; i < qMin(int(m_hours.size()), m_from + 8); ++i) {
        const Reading reading = m_hours.at(i).relativeHumidity;
        series.append(reading ? int(std::lround(*reading)) : 0);
    }

    const double later =
        value(m_hours.value(qMin(m_hourNow + 3, int(m_hours.size()) - 1)).relativeHumidity);
    const double reading = value(now.relativeHumidity);

    m_humidity = QVariantMap{
        { QStringLiteral("value"), qIsNaN(reading) ? 0 : int(std::lround(reading)) },
        { QStringLiteral("reading"), readingOf(now.relativeHumidity, Units::Quantity::Percentage) },
        { QStringLiteral("dewReading"), readingOf(now.dewPoint, Units::Quantity::Temperature) },
        { QStringLiteral("unit"), QStringLiteral("%") },
        { QStringLiteral("dewPoint"), roundedDisplay(now.dewPoint, Units::Quantity::Temperature) },
        { QStringLiteral("dewUnit"), Units::instance()->symbol(Units::Quantity::Temperature) },
        { QStringLiteral("series"), series },
        { QStringLiteral("trend"), trendOf(reading, later, 3) },
        { QStringLiteral("status"), reading >= 80 ? tr("Humid")
                                    : reading >= 40 ? tr("Normal")
                                                    : tr("Dry") },
        { QStringLiteral("body"), tr("Dew point %1%2.")
                                      .arg(roundedDisplay(now.dewPoint,
                                                          Units::Quantity::Temperature))
                                      .arg(Units::instance()->symbol(
                                          Units::Quantity::Temperature)) },
    };
}

void ConditionsData::buildUv()
{
    const DailyPoint day  = today();
    const double     nowUv = value(m_observation.uvIndex);
    const double     maxUv = value(day.uvIndexMax);

    QDateTime peakAt;
    double    peak = -1;
    for (const HourlyPoint &hour : m_hours) {
        if (hour.time.toTimeZone(m_zone).date() != day.date || !hour.uvIndex)
            continue;
        if (*hour.uvIndex > peak) { peak = *hour.uvIndex; peakAt = hour.time; }
    }

    m_uv = QVariantMap{
        { QStringLiteral("value"), qIsNaN(nowUv) ? 0 : int(std::lround(nowUv)) },
        { QStringLiteral("reading"), qIsNaN(nowUv) ? QStringLiteral("\u2014")
                                                    : QString::number(int(std::lround(nowUv))) },
        // 11 is where the WHO scale stops naming levels and starts saying
        // "extreme", which makes it the right ceiling for a bar.
        { QStringLiteral("max"), 11 },
        { QStringLiteral("band"), uvBand(qIsNaN(nowUv) ? 0 : nowUv) },
        { QStringLiteral("peakAt"), sentenceTime(peakAt, m_zone) },
        { QStringLiteral("trend"), trendOf(nowUv, peak, 0.5) },
        { QStringLiteral("status"), uvBand(qIsNaN(nowUv) ? 0 : nowUv) },
        { QStringLiteral("body"),
          peakAt.isValid()
              ? oneStop(tr("Today's maximum exposure is %1, expected at %2.")
                            .arg(uvBand(qIsNaN(maxUv) ? peak : maxUv).toLower())
                            .arg(sentenceTime(peakAt, m_zone)))
              : tr("No ultraviolet reading for today.") },
    };
}

void ConditionsData::buildAirQuality()
{
    const AirQualityPoint &now = m_air.current;

    const double index = now.europeanAqi ? double(*now.europeanAqi) : qQNaN();
    const auto   worst = now.dominantPollutant();

    // Up, because the *index* is rising - which for air quality is the bad
    // direction. The trend tracks the number; the body says whether that is
    // good news. docs/10-design-system.md §10.5.
    double later = qQNaN();
    for (const AirQualityPoint &point : m_air.hourly) {
        if (point.time > m_now.addSecs(3 * 3600) && point.europeanAqi) {
            later = double(*point.europeanAqi);
            break;
        }
    }

    m_airQuality = QVariantMap{
        { QStringLiteral("value"), qIsNaN(index) ? 0 : int(index) },
        // The number as text, because 0 is a perfectly good European AQI and
        // "no air-quality product here" is not it. Every place that PRINTS the
        // index reads this; the numeric `value` above is for the ramps and the
        // bar, which need something finite to scale against.
        { QStringLiteral("reading"), qIsNaN(index) ? QStringLiteral("-")
                                                   : QString::number(int(index)) },
        { QStringLiteral("max"), 100 },
        { QStringLiteral("band"), qIsNaN(index) ? QString() : aqiBand(index) },
        { QStringLiteral("pollutant"),
          worst ? scales::pollutantLabel(pollutantId(*worst)) : QString() },
        { QStringLiteral("pollutantValue"),
          now.dominantConcentration().value_or(0.0) },
        { QStringLiteral("pollutantUnit"),
          worst ? pollutantUnit(*worst) : QString() },
        { QStringLiteral("trend"), trendOf(index, later, 3) },
        { QStringLiteral("status"), qIsNaN(index) ? tr("No reading") : aqiBand(index) },
        // Through pollutantLabel(), like the `pollutant` field six lines up.
        // `pollutantId(...).toUpper()` was a second, private copy of the same
        // table and it produced NITROGEN_DIOXIDE where the field beside it
        // said NO₂ - two spellings of one species, in one card, one of them a
        // machine identifier.
        { QStringLiteral("body"),
          worst ? tr("%1 is the main pollutant right now.")
                      .arg(scales::pollutantLabel(pollutantId(*worst)))
                : tr("No pollutant breakdown available here.") },
    };
}

void ConditionsData::buildVisibility()
{
    const Units *units = Units::instance();
    const double km    = value(m_observation.visibility);
    const double shown = display(m_observation.visibility, Units::Quantity::Visibility);

    QDateTime clearestAt;
    double    best = -1;
    for (int i = m_hourNow; i < qMin(int(m_hours.size()), m_hourNow + 12); ++i) {
        const Reading reading = m_hours.at(i).visibility;
        if (reading && *reading > best) { best = *reading; clearestAt = m_hours.at(i).time; }
    }

    m_visibility = QVariantMap{
        { QStringLiteral("value"), qIsNaN(shown) ? 0 : int(std::lround(shown)) },
        { QStringLiteral("reading"), readingOf(m_observation.visibility, Units::Quantity::Visibility) },
        { QStringLiteral("unit"), units->bareSymbol(Units::Quantity::Visibility) },
        // As far as a public forecast bothers to distinguish: past 20 km the
        // answer is just "you can see".
        { QStringLiteral("scaleMax"),
          int(std::lround(units->convert(Units::Quantity::Visibility, 20.0))) },
        { QStringLiteral("band"), visibilityBand(km) },
        { QStringLiteral("peak"),
          best < 0 ? 0 : int(std::lround(units->convert(Units::Quantity::Visibility, best))) },
        { QStringLiteral("peakAt"), sentenceTime(clearestAt, m_zone) },
        { QStringLiteral("trend"), trendOf(km, best, 1) },
        { QStringLiteral("status"), visibilityBand(km) },
        { QStringLiteral("body"),
          clearestAt.isValid() ? oneStop(tr("Clearest around %1.").arg(sentenceTime(clearestAt, m_zone)))
                               : tr("No visibility reading for the hours ahead.") },
    };
}

void ConditionsData::buildPressure()
{
    const Units *units = Units::instance();
    const double hpa   = value(m_observation.pressureMsl);
    const double shown = display(m_observation.pressureMsl, Units::Quantity::Pressure);

    const double before =
        value(m_hours.value(qMax(0, m_hourNow - 3)).pressureMsl);
    const double after =
        value(m_hours.value(qMin(m_hourNow + 3, int(m_hours.size()) - 1)).pressureMsl);

    const QString trend = trendOf(before, hpa, 0.7);
    const int     decimals = units->decimals(Units::Quantity::Pressure);

    m_pressure = QVariantMap{
        { QStringLiteral("value"), qIsNaN(shown) ? QString()
                                                 : QString::number(shown, 'f', decimals) },
        { QStringLiteral("reading"), readingOf(m_observation.pressureMsl, Units::Quantity::Pressure) },
        { QStringLiteral("unit"), units->bareSymbol(Units::Quantity::Pressure) },
        { QStringLiteral("at"), tr("%1 (Now)").arg(m_observedAt) },
        { QStringLiteral("series"), window(&HourlyPoint::pressureMsl,
                                           int(Units::Quantity::Pressure)) },
        { QStringLiteral("min"),
          int(std::lround(units->convert(Units::Quantity::Pressure, 995.0))) },
        { QStringLiteral("max"),
          int(std::lround(units->convert(Units::Quantity::Pressure, 1030.0))) },
        { QStringLiteral("trend"), trend },
        { QStringLiteral("status"), trend == QLatin1String("up")     ? tr("Rising slowly")
                                    : trend == QLatin1String("down") ? tr("Falling slowly")
                                                                     : tr("Steady") },
        { QStringLiteral("body"), after > hpa ? tr("Expected to rise over the next 3 hours.")
                                  : after < hpa ? tr("Expected to fall over the next 3 hours.")
                                                : tr("Expected to hold steady.") },
    };
}

// ---- the sun and the moon -----------------------------------------------------------

void ConditionsData::buildSunMoon()
{
    const DailyPoint day       = today();
    const QDate      reference = m_now.toTimeZone(m_zone).date();

    // Minutes from local midnight, measured from ONE reference date - not from
    // each instant's own midnight. libclimat/domain/timeaxis.h exists for this:
    // above the Arctic circle in summer, sunrise is that day's midnight and
    // sunset is the *next* day's, and an arc measured from two midnights comes
    // out zero minutes long.
    const int riseMin = minutesFromLocalMidnight(day.sunrise, m_zone, reference);
    const int setMin  = minutesFromLocalMidnight(day.sunset, m_zone, reference);
    const int nowMin  = minutesFromLocalMidnight(m_now, m_zone, reference);

    const Reading daylight = day.daylightSeconds;
    const int     dayHours = daylight ? int(*daylight) / 3600 : 0;
    const int     dayMins  = daylight ? (int(*daylight) % 3600) / 60 : 0;

    // Two fields rather than one, because the sun and moon cards draw the suffix
    // smaller and beside the reading - see DetailSunCard.qml. Under a 24-hour
    // clock `suffix` is empty and `hhmm` carries the whole reading, so the card
    // degrades to one field rather than to a stray "PM".
    //
    // The arithmetic that used to be here is in app/viewmodels/timeformat.cpp,
    // together with the note about QLocale's "h" being a 24-hour hour unless the
    // format string also carries AP - which is what produced "20:42 PM" on every
    // sunset after noon the first time this was written.
    const auto hhmm = [&](const QDateTime &instant) {
        return instant.isValid()
                   ? TimeFormat::instance()->clockBare(instant.toTimeZone(m_zone).time())
                   : QString();
    };
    const auto suffix = [&](const QDateTime &instant) {
        return instant.isValid()
                   ? TimeFormat::instance()->meridiem(instant.toTimeZone(m_zone).time())
                   : QString();
    };

    m_sun = QVariantMap{
        { QStringLiteral("riseMin"), riseMin },
        { QStringLiteral("setMin"), setMin },
        { QStringLiteral("nowMin"), nowMin },
        { QStringLiteral("riseLabel"), hhmm(day.sunrise) },
        { QStringLiteral("riseSuffix"), suffix(day.sunrise) },
        { QStringLiteral("setLabel"), hhmm(day.sunset) },
        { QStringLiteral("setSuffix"), suffix(day.sunset) },
        { QStringLiteral("dayLength"), tr("%1 hrs %2 mins").arg(dayHours).arg(dayMins) },
        { QStringLiteral("trend"), QStringLiteral("none") },
        { QStringLiteral("status"), (nowMin >= riseMin && nowMin < setMin) ? tr("Daylight")
                                                                          : tr("Night") },
        { QStringLiteral("body"),
          day.sunset.isValid()
              ? oneStop(tr("The sun is up for %1 hours and %2 minutes today, setting at %3.")
                            .arg(dayHours).arg(dayMins).arg(sentenceTime(day.sunset, m_zone)))
              : tr("The sun does not set here today.") },
    };

    const int moonRise = minutesFromLocalMidnight(day.moonrise, m_zone, reference);
    const int moonSet  = minutesFromLocalMidnight(day.moonset, m_zone, reference);
    const Reading lit  = moonIllumination(day.moonPhase);

    // The moon fails to rise on about one calendar day a month - its rising
    // drifts fifty minutes later each day and eventually skips a midnight - so
    // an absent moonrise is ordinary and the card must not read as broken.
    const int upMinutes = (day.moonrise.isValid() && day.moonset.isValid())
        ? qAbs(moonSet - moonRise)
        : 0;

    m_moon = QVariantMap{
        { QStringLiteral("riseMin"), moonRise },
        { QStringLiteral("setMin"), moonSet },
        { QStringLiteral("nowMin"), nowMin },
        { QStringLiteral("riseLabel"), hhmm(day.moonrise) },
        { QStringLiteral("riseSuffix"), suffix(day.moonrise) },
        { QStringLiteral("setLabel"), hhmm(day.moonset) },
        { QStringLiteral("setSuffix"), suffix(day.moonset) },
        { QStringLiteral("upLength"),
          tr("%1 hrs %2 mins").arg(upMinutes / 60).arg(upMinutes % 60) },
        { QStringLiteral("phase"), ForecastData::moonPhaseLabel(moonPhaseName(day.moonPhase)) },
        { QStringLiteral("illumination"), lit.value_or(0.0) },
        // Which limb is lit. The illuminated fraction cannot say: a waxing and
        // a waning gibbous are the same number and mirror images of each other,
        // and a disc drawn from the fraction alone gets one of the two
        // backwards for half of every month.
        { QStringLiteral("waxing"), isWaxing(day.moonPhase) },
        { QStringLiteral("trend"), QStringLiteral("none") },
        { QStringLiteral("status"), ForecastData::moonPhaseLabel(moonPhaseName(day.moonPhase)) },
        { QStringLiteral("body"),
          day.moonrise.isValid()
              ? tr("The moon is %1% illuminated and rises at %2 tonight.")
                    .arg(int(std::lround(lit.value_or(0.0) * 100)))
                    .arg(sentenceTime(day.moonrise, m_zone))
              : tr("The moon is %1% illuminated. It does not rise today.")
                    .arg(int(std::lround(lit.value_or(0.0) * 100))) },
    };

    buildMoonPhase(day, reference);
}

// ---- the phase, as its own card ------------------------------------------------------

void ConditionsData::buildMoonPhase(const DailyPoint &day, const QDate &reference)
{
    if (!day.moonPhase)
        return;

    const Reading lit     = moonIllumination(day.moonPhase);
    const int     percent = int(std::lround(lit.value_or(0.0) * 100));
    const bool    waxing  = isWaxing(day.moonPhase);

    // The next full moon, from the provider's own phase readings where the
    // horizon reaches it and from the mean cycle where it does not - see
    // libclimat/domain/forecast.h, which explains which of the two answered and
    // how far out the second one can be.
    const std::optional<QDate> nextFull = nextFullMoon(m_forecast.daily, reference);

    // Days from today rather than a stamp, because "in 5 days" is the reading a
    // sentence wants and the date is the reading the card's own line wants, and
    // deriving one from the other in QML would put date arithmetic in a view.
    const int days = nextFull ? int(reference.daysTo(*nextFull)) : -1;

    m_moonPhase = QVariantMap{
        { QStringLiteral("available"), true },
        { QStringLiteral("illumination"), lit.value_or(0.0) },
        { QStringLiteral("percent"), percent },
        { QStringLiteral("waxing"), waxing },
        // Month and day, and no year: the far end of the fallback is a month
        // out, so a year would be the same one every time it was printed and
        // would take room from the part that varies.
        //
        // The pattern is translatable rather than fixed. "MMM d" localises the
        // month NAME and not the order, so every locale would read "Aug 28"
        // however it writes a date; a translator supplies "d. MMM" or "M月d日"
        // instead. The default is the reference's own order.
        { QStringLiteral("nextFullLabel"),
          nextFull ? QLocale().toString(
                         *nextFull,
                         tr("MMM d", "abbreviated month and day, for the next full moon"))
                   : QString() },
        { QStringLiteral("nextFullDays"), days },
        { QStringLiteral("trend"), QStringLiteral("none") },
        { QStringLiteral("status"),
          ForecastData::moonPhaseLabel(moonPhaseName(day.moonPhase)) },
        // Three sentences and not one with `%n` in it. Qt only chooses a plural
        // form from a TRANSLATION, so in the source language `%n day(s)` stays
        // literally "day(s)" - which is how the card came to read "It is full
        // again in 28 day(s)". Branching gives every language a whole sentence
        // to translate and gives this one the words it actually uses; the case
        // that made it necessary is `days == 1`, once every twenty-nine and a
        // half nights, which read "in 1 days".
        { QStringLiteral("body"),
          days < 0
              ? tr("The moon is %1% illuminated tonight.").arg(percent)
              : (days == 0
                     ? tr("The moon is %1% illuminated, and full tonight.").arg(percent)
                     : (days == 1
                            ? tr("The moon is %1% illuminated and %2. It is full tomorrow.")
                                  .arg(percent)
                                  .arg(waxing ? tr("waxing") : tr("waning"))
                            : tr("The moon is %1% illuminated and %2. It is full again in "
                                 "%3 days.")
                                  .arg(percent)
                                  .arg(waxing ? tr("waxing") : tr("waning"))
                                  .arg(days))) },
    };
}

// ---- pollen: gated, not computed ------------------------------------------------------

void ConditionsData::buildPollen(bool hasPollen)
{
    m_hasPollen = hasPollen;
    m_pollen[QStringLiteral("available")] = hasPollen;
    if (!hasPollen || m_air.hourly.isEmpty())
        return;

    // The hour we are in, from the air-quality series' own axis.
    const AirQualityPoint *point = nullptr;
    for (const AirQualityPoint &candidate : m_air.hourly) {
        if (candidate.time <= m_now)
            point = &candidate;
        else
            break;
    }
    if (point == nullptr || !point->pollen)
        return;

    // Grains per cubic metre, banded the way every European pollen service
    // bands them. The three shown are the three a European reader recognises;
    // alder, birch and olive are seasonal and out of season most of the year,
    // and a card with six rows of "Low" is a card nobody reads.
    struct Row { PollenSpecies species; const char *label; double moderate; double high; };
    static const Row rows[] = {
        { PollenSpecies::Grass,   QT_TR_NOOP("Grass"),   20, 50 },
        { PollenSpecies::Birch,   QT_TR_NOOP("Tree"),    20, 50 },
        { PollenSpecies::Ragweed, QT_TR_NOOP("Weed"),    10, 20 },
    };

    QVariantList items;
    double       worstLevel = 0;
    QString      worstName;
    QString      worstLabel = tr("Low");
    QString      worstTone  = QStringLiteral("good");

    for (const Row &row : rows) {
        const auto found = point->pollen->constFind(row.species);
        if (found == point->pollen->cend())
            continue;

        const double grains = *found;
        const bool   high   = grains >= row.high;
        const bool   medium = grains >= row.moderate;

        const QString label = high ? tr("High") : medium ? tr("Moderate") : tr("Low");
        const QString tone  = toneFor(!medium, medium && !high);
        const double  level = qBound(0.0, grains / (row.high * 2.0), 1.0);

        items.append(QVariantMap{ { QStringLiteral("name"), tr(row.label) },
                                  { QStringLiteral("label"), label },
                                  { QStringLiteral("tone"), tone },
                                  { QStringLiteral("level"), level } });

        if (level > worstLevel) {
            worstLevel = level;
            worstName  = tr(row.label);
            worstLabel = label;
            worstTone  = tone;
        }
    }

    if (items.isEmpty())
        return;

    m_pollen[QStringLiteral("band")]  = worstLabel;
    m_pollen[QStringLiteral("tone")]  = worstTone;
    m_pollen[QStringLiteral("main")]  = worstName;
    m_pollen[QStringLiteral("items")] = items;
    m_pollen[QStringLiteral("body")]  =
        worstTone == QLatin1String("good")
            ? tr("Pollen is low across the board today.")
            : tr("%1 pollen is the main allergen today. %2 risk for sensitive individuals.")
                  .arg(worstName, worstLabel);
}

// ---- activities: ours, and labelled as ours -------------------------------------------

void ConditionsData::buildActivities()
{
    // NOTHING PUBLISHES THIS. There is no "do I need an umbrella" product at
    // Open-Meteo, at MET Norway or anywhere else, so every row below is a rule
    // written here, applied to numbers that are on the same screen. The card
    // says so in its own subtitle; see MobileActivitiesCard.qml.
    //
    // Each rule is one line and each is deliberately dull. A verdict a reader
    // cannot reconstruct from what they can see is a verdict they cannot
    // disagree with, which is worse than a crude one.

    const CurrentConditions &now = m_observation;

    // ---- umbrella: the highest probability in the next twelve hours ---------
    double wettest = 0;
    for (int i = m_hourNow; i < qMin(int(m_hours.size()), m_hourNow + 12); ++i)
        wettest = qMax(wettest, m_hours.at(i).precipitationProbability.value_or(0.0));

    const QString umbrella = wettest >= 60 ? tr("Take one")
                             : wettest >= 30 ? tr("Maybe")
                                             : tr("No need");

    // ---- UV: the WHO band, as it stands now ---------------------------------
    const double uvNow = value(now.uvIndex);

    // ---- heat: apparent temperature, which is the one that matters ----------
    const double feels = value(now.apparentTemperature);
    const QString heat = feels >= 32 ? tr("Take care")
                         : feels >= 27 ? tr("Caution")
                         : feels <= -10 ? tr("Take care")
                         : feels <= 0   ? tr("Caution")
                                        : tr("Comfortable");

    // ---- clothing: apparent temperature again, in four bands ----------------
    const QString clothing = feels >= 24 ? tr("Light wear")
                             : feels >= 15 ? tr("A layer")
                             : feels >= 5  ? tr("A jacket")
                                           : tr("Wrap up");

    // ---- outdoors: the composite, and the only row that reads the others ----
    const bool wet   = wettest >= 50;
    const bool burnt = uvNow >= 8;
    const bool harsh = feels >= 32 || feels <= 0;
    const QString outdoors = (wet || burnt || harsh) ? (wet && harsh ? tr("Poor") : tr("Fair"))
                                                     : tr("Good");

    m_activities = {
        activity(tr("Outdoors"), outdoors,
                 toneFor(outdoors == tr("Good"), outdoors == tr("Fair"))),
        activity(tr("Clothing"), clothing,
                 toneFor(feels >= 15 && feels < 27, true)),
        activity(tr("Heat"), heat,
                 toneFor(heat == tr("Comfortable"), heat == tr("Caution"))),
        activity(tr("Umbrella"), umbrella,
                 toneFor(wettest < 30, wettest < 60)),
        activity(tr("UV index"), qIsNaN(uvNow) ? tr("No reading") : uvBand(uvNow),
                 toneFor(uvNow < 6, uvNow < 8)),
    };
}

// ---- the headline sentence -------------------------------------------------------------

void ConditionsData::buildSummary()
{
    const CurrentConditions &now = m_observation;
    const DailyPoint         day = today();
    const Units             *units = Units::instance();

    const bool    daylight  = now.isDay.value_or(true);
    const QString condition = now.weatherCode ? climat::conditionText(*now.weatherCode, daylight)
                                              : QString();

    // The next run of precipitation, if there is one in the next twelve hours,
    // and how hard it gets. Both come out of the same series the chart draws,
    // which is what stops the sentence and the wash under it disagreeing about
    // whether it is going to rain.
    // An onset, not merely the next wet hour: `from` names a time the reader is
    // meant to act on, and an hour that is already raining does not have one.
    // The scan therefore wants a wet hour whose predecessor was dry, which is
    // what makes the clause hold when the standing hour is itself wet - before,
    // a rain that had been falling since two o'clock was announced as starting
    // at five, which is the same sentence a forecast would use for a dry
    // afternoon.
    const auto wet = [this](int i) {
        return i >= 0 && i < m_hours.size()
            && m_hours.at(i).precipitation.value_or(0.0) >= 0.1;
    };

    int    startsAt = -1;
    double peakMm   = 0;
    for (int i = m_hourNow + 1; i < qMin(int(m_hours.size()), m_hourNow + 13); ++i) {
        if (!wet(i))
            continue;
        if (startsAt < 0) {
            if (wet(i - 1))
                continue;   // the run we are already standing in
            startsAt = i;
        }
        peakMm = qMax(peakMm, m_hours.at(i).precipitation.value_or(0.0));
    }

    // Assembled as whole sentences and joined. Each carries its own full stop,
    // and oneStop() above folds it into the one a 12-hour time already ends in.
    QStringList sentences;

    if (!condition.isEmpty())
        sentences.append(tr("%1 now.").arg(condition));

    if (startsAt >= 0) {
        // The NWS bands precip.js classifies with, said in words. Capitalised
        // here because it opens its own sentence - which it does not do in
        // every language, and is why the whole clause is one translatable unit
        // rather than a noun slotted into a frame.
        const QString weight = peakMm >= 7.6 ? tr("Heavy rain")
                               : peakMm >= 2.5 ? tr("Rain")
                                               : tr("Light rain");
        sentences.append(oneStop(
            tr("%1 from %2.").arg(weight, sentenceTime(m_hours.at(startsAt).time, m_zone))));
    }

    if (day.temperatureMax) {
        sentences.append(tr("The high will be %1%2.")
                             .arg(roundedDisplay(day.temperatureMax, Units::Quantity::Temperature))
                             .arg(units->symbol(Units::Quantity::Temperature)));
    }

    m_current = QVariantMap{
        { QStringLiteral("conditionKind"),
          now.weatherCode
              ? conditionKindName(climat::conditionFor(*now.weatherCode, daylight))
              : QString() },
        { QStringLiteral("unitLabel"), units->bareSymbol(Units::Quantity::Temperature) },
        { QStringLiteral("summary"), sentences.join(QLatin1Char(' ')) },
    };
}

// ---- the two constants ------------------------------------------------------------------

QVariantList ConditionsData::order() const
{
    return { QStringLiteral("temperature"), QStringLiteral("feelsLike"),
             QStringLiteral("cloudCover"),  QStringLiteral("precipitation"),
             QStringLiteral("wind"),        QStringLiteral("humidity"),
             QStringLiteral("uv"),          QStringLiteral("airQuality"),
             QStringLiteral("visibility"),  QStringLiteral("pressure"),
             QStringLiteral("sun"),         QStringLiteral("moon"),
             QStringLiteral("moonPhase") };
}

QVariantMap ConditionsData::bands() const
{
    // Colour bands published by the relevant authority, normalised over each
    // card's own range. detaildata.js's table, moved rather than changed.
    return QVariantMap{
        { QStringLiteral("uv"),
          QVariantList{ stop(0.00, QStringLiteral("#5ec18a")), stop(0.27, QStringLiteral("#e8c73c")),
                        stop(0.50, QStringLiteral("#f5a02f")), stop(0.72, QStringLiteral("#d6484e")),
                        stop(1.00, QStringLiteral("#8b5fc4")) } },
        { QStringLiteral("aqi"),
          QVariantList{ stop(0.00, QStringLiteral("#4ec3c8")), stop(0.25, QStringLiteral("#5cc79a")),
                        stop(0.50, QStringLiteral("#e8c93f")), stop(0.75, QStringLiteral("#f08a45")),
                        stop(1.00, QStringLiteral("#c03050")) } },
        { QStringLiteral("visibility"),
          QVariantList{ stop(0.00, QStringLiteral("#5f8f78")), stop(0.50, QStringLiteral("#6fbf95")),
                        stop(1.00, QStringLiteral("#8fe0b4")) } },
    };
}
