// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Monthly: the whole month at a glance, and the forecast past its end.
//
// The reference puts a month picker beside the title. This does not, and the
// reason is the one LocationBar states about its chevron: there is one month
// of data behind this screen, so a picker here would be a control that opens a
// list with one thing in it, or worse, changes the title and not the numbers.
// When the provider arrives it is a dropdown next to the heading and a
// `monthDays` that takes an argument.
//
// Near the end of a month the forecast runs on into the next one, and a
// calendar that stopped at the 31st showed one known day and hid fifteen. So
// the next month follows, as far as the forecast reaches and no further.
import QtQuick

MobilePage {
    id: root

    Text {
        width: root.spanWidth(2)
        text: Data.month.name + " " + Data.month.year
        color: Theme.ink.primary
        font.pixelSize: Theme.type.sectionTitle
        font.bold: true
    }

    MobileCard {
        width: root.spanWidth(2)
        content: MobileCalendar { }
    }

    Text {
        visible: Data.nextMonthDays.length > 0
        width: root.spanWidth(2)
        text: Data.nextMonth.name + " " + Data.nextMonth.year
        color: Theme.ink.primary
        font.pixelSize: Theme.type.sectionTitle
        font.bold: true
    }

    MobileCard {
        visible: Data.nextMonthDays.length > 0
        width: root.spanWidth(2)
        content: MobileCalendar { days: Data.nextMonthDays }
    }
}
