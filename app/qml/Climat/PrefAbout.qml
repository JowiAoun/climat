// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// What this is, whose it is, and where its source lives.
//
// The Qt row is also the notice Qt's LGPL asks for: that the program uses Qt
// and that Qt is covered by that licence. A Windows build, an AppImage and the
// Android package all carry Qt inside them, and on a phone this screen is the
// only place a reader will ever see that said.
//
// Both licence rows unfold the text they name, under the row, from the copy
// built into the app. Offline, and on a phone, where the package is the only
// place the text can travel with the program.
//
// `Bound` because each licence row's `control` is a component that reads
// `root.reading`, which qmllint reports as unqualified access without it.
pragma ComponentBehavior: Bound

import QtQuick

PrefGroup {
    id: root

    title: qsTr("About")

    // Which licence is unfolded: "gpl", "lgpl" or "" for neither.
    property string reading: ""

    function toggle(which) {
        root.reading = root.reading === which ? "" : which
    }

    // A licence under its row. Laid out only while it is open, because the GPL
    // is 35 KB of text and nobody reads it on most visits.
    component Unfolded: Item {
        id: unfolded

        property bool open: false
        property string body: ""

        width: parent ? parent.width : 0
        height: open ? words.implicitHeight + 24 : 0
        visible: open

        Text {
            id: words
            x: 16
            y: 12
            width: unfolded.width - 32
            text: unfolded.open ? unfolded.body : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: Theme.ink.muted
            font.pixelSize: Theme.type.label
            lineHeight: 1.25
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.line.gridWeak
        }
    }

    // "Read" or "Hide", so the row says what a tap does. The other rows leave
    // the app; these two stay in it.
    component ReadWord: Text {
        property bool open: false

        text: open ? qsTr("Hide") : qsTr("Read")
        color: Theme.ink.dim
        font.pixelSize: Theme.type.label
    }

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
        control: ReadWord { open: root.reading === "gpl" }
        onActivated: root.toggle("gpl")
    }

    Unfolded {
        open: root.reading === "gpl"
        body: Engine.licenceText
    }

    PrefRow {
        title: qsTr("Built with Qt")
        subtitle: qsTr("Used under the GNU LGPL, version 3.")
        control: ReadWord { open: root.reading === "lgpl" }
        onActivated: root.toggle("lgpl")
    }

    Unfolded {
        open: root.reading === "lgpl"
        body: Engine.qtLicenceText
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
