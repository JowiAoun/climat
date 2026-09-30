// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The hourly and daily series, in the shape thirteen QML files already read.
//
// ============================================================================
// THIS FILE IS mockdata.js, WITH A PROVIDER BEHIND IT
//
// app/qml/Climat/mockdata.js said so in its own header, before any of this
// existed:
//
//     "Shape of the API deliberately mirrors what libclimat's ForecastProvider
//      will return: parallel per-hour arrays plus derived helpers, no
//      formatting decisions baked in."
//
// So this class keeps that shape exactly - the same property names, the same
// array layout, the same helper functions - and the QML that reads it did not
// have to change. `Data` is still `Data`; it is a C++ singleton in the Climat
// module rather than a JavaScript library, which is why the thirteen files that
// used to say `import "mockdata.js" as Data` now say nothing at all: a
// singleton in the same module is in scope without an import, the way `Theme`
// and `Viewports` already were.
//
// Two things did change and both are marked at their declarations:
// `labelIndices` and `precipBuckets` are properties rather than functions, so
// that a Repeater bound to one rebuilds when a refresh lands.
//
// ============================================================================
// THE WINDOW: ONE DAY, MIDNIGHT TO MIDNIGHT
//
// A provider hands over several hundred hours. The window is twenty-four of
// them - the calendar day `selectedDay` names, in the place's own zone - and
// the day strip above the chart is what moves it.
//
// It was not always. Today used to get a rolling forty-eight-hour window
// starting fifteen hours behind the present, and only every *other* day was a
// day. The asymmetry had a real argument behind it: "today" is the present
// rather than a date, the hours behind you are ones you lived through, and
// "later" does not stop at midnight.
//
// What ended it is the pair of arrows either side of the chart. They step the
// day now instead of scrolling the hours, so the chart shows a whole day at
// once and nothing else - and a window that ran past midnight would put
// Saturday morning on the end of a chart of Friday, where no arrow can reach it
// without also meaning the part already on screen. One window per day is the
// shape that makes "left is yesterday, right is tomorrow" true.
//
// A day the series only partly covers gives a partly covered window. MET
// Norway's hourly series begins at the current hour, so its today has no
// morning in it, and padding one in would be inventing hours.
//
// `nowIndex` is therefore an offset to the present rather than an index into
// the window, and it MAY FALL OUTSIDE IT: negative when the whole window is
// still ahead, `>= count` when it is all behind. That is not a degenerate case
// to guard against, it is the answer - the chart's past veil runs from the
// plot's left edge to `nowIndex`, so a future day veils nothing and a past day
// veils everything, with no branch anywhere. `nowInWindow` is for the two
// things that cannot be expressed as a width: whether to draw the now line, and
// whether any column is labelled "Now".
//
// What does NOT move with it: `days`, `todayIndex`, the calendar, the moon -
// all of those are about dates rather than about the window - and `ahead()`,
// which is below and exists precisely because two readouts mean "now" and have
// to keep meaning it while the chart is showing Friday.
//
// ============================================================================
// UNITS: THE SERIES ARE CANONICAL EXCEPT WHERE THE READER SEES THEM
//
// Temperatures, wind speeds, pressures and visibilities are converted here, on
// the way out, through app/viewmodels/units.h - because a QML file that writes
// `Data.temperature[i] + "°"` is right in Celsius and in Fahrenheit, and one
// that writes `+ " km/h"` is not, which is why HourlyList asks Units instead.
//
// `precipMm` is the exception and stays in millimetres. It has to: precip.js
// classifies intensity against the NWS bands - 2.5 mm/h moderate, 7.6 mm/h
// heavy - and those are statements about millimetres. Converted to inches they
// would silently reclassify every rain band in the app to "light". The chart
// converts it at the plotting boundary instead (app/viewmodels/metrics.h), and
// this is the one series where those two are different places.

#pragma once

#include "libclimat/domain/airquality.h"
#include "libclimat/domain/forecast.h"
#include "libclimat/domain/place.h"

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QTimeZone>
#include <QVariantList>
#include <QVariantMap>

class ForecastData : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Data)
    QML_SINGLETON

    // ---- the window --------------------------------------------------------
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(int nowIndex READ nowIndex NOTIFY changed)
    Q_PROPERTY(int startHour READ startHour NOTIFY changed)
    Q_PROPERTY(int firstLabelIndex READ firstLabelIndex NOTIFY changed)
    Q_PROPERTY(int labelStep READ labelStep NOTIFY changed)

    // Which row of `days` the window is of, and whether the present is inside
    // it. The setter is the one thing in this class a view is allowed to write:
    // the day strip and the week strip both push into it and both read it back,
    // so two shells cannot disagree about which day is on screen.
    Q_PROPERTY(int selectedDay READ selectedDay WRITE setSelectedDay NOTIFY selectedDayChanged)
    Q_PROPERTY(bool nowInWindow READ nowInWindow NOTIFY changed)

    // ---- now, wherever the window is ---------------------------------------
    //
    // Hours counted forward from the present, into the whole series rather than
    // into the window. The Today screen's hourly strip and the Hourly screen's
    // reading both mean "right now", and before this they said `nowIndex` and
    // meant it - which stopped being the same thing the moment the window
    // could be Friday's. `aheadCount` is how many exist; `ahead(0)` is now.
    Q_PROPERTY(int aheadCount READ aheadCount NOTIFY changed)

    // ---- the series --------------------------------------------------------
    //
    // Parallel arrays, `count` long, indexed by hour. Named exactly as
    // metrics.js's `series` field names them, because that registry looks them
    // up by string - `Data[metric.series]` - and a rename here is a tab that
    // silently draws nothing.
    Q_PROPERTY(QVariantList temperature READ temperature NOTIFY changed)
    Q_PROPERTY(QVariantList apparent READ apparent NOTIFY changed)
    Q_PROPERTY(QVariantList precipProb READ precipProb NOTIFY changed)
    Q_PROPERTY(QVariantList precipMm READ precipMm NOTIFY changed)
    Q_PROPERTY(QVariantList cloud READ cloud NOTIFY changed)
    Q_PROPERTY(QVariantList humidity READ humidity NOTIFY changed)
    Q_PROPERTY(QVariantList windSpeed READ windSpeed NOTIFY changed)
    Q_PROPERTY(QVariantList windGust READ windGust NOTIFY changed)
    Q_PROPERTY(QVariantList windDirection READ windDirection NOTIFY changed)
    Q_PROPERTY(QVariantList pressure READ pressure NOTIFY changed)
    Q_PROPERTY(QVariantList uvIndex READ uvIndex NOTIFY changed)
    Q_PROPERTY(QVariantList visibility READ visibility NOTIFY changed)
    Q_PROPERTY(QVariantList airQuality READ airQuality NOTIFY changed)

    // Whether `apparent` carries anything at all. MET Norway has no apparent
    // temperature and Open-Meteo does, so this is the difference between a
    // "Feels like" toggle that shows a second curve and one that redraws the
    // first - see HourlyOverview, which hides the control rather than offering
    // a switch whose only effect is to claim something untrue.
    Q_PROPERTY(bool hasApparent READ hasApparent NOTIFY changed)

    // ---- the per-hour strings, as arrays ------------------------------------
    //
    // The same three answers `hourLabel()`, `conditionFor()` and
    // `conditionText()` give, and the reason both shapes exist is the same one
    // `labelIndices` is a property for, one paragraph down: A BINDING SUBSCRIBES
    // TO THE PROPERTIES IT READS, AND A METHOD CALL IS NOT ONE.
    //
    // The functions are safe inside a view whose model changes with the data,
    // because a changed model rebuilds the delegates and the calls happen
    // again. The window stopped being that. It is one calendar day now, so
    // `count` is 24 on Thursday and 24 on Friday and `labelIndices` is the same
    // list on both - and Qt's item views return early from `setModel` when the
    // new model equals the old one. Nothing was rebuilt, so nothing re-ran, and
    // the day strip moved Friday's temperatures under Thursday's weather glyph
    // and Thursday's hour labels. The chart's header band said "Now" over a day
    // that had already happened, which is how it was found.
    //
    // Arrays, `count` long, exactly like the series above and notified by the
    // same signal. That also fixes a second thing quietly: a reader changing
    // the clock to 24-hour used to see the labels update only after the next
    // fetch, because nothing re-evaluated those calls either.
    Q_PROPERTY(QVariantList hourLabels READ hourLabels NOTIFY changed)
    Q_PROPERTY(QVariantList conditions READ conditions NOTIFY changed)
    Q_PROPERTY(QVariantList conditionTexts READ conditionTexts NOTIFY changed)

    // The header band's glyph per LABELLED column, which is not the glyph for
    // an hour - see `conditionForLabel` below for what it folds and why. Indexed
    // by hour like the three above, and empty at every column that carries no
    // label.
    Q_PROPERTY(QVariantList labelConditions READ labelConditions NOTIFY changed)

    // One of precip.js's six type names per hour, empty where the hour is dry.
    // The provider's WMO code decides, which is the whole reason it is here:
    // thunder and hail cannot be derived from an amount and a temperature, and
    // precip.js said so before there was a code to read.
    Q_PROPERTY(QVariantList precipTypes READ precipTypes NOTIFY changed)

    // ---- days --------------------------------------------------------------
    Q_PROPERTY(QVariantList days READ days NOTIFY changed)
    Q_PROPERTY(int todayIndex READ todayIndex NOTIFY changed)

    // ---- the calendar ------------------------------------------------------
    Q_PROPERTY(QVariantMap month READ month NOTIFY changed)
    Q_PROPERTY(QVariantList monthDays READ monthDays NOTIFY changed)
    // The month after, as far as the forecast reaches into it, and empty until
    // it does. On the 31st the month above has one day left and a sixteen-day
    // forecast has fifteen more: they are here, where the calendar can show them.
    Q_PROPERTY(QVariantMap nextMonth READ nextMonth NOTIFY changed)
    Q_PROPERTY(QVariantList nextMonthDays READ nextMonthDays NOTIFY changed)
    Q_PROPERTY(QVariantList weekdayNames READ weekdayNames CONSTANT)

    // ---- the sky -----------------------------------------------------------
    Q_PROPERTY(QVariantList sunEvents READ sunEvents NOTIFY changed)
    Q_PROPERTY(QVariantMap moonPhase READ moonPhase NOTIFY changed)

    // ---- derived, and properties rather than functions ----------------------
    //
    // These two were `labelIndices()` and `precipBuckets()` in mockdata.js. A
    // Repeater whose model is a function call never re-runs it, because a
    // binding subscribes to the properties it reads and a method call is not
    // one - so a refresh would land in the arrays above and leave the header
    // band drawing the hours from before it. Properties, and the call sites
    // lost their parentheses.
    Q_PROPERTY(QVariantList labelIndices READ labelIndices NOTIFY changed)
    Q_PROPERTY(QVariantList precipBuckets READ precipBuckets NOTIFY changed)

public:
    // The parent has NO default value, and that is what makes create() run.
    //
    // QQmlPrivate::singletonConstructionMode() tests is_default_constructible
    // before it looks for a factory, so a QML_SINGLETON that can be
    // default-constructed is default-constructed and create() is never called.
    // app/appoptions.h has the same note and the same fix; the symptom here was
    // perfect - the type registered, every binding evaluated, and QML read a
    // second, empty snapshot while C++ pushed into the one AppEngine owns.
    // Nothing warned. The screen came up with `undefined` in every card.
    explicit ForecastData(QObject *parent);

    static ForecastData *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // The traditional name for a phase, localised. libclimat returns an
    // identifier - "waning-gibbous" - precisely so that the wording is the
    // app's; public and static here so that the Sun & Moon card and the chart
    // legend cannot end up with two spellings of the same moon.
    [[nodiscard]] static QString moonPhaseLabel(const QString &identifier);

    // Rebuilds everything from one snapshot. `now` is the clock's, never the
    // wall clock's, which is what makes the "Now" column, the past veil and the
    // sky phase land on the same hour in a fixture run as they did on the
    // afternoon it was recorded.
    void setSnapshot(const climat::Forecast &forecast, const climat::AirQuality &airQuality,
                     const QDateTime &now, const climat::Place &place);

    [[nodiscard]] int count() const { return m_count; }
    [[nodiscard]] int nowIndex() const { return m_nowIndex; }
    [[nodiscard]] int startHour() const { return m_startHour; }
    [[nodiscard]] int firstLabelIndex() const { return m_firstLabelIndex; }
    [[nodiscard]] int labelStep() const { return m_labelStep; }

    [[nodiscard]] int  selectedDay() const { return m_selectedDay; }
    void               setSelectedDay(int index);

    // One day earlier or later, clamped by the setter. The chart's arrows, and
    // in C++ rather than in QML because "the next day" is a fact about `days`
    // that two shells would otherwise each have their own arithmetic for.
    Q_INVOKABLE void stepDay(int delta);
    [[nodiscard]] bool nowInWindow() const { return m_nowIndex >= 0 && m_nowIndex < m_count; }

    [[nodiscard]] int aheadCount() const
    {
        return qMax(0, int(m_hours.size()) - m_nowAbsolute);
    }

    // One hour, `offset` hours from now: temperature and probability in the
    // reader's units, the condition glyph's name, the axis label, and whether
    // it is dark. A map rather than five parallel accessors because every
    // caller wants all of it for one column, and an empty map for an hour past
    // the end of the series - QML reads a missing key as `undefined`, which is
    // what a column with nothing in it should be.
    [[nodiscard]] Q_INVOKABLE QVariantMap ahead(int offset) const;

    [[nodiscard]] QVariantList temperature() const { return m_temperature; }
    [[nodiscard]] QVariantList apparent() const { return m_apparent; }
    [[nodiscard]] QVariantList precipProb() const { return m_precipProb; }
    [[nodiscard]] QVariantList precipMm() const { return m_precipMm; }
    [[nodiscard]] QVariantList cloud() const { return m_cloud; }
    [[nodiscard]] QVariantList humidity() const { return m_humidity; }
    [[nodiscard]] QVariantList windSpeed() const { return m_windSpeed; }
    [[nodiscard]] QVariantList windGust() const { return m_windGust; }
    [[nodiscard]] QVariantList windDirection() const { return m_windDirection; }
    [[nodiscard]] QVariantList pressure() const { return m_pressure; }
    [[nodiscard]] QVariantList uvIndex() const { return m_uvIndex; }
    [[nodiscard]] QVariantList visibility() const { return m_visibility; }
    [[nodiscard]] QVariantList airQuality() const { return m_airQuality; }
    [[nodiscard]] QVariantList precipTypes() const { return m_precipTypes; }
    [[nodiscard]] QVariantList hourLabels() const { return m_hourLabels; }
    [[nodiscard]] QVariantList conditions() const { return m_conditions; }
    [[nodiscard]] QVariantList conditionTexts() const { return m_conditionTexts; }
    [[nodiscard]] QVariantList labelConditions() const { return m_labelConditions; }
    [[nodiscard]] bool         hasApparent() const { return m_hasApparent; }

    [[nodiscard]] QVariantList days() const { return m_days; }
    [[nodiscard]] int          todayIndex() const { return m_todayIndex; }

    [[nodiscard]] QVariantMap  month() const { return m_month; }
    [[nodiscard]] QVariantList monthDays() const { return m_monthDays; }
    [[nodiscard]] QVariantMap  nextMonth() const { return m_nextMonth; }
    [[nodiscard]] QVariantList nextMonthDays() const { return m_nextMonthDays; }
    [[nodiscard]] QVariantList weekdayNames() const;

    [[nodiscard]] QVariantList sunEvents() const { return m_sunEvents; }
    [[nodiscard]] QVariantMap  moonPhase() const { return m_moonPhase; }

    [[nodiscard]] QVariantList labelIndices() const { return m_labelIndices; }
    [[nodiscard]] QVariantList precipBuckets() const { return m_precipBuckets; }

    // ---- the helpers mockdata.js had ---------------------------------------
    //
    // Still functions, because they take an index and there is no property
    // shape for that. They are safe as functions because every one of them is
    // called inside a Repeater delegate whose model is one of the properties
    // above - so a refresh rebuilds the delegates and the calls happen again.
    [[nodiscard]] Q_INVOKABLE bool    isNight(int index) const;
    [[nodiscard]] Q_INVOKABLE QString conditionFor(int index) const;

    // The glyph for a *labelled* column of the chart's header band, which is
    // not the same thing as the glyph for an hour.
    //
    // The band cannot draw twenty-four 27 px icons across a plot, so it labels
    // every second column - and for as long as it asked `conditionFor` for the
    // single hour it happened to land on, the other twelve hours of the day had
    // no icon anywhere. Which twelve is arbitrary: a day window's labels start
    // at column 1 and a today window's phase comes from where the present fell,
    // so the same forecast showed or hid the same storm depending on the time
    // of day it was opened.
    //
    // Measured against Open-Meteo on 2026-08-22, 7 of 79 forecast days whose
    // daily code was a thunderstorm drew no thunderstorm glyph anywhere in this
    // band - including Houston's Thursday, where the ten-day strip's card said
    // thunderstorm and every hour beside it said rain.
    //
    // So a labelled column answers for the whole span it stands for, under
    // libclimat/domain/weathercode.h's `codeForLabelledSpan` - its own hour's
    // sky, and anything that is happening anywhere in the span. The day/night
    // form comes from the hour that won, not from the column's first hour: a
    // 7 p.m. storm in July is a day glyph.
    [[nodiscard]] Q_INVOKABLE QString conditionForLabel(int index) const;
    [[nodiscard]] Q_INVOKABLE QString conditionText(int index) const;
    [[nodiscard]] Q_INVOKABLE QString hourLabel(int index) const;
    [[nodiscard]] Q_INVOKABLE QString clockLabel(int index) const;

    // The entry in `days` for a calendar date, or an empty map. The calendar
    // screen's one lookup.
    [[nodiscard]] Q_INVOKABLE QVariantMap dayFor(int date, int month) const;
    [[nodiscard]] Q_INVOKABLE int         weekdayOf(int date) const;

Q_SIGNALS:
    // One signal for the whole snapshot. There is no cheaper granularity worth
    // having: a new forecast changes every series at once, and a per-property
    // signal set would be twenty ways to forget one.
    void changed();

    // Its own signal as well as `changed()`, because this is the one property a
    // view writes: a strip that bound `currentIndex` to `changed()` would
    // re-read the selection on every refresh, and one that bound it to nothing
    // would never see another view move it.
    void selectedDayChanged();

private:
    void clear();

    // Everything downstream of which hours the window covers, rebuilt. Called
    // for a new snapshot and again for every day change - the series, the sun
    // markers and the precipitation buckets are all slices of the window, and
    // `clearWindow()` exists because all three builders append.
    void clearWindow();
    void retarget();

    void buildWindow(const QDateTime &now);
    void buildSeries();

    // The array forms of the three per-hour helpers, and of the header band's
    // per-label glyph. Separate from buildSeries() because it has to run after
    // buildWindow() has settled `labelIndices`, and because the three it fills
    // are strings rather than readings.
    void buildLabels();
    void buildDays(const QDateTime &now);
    void buildMonth(const QDateTime &now);
    [[nodiscard]] QVariantMap calendarCell(QDate date, QDate today) const;
    void buildSunEvents();
    void buildBuckets();

    // The local time of the hour at `index`, or an invalid QDateTime.
    [[nodiscard]] QDateTime localTimeAt(int index) const;

    climat::Forecast   m_forecast;
    climat::AirQuality m_air;
    climat::Place      m_place;
    QTimeZone         m_zone = QTimeZone::UTC;
    QDateTime         m_now;

    // The hourly series after libclimat/domain/hourconvention.h's shift, so
    // that every accumulated quantity in it describes the hour STARTING at its
    // timestamp - the convention precip.js draws on, and the one place in the
    // whole app where that shift happens.
    QList<climat::HourlyPoint> m_hours;

    // The calendar day the window is of. Not derivable from `m_selectedDay`:
    // a card the hourly series cannot reach clamps onto the last day there is
    // data for, and the moon in the chart's legend has to follow the hours
    // rather than the card.
    QDate m_windowDate;

    int m_start           = 0;   // index into m_hours of column 0
    int m_count           = 0;
    int m_nowIndex        = 0;   // may fall outside [0, count) - see the header
    int m_startHour       = 0;
    int m_firstLabelIndex = 2;
    int m_labelStep       = 2;

    // Where the present is in `m_hours`, which is what `nowIndex` is measured
    // from and what `ahead()` counts from. Kept separately because it is the
    // one index in this class that does not move when the window does.
    int m_nowAbsolute = 0;

    QVariantList m_temperature, m_apparent, m_precipProb, m_precipMm, m_cloud, m_humidity;
    QVariantList m_windSpeed, m_windGust, m_windDirection, m_pressure, m_uvIndex;
    QVariantList m_visibility, m_airQuality, m_precipTypes;
    QVariantList m_hourLabels, m_conditions, m_conditionTexts, m_labelConditions;
    bool         m_hasApparent = false;

    QVariantList m_days;
    int          m_todayIndex = 0;

    // The dates behind `m_days`, in the same order. `days` publishes a day and
    // a month for the card to print and that is not enough to find an hour: a
    // sixteen-day forecast crosses a month boundary and two rows can carry the
    // same `date`.
    QList<QDate> m_dayDates;
    int          m_selectedDay = 0;

    QVariantMap  m_month;
    QVariantList m_monthDays;
    QVariantMap  m_nextMonth;
    QVariantList m_nextMonthDays;

    // Every day the provider sent, before `m_days` trims them to the day strip's
    // eleven. The calendar reads these, so it shows the whole forecast.
    QVariantList m_allDays;
    QList<QDate> m_allDates;

    QVariantList m_sunEvents;
    QVariantMap  m_moonPhase;

    QVariantList m_labelIndices;
    QVariantList m_precipBuckets;
};
