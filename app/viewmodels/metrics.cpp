// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later

#include "metrics.h"

#include "units.h"

#include <QQmlEngine>

#include <cmath>

namespace {

using Quantity = Units::Quantity;

QVariantMap entry(const QString &id, const QString &label, const QString &kind,
                  const QString &series, Quantity quantity, const QString &ramp,
                  const QString &legend)
{
    return QVariantMap{
        { QStringLiteral("id"), id },
        { QStringLiteral("label"), label },
        { QStringLiteral("kind"), kind },
        { QStringLiteral("series"), series },
        { QStringLiteral("quantity"), int(quantity) },
        { QStringLiteral("ramp"), ramp },
        { QStringLiteral("legend"), legend },
    };
}

Quantity quantityOf(const QVariantMap &metric)
{
    return static_cast<Quantity>(metric.value(QStringLiteral("quantity")).toInt());
}

// Smallest "nice" upper bound that contains the data, so an auto-scaled axis
// still lands on round numbers a human would have chosen. metrics.js's ladder,
// with the sub-1 rungs it needed once precipitation could be inches: 0.4 mm of
// rain is 0.016 in, and a ladder that starts at 0.5 would put that under an
// axis running to half an inch and draw it as nothing.
double niceMax(const QVariantList &values, double floor)
{
    double most = 0;
    for (const QVariant &value : values) {
        const double number = value.toDouble();
        if (!qIsNaN(number) && number > most)
            most = number;
    }
    if (most <= 0)
        return floor > 0 ? floor : 1;

    static const double ladder[] = { 0.02, 0.05, 0.1, 0.25, 0.5, 1,  2,   2.5,  5,
                                     10,   20,   25,  50,   100, 200, 250, 500,  1000 };
    for (const double rung : ladder) {
        if (most <= rung)
            return rung;
    }
    return std::ceil(most);
}

// The largest or smallest finite value, or NaN if there is not one. Written out
// rather than std::minmax_element because the list is QVariants and a third of
// them can be NaN where the provider had no reading.
double extremeOf(const QVariantList &values, bool wantMax)
{
    double best = qQNaN();
    for (const QVariant &value : values) {
        const double number = value.toDouble();
        if (qIsNaN(number))
            continue;
        if (qIsNaN(best) || (wantMax ? number > best : number < best))
            best = number;
    }
    return best;
}

} // namespace

Metrics::Metrics()
{
    // The list carries units and axis bounds, so it changes when a preference
    // does. Nothing here caches; the list is rebuilt on every read, which is a
    // dozen QVariantMaps and is not a cost worth a cache that could go stale.
    connect(Units::instance(), &Units::changed, this, &Metrics::changed);
}

Metrics *Metrics::instance()
{
    static Metrics metrics;
    return &metrics;
}

Metrics *Metrics::create(QQmlEngine *, QJSEngine *)
{
    Metrics *metrics = instance();
    QQmlEngine::setObjectOwnership(metrics, QQmlEngine::CppOwnership);
    return metrics;
}

QVariantList Metrics::list() const
{
    const Units *units = Units::instance();

    // Fills in the four fields that depend on the unit in force. `axis()`
    // returns {min, max, step} chosen FOR that unit - see units.h on why a
    // converted axis lands on 32.0 / 50.0 / 68.0.
    const auto scaled = [units](QVariantMap metric, double fallbackMin, double fallbackMax,
                                double fallbackStep, int decimals) {
        const QList<double> axis = units->axis(quantityOf(metric));
        metric[QStringLiteral("min")]  = axis.size() == 3 ? axis.at(0) : fallbackMin;
        metric[QStringLiteral("max")]  = axis.size() == 3 ? axis.at(1) : fallbackMax;
        metric[QStringLiteral("step")] = axis.size() == 3 ? axis.at(2) : fallbackStep;
        metric[QStringLiteral("unit")] = units->symbol(quantityOf(metric));
        metric[QStringLiteral("decimals")] =
            quantityOf(metric) == Quantity::None ? decimals : units->decimals(quantityOf(metric));
        return metric;
    };

    QVariantList out;

    // Overview and Feels like are the same pair of series with the roles
    // swapped: whichever one the reader asked for is the filled area, and the
    // other is a line over it. That is the comparison both charts exist to
    // make - 30° only means something next to the 27° it is being compared
    // with - and it is what the toggle used to do by *replacing* the curve,
    // which showed one reading at a time and no comparison at all.
    //
    // On Overview the line is the toggle's; on Feels like it is always there,
    // because a card called Feels like that does not say what it is unlike is
    // the same missing comparison from the other side.
    {
        QVariantMap overview = scaled(entry(QStringLiteral("overview"), tr("Overview"),
                                            QStringLiteral("area"),
                                            QStringLiteral("temperature"), Quantity::Temperature,
                                            QStringLiteral("temp"), tr("Temperature")),
                                      0, 40, 10, 0);
        overview[QStringLiteral("overlay")]       = QStringLiteral("apparent");
        overview[QStringLiteral("overlayLegend")] = tr("Feels like");
        out.append(overview);
    }

    // autoScale: a fixed axis renders a drizzle as a flat line, which reads as
    // "no data" rather than "a little rain". Rain is the one variable whose
    // range genuinely spans orders of magnitude, so its axis follows the data.
    {
        QVariantMap rain = scaled(entry(QStringLiteral("precipitation"), tr("Precipitation"),
                                        QStringLiteral("bars"), QStringLiteral("precipMm"),
                                        Quantity::Precipitation, QStringLiteral("precip"),
                                        tr("Precipitation amount")),
                                  0, 4, 1, 1);
        rain[QStringLiteral("autoScale")] = true;
        // The floor an auto-scaled axis will not go below, in the display unit.
        rain[QStringLiteral("step")] = Units::instance()->convert(Quantity::Precipitation, 1.0);
        out.append(rain);
    }

    {
        QVariantMap wind = scaled(entry(QStringLiteral("wind"), tr("Wind"), QStringLiteral("area"),
                                        QStringLiteral("windSpeed"), Quantity::Wind,
                                        QStringLiteral("wind"), tr("Wind speed")),
                                  0, 40, 10, 0);
        wind[QStringLiteral("overlay")]       = QStringLiteral("windGust");
        wind[QStringLiteral("overlayLegend")] = tr("Gusts");
        // Dashed, and the only overlay that is. A gust line is the *envelope*
        // of the series under it - same quantity, upper bound - and a dash is
        // how a bound is conventionally drawn. The temperature pair are two
        // readings of two different things that happen to share an axis, and a
        // second reading is a solid line.
        wind[QStringLiteral("overlayDashed")] = true;
        out.append(wind);
    }

    out.append(scaled(entry(QStringLiteral("airquality"), tr("Air quality"), QStringLiteral("bars"),
                            QStringLiteral("airQuality"), Quantity::None, QStringLiteral("aqi"),
                            tr("European AQI")),
                      0, 100, 25, 0));

    out.append(scaled(entry(QStringLiteral("humidity"), tr("Humidity"), QStringLiteral("area"),
                            QStringLiteral("humidity"), Quantity::Percentage,
                            QStringLiteral("humidity"), tr("Relative humidity")),
                      0, 100, 25, 0));

    out.append(scaled(entry(QStringLiteral("cloud"), tr("Cloud cover"), QStringLiteral("area"),
                            QStringLiteral("cloud"), Quantity::Percentage, QStringLiteral("cloud"),
                            tr("Total cloud cover")),
                      0, 100, 25, 0));

    out.append(scaled(entry(QStringLiteral("pressure"), tr("Pressure"), QStringLiteral("area"),
                            QStringLiteral("pressure"), Quantity::Pressure,
                            QStringLiteral("pressure"), tr("Pressure (MSL)")),
                      995, 1030, 10, 0));

    out.append(scaled(entry(QStringLiteral("uv"), tr("UV"), QStringLiteral("bars"),
                            QStringLiteral("uvIndex"), Quantity::None, QStringLiteral("uv"),
                            tr("UV index")),
                      0, 12, 3, 0));

    out.append(scaled(entry(QStringLiteral("visibility"), tr("Visibility"), QStringLiteral("area"),
                            QStringLiteral("visibility"), Quantity::Visibility,
                            QStringLiteral("visibility"), tr("Visibility")),
                      0, 25, 5, 0));

    {
        QVariantMap feels = scaled(entry(QStringLiteral("feels"), tr("Feels like"),
                                         QStringLiteral("area"),
                                         QStringLiteral("apparent"), Quantity::Temperature,
                                         QStringLiteral("temp"), tr("Feels like")),
                                   0, 40, 10, 0);
        feels[QStringLiteral("overlay")]       = QStringLiteral("temperature");
        feels[QStringLiteral("overlayLegend")] = tr("Temperature");
        out.append(feels);
    }

    return out;
}

QVariantMap Metrics::byId(const QString &id) const
{
    const QVariantList all = list();
    for (const QVariant &candidate : all) {
        if (candidate.toMap().value(QStringLiteral("id")).toString() == id)
            return candidate.toMap();
    }
    return all.constFirst().toMap();
}

double Metrics::display(const QVariantMap &metric, double canonical) const
{
    if (qIsNaN(canonical))
        return canonical;
    return Units::instance()->convert(quantityOf(metric), canonical);
}

QVariantList Metrics::displayAll(const QVariantMap &metric, const QVariantList &canonical) const
{
    const Quantity quantity = quantityOf(metric);
    const Units   *units    = Units::instance();

    QVariantList out;
    out.reserve(canonical.size());
    for (const QVariant &value : canonical) {
        const double number = value.toDouble();
        out.append(qIsNaN(number) ? number : units->convert(quantity, number));
    }
    return out;
}

// ---- the axis, and why a fixed one still moves ------------------------------
//
// Nine of the eleven metrics carry a fixed min/max, and that is right: an axis
// that means the same thing every time you look at it is what lets a reader
// compare Tuesday with Friday, and an auto axis makes a flat day look dramatic.
// What it must not be is a *clip*. Every one of those nine was written down as
// the range the weather usually sits in, and weather leaves it - 32 km of
// visibility against a 25 km axis, gusts past 40, an August afternoon past 40°,
// a January morning below zero, a deep low under 995 hPa. The curve was drawn
// anyway, off the top of the plot box, past the last gridline and out of the
// card, which is a chart lying about a number it has.
//
// So the fixed bounds are a *preferred* range: the axis shows at least that
// much, and gives way at either end by whole steps when the data asks. Whole
// steps, because the labelled rhythm is the other half of what makes a fixed
// axis readable - an axis running to 32.4 is not one anybody chose.
//
// autoScale is untouched. Precipitation asked for its axis to follow the data
// and this is not that question.
double Metrics::axisMin(const QVariantMap &metric, const QVariantList &values) const
{
    const double fixed = metric.value(QStringLiteral("min")).toDouble();
    if (metric.value(QStringLiteral("autoScale")).toBool())
        return fixed;

    const double least = extremeOf(values, /*wantMax=*/false);
    if (qIsNaN(least) || least >= fixed)
        return fixed;

    const double step = metric.value(QStringLiteral("step")).toDouble();
    if (step <= 0)
        return least;
    return fixed - std::ceil((fixed - least) / step) * step;
}

double Metrics::axisMax(const QVariantMap &metric, const QVariantList &values) const
{
    const double step = metric.value(QStringLiteral("step")).toDouble();
    if (metric.value(QStringLiteral("autoScale")).toBool())
        return niceMax(values, step);

    const double fixed = metric.value(QStringLiteral("max")).toDouble();
    const double most  = extremeOf(values, /*wantMax=*/true);
    if (qIsNaN(most) || most <= fixed)
        return fixed;

    if (step <= 0)
        return most;
    return fixed + std::ceil((most - fixed) / step) * step;
}

QVariantList Metrics::axisTicks(const QVariantMap &metric, const QVariantList &values) const
{
    QVariantList ticks;

    if (metric.value(QStringLiteral("autoScale")).toBool()) {
        // Four divisions of whatever the data needed. Rounded to two places
        // because an auto axis over inches produces 0.0125 and a label that
        // long is a label nobody reads.
        const double min = metric.value(QStringLiteral("min")).toDouble();
        const double max = axisMax(metric, values);
        for (int i = 0; i <= 4; ++i)
            ticks.append(std::round((min + (max - min) * i / 4.0) * 1000.0) / 1000.0);
        return ticks;
    }

    // Off the resolved bounds and not the registry's, or an axis that gave way
    // would grow a stretch with no gridlines on it - which is the same defect
    // as clipping, one step further out.
    const double min = axisMin(metric, values);
    const double max = axisMax(metric, values);

    double step = metric.value(QStringLiteral("step")).toDouble();
    if (step <= 0)
        return { min, max };

    // A range that has given way at both ends can carry more labels than the
    // gutter has room for, and eleven gridlines is a hatch rather than a scale.
    // Doubling keeps them on round numbers, which halving the count any other
    // way would not.
    while ((max - min) / step > 8.0)
        step *= 2.0;

    for (double value = min; value <= max + 0.001; value += step)
        ticks.append(std::round(value * 100.0) / 100.0);
    return ticks;
}

QString Metrics::formatDisplay(const QVariantMap &metric, double value) const
{
    if (qIsNaN(value))
        return QStringLiteral("-");
    const int decimals = metric.value(QStringLiteral("decimals")).toInt();
    return QString::number(value, 'f', decimals)
         + metric.value(QStringLiteral("unit")).toString();
}
