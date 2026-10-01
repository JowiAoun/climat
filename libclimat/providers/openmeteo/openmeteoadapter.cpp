// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0

#include "openmeteoadapter.h"

#include "libclimat/domain/timeaxis.h"
#include "libclimat/providers/openmeteo/openmeteovariables.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace climat {
namespace openmeteo {
namespace {

// Whether any hour in the series has a value for this field.
//
// This is the question that decides whether a metric tab exists, and it has to
// be asked of the whole column rather than of the first row: the recorded
// Toronto response has a null hour in the middle of an otherwise complete
// series, and a first-row test would hide the temperature tab for a forecast
// that begins with one gap.
template <typename Field>
bool anyHourHas(const QList<HourlyPoint> &hourly, Field field)
{
    for (const HourlyPoint &point : hourly) {
        if ((point.*field).has_value())
            return true;
    }
    return false;
}


Error parseError(const QString &message, const QString &providerId)
{
    Error error(ErrorKind::Parse, message);
    error.setProviderId(providerId);
    return error;
}

// ---- reading one cell --------------------------------------------------------
//
// Every column read goes through these three, and they all answer "absent" the
// same way for the same four situations: the array is missing, it is shorter
// than the row we want, the cell is JSON null, or the cell is not a number.
//
// Not one templated helper, because the three return types mean three
// different things downstream and collapsing them would put a
// `static_cast<int>(double)` somewhere a reader cannot see it.

Reading readNumber(const QJsonArray &column, int index)
{
    if (index < 0 || index >= column.size())
        return std::nullopt;
    const QJsonValue value = column.at(index);
    if (!value.isDouble())
        return std::nullopt;
    return value.toDouble();
}

WeatherCode readCode(const QJsonArray &column, int index)
{
    if (index < 0 || index >= column.size())
        return std::nullopt;
    const QJsonValue value = column.at(index);
    if (!value.isDouble())
        return std::nullopt;
    return int(value.toDouble());
}

std::optional<bool> readFlag(const QJsonArray &column, int index)
{
    // Open-Meteo sends `is_day` as 1 and 0 rather than as true and false.
    //
    // Left absent when the provider did not say, rather than guessed from the
    // sun's position - libclimat/domain/forecast.h asks for exactly that, and
    // guessing is how a night hour at 78° north gets a sun over it on the one
    // day of the year the question is interesting.
    if (index < 0 || index >= column.size())
        return std::nullopt;
    const QJsonValue value = column.at(index);
    if (value.isBool())
        return value.toBool();
    if (!value.isDouble())
        return std::nullopt;
    return value.toDouble() != 0.0;
}

QDateTime readInstant(const QJsonArray &column, int index, int offsetSeconds)
{
    if (index < 0 || index >= column.size())
        return {};
    const QJsonValue value = column.at(index);
    if (!value.isString())
        return {};
    return utcFromNaiveLocal(value.toString(), offsetSeconds);
}

// Metres to kilometres, absent-preserving. A named function rather than a `/
// 1000.0` at the call site so that the one place this conversion happens is
// greppable, and so that the call site reads as a unit change rather than as
// arithmetic.
Reading metresToKilometres(Reading metres)
{
    if (!metres)
        return std::nullopt;
    return *metres / 1000.0;
}

// ---- checking the shape ------------------------------------------------------

// Every column a block declares must be exactly as long as its `time` column.
// Returns the name of the first that is not, or an empty string.
//
// Open-Meteo has never sent a ragged block and this is not defensive
// programming for its own sake: a series silently truncated to its shortest
// column is a forecast that ends early, and the hour it ends at moves with
// whichever variable was short. That is unreproducible by the time anyone
// reports it.
QString firstRaggedColumn(const QJsonObject &block, const QList<QLatin1String> &names, int expected)
{
    for (const QLatin1String &name : names) {
        const QJsonValue value = block.value(name);
        if (value.isUndefined())
            continue;    // not asked for, or not served here. Absent, not ragged.
        if (!value.isArray())
            return QString(name);
        if (value.toArray().size() != expected)
            return QString(name);
    }
    return {};
}

} // namespace

Capabilities capabilitiesFor(const Forecast &forecast)
{
    CapabilityFlags available;

    if (!forecast.current.isEmpty())
        available |= Capability::CurrentConditions;
    if (!forecast.hourly.isEmpty())
        available |= Capability::Hourly;
    if (!forecast.daily.isEmpty())
        available |= Capability::Daily;

    const QList<HourlyPoint> &hourly = forecast.hourly;

    if (anyHourHas(hourly, &HourlyPoint::temperature))
        available |= Capability::Temperature;
    if (anyHourHas(hourly, &HourlyPoint::apparentTemperature))
        available |= Capability::ApparentTemperature;
    if (anyHourHas(hourly, &HourlyPoint::dewPoint))
        available |= Capability::DewPoint;
    if (anyHourHas(hourly, &HourlyPoint::relativeHumidity))
        available |= Capability::Humidity;
    if (anyHourHas(hourly, &HourlyPoint::precipitation))
        available |= Capability::Precipitation;
    if (anyHourHas(hourly, &HourlyPoint::precipitationProbability))
        available |= Capability::PrecipitationProbability;
    if (anyHourHas(hourly, &HourlyPoint::windSpeed))
        available |= Capability::Wind;
    if (anyHourHas(hourly, &HourlyPoint::windGust))
        available |= Capability::WindGust;
    if (anyHourHas(hourly, &HourlyPoint::pressureMsl))
        available |= Capability::Pressure;
    if (anyHourHas(hourly, &HourlyPoint::cloudCover))
        available |= Capability::CloudCover;
    if (anyHourHas(hourly, &HourlyPoint::uvIndex))
        available |= Capability::UvIndex;

    // The two that ECMWF IFS does not carry, and the reason this function
    // exists rather than a constant. toronto-ecmwf-gaps.json is 72 hours of
    // null for both.
    if (anyHourHas(hourly, &HourlyPoint::visibility))
        available |= Capability::Visibility;

    // The rain / showers / snow split, which is what lets a chart colour a
    // band by phase rather than guessing from temperature.
    if (anyHourHas(hourly, &HourlyPoint::rain) || anyHourHas(hourly, &HourlyPoint::showers)
        || anyHourHas(hourly, &HourlyPoint::snowfall))
        available |= Capability::PrecipitationType;

    for (const HourlyPoint &point : hourly) {
        if (point.weatherCode) {
            available |= Capability::WeatherCode;
            break;
        }
    }

    for (const DailyPoint &day : forecast.daily) {
        if (day.sunrise.isValid() || day.sunset.isValid()) {
            available |= Capability::SunTimes;
            break;
        }
    }

    // Nothing is undetermined once a payload has been seen: the response is
    // the witness, and it has now testified.
    return Capabilities(available);
}

Attribution attribution()
{
    Attribution credit;

    credit.name = QStringLiteral("Open-Meteo");

    // The exact sentence, from docs/02-data-sources.md §2.9. Not built from
    // `name`: the obligation is to this wording, and ".com" is part of it.
    credit.creditLine  = QStringLiteral("Weather data by Open-Meteo.com");
    credit.homepage    = QUrl(QStringLiteral("https://open-meteo.com/"));
    credit.licenceName = QStringLiteral("CC BY 4.0");
    credit.licenceUrl  = QUrl(QStringLiteral("https://creativecommons.org/licenses/by/4.0/"));

    // §2.9 again: "underlying model owners (ECMWF, NOAA, DWD, Météo-France, …)
    // named". These are the owners behind the seamless blend `/v1/forecast`
    // serves by default, in the order docs/02-data-sources.md §2.3 lists them.
    credit.upstream = { QStringLiteral("ECMWF"),   QStringLiteral("NOAA"),
                        QStringLiteral("DWD"),     QStringLiteral("Météo-France"),
                        QStringLiteral("UK Met Office"), QStringLiteral("ECCC"),
                        QStringLiteral("JMA"),     QStringLiteral("KMA"),
                        QStringLiteral("BOM"),     QStringLiteral("CMA") };

    // The last sentence is CC BY 4.0's "indicate if changes were made". See
    // openmeteo/openmeteoconsensus.h for the change and why.
    credit.note = QStringLiteral(
        "Free for non-commercial use with no API key. Model output is stitched "
        "across runs by Open-Meteo; the owners above are credited for the "
        "underlying forecasts. In Canada, Climat averages this forecast with ECCC's GEM "
        "and ECMWF's IFS for temperature, humidity and wind.");

    return credit;
}

Result<Forecast> adaptForecast(const QByteArray &body, const QString &providerId)
{
    QJsonParseError syntax{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &syntax);
    if (syntax.error != QJsonParseError::NoError) {
        return parseError(QStringLiteral("response is not JSON: %1 at offset %2")
                              .arg(syntax.errorString())
                              .arg(syntax.offset),
                          providerId);
    }
    if (!document.isObject())
        return parseError(QStringLiteral("response is not a JSON object"), providerId);

    const QJsonObject root = document.object();

    // Open-Meteo reports a rejected request with HTTP 400 and this envelope.
    // Read it before anything else: the body parses cleanly as JSON and has
    // none of the blocks below, so without this the failure would surface as
    // "no hourly series", which sends whoever is debugging to the wrong file.
    if (root.value(QLatin1String("error")).toBool(false)) {
        const QString reason = root.value(QLatin1String("reason")).toString();
        return parseError(reason.isEmpty() ? QStringLiteral("provider reported an error")
                                           : reason,
                          providerId);
    }

    Forecast forecast;
    forecast.providerId = providerId;

    // ---- where, and in what zone --------------------------------------------
    //
    // The coordinate in the response is the model grid cell, not the point we
    // asked for; they differ by up to a few kilometres and forecast.h keeps
    // the provider's because it is the one the numbers belong to.
    forecast.coordinate.latitude  = root.value(QLatin1String("latitude")).toDouble();
    forecast.coordinate.longitude = root.value(QLatin1String("longitude")).toDouble();

    if (root.value(QLatin1String("elevation")).isDouble())
        forecast.elevation = root.value(QLatin1String("elevation")).toDouble();

    const int     offsetSeconds = root.value(QLatin1String("utc_offset_seconds")).toInt(0);
    const QString timezoneId    = root.value(QLatin1String("timezone")).toString();

    // The IANA zone, not the fixed offset. This single line is the whole of
    // trap 3 - see libclimat/domain/timeaxis.h for the measurement showing that
    // `utc_offset_seconds` is a constant Open-Meteo applies to the entire
    // window regardless of what the zone does inside it.
    forecast.timeZone = zoneFor(timezoneId, offsetSeconds);

    // ---- hourly --------------------------------------------------------------

    const QJsonObject hourly     = root.value(QLatin1String("hourly")).toObject();
    const QJsonArray  hourlyTime = hourly.value(QLatin1String("time")).toArray();

    if (hourlyTime.isEmpty())
        return parseError(QStringLiteral("response carries no hourly series"), providerId);

    if (const QString ragged = firstRaggedColumn(hourly, hourlyVariables(), hourlyTime.size());
        !ragged.isEmpty()) {
        return parseError(QStringLiteral("hourly column '%1' is not the same length as "
                                         "hourly.time (%2 rows)")
                              .arg(ragged)
                              .arg(hourlyTime.size()),
                          providerId);
    }

    const QJsonArray temperature   = hourly.value(QLatin1String("temperature_2m")).toArray();
    const QJsonArray apparent      = hourly.value(QLatin1String("apparent_temperature")).toArray();
    const QJsonArray dewPoint      = hourly.value(QLatin1String("dew_point_2m")).toArray();
    const QJsonArray humidity      = hourly.value(QLatin1String("relative_humidity_2m")).toArray();
    const QJsonArray precipProb    = hourly.value(QLatin1String("precipitation_probability")).toArray();
    const QJsonArray precipitation = hourly.value(QLatin1String("precipitation")).toArray();
    const QJsonArray rain          = hourly.value(QLatin1String("rain")).toArray();
    const QJsonArray showers       = hourly.value(QLatin1String("showers")).toArray();
    const QJsonArray snowfall      = hourly.value(QLatin1String("snowfall")).toArray();
    const QJsonArray weatherCode   = hourly.value(QLatin1String("weather_code")).toArray();
    const QJsonArray cloudCover    = hourly.value(QLatin1String("cloud_cover")).toArray();
    const QJsonArray pressureMsl   = hourly.value(QLatin1String("pressure_msl")).toArray();
    const QJsonArray windSpeed     = hourly.value(QLatin1String("wind_speed_10m")).toArray();
    const QJsonArray windGust      = hourly.value(QLatin1String("wind_gusts_10m")).toArray();
    const QJsonArray windDirection = hourly.value(QLatin1String("wind_direction_10m")).toArray();
    const QJsonArray uvIndex       = hourly.value(QLatin1String("uv_index")).toArray();
    const QJsonArray visibility    = hourly.value(QLatin1String("visibility")).toArray();
    const QJsonArray isDay         = hourly.value(QLatin1String("is_day")).toArray();

    forecast.hourly.reserve(hourlyTime.size());

    for (int i = 0; i < hourlyTime.size(); ++i) {
        HourlyPoint point;

        // The instant, reconstructed rather than read. `utcFromNaiveLocal`
        // subtracts exactly the offset Open-Meteo added, which is exact
        // because that is how the label was produced.
        point.time = utcFromNaiveLocal(hourlyTime.at(i).toString(), offsetSeconds);

        point.temperature         = readNumber(temperature, i);
        point.apparentTemperature = readNumber(apparent, i);
        point.dewPoint            = readNumber(dewPoint, i);
        point.relativeHumidity    = readNumber(humidity, i);

        point.precipitation             = readNumber(precipitation, i);   // mm
        point.rain                      = readNumber(rain, i);            // mm
        point.showers                   = readNumber(showers, i);         // mm
        point.snowfall                  = readNumber(snowfall, i);        // cm, not mm
        point.precipitationProbability  = readNumber(precipProb, i);

        point.windSpeed     = readNumber(windSpeed, i);
        point.windGust      = readNumber(windGust, i);
        point.windDirection = readNumber(windDirection, i);

        point.pressureMsl = readNumber(pressureMsl, i);
        point.cloudCover  = readNumber(cloudCover, i);
        point.uvIndex     = readNumber(uvIndex, i);

        // Trap 2. Open-Meteo's unit for this one field is metres and the
        // domain's is kilometres, and 24 100 against a 25 km axis is a chart
        // that renders perfectly and says nothing.
        point.visibility = metresToKilometres(readNumber(visibility, i));

        point.weatherCode = readCode(weatherCode, i);
        point.isDay       = readFlag(isDay, i);

        forecast.hourly.append(point);
    }

    // ---- daily ---------------------------------------------------------------

    const QJsonObject daily     = root.value(QLatin1String("daily")).toObject();
    const QJsonArray  dailyTime = daily.value(QLatin1String("time")).toArray();

    if (!dailyTime.isEmpty()) {
        if (const QString ragged = firstRaggedColumn(daily, dailyVariables(), dailyTime.size());
            !ragged.isEmpty()) {
            return parseError(QStringLiteral("daily column '%1' is not the same length as "
                                             "daily.time (%2 rows)")
                                  .arg(ragged)
                                  .arg(dailyTime.size()),
                              providerId);
        }

        const QJsonArray tMax        = daily.value(QLatin1String("temperature_2m_max")).toArray();
        const QJsonArray tMin        = daily.value(QLatin1String("temperature_2m_min")).toArray();
        const QJsonArray aMax        = daily.value(QLatin1String("apparent_temperature_max")).toArray();
        const QJsonArray aMin        = daily.value(QLatin1String("apparent_temperature_min")).toArray();
        const QJsonArray pSum        = daily.value(QLatin1String("precipitation_sum")).toArray();
        const QJsonArray pProbMax    = daily.value(QLatin1String("precipitation_probability_max")).toArray();
        const QJsonArray pHours      = daily.value(QLatin1String("precipitation_hours")).toArray();
        const QJsonArray dCode       = daily.value(QLatin1String("weather_code")).toArray();
        const QJsonArray sunrise     = daily.value(QLatin1String("sunrise")).toArray();
        const QJsonArray sunset      = daily.value(QLatin1String("sunset")).toArray();
        const QJsonArray daylight    = daily.value(QLatin1String("daylight_duration")).toArray();
        const QJsonArray sunshine    = daily.value(QLatin1String("sunshine_duration")).toArray();
        const QJsonArray uvMax       = daily.value(QLatin1String("uv_index_max")).toArray();
        const QJsonArray wMax        = daily.value(QLatin1String("wind_speed_10m_max")).toArray();
        const QJsonArray gMax        = daily.value(QLatin1String("wind_gusts_10m_max")).toArray();
        const QJsonArray wDominant   = daily.value(QLatin1String("wind_direction_10m_dominant")).toArray();
        const QJsonArray moonPhase   = daily.value(QLatin1String("moon_phase")).toArray();
        const QJsonArray moonrise    = daily.value(QLatin1String("moonrise")).toArray();
        const QJsonArray moonset     = daily.value(QLatin1String("moonset")).toArray();

        forecast.daily.reserve(dailyTime.size());

        for (int i = 0; i < dailyTime.size(); ++i) {
            DailyPoint day;

            // A bare "2026-07-30", and it is already the local calendar date -
            // no offset arithmetic, because a date is not an instant. Running
            // it through utcFromNaiveLocal and taking .date() would be the
            // same answer by luck and a different one at any other offset.
            day.date = QDate::fromString(dailyTime.at(i).toString(), Qt::ISODate);

            day.temperatureMax         = readNumber(tMax, i);
            day.temperatureMin         = readNumber(tMin, i);
            day.apparentTemperatureMax = readNumber(aMax, i);
            day.apparentTemperatureMin = readNumber(aMin, i);

            day.precipitationSum            = readNumber(pSum, i);
            day.precipitationProbabilityMax = readNumber(pProbMax, i);
            day.precipitationHours          = readNumber(pHours, i);

            day.windSpeedMax          = readNumber(wMax, i);
            day.windGustMax           = readNumber(gMax, i);
            day.windDirectionDominant = readNumber(wDominant, i);
            day.uvIndexMax            = readNumber(uvMax, i);

            day.weatherCode = readCode(dCode, i);

            day.sunrise = readInstant(sunrise, i, offsetSeconds);
            day.sunset  = readInstant(sunset, i, offsetSeconds);

            day.daylightSeconds = readNumber(daylight, i);
            day.sunshineSeconds = readNumber(sunshine, i);

            // Null on about one day a month, when the moon's rising drifts past
            // midnight and skips a calendar day. Not an error, not a gap to
            // fill - the moon really did not rise.
            day.moonrise  = readInstant(moonrise, i, offsetSeconds);
            day.moonset   = readInstant(moonset, i, offsetSeconds);
            day.moonPhase = readNumber(moonPhase, i);

            forecast.daily.append(day);
        }
    }

    // ---- current -------------------------------------------------------------
    //
    // A separate block with its own quarter-hourly timestamp, not "hour zero":
    // it lands between two hourly samples and it has its own cache row
    // (DataKind::CurrentConditions, ten minutes, against the forecast's thirty).

    const QJsonObject current = root.value(QLatin1String("current")).toObject();
    if (!current.isEmpty()) {
        CurrentConditions &now = forecast.current;

        now.time = utcFromNaiveLocal(current.value(QLatin1String("time")).toString(),
                                     offsetSeconds);

        // One-element arrays, so that the same three readers serve both blocks
        // rather than a second set that differs only in not taking an index.
        const auto scalar = [&current](QLatin1String name) {
            return QJsonArray{ current.value(name) };
        };

        now.temperature         = readNumber(scalar(QLatin1String("temperature_2m")), 0);
        now.apparentTemperature = readNumber(scalar(QLatin1String("apparent_temperature")), 0);
        now.dewPoint            = readNumber(scalar(QLatin1String("dew_point_2m")), 0);
        now.relativeHumidity    = readNumber(scalar(QLatin1String("relative_humidity_2m")), 0);
        now.precipitation       = readNumber(scalar(QLatin1String("precipitation")), 0);
        now.windSpeed           = readNumber(scalar(QLatin1String("wind_speed_10m")), 0);
        now.windGust            = readNumber(scalar(QLatin1String("wind_gusts_10m")), 0);
        now.windDirection       = readNumber(scalar(QLatin1String("wind_direction_10m")), 0);
        now.pressureMsl         = readNumber(scalar(QLatin1String("pressure_msl")), 0);
        now.cloudCover          = readNumber(scalar(QLatin1String("cloud_cover")), 0);
        now.uvIndex             = readNumber(scalar(QLatin1String("uv_index")), 0);
        now.visibility          = metresToKilometres(
            readNumber(scalar(QLatin1String("visibility")), 0));
        now.weatherCode         = readCode(scalar(QLatin1String("weather_code")), 0);
        now.isDay               = readFlag(scalar(QLatin1String("is_day")), 0);
    }

    return forecast;
}

} // namespace openmeteo
} // namespace climat
