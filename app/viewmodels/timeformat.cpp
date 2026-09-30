// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later

#include "timeformat.h"

#include "settings.h"

namespace {

// The 12-hour hour. Midnight and noon are 12, not 0 - which is the whole of the
// arithmetic, and the whole of why this is written out rather than handed to
// QLocale.
//
// QLocale's "h" is a 24-hour hour unless the format string also carries AP, so
// asking for "h:mm" and appending "PM" produces "20:42 PM" on every sunset after
// noon. Both conditionsdata.cpp and widgets/wx.cpp hit that and both wrote these
// same two lines; this is the third and last copy.
int twelve(QTime time)
{
    const int hour = time.hour() % 12;
    return hour == 0 ? 12 : hour;
}

QString minutes(QTime time)
{
    return QStringLiteral("%1").arg(time.minute(), 2, 10, QLatin1Char('0'));
}

} // namespace

TimeFormat::TimeFormat()
{
    connect(Settings::instance(), &Settings::clockFormatChanged, this, &TimeFormat::changed);
}

TimeFormat *TimeFormat::instance()
{
    static TimeFormat format;
    return &format;
}

Settings *TimeFormat::settings() const
{
    return Settings::instance();
}

bool TimeFormat::twentyFourHour() const
{
    return settings()->clockFormat() == QLatin1String("24h");
}

QString TimeFormat::hour(QTime time) const
{
    if (!time.isValid())
        return {};

    // "15:00" and not "15". The 12-hour spelling drops the minutes because
    // ":00" is four pixels restating that an hourly point falls on the hour, and
    // "3 PM" still reads as a time without them. "15" does not: on an axis under
    // a temperature series it is a bare number beside other bare numbers, and
    // every 24-hour weather service prints the colon for that reason.
    // Padded - "00:00" and "04:00", not "0:00" and "4:00". A 24-hour clock pads
    // and a 12-hour one does not, which is not a style choice: "0:00" is the one
    // spelling of midnight nobody writes, and an axis running 22:00, 0:00, 2:00
    // has a column that is two characters narrower than its neighbours.
    if (twentyFourHour())
        return QStringLiteral("%1:00").arg(time.hour(), 2, 10, QLatin1Char('0'));

    return QStringLiteral("%1 %2").arg(twelve(time))
                                  .arg(time.hour() < 12 ? tr("AM") : tr("PM"));
}

QString TimeFormat::clock(QTime time) const
{
    if (!time.isValid())
        return {};

    const QString suffix = meridiem(time);
    return suffix.isEmpty() ? clockBare(time)
                            : clockBare(time) + QLatin1Char(' ') + suffix;
}

QString TimeFormat::clockBare(QTime time) const
{
    if (!time.isValid())
        return {};

    if (twentyFourHour())
        return QStringLiteral("%1:%2").arg(time.hour(), 2, 10, QLatin1Char('0'))
                                      .arg(minutes(time));

    // "8:42", not "08:42". A 12-hour clock does not pad - the reference does
    // not, and neither does any platform's own clock.
    return QStringLiteral("%1:%2").arg(twelve(time)).arg(minutes(time));
}

QString TimeFormat::meridiem(QTime time) const
{
    if (!time.isValid() || twentyFourHour())
        return {};
    return time.hour() < 12 ? tr("AM") : tr("PM");
}

QString TimeFormat::sentence(QTime time) const
{
    if (!time.isValid())
        return {};

    if (twentyFourHour())
        return clockBare(time);

    // The two times "a.m." and "p.m." get wrong: "12:00 a.m." is the one a
    // reader has to stop and work out. A sentence says the word.
    if (time.minute() == 0) {
        if (time.hour() == 0)
            return tr("midnight");
        if (time.hour() == 12)
            return tr("noon");
    }

    return QStringLiteral("%1:%2 %3").arg(twelve(time))
                                     .arg(minutes(time))
                                     .arg(time.hour() < 12 ? tr("a.m.") : tr("p.m."));
}
