// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which hours the chart is looking at, and which hour is "now".
//
// ============================================================================
// WHAT THIS TEST IS ABOUT
//
// The day strip was wired to nothing. Every card selected, the selected card
// grew into the panel below it and merged with it, and the panel went on
// drawing today - because `ForecastData` published one window, fixed around the
// present, and nothing could ask it for another. Picking Friday changed the
// picture and not the data, which is the worst kind of broken control: it
// answers.
//
// So the window moved, and the properties that describe it changed meaning
// with it. This asserts the new meanings, because they are not obvious and two
// of them are deliberately allowed to look wrong:
//
//   * `nowIndex` may fall OUTSIDE [0, count). It is an offset to the present,
//     not an index into the window, and that is what makes the chart's past
//     veil correct on every day with no branch in it - `xForIndex(nowIndex)`
//     wide, so a day still ahead veils nothing and a day gone veils everything.
//
//   * `ahead()` indexes the whole series from the present and is unaffected by
//     any of it. The phone's Today screen and the Hourly screen's reading both
//     mean "right now", and both said `nowIndex` and meant it until the window
//     could be Friday's.
//
// ============================================================================
// WHY IT LINKS app/
//
// The same reason tst_conditionsdata does, and tests/CMakeLists.txt has the
// argument at length: the subject is a view model, which lives in app/, is
// GPL-3.0-or-later rather than MPL-2.0, and is allowed to depend on QML because
// being read by QML is its whole job. `climat_forbid_gui()` is not applied here
// and libclimat's no-GUI promise is still read off tst_httpclient.

#include "forecastdata.h"

#include "libclimat/providers/fixture/fixtureprovider.h"
#include "support/settingssandbox.h"

#include <QDate>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QTime>
#include <QTimeZone>

using namespace climat;

namespace {

// The six glyph names that are only a statement about how much sky is showing.
// Everything else in `ConditionKind` is a thing happening - fog, something
// falling, lightning - and the difference is the line
// libclimat/domain/weathercode.h folds a labelled column across.
bool isSky(const QString &kind)
{
    static const QSet<QString> sky = {
        QStringLiteral("clear-day"),  QStringLiteral("clear-night"),
        QStringLiteral("partly-day"), QStringLiteral("partly-night"),
        QStringLiteral("cloudy"),
    };
    return sky.contains(kind);
}

// Four days of overcast with one thunderstorm in it, stamped at `stormHour` on
// the third day. UTC throughout, so a local hour and an index are the same
// number and the test can say which column it means.
//
// Four days and not three because libclimat/domain/hourconvention.h's shift
// costs the series its last hour: day 2 keeps its 11 p.m. only if day 3 exists
// to supply it.
Forecast oneStormyHour(int stormHour)
{
    const QDate first(2026, 8, 20);

    Forecast forecast;
    forecast.timeZone   = QTimeZone::UTC;
    forecast.providerId = QStringLiteral("test");

    for (int day = 0; day < 4; ++day) {
        const QDate date = first.addDays(day);

        DailyPoint daily;
        daily.date           = date;
        daily.temperatureMax = 20.0;
        daily.temperatureMin = 10.0;
        daily.weatherCode    = day == 2 ? 95 : 3;
        forecast.daily.append(daily);

        for (int hour = 0; hour < 24; ++hour) {
            HourlyPoint point;
            point.time        = QDateTime(date, QTime(hour, 0), QTimeZone::UTC);
            point.temperature = 15.0;
            point.isDay       = hour >= 6 && hour < 20;
            point.weatherCode = (day == 2 && hour == stormHour) ? 95 : 3;
            forecast.hourly.append(point);
        }
    }

    return forecast;
}

} // namespace

class TestForecastData : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void aFreshSnapshotOpensOnToday();
    void todaysWindowIsTodayFromMidnight();
    void theOutermostColumnsAreNeverLabelled();
    void nowIsALabelledColumnWhereverThereIsOne();
    void theStripCoversEveryHourOfTheDay();
    void steppingWalksTheStripAndStopsAtItsEnds();
    void anotherDayIsThatDayFromMidnight();
    void everyDayInTheStripSelectsRealHours();

    void nowIsBehindAWindowThatHasNotStartedYet();
    void nowIsAheadOfAWindowThatIsOver();
    void onlyTodayHasAColumnCalledNow();

    void aheadIsCountedFromThePresentAndNotFromTheWindow();
    void aheadRunsOutRatherThanWrappingRound();

    void anOutOfRangeSelectionIsClampedRatherThanObeyed();
    void reselectingTheSameDayRebuildsNothing();

    void everyLabelledColumnHasAGlyph();
    void aColumnNeverDrawsPlainSkyOverAnHourWithWeatherInIt();
    void aThunderstormOnAColumnTheBandSkipsIsStillDrawn();
    void noHourOfTheDayIsAnHourNoColumnAnswersFor();
    void theMoonFollowsTheHoursRatherThanTheCard();
    void theLastLabelDoesNotReadPastItsOwnDay();

    void theCalendarRunsIntoNextMonthAsFarAsTheForecast();
    void aForecastInsideTheMonthAddsNoNextMonth();

private:
    // One fixture, loaded once: Toronto is the one every capture uses and its
    // recorded instant is midday, which is the only interesting case - a
    // fixture recorded at 00:30 would make "today's window" and "today from
    // midnight" almost the same window and prove nothing.
    Fixture   m_fixture;
    Forecast  m_forecast;
    AirQuality m_air;

    // The zone the fixture's own timestamps are in, which is not the machine's.
    [[nodiscard]] QTimeZone zone() const;

    void load(ForecastData &data);
};

QTimeZone TestForecastData::zone() const
{
    if (m_forecast.timeZone.isValid())
        return m_forecast.timeZone;
    return QTimeZone(m_fixture.place.timezone.toUtf8());
}

void TestForecastData::initTestCase()
{
    // ForecastData reaches Units, which is a QSettings away from the
    // developer's own preferences. Same guard tst_conditionsdata sets.
    SettingsSandbox::install();

    m_fixture = fixtures::load(QStringLiteral("toronto"));
    QVERIFY(m_fixture.isValid());

    ForecastRequest request;
    request.coord = m_fixture.place.coordinate;

    FixtureForecastProvider   forecasts(m_fixture);
    FixtureAirQualityProvider air(m_fixture);

    const QFuture<Result<Forecast>>   forecastFuture = forecasts.fetchForecast(request);
    const QFuture<Result<AirQuality>> airFuture      = air.fetchAirQuality(request);
    QVERIFY(forecastFuture.isFinished());
    QVERIFY(airFuture.isFinished());

    QVERIFY(forecastFuture.result().hasValue());
    QVERIFY(airFuture.result().hasValue());
    m_forecast = forecastFuture.result().value();
    m_air      = airFuture.result().value();

    QVERIFY(!m_forecast.hourly.isEmpty());
    QVERIFY(!m_forecast.daily.isEmpty());
}

void TestForecastData::load(ForecastData &data)
{
    data.setSnapshot(m_forecast, m_air, m_fixture.recordedAt, m_fixture.place);
    QVERIFY(data.count() > 0);
    QVERIFY(!data.days().isEmpty());
}

// ---- what the window is -----------------------------------------------------

void TestForecastData::aFreshSnapshotOpensOnToday()
{
    ForecastData data(nullptr);
    load(data);

    QCOMPARE(data.selectedDay(), data.todayIndex());
    QVERIFY(data.nowInWindow());
}

void TestForecastData::todaysWindowIsTodayFromMidnight()
{
    ForecastData data(nullptr);
    load(data);

    // Today is a day like any other, and that is the change. It used to be
    // forty-eight hours with fifteen of them behind the present - a rolling
    // window that ran past midnight into tomorrow - and the arrows either side
    // of the chart are what ended it: they step the day, so a window that
    // spilled into the next one had hours no arrow could be about.
    //
    // `nowIndex` is the giveaway. On a window that starts at midnight it is not
    // an arbitrary offset any more, it IS the hour of the day, and asserting
    // that is asserting the window's left edge without repeating the lookup
    // that found it.
    QCOMPARE(data.count(), 24);
    QCOMPARE(data.startHour(), 0);
    QVERIFY(data.nowInWindow());

    const QDateTime nowLocal = m_fixture.recordedAt.toTimeZone(zone());
    QCOMPARE(data.nowIndex(), nowLocal.time().hour());
}

// The header band draws an entry per label, two columns wide and centred on it,
// so a label on either outermost column is an entry half outside the plot. That
// did not show while the chart scrolled and the clip took it; it shows now that
// the day is drawn to the plot's exact width.
void TestForecastData::theOutermostColumnsAreNeverLabelled()
{
    ForecastData data(nullptr);
    load(data);

    for (int day = 0; day < data.days().size(); ++day) {
        data.setSelectedDay(day);
        if (data.count() < 3)
            continue;

        const QVariantList labels = data.labelIndices();
        QVERIFY2(!labels.isEmpty(),
                 qPrintable(QStringLiteral("day %1 has no labelled column at all").arg(day)));

        for (const QVariant &label : labels) {
            const int index = label.toInt();
            QVERIFY2(index > 0 && index < data.count() - 1,
                     qPrintable(QStringLiteral("day %1 labels column %2 of %3, which is on the edge")
                                    .arg(day).arg(index).arg(data.count())));
        }
    }
}

// …and the constraint that pulls the other way: "Now" has to be one of the
// labels, or the word is never drawn. Midnight is the one hour it cannot be,
// because column 0 is not available - and a now line with no label under it is
// a smaller loss than a label sliced in half.
void TestForecastData::nowIsALabelledColumnWhereverThereIsOne()
{
    ForecastData data(nullptr);
    load(data);

    QVERIFY(data.nowInWindow());
    QVERIFY(data.nowIndex() > 0);

    const QVariantList labels = data.labelIndices();
    QVERIFY2(labels.contains(QVariant(data.nowIndex())),
             qPrintable(QStringLiteral("column %1 is now and is not labelled").arg(data.nowIndex())));
}

// The precipitation strip tiles the window in two-hour cells and the labels no
// longer do - they skip the outermost columns. Buckets that followed the labels
// left the first hours of the day with no cell over them, which is a gap at the
// left of the strip on every day of the forecast.
void TestForecastData::theStripCoversEveryHourOfTheDay()
{
    ForecastData data(nullptr);
    load(data);

    const QVariantList buckets = data.precipBuckets();
    QVERIFY(!buckets.isEmpty());
    QCOMPARE(buckets.constFirst().toMap().value(QStringLiteral("index")).toInt(), 0);

    int covered = 0;
    for (const QVariant &entry : buckets) {
        const QVariantMap bucket = entry.toMap();
        QCOMPARE(bucket.value(QStringLiteral("index")).toInt(), covered);
        covered += bucket.value(QStringLiteral("span")).toInt();
    }
    // Exactly, not "at least". The plot maps hour i to i * columnWidth, so a
    // window of N hours is N-1 intervals wide - and a cell that ran past that
    // was clipped to half its width with its droplet spilling out of it.
    QCOMPARE(covered, data.count() - 1);
}

// What the chart's arrows do. Clamped by the setter rather than by the caller,
// which is the whole reason this is a method and not two lines of QML.
void TestForecastData::steppingWalksTheStripAndStopsAtItsEnds()
{
    ForecastData data(nullptr);
    load(data);

    const int today = data.selectedDay();
    const int last  = int(data.days().size()) - 1;
    QVERIFY(last > today);

    data.stepDay(1);
    QCOMPARE(data.selectedDay(), today + 1);
    data.stepDay(-1);
    QCOMPARE(data.selectedDay(), today);

    data.setSelectedDay(0);
    data.stepDay(-1);
    QCOMPARE(data.selectedDay(), 0);

    data.setSelectedDay(last);
    data.stepDay(1);
    QCOMPARE(data.selectedDay(), last);
}

void TestForecastData::anotherDayIsThatDayFromMidnight()
{
    ForecastData data(nullptr);
    load(data);

    const int tomorrow = data.todayIndex() + 1;
    QVERIFY(tomorrow < data.days().size());

    data.setSelectedDay(tomorrow);

    // A day, and a whole one. 24 rather than 48: a chart of Friday that opens
    // on Thursday evening is a chart of Friday you have to go and find.
    QCOMPARE(data.count(), 24);
    QCOMPARE(data.startHour(), 0);

    // A day nobody is living through has no "Now" to anchor the label phase to,
    // so it takes the even one - 2 AM, 4 AM, 6 AM - which is how a clock reads.
    // Two rather than zero because column 0 is on the edge; see
    // theOutermostColumnsAreNeverLabelled.
    QCOMPARE(data.firstLabelIndex(), 2);
    QVERIFY(!data.labelIndices().isEmpty());
    QCOMPARE(data.labelIndices().first().toInt(), 2);

    // And it is the RIGHT twenty-four hours, checked against a reading that
    // does not move with the window rather than against this class's own
    // arithmetic repeated.
    //
    // `ahead()` indexes the whole series from the present and is unaffected by
    // which day is selected, so tomorrow's first column has to be the hour
    // `24 - now` hours from now, and every column after it the hour after that.
    // If the day lookup were off by one - a UTC date compared against a local
    // one is the obvious way - this is the assertion that says so.
    const QDateTime nowLocal   = m_fixture.recordedAt.toTimeZone(zone());
    const int       toMidnight = 24 - nowLocal.time().hour();

    const QVariantList tomorrows = data.temperature();
    QCOMPARE(tomorrows.size(), 24);

    for (int i = 0; i < tomorrows.size(); ++i) {
        const QVariantMap hour = data.ahead(toMidnight + i);
        QVERIFY2(!hour.isEmpty(),
                 qPrintable(QStringLiteral("the series runs out %1 hours into tomorrow").arg(i)));
        QCOMPARE(tomorrows.at(i), hour.value(QStringLiteral("temperature")));
    }
}

void TestForecastData::everyDayInTheStripSelectsRealHours()
{
    ForecastData data(nullptr);
    load(data);

    // The strip draws a card per row of `days`, and every one of them is
    // tappable. A row whose date is past the hourly horizon has to fall back to
    // something rather than to an empty chart - MET Norway's daily series
    // outruns its hourly one by days, and a blank panel under a card that
    // lights up is the same defect this whole change is about.
    const int rows = int(data.days().size());
    QVERIFY(rows > 2);

    for (int i = 0; i < rows; ++i) {
        data.setSelectedDay(i);
        QVERIFY2(data.count() > 0,
                 qPrintable(QStringLiteral("day %1 selects an empty window").arg(i)));
        QVERIFY2(!data.temperature().isEmpty(),
                 qPrintable(QStringLiteral("day %1 selects hours with no series").arg(i)));
        QCOMPARE(data.temperature().size(), data.count());
        QCOMPARE(data.precipProb().size(), data.count());
        QCOMPARE(data.precipTypes().size(), data.count());
    }
}

// ---- where "now" is ---------------------------------------------------------

void TestForecastData::nowIsBehindAWindowThatHasNotStartedYet()
{
    ForecastData data(nullptr);
    load(data);

    const int ahead = data.todayIndex() + 2;
    QVERIFY(ahead < data.days().size());
    data.setSelectedDay(ahead);

    // Negative, and that is the answer rather than an error. The chart's past
    // veil is this many columns wide, so a day that has not happened is a day
    // with nothing veiled.
    QVERIFY2(data.nowIndex() < 0,
             qPrintable(QStringLiteral("nowIndex is %1 on a day two days ahead")
                            .arg(data.nowIndex())));
    QVERIFY(!data.nowInWindow());
}

void TestForecastData::nowIsAheadOfAWindowThatIsOver()
{
    ForecastData data(nullptr);
    load(data);

    // Yesterday, which the strip carries as its first card.
    QVERIFY(data.todayIndex() > 0);
    data.setSelectedDay(data.todayIndex() - 1);

    QVERIFY2(data.nowIndex() >= data.count(),
             qPrintable(QStringLiteral("nowIndex is %1 in a window of %2 on yesterday")
                            .arg(data.nowIndex())
                            .arg(data.count())));
    QVERIFY(!data.nowInWindow());
}

void TestForecastData::onlyTodayHasAColumnCalledNow()
{
    ForecastData data(nullptr);
    load(data);

    const QString now = data.hourLabel(data.nowIndex());
    QVERIFY(!now.isEmpty());

    // The label is the one thing that must not fall out of a comparison of two
    // integers: on a day window `nowIndex` is an offset that can be any number,
    // and 0 == 0 would print "Now" over Friday midnight on the afternoon the
    // arithmetic happened to agree.
    for (int day = 0; day < data.days().size(); ++day) {
        data.setSelectedDay(day);
        if (data.nowInWindow())
            continue;
        for (int i = 0; i < data.count(); ++i)
            QVERIFY2(data.hourLabel(i) != now,
                     qPrintable(QStringLiteral("day %1 column %2 is labelled \"%3\"")
                                    .arg(day)
                                    .arg(i)
                                    .arg(now)));
    }
}

// ---- and what is not affected by any of it ----------------------------------

void TestForecastData::aheadIsCountedFromThePresentAndNotFromTheWindow()
{
    ForecastData data(nullptr);
    load(data);

    // Recorded while the window is today's…
    QVERIFY(data.nowInWindow());
    QVariantList expected;
    for (int i = 0; i < 12; ++i)
        expected.append(data.ahead(i));
    QCOMPARE(data.ahead(0).value(QStringLiteral("label")).toString(), QStringLiteral("Now"));

    // …and identical after the chart has been sent three days away. This is
    // the phone's Today screen: it shows the next twelve hours and it must go
    // on showing the next twelve hours while the Hourly tab is looking at
    // Friday.
    data.setSelectedDay(data.todayIndex() + 3 < data.days().size() ? data.todayIndex() + 3
                                                                  : data.todayIndex() + 1);
    QVERIFY(!data.nowInWindow());

    for (int i = 0; i < 12; ++i)
        QCOMPARE(data.ahead(i), expected.at(i).toMap());
}

void TestForecastData::aheadRunsOutRatherThanWrappingRound()
{
    ForecastData data(nullptr);
    load(data);

    QVERIFY(data.aheadCount() > 0);
    QVERIFY(!data.ahead(data.aheadCount() - 1).isEmpty());

    // Past the end is an empty map, which QML reads as `undefined` per key -
    // a column with nothing in it, rather than the first hour of the series
    // wearing tomorrow's label.
    QVERIFY(data.ahead(data.aheadCount()).isEmpty());
    QVERIFY(data.ahead(-1000).isEmpty());
}

// ---- the setter -------------------------------------------------------------

void TestForecastData::anOutOfRangeSelectionIsClampedRatherThanObeyed()
{
    ForecastData data(nullptr);
    load(data);

    const int last = int(data.days().size()) - 1;

    data.setSelectedDay(9999);
    QCOMPARE(data.selectedDay(), last);
    QVERIFY(data.count() > 0);

    data.setSelectedDay(-4);
    QCOMPARE(data.selectedDay(), 0);
    QVERIFY(data.count() > 0);
}

void TestForecastData::reselectingTheSameDayRebuildsNothing()
{
    ForecastData data(nullptr);
    load(data);

    QSignalSpy spy(&data, &ForecastData::selectedDayChanged);
    data.setSelectedDay(data.selectedDay());
    QCOMPARE(spy.count(), 0);

    // And a rebuild really is a rebuild rather than an append - every builder
    // downstream of the window appends to a list, so selecting a day twice over
    // is where a missing clear shows up as a doubled series.
    const int day = data.todayIndex() + 1 < data.days().size() ? data.todayIndex() + 1 : 0;
    data.setSelectedDay(day);
    const int count = data.count();
    const int marks = int(data.sunEvents().size());
    const int buckets = int(data.precipBuckets().size());

    data.setSelectedDay(data.todayIndex());
    data.setSelectedDay(day);

    QCOMPARE(data.count(), count);
    QCOMPARE(data.temperature().size(), count);
    QCOMPARE(data.sunEvents().size(), marks);
    QCOMPARE(data.precipBuckets().size(), buckets);
}

// ---- the header band's glyphs -----------------------------------------------
//
// The band draws one icon per *label*, and it labels every second column,
// because two dozen 27 px glyphs will not fit across a plot. For as long as
// each label asked `conditionFor` about the single hour it happened to land on,
// half of every day had no icon anywhere - and which half was arbitrary, since
// a day window's labels start at column 1 while today's take their phase from
// where the present fell. The visible result was a ten-day card that said
// thunderstorm above an hourly row that said rain all evening.

void TestForecastData::everyLabelledColumnHasAGlyph()
{
    ForecastData data(nullptr);
    load(data);

    for (int day = 0; day < data.days().size(); ++day) {
        data.setSelectedDay(day);
        const QVariantList labels = data.labelIndices();
        QVERIFY(!labels.isEmpty());

        for (const QVariant &label : labels) {
            QVERIFY2(!data.conditionForLabel(label.toInt()).isEmpty(),
                     qPrintable(QStringLiteral("day %1 column %2 has no glyph")
                                    .arg(day).arg(label.toInt())));
        }
    }
}

void TestForecastData::aColumnNeverDrawsPlainSkyOverAnHourWithWeatherInIt()
{
    // The property behind the fix, stated as a property. A column may show a
    // different glyph from one of its hours - two events in one span, and the
    // louder wins - but it may never show *sky* over a span with weather in it,
    // because that is the case where a reader is told nothing is happening.
    ForecastData data(nullptr);
    load(data);

    for (int day = 0; day < data.days().size(); ++day) {
        data.setSelectedDay(day);
        const QVariantList labels = data.labelIndices();

        for (int n = 0; n < labels.size(); ++n) {
            const int from = labels.at(n).toInt();
            const int to   = n + 1 < labels.size() ? labels.at(n + 1).toInt() : data.count();

            const QString drawn = data.conditionForLabel(from);
            if (!isSky(drawn))
                continue;

            for (int hour = from; hour < to; ++hour) {
                const QString covered = data.conditionFor(hour);
                QVERIFY2(covered.isEmpty() || isSky(covered),
                         qPrintable(QStringLiteral("day %1 column %2 draws %3 over hour %4, "
                                                   "which is %5")
                                        .arg(day).arg(from).arg(drawn).arg(hour).arg(covered)));
            }
        }
    }
}

void TestForecastData::aThunderstormOnAColumnTheBandSkipsIsStillDrawn()
{
    // The reported bug, built rather than borrowed. The Toronto fixture cannot
    // catch this: its thunderstorm runs 3 p.m. to 6 p.m., four consecutive
    // hours, so it lands on a labelled column whatever the phase and the band
    // drew it even when the band was wrong.
    //
    // One hour is what it takes. A day nobody is living through labels columns
    // 2, 4, 6 … so the odd columns are the ones nothing asks about, and column
    // 13 is the hour starting at 1 p.m. - WMO 95 stamped 2 p.m. under
    // Open-Meteo's convention. Before this fix that storm had no glyph anywhere
    // in its own day.
    const Forecast forecast = oneStormyHour(14);

    ForecastData data(nullptr);
    data.setSnapshot(forecast, AirQuality(), forecast.hourly.at(36).time, Place());

    // Yesterday, today, tomorrow, and the spare day the shift needs. Tomorrow
    // is the day the storm is on.
    QCOMPARE(data.days().size(), 4);
    data.setSelectedDay(2);
    QCOMPARE(data.days().at(2).toMap().value(QStringLiteral("label")).toString(),
             QStringLiteral("Tomorrow"));
    QCOMPARE(data.count(), 24);

    QVERIFY2(!data.labelIndices().contains(13),
             "column 13 is labelled after all - this test no longer tests anything");
    QCOMPARE(data.conditionFor(13), QStringLiteral("thunder"));

    bool drawn = false;
    for (const QVariant &label : data.labelIndices()) {
        if (data.conditionForLabel(label.toInt()) == QLatin1String("thunder"))
            drawn = true;
    }
    QVERIFY2(drawn, "the day's only thunderstorm has no glyph in the day's own band");
}

// The general form of the test above, and the one that would have caught both
// times this was wrong at an end rather than in the middle.
//
// The band labels every second column and skips the outermost one at each end,
// so the day's first hours and its last are covered by a label's span or by
// nothing. It was nothing at the start of the day, and then - once the first
// label was made to reach back - still nothing at the end, on any day whose
// label phase is odd. Sweeping the storm across all twenty-four hours is what
// makes "no hour is an hour no column answers for" a property rather than a
// case somebody remembered.
void TestForecastData::noHourOfTheDayIsAnHourNoColumnAnswersFor()
{
    for (int stormHour = 0; stormHour < 24; ++stormHour) {
        const Forecast forecast = oneStormyHour(stormHour);

        ForecastData data(nullptr);
        data.setSnapshot(forecast, AirQuality(), forecast.hourly.at(36).time, Place());
        data.setSelectedDay(2);
        QCOMPARE(data.count(), 24);

        // The hour the storm lands on after the accumulation shift, which is one
        // index earlier than the code was stamped on.
        const int column = stormHour - 1;
        if (column < 0)
            continue;

        bool drawn = false;
        for (const QVariant &label : data.labelIndices()) {
            if (data.conditionForLabel(label.toInt()) == QLatin1String("thunder"))
                drawn = true;
        }

        QVERIFY2(drawn,
                 qPrintable(QStringLiteral("a storm in column %1 has no glyph in the day's band")
                                .arg(column)));
    }
}

// The chart's legend names a moon, and it has to be the moon of the hours on
// the plot. Those are the same day except where the strip carries a card the
// hourly series cannot reach - MET Norway's daily horizon runs days past its
// hourly one - and there the window clamps onto the last day it has hours for
// while the card stays where it was put.
void TestForecastData::theMoonFollowsTheHoursRatherThanTheCard()
{
    // Four daily rows, two days of hours. Cards 2 and 3 select nothing of their
    // own, and each daily row carries a phase far enough from its neighbours to
    // be named differently.
    Forecast forecast;
    forecast.timeZone   = QTimeZone::UTC;
    forecast.providerId = QStringLiteral("test");

    const QDate first(2026, 8, 20);
    const double phases[] = { 0.00, 0.25, 0.50, 0.75 };

    for (int day = 0; day < 4; ++day) {
        DailyPoint daily;
        daily.date           = first.addDays(day);
        daily.temperatureMax = 20.0;
        daily.temperatureMin = 10.0;
        daily.weatherCode    = 3;
        daily.moonPhase      = phases[day];
        forecast.daily.append(daily);

        if (day > 1)
            continue;

        for (int hour = 0; hour < 24; ++hour) {
            HourlyPoint point;
            point.time        = QDateTime(daily.date, QTime(hour, 0), QTimeZone::UTC);
            point.temperature = 15.0;
            point.isDay       = hour >= 6 && hour < 20;
            point.weatherCode = 3;
            forecast.hourly.append(point);
        }
    }

    ForecastData data(nullptr);
    data.setSnapshot(forecast, AirQuality(), forecast.hourly.at(12).time, Place());

    // The last day the hours reach. asHourStarting drops the final point, so
    // the 21st is the last date with a full set.
    data.setSelectedDay(1);
    const QString reachable = data.moonPhase().value(QStringLiteral("name")).toString();
    QVERIFY(!reachable.isEmpty());

    // A card past the horizon draws the reachable day's hours, and must draw its
    // moon with them.
    data.setSelectedDay(3);
    QCOMPARE(data.moonPhase().value(QStringLiteral("name")).toString(), reachable);
}

void TestForecastData::theLastLabelDoesNotReadPastItsOwnDay()
{
    // The final column spans fewer hours than the step, and reading the hour
    // after it would put tomorrow morning on tonight's last glyph.
    ForecastData data(nullptr);
    load(data);

    for (int day = 0; day < data.days().size(); ++day) {
        data.setSelectedDay(day);
        const QVariantList labels = data.labelIndices();
        QVERIFY(!labels.isEmpty());

        const int last = labels.constLast().toInt();
        QVERIFY(last < data.count());

        QSet<QString> mine;
        for (int i = last; i < data.count(); ++i)
            mine.insert(data.conditionFor(i));

        const QString drawn = data.conditionForLabel(last);
        QVERIFY2(drawn.isEmpty() || mine.contains(drawn),
                 qPrintable(QStringLiteral("day %1 last column draws %2; its own hours are %3")
                                .arg(day)
                                .arg(drawn, QStringList(mine.values()).join(QLatin1Char(',')))));
    }
}

// ---- the calendar ------------------------------------------------------------
//
// Toronto is recorded on July 31. A calendar of July alone showed three days
// with numbers in them and hid the thirteen the forecast has in August.
void TestForecastData::theCalendarRunsIntoNextMonthAsFarAsTheForecast()
{
    ForecastData data(nullptr);
    load(data);

    const QVariantList july = data.monthDays();
    QCOMPARE(july.size(), 31);
    QVERIFY(july.last().toMap().value(QStringLiteral("isToday")).toBool());
    QVERIFY(july.last().toMap().value(QStringLiteral("known")).toBool());

    QCOMPARE(data.nextMonth().value(QStringLiteral("number")).toInt(), 8);
    const QVariantList august = data.nextMonthDays();
    QVERIFY(!august.isEmpty());
    QCOMPARE(august.first().toMap().value(QStringLiteral("date")).toInt(), 1);
    for (const QVariant &cell : august)
        QVERIFY2(cell.toMap().value(QStringLiteral("known")).toBool(),
                 "the next month runs past the forecast's last day");
    QCOMPARE(august.last().toMap().value(QStringLiteral("date")).toInt(),
             m_forecast.daily.last().date.day());

    // Further than the day strip reaches: the calendar reads the whole forecast,
    // not the eleven days the strip trims it to.
    QVERIFY(1 + august.size() > data.days().size() - data.todayIndex());
}

// A week into August the same forecast ends inside the month it is in, and
// there is no next month to show.
void TestForecastData::aForecastInsideTheMonthAddsNoNextMonth()
{
    ForecastData data(nullptr);
    data.setSnapshot(m_forecast, m_air, m_fixture.recordedAt.addDays(6), m_fixture.place);

    QCOMPARE(data.month().value(QStringLiteral("number")).toInt(), 8);
    QVERIFY(data.nextMonthDays().isEmpty());
    QCOMPARE(data.nextMonth().value(QStringLiteral("name")).toString(), QString());
}

QTEST_MAIN(TestForecastData)

#include "tst_forecastdata.moc"
