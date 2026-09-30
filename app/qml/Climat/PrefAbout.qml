// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// What this is, whose it is, and where its source lives.
//
// The Qt row is also the notice Qt's LGPL asks for: that the program uses Qt
// and that Qt is covered by that licence. A Windows build, an AppImage and the
// Android package all carry Qt inside them, and on a phone this screen is the
// only place a reader will ever see that said. Both licence rows open the text
// they name, as it stands in the repository.
import QtQuick

PrefGroup {
    id: root

    title: qsTr("About")

    PrefRow {
        title: qsTr("Version")
        interactive: false
        control: Text {
            text: Qt.application.version
            color: Theme.ink.muted
            font.pixelSize: Theme.type.status
        }
    }

    PrefRow {
        title: qsTr("Licence")
        subtitle: qsTr("Free software under the GNU GPL, version 3 or later.")
        onActivated: Qt.openUrlExternally(Engine.homepage + "/blob/main/LICENSE")
    }

    PrefRow {
        title: qsTr("Built with Qt")
        subtitle: qsTr("Used under the GNU LGPL, version 3.")
        onActivated: Qt.openUrlExternally(Engine.homepage + "/blob/main/COPYING.LESSER")
    }

    PrefRow {
        title: qsTr("Source code")
        subtitle: Engine.homepage.replace(/^https:\/\//, "")
        onActivated: Qt.openUrlExternally(Engine.homepage)
    }

    PrefRow {
        title: qsTr("Report a problem")
        subtitle: qsTr("Opens the issue tracker in your browser.")
        onActivated: Qt.openUrlExternally(Engine.homepage + "/issues")
    }
}
