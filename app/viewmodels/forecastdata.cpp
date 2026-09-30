// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later

#include "forecastdata.h"

#include "appengine.h"
#include "timeformat.h"
#include "units.h"

#include "libclimat/domain/hourconvention.h"
#include "libclimat/domain/weathercode.h"

#include <QHash>
#include <QLocale>
#include <QQmlEngine>

using namespace climat;

namespace {

// The window: one calendar day, midnight to midnight, of whichever day the
// strip has selected - today included.
//
// It used to be forty-eight hours starting fifteen behind the present, and only
// for today; every other day was already the day itself. What removed the
// asymmetry is that the chart's arrows step the day now instead of scrolling
// the hours. A window running past midnight would be a chart of Friday with
// Saturday morning on the end of it, and no arrow that could mean "the rest of
// Saturday" without also meaning "the part you can already see".
// The most hours a calendar day can hold. Twenty-five on the night a fall-back
// DST transition repeats an hour - the window is counted by date rather than by
// this number, and the bound is only so that a provider handing over a series
// with a repeating or broken date cannot make it unbounded.
constexpr int kMaxDayHours = 26;

// Below this an hour is dry. precip.js's TRACE, and the only number from that
// file repeated here - repeated because the question "is this hour wet" is
// asked before precip.js sees the data, when the type is being read off the
// weather code.
constexpr double kTrace = 0.1;

// An absent Reading becomes NaN rather than 0. Zero is a temperature, a wind
// speed and a rainfall; NaN is not, and every formatter in this app already
// draws it as an em dash. libclimat/domain/reading.h makes the same argument at
// greater length.
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

// Rounded to whole units for the series a UI prints as an integer - humidity,
// cloud cover, probability. Absent stays absent.
double rounded(const Reading &reading)
{
    return reading.has_value() ? std::round(*reading) : qQNaN();
}

QString shortWeekday(QDate date)
{
    return QLocale().dayName(date.dayOfWeek(), QLocale::ShortFormat);
}


// "3 PM", or "15:00" for a reader who asked for a 24-hour clock. The reference's
// spelling was the only one this returned until the preferences screen landed,
// and the arithmetic that produced it now lives in one file for the whole app -
// see app/viewmodels/timeformat.h, whose header says why five copies of it was
// not a tidiness problem.
//
// Not called `hourLabel`: ForecastData has a member of that name, and a member
// hides a namespace-scope function of the same name at every call site inside
// the class.
QString hourOf(const QDateTime &local)
{
    return TimeFormat::instance()->hour(local.time());
}

// ---- the two maps, and why only they need this ------------------------------
//
// Almost everything this class publishes is a QVariantList or an int, and both
// are well formed empty: an empty array answers `.length` with 0 and a Repeater
// bound to it draws nothing. A QVariantMap is not. `Data.moonPhase.illuminated`
// on an empty map is `undefined`, which is a failed assignment to a real
// property and one line of console on every start that has to fetch.
//
// app/viewmodels/conditionsdata.cpp carries the full argument - this is the same
// rule applied to the two maps on this side of the boundary.
QVariantMap neutralMonth()
{
    return QVariantMap{
        { QStringLiteral("name"), QString() },
        { QStringLiteral("year"), 0 },
        { QStringLiteral("number"), 0 },
        { QStringLiteral("length"), 0 },
        { QStringLiteral("firstWeekday"), 0 },
        { QStringLiteral("today"), 0 },
    };
}

QVariantMap neutralMoonPhase()
{
    return QVariantMap{
        { QStringLiteral("name"), QString() },
        { QStringLiteral("illuminated"), 0.0 },
        { QStringLiteral("waxing"), true },
    };
}

} // namespace

ForecastData::ForecastData(QObject *parent)
    : QObject(parent)
{
    // Born with the shape. `Data.moonPhase` is read while HourlyOverview is
    // being constructed, which precedes the first snapshot on any start that
    // has to fetch one.
    clear();

    // Everything on screen that carries a unit is rebuilt when a preference
    // changes. Cheaper would be to convert lazily in the getters; that would
    // also mean nothing notifies, and a settings screen whose effect appears
    // after the next refresh is a settings screen that looks broken.
    const auto rebuild = [this]() {
        if (!m_forecast.isEmpty())
            setSnapshot(m_forecast, m_air, m_now, m_place);
    };
    connect(Units::instance(), &Units::changed, this, rebuild);

    // The same treatment for the clock, and for the same reason: every hour
    // label on the chart and in the list is a formatted string held in a
    // snapshot, so a format changed while the app is open has to rebuild it or
    // the preference appears to do nothing until the next fetch.
    connect(TimeFormat::instance(), &TimeFormat::changed, this, rebuild);
}

ForecastData *ForecastData::create(QQmlEngine *, QJSEngine *)
{
    // Not a second instance: the one AppEngine owns and pushes into. A
    // default-constructed singleton here would register, resolve, evaluate
    // every binding and report an empty forecast for ever, with nothing
    // warning - see the note on private constructors in app/appoptions.h.
    ForecastData *data = AppEngine::instance()->forecastData();
    QQmlEngine::setObjectOwnership(data, QQmlEngine::CppOwnership);
    return data;
}

// The traditional phase name, translated. libclimat returns an identifier for
// exactly this reason - the table is data, and the CLI and the applet in D6
// need the same sentence in the same language as the app.
QString ForecastData::moonPhaseLabel(const QString &identifier)
{
    if (identifier == QLatin1String("new"))             return tr("New moon");
    if (identifier == QLatin1String("waxing-crescent")) return tr("Waxing crescent");
    if (identifier == QLatin1String("first-quarter"))   return tr("First quarter");
    if (identifier == QLatin1String("waxing-gibbous"))  return tr("Waxing gibbous");
    if (identifier == QLatin1String("full"))            return tr("Full moon");
    if (identifier == QLatin1String("waning-gibbous"))  return tr("Waning gibbous");
    if (identifier == QLatin1String("last-quarter"))    return tr("Last quarter");
    if (identifier == QLatin1String("waning-crescent")) return tr("Waning crescent");
    return {};
}

QVariantList ForecastData::weekdayNames() const
{
    // Sunday first, because `weekdayOf()` returns 0 for Sunday and the calendar
    // grid indexes this list with it. Localised: the names move, the order is
    // the grid's.
    QVariantList names;
    const QLocale locale;
    for (int day = 0; day < 7; ++day)
        names.append(locale.dayName(day == 0 ? 7 : day, QLocale::ShortFormat));
    return names;
}

// Everything a day change invalidates, and nothing else. Every builder below
// appends, so this is what makes calling one twice mean "rebuild" rather than
// "append a second copy" - which is what a day change does four times over.
void ForecastData::clearWindow()
{
    m_count = 0;
    m_nowIndex = 0;
    m_start = 0;
    m_temperature.clear(); m_apparent.clear(); m_precipProb.clear(); m_precipMm.clear();
    m_cloud.clear(); m_humidity.clear(); m_windSpeed.clear(); m_windGust.clear();
    m_windDirection.clear(); m_pressure.clear(); m_uvIndex.clear(); m_visibility.clear();
    m_airQuality.clear(); m_precipTypes.clear();
    m_hourLabels.clear(); m_conditions.clear(); m_conditionTexts.clear();
    m_labelConditions.clear();
    m_hasApparent = false;
    m_sunEvents.clear();
    m_labelIndices.clear(); m_precipBuckets.clear();
}

void ForecastData::clear()
{
    m_hours.clear();
    m_nowAbsolute = 0;
    clearWindow();
    m_days.clear(); m_dayDates.clear(); m_todayIndex = 0; m_selectedDay = 0;
    m_monthDays.clear();

    // Reset to a shape rather than emptied, for the reason the two functions
    // give. buildMonth() and buildSunEvents() overwrite these keys in place, so
    // the neutral values survive only until there is a forecast to replace them.
    m_month     = neutralMonth();
    m_moonPhase = neutralMoonPhase();
}

void ForecastData::setSnapshot(const Forecast &forecast, const AirQuality &airQuality,
                               const QDateTime &now, const Place &place)
{
    // Captured before clear() zeroes it. A refresh that leaves the selection
    // where it was must not announce a change: the chart re-opens on
    // `selectedDayChanged` and a signal every ten minutes would haul a reader
    // who had scrolled back to this morning forward again, on a timer, for no
    // reason they could see.
    const int previousDay = m_selectedDay;

    m_forecast = forecast;
    m_air      = airQuality;
    m_place    = place;
    m_now      = now;

    // The forecast's own zone first, the place's second, UTC last. Not the
    // machine's, ever: a saved location in Tokyo renders its own evening while
    // the app runs in Toronto, and the moment this falls back to the system
    // zone that stops being true without anything looking wrong.
    if (forecast.timeZone.isValid())
        m_zone = forecast.timeZone;
    else if (!place.timezone.isEmpty())
        m_zone = QTimeZone(place.timezone.toUtf8());
    else
        m_zone = QTimeZone::UTC;

    clear();

    if (forecast.isEmpty()) {
        Q_EMIT changed();
        return;
    }

    // THE SHIFT. Once, here, at the boundary where domain data becomes chart
    // data - libclimat/domain/hourconvention.h says to call it exactly once and
    // explains what calling it twice does. Everything below indexes `m_hours`,
    // in which an accumulated quantity describes the hour STARTING at its
    // timestamp.
    m_hours = asHourStarting(forecast.hourly);

    // Days first now, and that ordering is load-bearing: the window is of a day
    // and has to be able to look its date up. A fresh snapshot always opens on
    // today - the rows have moved, so a remembered index would point at a
    // different date, and the one thing worse than losing a selection is
    // keeping the number and silently changing what it means.
    buildDays(now);
    m_selectedDay = m_todayIndex;

    buildWindow(now);
    buildSeries();
    buildLabels();
    buildMonth(now);
    buildSunEvents();
    buildBuckets();

    if (m_selectedDay != previousDay)
        Q_EMIT selectedDayChanged();
    Q_EMIT changed();
}

void ForecastData::setSelectedDay(int index)
{
    const int clamped = m_days.isEmpty() ? 0 : qBound(0, index, int(m_days.size()) - 1);
    if (clamped == m_selectedDay)
        return;

    m_selectedDay = clamped;
    if (!m_hours.isEmpty())
        retarget();

    Q_EMIT selectedDayChanged();
    Q_EMIT changed();
}

// The arrows either side of the chart, and the reason they are not two lines of
// QML: "the next day" is a fact about `days`, both shells ask for it, and the
// setter above is what clamps. A view that did the arithmetic itself would be a
// second opinion about how many days there are.
void ForecastData::stepDay(int delta)
{
    setSelectedDay(m_selectedDay + delta);
}

void ForecastData::retarget()
{
    clearWindow();
    buildWindow(m_now);
    buildSeries();
    buildLabels();
    buildSunEvents();
    buildBuckets();
}

// ---- the window ------------------------------------------------------------------

void ForecastData::buildWindow(const QDateTime &now)
{
    if (m_hours.isEmpty())
        return;

    // The hour containing `now`. A linear scan over a few hundred entries, for
    // the reason Forecast::hourAt gives: a binary search would need the sort
    // order to be a documented invariant rather than a thing that happens to be
    // true.
    int current = 0;
    for (int i = 0; i < m_hours.size(); ++i) {
        if (m_hours.at(i).time <= now)
            current = i;
        else
            break;
    }
    m_nowAbsolute = current;

    // The selected day, from its first hour. Falling back to today rather than
    // to an empty chart, because the strip draws eleven cards off the *daily*
    // series and a provider whose hourly horizon is shorter than its daily one
    // - MET Norway's is, by days - would otherwise have cards on it that select
    // nothing.
    //
    // A day the series only partly covers gives a partly covered window, and
    // that is the honest answer rather than a case to pad: MET Norway's hourly
    // series begins at the current hour, so its "today" genuinely has no
    // morning in it.
    const auto dateAt = [this](int index) {
        return m_hours.at(index).time.toTimeZone(m_zone).date();
    };

    const auto firstHourOn = [&](const QDate &date) {
        if (!date.isValid())
            return -1;
        for (int i = 0; i < m_hours.size(); ++i) {
            if (dateAt(i) == date)
                return i;
        }
        return -1;
    };

    // A day the series does not reach is clamped to the nearest one it does,
    // rather than falling back to today. The strip draws eleven cards off the
    // *daily* series and a provider whose hourly horizon is shorter than its
    // daily one - MET Norway's is, by days - has cards on it that no hour
    // answers for. Falling back to today put a column labelled "Now" and a live
    // past veil under a card that says "Sun", which is a chart lying about
    // which day it is of; clamping shows the nearest day there is data for and
    // says nothing untrue.
    const QDate firstDate = dateAt(0);
    const QDate lastDate  = dateAt(int(m_hours.size()) - 1);

    // Clamped by hand rather than with qBound. This class deliberately does not
    // assume the series is sorted - the scan above says so at length - and
    // qBound asserts when its two bounds arrive the wrong way round, so a
    // provider handing back a descending series would abort a debug build
    // instead of degrading.
    QDate wanted = m_dayDates.value(m_selectedDay);
    if (wanted.isValid()) {
        if (firstDate.isValid() && wanted < firstDate)
            wanted = firstDate;
        if (lastDate.isValid() && wanted > lastDate)
            wanted = lastDate;
    }

    int firstOfDay = firstHourOn(wanted);
    if (firstOfDay < 0) {
        // Inside the horizon and still missing - a gap in the series. The last
        // day there is data for is the honest place to land.
        wanted     = lastDate;
        firstOfDay = firstHourOn(wanted);
    }

    m_start = qMax(0, firstOfDay);

    // What the window is actually of, which is not always what was asked for:
    // the clamp above moves a selection past the hourly horizon onto the last
    // day there is data for. The moon in the legend reads this rather than
    // `selectedDay`, or a chart of Friday's hours would carry Sunday's phase
    // under it.
    m_windowDate = wanted;

    // Counted forward while the date holds, NOT `qMin(24, …)`.
    //
    // Twenty-four entries from the first hour of a day is twenty-four hours of
    // *series*, which is a different thing from the day in three cases that all
    // occur: a spring-forward day is 23 hours long and a fall-back day is 25, so
    // a fixed 24 spills into the next date or drops the last hour of this one;
    // and MET Norway's series begins at the current hour, so its "today" starts
    // at 05:00 and twenty-four entries would run to 04:00 tomorrow under a card
    // labelled today. The window is a calendar day or it is not one.
    m_count = 0;
    while (m_start + m_count < m_hours.size() && m_count < kMaxDayHours
           && dateAt(m_start + m_count) == wanted)
        ++m_count;

    // Deliberately not clamped. See the header: this is an offset to the
    // present, and the chart's past veil is `xForIndex(nowIndex)` wide - so a
    // day still ahead of us produces a negative width and veils nothing, and a
    // day behind us produces one wider than the plot and veils all of it,
    // without a single branch in the QML.
    m_nowIndex = current - m_start;

    // Every second hour, and never the outermost column at either end.
    //
    // The header band centres a two-column entry on its label, so a label in
    // column 0 or in the last column is half outside the plot - which did not
    // show while the chart scrolled and the clip took it, and does now that the
    // day is drawn to the plot's exact width.
    //
    // "Now" is one of the labels wherever the window has a now in it, or the
    // word is never drawn. That fixes the phase to the parity of `nowIndex`, and
    // the sequence starts at whichever of 1 and 2 shares it.
    //
    // The exact invariant is `0 < nowIndex < count - 1`, and both ends of it
    // cost something real. Between 11 p.m. and midnight the present is the last
    // column and goes unlabelled; on MET Norway, whose series begins at the
    // current hour, the present is column 0 of its own day and goes unlabelled
    // every time. In both the now line and the past veil still mark it, and the
    // alternative - an entry centred on an outermost column - is half a glyph
    // and a sliced "AM" over the neighbouring label, which is worse in the case
    // it fixes and worse again in the eleven it does not.
    //
    // A day the reader is not living through has no such constraint and takes
    // the even phase - 2 AM, 4 AM, 6 AM - which is how a clock reads.
    m_labelStep       = 2;
    m_firstLabelIndex = (nowInWindow() && (m_nowIndex % m_labelStep) != 0) ? 1 : 2;

    const QDateTime first = localTimeAt(0);
    m_startHour = first.isValid() ? first.time().hour() : 0;

    m_labelIndices.clear();
    for (int i = m_firstLabelIndex; i < m_count - 1; i += m_labelStep)
        m_labelIndices.append(i);

    // A stub of a day - three columns, which is what the last day of MET
    // Norway's hourly horizon can come to - has no index that is both inside
    // the edges and on the phase, and comes out with no labels at all. That is
    // a chart with no hours on it, no condition glyphs and no vertical guides.
    // One label in the middle is the whole of what will fit.
    if (m_labelIndices.isEmpty() && m_count >= 3) {
        m_firstLabelIndex = m_count / 2;
        m_labelIndices.append(m_firstLabelIndex);
    }
}

QDateTime ForecastData::localTimeAt(int index) const
{
    const int absolute = m_start + index;
    if (absolute < 0 || absolute >= m_hours.size())
        return {};
    return m_hours.at(absolute).time.toTimeZone(m_zone);
}

// ---- the series ------------------------------------------------------------------

void ForecastData::buildSeries()
{
    // Air quality arrives from a different endpoint on a different time axis
    // and at a different horizon - five days against sixteen. Matched by
    // timestamp rather than by index, because the two series do not start at
    // the same hour and lining them up by position would put yesterday's
    // pollution under tomorrow's temperature.
    QHash<qint64, double> aqiByHour;
    for (const AirQualityPoint &point : m_air.hourly) {
        if (point.europeanAqi)
            aqiByHour.insert(point.time.toSecsSinceEpoch(), double(*point.europeanAqi));
    }

    for (int i = 0; i < m_count; ++i) {
        const HourlyPoint &hour = m_hours.at(m_start + i);

        m_temperature.append(display(hour.temperature, Units::Quantity::Temperature));
        m_apparent.append(display(hour.apparentTemperature, Units::Quantity::Temperature));
        if (hour.apparentTemperature.has_value())
            m_hasApparent = true;
        m_precipProb.append(rounded(hour.precipitationProbability));
        m_cloud.append(rounded(hour.cloudCover));
        m_humidity.append(rounded(hour.relativeHumidity));
        m_windSpeed.append(display(hour.windSpeed, Units::Quantity::Wind));
        m_windGust.append(display(hour.windGust, Units::Quantity::Wind));
        m_windDirection.append(rounded(hour.windDirection));
        m_pressure.append(display(hour.pressureMsl, Units::Quantity::Pressure));
        m_uvIndex.append(value(hour.uvIndex));
        m_visibility.append(display(hour.visibility, Units::Quantity::Visibility));

        // Millimetres, always. See the header: precip.js's intensity bands are
        // statements about millimetres and converting them would reclassify
        // every rain band in the app.
        const double mm = value(hour.precipitation);
        m_precipMm.append(mm);

        // The type comes from the weather code and nothing else. An hour with
        // rain in it and no code is dry as far as the wash is concerned, which
        // is the honest reading: we do not know what was falling.
        QString type;
        if (hour.weatherCode && mm >= kTrace) {
            const PrecipitationType kind = precipitationTypeFor(*hour.weatherCode);
            if (kind != PrecipitationType::None)
                type = precipitationTypeName(kind);
        }
        m_precipTypes.append(type);

        const auto aqi = aqiByHour.constFind(hour.time.toSecsSinceEpoch());
        m_airQuality.append(aqi == aqiByHour.cend() ? qQNaN() : *aqi);
    }
}

// ---- days ------------------------------------------------------------------------

void ForecastData::buildDays(const QDateTime &now)
{
    const QDate today = now.toTimeZone(m_zone).date();

    QVariantList all;
    QList<QDate> allDates;
    int          todayRow = -1;

    for (const DailyPoint &day : m_forecast.daily) {
        QVariantMap entry;
        entry[QStringLiteral("date")]    = day.date.day();
        entry[QStringLiteral("month")]   = day.date.month();
        entry[QStringLiteral("weekday")] = shortWeekday(day.date);

        const qint64 offset = today.daysTo(day.date);
        entry[QStringLiteral("label")] = offset == 0   ? tr("Today")
                                       : offset == -1  ? tr("Yesterday")
                                       : offset == 1   ? tr("Tomorrow")
                                                       : shortWeekday(day.date);

        entry[QStringLiteral("high")] = display(day.temperatureMax, Units::Quantity::Temperature);
        entry[QStringLiteral("low")]  = display(day.temperatureMin, Units::Quantity::Temperature);
        entry[QStringLiteral("precip")] = rounded(day.precipitationProbabilityMax);

        // Two glyphs per day, the same code read twice. A day card shows the
        // daytime one; selecting it reveals the pair, which is the shape
        // DayStrip has drawn since the prototype.
        const int code = day.weatherCode ? *day.weatherCode : -1;
        entry[QStringLiteral("icon")] =
            code < 0 ? QString() : conditionKindName(climat::conditionFor(code, true));
        entry[QStringLiteral("nightIcon")] =
            code < 0 ? QString() : conditionKindName(climat::conditionFor(code, false));

        if (day.date == today)
            todayRow = int(all.size());
        all.append(entry);
        allDates.append(day.date);
    }

    if (todayRow < 0) {
        m_days       = all;
        m_dayDates   = allDates;
        m_todayIndex = 0;
        return;
    }

    // Yesterday first, then today, then nine more - eleven cards, which is what
    // the day strip has always drawn and what the "10 Day" screen slices ten
    // out of. Trimmed rather than passed through whole because a provider that
    // sends two past days would put the day strip's selection on the wrong card
    // and a provider that sends none would leave it off the end.
    const int from = qMax(0, todayRow - 1);
    const int to   = qMin(int(all.size()), todayRow + 10);
    for (int i = from; i < to; ++i) {
        m_days.append(all.at(i));
        m_dayDates.append(allDates.at(i));
    }
    m_todayIndex = todayRow - from;
}

// ---- the calendar ----------------------------------------------------------------

void ForecastData::buildMonth(const QDateTime &now)
{
    const QDate today = now.toTimeZone(m_zone).date();
    const QDate first(today.year(), today.month(), 1);

    m_month[QStringLiteral("name")]   = QLocale().monthName(today.month(), QLocale::LongFormat);
    m_month[QStringLiteral("year")]   = today.year();
    m_month[QStringLiteral("number")] = today.month();
    m_month[QStringLiteral("length")] = today.daysInMonth();
    // 0 = Sunday, which is the convention weekdayNames() and the calendar grid
    // share. Qt counts Monday as 1 and Sunday as 7.
    m_month[QStringLiteral("firstWeekday")] = first.dayOfWeek() % 7;
    m_month[QStringLiteral("today")]        = today.day();

    for (int d = 1; d <= today.daysInMonth(); ++d) {
        const QDate      date  = QDate(today.year(), today.month(), d);
        const QVariantMap known = dayFor(d, today.month());

        QVariantMap cell;
        cell[QStringLiteral("date")]    = d;
        cell[QStringLiteral("weekday")] = shortWeekday(date);
        cell[QStringLiteral("isToday")] = date == today;

        // A day outside the forecast horizon carries no numbers, and the
        // calendar draws the cell empty. mockdata.js generated a plausible
        // seasonal shape for those days; a real calendar must not, because a
        // high for the 29th that nobody forecast is a number a reader will
        // plan around. docs/08-risks.md R9.
        cell[QStringLiteral("high")] = known.value(QStringLiteral("high"), QVariant());
        cell[QStringLiteral("low")]  = known.value(QStringLiteral("low"), QVariant());
        cell[QStringLiteral("icon")] = known.value(QStringLiteral("icon"), QString());
        cell[QStringLiteral("known")] = !known.isEmpty();

        m_monthDays.append(cell);
    }
}

QVariantMap ForecastData::dayFor(int date, int month) const
{
    for (const QVariant &entry : m_days) {
        const QVariantMap day = entry.toMap();
        if (day.value(QStringLiteral("date")).toInt() == date
            && day.value(QStringLiteral("month")).toInt() == month) {
            return day;
        }
    }
    return {};
}

int ForecastData::weekdayOf(int date) const
{
    return (m_month.value(QStringLiteral("firstWeekday")).toInt() + date - 1) % 7;
}

void ForecastData::buildLabels()
{
    // Straight off the three helpers, so there is one implementation of each
    // answer and the arrays cannot drift from the functions the tests read.
    for (int i = 0; i < m_count; ++i) {
        m_hourLabels.append(hourLabel(i));
        m_conditions.append(conditionFor(i));
        m_conditionTexts.append(conditionText(i));
        m_labelConditions.append(QString());
    }

    for (const QVariant &label : std::as_const(m_labelIndices)) {
        const int index = label.toInt();
        if (index >= 0 && index < m_labelConditions.size())
            m_labelConditions[index] = conditionForLabel(index);
    }
}

// ---- the sun and the moon ---------------------------------------------------------

void ForecastData::buildSunEvents()
{
    const QDateTime windowStart = m_count > 0 ? m_hours.at(m_start).time : QDateTime();
    if (!windowStart.isValid())
        return;

    const auto place = [&](const QDateTime &instant, const QString &kind) {
        if (!instant.isValid())
            return;
        // Fractional, so a marker can sit between two samples - the sun does
        // not rise on the hour.
        const double index = windowStart.secsTo(instant) / 3600.0;
        if (index < 0 || index > m_count - 1)
            return;

        QVariantMap event;
        event[QStringLiteral("index")] = index;
        event[QStringLiteral("kind")]  = kind;
        // The badge on a sunrise or sunset marker in the hourly chart. It was
        // the sixth and last clock in this application to be formatted by hand,
        // and the one that made the case for TimeFormat: it is two badges on a
        // chart whose axis is right above them, so a 24-hour axis over a 12-hour
        // badge is a contradiction inside one card.
        event[QStringLiteral("text")] =
            TimeFormat::instance()->clock(instant.toTimeZone(m_zone).time());
        m_sunEvents.append(event);
    };

    for (const DailyPoint &day : m_forecast.daily) {
        place(day.sunrise, QStringLiteral("sunrise"));
        place(day.sunset, QStringLiteral("sunset"));
    }

    // The moon of the day the window IS OF - `m_windowDate`, not
    // `selectedDay` - which are the same thing except where the strip has a
    // card the hourly series cannot reach and the window clamped. It is read by one thing - the chart's own legend, right
    // under the plot - so a legend naming today's phase over Friday's hours
    // would be the chart contradicting itself. The phase is a position in the
    // cycle and the illumination is the lit fraction, which is not linear in
    // it: libclimat computes the second from the first so that a waxing crescent
    // is not reported as a quarter lit.
    const QDate today  = m_now.toTimeZone(m_zone).date();
    const QDate wanted = m_windowDate.isValid() ? m_windowDate : today;
    for (const DailyPoint &day : m_forecast.daily) {
        if (day.date != wanted)
            continue;
        const Reading lit = moonIllumination(day.moonPhase);
        m_moonPhase[QStringLiteral("name")]        = moonPhaseLabel(moonPhaseName(day.moonPhase));
        m_moonPhase[QStringLiteral("illuminated")] = lit.has_value() ? *lit : 0.0;
        // Which limb is lit. The fraction alone cannot say - see
        // libclimat/domain/forecast.h - and the legend's disc is small enough
        // that drawing it mirrored looks like nothing at all until you compare
        // it with the card that has it right.
        m_moonPhase[QStringLiteral("waxing")]      = isWaxing(day.moonPhase);
        break;
    }
}

// ---- the precipitation strip -------------------------------------------------------

void ForecastData::buildBuckets()
{
    // One bucket per two-hour interval, carrying the interval's peak probability
    // - mockdata.js's rule, kept because the strip is a row of two-hour columns
    // and the honest number for a column is the worst hour in it.
    //
    // From midnight, not from the first label. The strip tiles the window and
    // the labels no longer do: they skip the outermost columns so the header
    // band's entries stay inside the plot, and buckets that followed them would
    // leave the first hours of the day with no cell over them.
    // The last cell takes what is left over, which is one column more than the
    // rest. The plot maps hour `i` to `i * columnWidth`, so a window of N hours
    // is N-1 intervals wide - and N-1 does not divide by the step. A cell of the
    // usual span at the end therefore ran past the plot and was clipped to half
    // its width, with its droplet and percentage spilling out of it.
    for (int i = 0; i < m_count - 1;) {
        const int  remaining = m_count - 1 - i;
        const bool last      = remaining < 2 * m_labelStep;
        const int  span      = last ? remaining : m_labelStep;

        // Inclusive of the final hour on the last cell: that cell reaches the
        // plot's right edge, and the right edge IS the last hour.
        const int through = last ? m_count - 1 : i + span - 1;

        double peak = m_precipProb.value(i).toDouble();
        for (int k = i + 1; k <= through && k < m_count; ++k)
            peak = qMax(peak, m_precipProb.value(k).toDouble());

        QVariantMap bucket;
        bucket[QStringLiteral("index")] = i;
        bucket[QStringLiteral("span")]  = span;
        bucket[QStringLiteral("prob")]  = qIsNaN(peak) ? 0 : int(peak);
        m_precipBuckets.append(bucket);

        i += span;
    }
}

// ---- the helpers -------------------------------------------------------------------

bool ForecastData::isNight(int index) const
{
    const int absolute = m_start + index;
    if (absolute < 0 || absolute >= m_hours.size())
        return false;

    const std::optional<bool> day = m_hours.at(absolute).isDay;

    // Absent means we were not told, and the honest default is day: a night
    // glyph on an hour that turns out to be noon is the more wrong of the two,
    // and it is also the one a reader notices.
    return day.has_value() && !*day;
}

QString ForecastData::conditionFor(int index) const
{
    const int absolute = m_start + index;
    if (absolute < 0 || absolute >= m_hours.size())
        return {};

    const WeatherCode code = m_hours.at(absolute).weatherCode;
    if (!code)
        return {};

    return conditionKindName(climat::conditionFor(*code, !isNight(index)));
}

QString ForecastData::conditionForLabel(int index) const
{
    // The span this label stands for: itself and the columns up to the next
    // label. Clamped to the window, so the last label answers for whatever is
    // left rather than reading past the end of the day.
    //
    // BOTH OUTERMOST labels reach to the window's own edge. Labels start at
    // column 1 or 2 and stop one short of the end, so that the header band's
    // entries stay inside the plot - and a span that ran only from label to
    // label left the day's first and last hours covered by nothing, which is
    // the same defect this function exists to fix, moved to the ends of the
    // day. A storm at midnight, or at 11 p.m. on an odd-phased today, had no
    // glyph anywhere in its own band.
    const bool isLast = !m_labelIndices.isEmpty()
                        && m_labelIndices.constLast().toInt() == index;

    const int first = (index == m_firstLabelIndex) ? 0 : index;
    const int last  = isLast ? m_count : qMin(index + m_labelStep, m_count);

    QList<int> codes;
    for (int i = first; i < last; ++i) {
        const int absolute = m_start + i;
        if (absolute < 0 || absolute >= m_hours.size())
            continue;
        if (const WeatherCode code = m_hours.at(absolute).weatherCode)
            codes.append(*code);
    }

    const std::optional<int> worst = codeForLabelledSpan(codes);
    if (!worst)
        return {};

    // Whichever hour carried the winning code decides day or night. Taking the
    // first hour's would put a moon over an evening thunderstorm whenever the
    // span opened after sunset and the storm was in its second hour.
    int at = index;
    for (int i = first; i < last; ++i) {
        const int absolute = m_start + i;
        if (absolute < 0 || absolute >= m_hours.size())
            continue;
        const WeatherCode code = m_hours.at(absolute).weatherCode;
        if (code && *code == *worst) {
            at = i;
            break;
        }
    }

    return conditionKindName(climat::conditionFor(*worst, !isNight(at)));
}

QString ForecastData::conditionText(int index) const
{
    const int absolute = m_start + index;
    if (absolute < 0 || absolute >= m_hours.size())
        return QStringLiteral("-");

    const WeatherCode code = m_hours.at(absolute).weatherCode;
    if (!code)
        return QStringLiteral("-");

    const QString text = climat::conditionText(*code, !isNight(index));
    return text.isEmpty() ? QStringLiteral("-") : text;
}

QString ForecastData::hourLabel(int index) const
{
    // `nowInWindow()` and not just the comparison: on a day window `nowIndex`
    // is an offset that can land anywhere, and 0 == 0 would print "Now" over
    // Friday midnight on the one afternoon a year the arithmetic agreed.
    if (nowInWindow() && index == m_nowIndex)
        return tr("Now");
    return clockLabel(index);
}

QVariantMap ForecastData::ahead(int offset) const
{
    QVariantMap out;

    const int absolute = m_nowAbsolute + offset;
    if (absolute < 0 || absolute >= m_hours.size())
        return out;

    const HourlyPoint &hour = m_hours.at(absolute);
    const bool night = hour.isDay.has_value() && !*hour.isDay;

    out[QStringLiteral("temperature")] =
        display(hour.temperature, Units::Quantity::Temperature);
    out[QStringLiteral("apparent")] =
        display(hour.apparentTemperature, Units::Quantity::Temperature);
    out[QStringLiteral("precipProb")] = rounded(hour.precipitationProbability);
    out[QStringLiteral("night")]      = night;
    out[QStringLiteral("condition")] =
        hour.weatherCode
            ? conditionKindName(climat::conditionFor(*hour.weatherCode, !night))
            : QString();
    out[QStringLiteral("label")] =
        offset == 0 ? tr("Now") : hourOf(hour.time.toTimeZone(m_zone));

    return out;
}

QString ForecastData::clockLabel(int index) const
{
    const QDateTime local = localTimeAt(index);
    if (!local.isValid())
        return {};
    return hourOf(local);
}
