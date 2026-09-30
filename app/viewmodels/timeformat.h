// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every clock reading Climat prints, in one file, spelled one way.
//
// ============================================================================
// WHY THIS EXISTS AT ALL
//
// It exists for the reason app/viewmodels/units.h exists, one quantity over.
// Before the preferences screen there were five independent implementations of
// "what time is it" in this repository:
//
//   conditionsdata.cpp   clockLabel()   "12:28 PM"   QLocale "h:mm AP"
//   conditionsdata.cpp   sentenceTime() "3:00 p.m."  arithmetic
//   conditionsdata.cpp   hhmm()/suffix() "8:42"+"AM" arithmetic, split in two
//   forecastdata.cpp     twelveHour()   "3 PM"       arithmetic
//   alertsdata.cpp       stamp()        "23:00"      QLocale ShortFormat
//   widgets/wx.cpp       four more of the same
//
// Four of those hardcode a 12-hour clock and one follows the locale, which is
// how the same application came to print "3 PM" on the chart and "23:00" in the
// alert banner underneath it, in the same window, in the same second. Neither
// spelling was a decision; they were two people solving the same problem twice.
//
// So the preference is the occasion rather than the cause. A setting that
// reached four of the six call sites would be worse than none, because the two
// it missed would look like the app ignoring it.
//
// ============================================================================
// THE PREFERENCE, AND WHERE ITS DEFAULT COMES FROM
//
// `Settings::clockFormat` is "12h" or "24h", and a reader who has never
// touched it gets their own locale's answer - `settingskeys::defaultClockFormat`
// asks QLocale, so a French desktop opens on 15:30 and an American one on
// 3 PM.
//
// It was a flat "12h" for longer than it should have been, and the reason was
// the capture path rather than the clock: every picture this project takes of
// itself runs under LC_ALL=C.UTF-8, whose short format is 24-hour, so a
// locale-derived default would have made the reference images a picture of the
// C locale. That is now pinned where it belongs - scripts/golden.sh and
// scripts/shots.sh write the preference into their own scratch config, the way
// Main.qml pins the colour scheme under `--grab` - which leaves the default
// free to follow the reader and the pictures free to stay what they were.
//
// ============================================================================
// IT TAKES A QTime, NOT A QDateTime
//
// Deliberately, and it is the one thing to be careful about when calling it.
// Which zone an instant should be read in is a question this class cannot
// answer - conditionsdata has `m_zone`, the forecast's own zone, and reading a
// sunset in the machine's zone instead is a bug that looks like a rendering
// problem. Every caller already knows its zone and has already converted; a
// QDateTime overload here would be an invitation to stop.
#pragma once

#include <QObject>
#include <QString>
#include <QTime>

class Settings;

class TimeFormat : public QObject
{
    Q_OBJECT

public:
    // Not a QML singleton, unlike Units and Settings beside it. Nothing in QML
    // formats a time: every string below is produced by a view model and read
    // as a plain property, and the preferences screen edits
    // `Settings.clockFormat` rather than asking this class anything. Registering
    // it would be adding a QML type on the chance that someone wants one.
    static TimeFormat *instance();

    [[nodiscard]] bool twentyFourHour() const;

    // "3 PM" / "15:00". An hourly axis column, where the minutes are always
    // zero - see forecastdata.cpp, which argues ":00" out of the 12-hour
    // spelling and back into the 24-hour one, because "15" alone beside a
    // temperature is a number with no unit.
    [[nodiscard]] QString hour(QTime time) const;

    // "12:28 PM" / "12:28". The whole reading, for anywhere with room for it.
    [[nodiscard]] QString clock(QTime time) const;

    // The same reading with the suffix held back, and the suffix on its own.
    // Two fields rather than one because the sun and moon cards draw the suffix
    // smaller and beside - see DetailSunCard.qml - and a caller that split
    // `clock()` on a space would be parsing its own output.
    //
    // Under a 24-hour clock `meridiem()` is empty and `clockBare()` is the whole
    // reading, so a layout built around the pair degrades to one field rather
    // than to a stray "PM".
    [[nodiscard]] QString clockBare(QTime time) const;
    [[nodiscard]] QString meridiem(QTime time) const;

    // "3:00 p.m." / "15:00". The reference's spelling inside a sentence, as
    // distinct from the "3:00 PM" a label uses - detaildata.js used both, in the
    // same two places, and the distinction survived the port. On the hour at
    // midnight and noon a 12-hour clock says the word.
    [[nodiscard]] QString sentence(QTime time) const;

Q_SIGNALS:
    // One signal, like Units'. A view model rebuilds its whole snapshot when the
    // format changes, because the hour labels, the sun card and the alert
    // banner all move together.
    void changed();

private:
    TimeFormat();

    [[nodiscard]] Settings *settings() const;
};
