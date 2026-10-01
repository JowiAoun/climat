// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0

#include "openmeteoconsensus.h"

#include "libclimat/providers/openmeteo/openmeteoadapter.h"
#include "libclimat/providers/registry.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include <cmath>

namespace climat {
namespace openmeteo {

namespace {

// The readings averaged, as members, in one place for the hours and for "now".
// Both structs name them the same, which is what lets one list serve both.
template <typename Point>
QList<Reading Point::*> averagedReadings()
{
    return { &Point::temperature, &Point::apparentTemperature, &Point::dewPoint,
             &Point::relativeHumidity, &Point::windSpeed, &Point::windGust };
}

// The provider's own precision: tenths for everything but humidity, which
// Open-Meteo serves in whole percent. An average of three tenths is a number
// with fifteen decimals in climat-cli's JSON otherwise.
//
// Divided back down rather than multiplied by 0.1: 141 * 0.1 is
// 14.100000000000001, and 141 / 10.0 is the same double "14.1" parses to.
double roundAs(Reading HourlyPoint::*member, double value)
{
    const double per = member == &HourlyPoint::relativeHumidity ? 1.0 : 10.0;
    return std::round(value * per) / per;
}

Reading mean(const QList<double> &values)
{
    if (values.isEmpty())
        return std::nullopt;
    double sum = 0.0;
    for (double value : values)
        sum += value;
    return sum / double(values.size());
}

// A model's reading at an instant between two of its hours, by straight line.
// "Now" is a quarter-hour mark, and the nearest hour is up to thirty minutes
// of a day's warming away from it.
Reading interpolate(const Forecast &model, const QDateTime &at, Reading HourlyPoint::*member)
{
    const HourlyPoint *before = nullptr;
    const HourlyPoint *after  = nullptr;
    for (const HourlyPoint &point : model.hourly) {
        if (!point.time.isValid() || !(point.*member).has_value())
            continue;
        if (point.time <= at && (before == nullptr || point.time > before->time))
            before = &point;
        if (point.time >= at && (after == nullptr || point.time < after->time))
            after = &point;
    }
    if (before == nullptr || after == nullptr)
        return std::nullopt;
    const qint64 span = before->time.msecsTo(after->time);
    if (span <= 0)
        return *(before->*member);
    // Two hours apart at most, or the gap is a hole in the series and not
    // something to draw a line across.
    if (span > 2 * 3600 * 1000)
        return std::nullopt;
    const double share = double(before->time.msecsTo(at)) / double(span);
    return *(before->*member) + (*(after->*member) - *(before->*member)) * share;
}

} // namespace

bool consensusApplies(Coordinate coord)
{
    return regionContains(Region::Canada, coord);
}

QStringList consensusModels()
{
    return { QStringLiteral("gem_seamless"), QStringLiteral("ecmwf_ifs") };
}

QList<QLatin1String> consensusVariables()
{
    return { QLatin1String("temperature_2m"),       QLatin1String("apparent_temperature"),
             QLatin1String("dew_point_2m"),         QLatin1String("relative_humidity_2m"),
             QLatin1String("wind_speed_10m"),       QLatin1String("wind_gusts_10m") };
}

QString consensusHourlyParameter()
{
    QStringList names;
    for (const QLatin1String &name : consensusVariables())
        names.append(QString(name));
    return names.join(QLatin1Char(','));
}

Result<QList<Forecast>> adaptConsensus(const QByteArray &body, const QString &providerId)
{
    QJsonParseError syntax{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &syntax);
    if (syntax.error != QJsonParseError::NoError || !document.isObject()) {
        Error error(ErrorKind::Parse,
                    QStringLiteral("the consensus response is not a JSON object"));
        error.setProviderId(providerId);
        return error;
    }
    const QJsonObject root = document.object();

    if (root.value(QLatin1String("error")).toBool(false)) {
        Error error(ErrorKind::Parse, root.value(QLatin1String("reason")).toString());
        error.setProviderId(providerId);
        return error;
    }

    const QJsonObject hourly = root.value(QLatin1String("hourly")).toObject();

    // Each model's columns, renamed to what a one-model response calls them
    // and handed to the adapter every other forecast goes through. A second
    // parser for the same timestamps would be a second place for trap 3 in
    // libclimat/domain/timeaxis.h to be got wrong.
    QList<Forecast> models;
    for (const QString &model : consensusModels()) {
        QJsonObject columns;
        columns.insert(QLatin1String("time"), hourly.value(QLatin1String("time")));
        for (const QLatin1String &name : consensusVariables()) {
            const QJsonValue column = hourly.value(QString(name) + QLatin1Char('_') + model);
            if (column.isArray())
                columns.insert(name, column);
        }
        if (columns.size() == 1) {
            models.append(Forecast{});
            continue;
        }

        QJsonObject single = root;
        single.remove(QLatin1String("current"));
        single.remove(QLatin1String("daily"));
        single.insert(QLatin1String("hourly"), columns);

        Result<Forecast> adapted =
            adaptForecast(QJsonDocument(single).toJson(QJsonDocument::Compact), providerId);
        if (!adapted)
            return adapted.error();
        models.append(adapted.takeValue());
    }
    return models;
}

void applyConsensus(Forecast &forecast, const QList<Forecast> &others)
{
    // ---- the hours ----------------------------------------------------------

    QList<QHash<qint64, const HourlyPoint *>> byInstant;
    for (const Forecast &other : others) {
        QHash<qint64, const HourlyPoint *> index;
        for (const HourlyPoint &point : other.hourly)
            index.insert(point.time.toMSecsSinceEpoch(), &point);
        byInstant.append(index);
    }

    const QTimeZone zone = forecast.timeZone.isValid() ? forecast.timeZone : QTimeZone::utc();
    QHash<QDate, int> averagedHours;

    for (HourlyPoint &point : forecast.hourly) {
        QList<const HourlyPoint *> matches;
        for (const auto &index : byInstant) {
            const auto found = index.constFind(point.time.toMSecsSinceEpoch());
            if (found != index.constEnd())
                matches.append(*found);
        }
        if (matches.isEmpty())
            continue;

        bool changed = false;
        for (Reading HourlyPoint::*member : averagedReadings<HourlyPoint>()) {
            QList<double> values;
            if ((point.*member).has_value())
                values.append(*(point.*member));
            for (const HourlyPoint *match : matches) {
                if ((match->*member).has_value())
                    values.append(*(match->*member));
            }
            if (values.size() < 2)
                continue;
            point.*member = roundAs(member, *mean(values));
            changed = true;
        }
        if (changed)
            ++averagedHours[point.time.toTimeZone(zone).date()];
    }

    // ---- now ----------------------------------------------------------------

    CurrentConditions &now = forecast.current;
    if (now.time.isValid() && !now.isEmpty()) {
        const auto nowMembers  = averagedReadings<CurrentConditions>();
        const auto hourMembers = averagedReadings<HourlyPoint>();
        for (int i = 0; i < nowMembers.size(); ++i) {
            QList<double> values;
            if ((now.*nowMembers[i]).has_value())
                values.append(*(now.*nowMembers[i]));
            for (const Forecast &other : others) {
                const Reading at = interpolate(other, now.time, hourMembers[i]);
                if (at.has_value())
                    values.append(*at);
            }
            if (values.size() >= 2)
                now.*nowMembers[i] = roundAs(hourMembers[i], *mean(values));
        }
    }

    // ---- the days -----------------------------------------------------------
    //
    // Only days an averaged hour fell in, and only from a whole day of hours:
    // the first and last days of a window can be partial, and a high taken
    // from half a day is the morning's, not the day's.
    for (DailyPoint &day : forecast.daily) {
        if (averagedHours.value(day.date) == 0)
            continue;

        QList<const HourlyPoint *> hours;
        for (const HourlyPoint &point : forecast.hourly) {
            if (point.time.toTimeZone(zone).date() == day.date)
                hours.append(&point);
        }
        if (hours.size() < 23)
            continue;

        const auto extreme = [&hours](Reading HourlyPoint::*member, bool highest) -> Reading {
            Reading best;
            for (const HourlyPoint *point : hours) {
                const Reading value = point->*member;
                if (!value.has_value())
                    continue;
                if (!best.has_value() || (highest ? *value > *best : *value < *best))
                    best = value;
            }
            return best;
        };

        const auto replace = [](Reading &target, Reading value) {
            if (value.has_value())
                target = value;
        };
        replace(day.temperatureMax, extreme(&HourlyPoint::temperature, true));
        replace(day.temperatureMin, extreme(&HourlyPoint::temperature, false));
        replace(day.apparentTemperatureMax, extreme(&HourlyPoint::apparentTemperature, true));
        replace(day.apparentTemperatureMin, extreme(&HourlyPoint::apparentTemperature, false));
        replace(day.windSpeedMax, extreme(&HourlyPoint::windSpeed, true));
        replace(day.windGustMax, extreme(&HourlyPoint::windGust, true));
    }
}

QList<Attribution> consensusAttributions(int year)
{
    // What averaging owes. Open-Meteo's CC BY 4.0 asks that a change be said,
    // and both models' own licences ask for their own words on top of
    // Open-Meteo's line. Both wordings are each licence's own, transcribed.
    const auto change = [](const QString &others) {
        return QStringLiteral("In Canada, Climat averages it with %1 for temperature, "
                              "humidity and wind, and takes the daily highs and lows from "
                              "that average.").arg(others);
    };

    Attribution gem;
    gem.name = QStringLiteral("GEM, Environment and Climate Change Canada");
    // ECCC's statement for information combined from several sources, which
    // an average of three models is. See docs/02-data-sources.md §2.10.
    gem.creditLine = QStringLiteral(
        "Contains information licenced under the Data Server End-use Licence of "
        "Environment and Climate Change Canada.");
    gem.homepage    = QUrl(QStringLiteral("https://eccc-msc.github.io/open-data/"));
    gem.licenceName = QStringLiteral("ECCC Data Server End-use Licence");
    gem.licenceUrl =
        QUrl(QStringLiteral("https://eccc-msc.github.io/open-data/licence/readme_en/"));
    gem.note        = QStringLiteral("The Global Environmental Multiscale model, through "
                                     "Open-Meteo. ")
                      + change(QStringLiteral("ECMWF's IFS and Open-Meteo's best match"));

    Attribution ifs;
    ifs.name = QStringLiteral("IFS, ECMWF");
    // ECMWF's terms of use for its open data: a copyright line with the year,
    // the source, CC BY 4.0, its disclaimer, and whether the data was changed.
    ifs.creditLine = QStringLiteral(
        "Copyright © %1 European Centre for Medium-Range Weather Forecasts (ECMWF)").arg(year);
    ifs.homepage    = QUrl(QStringLiteral("https://www.ecmwf.int/"));
    ifs.licenceName = QStringLiteral("CC BY 4.0");
    ifs.licenceUrl  = QUrl(QStringLiteral("https://creativecommons.org/licenses/by/4.0/"));
    ifs.note        = QStringLiteral(
        "The Integrated Forecasting System, through Open-Meteo. ")
        + change(QStringLiteral("ECCC's GEM and Open-Meteo's best match"))
        + QStringLiteral(" ECMWF does not accept any liability whatsoever for any error or "
                         "omission in the data, their availability, or for any loss or damage "
                         "arising from their use.");

    return { gem, ifs };
}

} // namespace openmeteo
} // namespace climat
