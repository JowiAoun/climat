// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0
//
// Three forecasts averaged, for the readings an average improves, in Canada.
//
// ---- why, measured ----------------------------------------------------------
//
// For a point in southern Canada, Open-Meteo's default `best_match` is NOAA's
// GFS with HRRR, hour for hour; in the North it is something else again. Its
// archived forecasts for June to September 2026 were scored against ECCC's
// hourly observations at 14 airports, Vancouver to St. John's and Iqaluit, by
// scripts/forecast-backtest.py. Average error, every hour pooled:
//
//                               best match   GEM    average of three
//     temperature, 1 day ahead     1.39      1.38        1.14 °C
//     temperature, 3 days          1.82      1.82        1.47
//     temperature, 5 days          2.23      2.12        1.80
//     wind, 1 day ahead            4.58      4.38        4.17 km/h
//     wind, 3 days                 5.04      5.42        4.68
//     wind, 5 days                 5.88      6.09        5.29
//
// The three are best match, ECCC's GEM and ECMWF's IFS. The average beat best
// match at 13 or 14 of the 14 stations on every row. GEM on its own did not:
// level on temperature and worse on wind past the first day. At the shortest
// range, the latest run, the average ties best match (0.83 against 0.84 °C),
// so "now" is averaged too and the hero agrees with the chart under it.
//
// ---- what is not averaged, and why ------------------------------------------
//
// Rain, snow, the weather code, cloud cover, the chance of rain, pressure, UV
// and visibility stay best match's. Averaging rain amounts spreads one model's
// shower across all three, and in the same test it called fewer days right as
// wet or dry three and five days out: 76.5 % against 79.3 % at three days.
// Keeping them whole keeps the glyph, the bars and the sentence about rain
// exactly what they were, and GEM serves no UV or visibility at all.
//
// The daily highs, lows and wind maxima are taken again from the averaged
// hours. That is how Open-Meteo makes them - its daily max is the max of its
// hourly series, checked - so the strip's high is still the chart's peak.
//
// ---- where -----------------------------------------------------------------
//
// Region::Canada, the box the alerts use. It is loose and takes in Seattle and
// Detroit, where the same three models and the same HRRR apply. Nowhere else:
// the measurement is Canadian.
#pragma once

#include "libclimat/core/result.h"
#include "libclimat/domain/coordinate.h"
#include "libclimat/domain/forecast.h"
#include "libclimat/providers/iforecastprovider.h"

#include <QByteArray>
#include <QLatin1String>
#include <QList>
#include <QString>
#include <QStringList>

namespace climat {
namespace openmeteo {

[[nodiscard]] bool consensusApplies(Coordinate coord);

// The two models averaged in with the default request, as Open-Meteo names
// them, and in the order a response suffixes its columns with.
[[nodiscard]] QStringList consensusModels();

// The hourly variables averaged, and the `hourly=` parameter that asks for
// them. No `current=`: with two models Open-Meteo returns one unsuffixed
// current block, so "now" is read off each model's hours instead.
[[nodiscard]] QList<QLatin1String> consensusVariables();
[[nodiscard]] QString              consensusHourlyParameter();

// One Forecast per model in a two-model response, in consensusModels() order,
// holding only the hourly readings above. A model the response does not carry
// is an empty Forecast rather than an error, so a model Open-Meteo stops
// serving leaves the forecast averaged over fewer, not refused.
[[nodiscard]] Result<QList<Forecast>> adaptConsensus(const QByteArray &body,
                                                     const QString &providerId);

// Averages `others` into `forecast`, hour by hour, for consensusVariables();
// then "now", from each other model's hours either side of it; then the daily
// highs, lows and wind maxima of every day an averaged hour fell in. Hours no
// other model has are left as they are.
void applyConsensus(Forecast &forecast, const QList<Forecast> &others);

// The credits the two models need, for data from `year`. Their licences ask
// for more than Open-Meteo's line: ECCC's for its own sentence, ECMWF's for a
// copyright year, both for a link and a word on what was changed.
[[nodiscard]] QList<Attribution> consensusAttributions(int year);

} // namespace openmeteo
} // namespace climat
