// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TimeFormat: the five spellings of a clock reading, under both formats.
//
// ---- what is worth asserting here, and what is not --------------------------
//
// Not that a QSettings round-trips a string. That is Qt's test suite, and a copy
// of it here would pass for ever without saying anything about this program.
//
// What is worth asserting is that the five spellings move TOGETHER. The whole
// reason this class exists is that six places in this application formatted a
// time by hand, five of them hardcoded a 12-hour clock and one followed the
// locale - so under LC_ALL=C the chart said "3 PM" and the alert banner
// underneath it said "23:00", in the same window, in the same second. A
// preference that reached four of the six would be worse than none, because the
// two it missed would look like the app ignoring it.
//
// So: every spelling, under both formats, including the hours the 12-hour
// arithmetic gets wrong. Midnight is 12 AM and not 0 AM; noon is 12 PM and not
// 0 PM; and an hourly axis is guaranteed to reach both.
//
// ---- why it links climatqml ---------------------------------------------------
//
// Same reason tst_conditionsdata does: its subject is app/, not libclimat.
// `climat_forbid_gui()` is deliberately not applied, which the function in
// tests/CMakeLists.txt cannot express - so this is registered by hand there.
#include "settings.h"
#include "timeformat.h"

#include <QStandardPaths>
#include <QtTest>

class TestTimeFormat : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void everySpellingFollowsTheFormat_data();
    void everySpellingFollowsTheFormat();

    void anUnknownFormatReadsAsTwelveHour();
    void theFormatChangeIsAnnouncedOnce();
};

void TestTimeFormat::initTestCase()
{
    // Before anything constructs a Settings, which TimeFormat does on first use.
    // Without this the test writes to the developer's own preferences - and
    // worse, reads them: a developer who had set 24-hour would see this suite
    // pass for the wrong reason.
    QStandardPaths::setTestModeEnabled(true);
}

// Every test starts from the shipped default rather than from whatever the one
// before it left behind. The settings file is a process-wide singleton, so this
// is the only thing standing between an ordering change and an afternoon.
void TestTimeFormat::init()
{
    Settings::instance()->setClockFormat(QStringLiteral("12h"));
}

void TestTimeFormat::everySpellingFollowsTheFormat_data()
{
    QTest::addColumn<QString>("format");
    QTest::addColumn<QTime>("time");
    QTest::addColumn<QString>("hour");
    QTest::addColumn<QString>("clock");
    QTest::addColumn<QString>("bare");
    QTest::addColumn<QString>("meridiem");
    QTest::addColumn<QString>("sentence");

    // Afternoon, which is where a 12-hour clock has arithmetic to do.
    QTest::newRow("12h afternoon") << "12h" << QTime(15, 30)
        << "3 PM" << "3:30 PM" << "3:30" << "PM" << "3:30 p.m.";
    QTest::newRow("24h afternoon") << "24h" << QTime(15, 30)
        << "15:00" << "15:30" << "15:30" << "" << "15:30";

    // Midnight: hour 0, which is 12 AM and not 0 AM. The modulo gets this wrong
    // the obvious way round, and midnight is the one hour a forecast axis is
    // guaranteed to reach.
    QTest::newRow("12h midnight") << "12h" << QTime(0, 5)
        << "12 AM" << "12:05 AM" << "12:05" << "AM" << "12:05 a.m.";
    QTest::newRow("24h midnight") << "24h" << QTime(0, 5)
        << "00:00" << "00:05" << "00:05" << "" << "00:05";

    // On the hour, a sentence says the word: "12:00 a.m." is the one spelling
    // of a time a reader has to stop and work out. A label keeps the digits.
    QTest::newRow("12h midnight exactly") << "12h" << QTime(0, 0)
        << "12 AM" << "12:00 AM" << "12:00" << "AM" << "midnight";
    QTest::newRow("24h midnight exactly") << "24h" << QTime(0, 0)
        << "00:00" << "00:00" << "00:00" << "" << "00:00";

    // Noon: hour 12, which is 12 PM. The other end of the same mistake.
    QTest::newRow("12h noon") << "12h" << QTime(12, 0)
        << "12 PM" << "12:00 PM" << "12:00" << "PM" << "noon";
    QTest::newRow("24h noon") << "24h" << QTime(12, 0)
        << "12:00" << "12:00" << "12:00" << "" << "12:00";

    // Single-digit minutes, which have to pad in both formats - "8:5" is not a
    // time - while the 24-hour HOUR pads and the 12-hour one does not.
    QTest::newRow("12h early") << "12h" << QTime(8, 5)
        << "8 AM" << "8:05 AM" << "8:05" << "AM" << "8:05 a.m.";
    QTest::newRow("24h early") << "24h" << QTime(8, 5)
        << "08:00" << "08:05" << "08:05" << "" << "08:05";
}

void TestTimeFormat::everySpellingFollowsTheFormat()
{
    QFETCH(QString, format);
    QFETCH(QTime, time);

    Settings::instance()->setClockFormat(format);

    const TimeFormat *clock = TimeFormat::instance();
    QCOMPARE(clock->twentyFourHour(), format == QLatin1String("24h"));

    QTEST(clock->hour(time), "hour");
    QTEST(clock->clock(time), "clock");
    QTEST(clock->clockBare(time), "bare");
    QTEST(clock->meridiem(time), "meridiem");
    QTEST(clock->sentence(time), "sentence");

    // An invalid time is empty in every spelling, not "12:00 AM". Every caller
    // passes an instant that may be absent - a place where the sun does not set
    // has no sunset - and a formatter that invented midnight for it would put a
    // confident wrong time on the sun card.
    QVERIFY(clock->hour(QTime()).isEmpty());
    QVERIFY(clock->clock(QTime()).isEmpty());
    QVERIFY(clock->clockBare(QTime()).isEmpty());
    QVERIFY(clock->meridiem(QTime()).isEmpty());
    QVERIFY(clock->sentence(QTime()).isEmpty());
}

// A hand-edited INI, or one written by a version that grew a third spelling. A
// clock may not fail closed: an unrecognised value still has to put a time on
// the screen, and the 12-hour spelling is the one this app has always had.
void TestTimeFormat::anUnknownFormatReadsAsTwelveHour()
{
    Settings::instance()->setClockFormat(QStringLiteral("swatch-beats"));

    QCOMPARE(Settings::instance()->clockFormat(), QStringLiteral("12h"));
    QVERIFY(!TimeFormat::instance()->twentyFourHour());
    QCOMPARE(TimeFormat::instance()->hour(QTime(15, 0)), QStringLiteral("3 PM"));
}

// The signal three view models rebuild their whole snapshot on. Once per real
// change and not at all for a write of the value that is already stored -
// without which every unit row's redraw would also rebuild every hour label.
void TestTimeFormat::theFormatChangeIsAnnouncedOnce()
{
    QSignalSpy spy(TimeFormat::instance(), &TimeFormat::changed);

    Settings::instance()->setClockFormat(QStringLiteral("24h"));
    QCOMPARE(spy.count(), 1);

    Settings::instance()->setClockFormat(QStringLiteral("24h"));
    QCOMPARE(spy.count(), 1);

    Settings::instance()->setClockFormat(QStringLiteral("12h"));
    QCOMPARE(spy.count(), 2);
}

QTEST_MAIN(TestTimeFormat)
#include "tst_timeformat.moc"
