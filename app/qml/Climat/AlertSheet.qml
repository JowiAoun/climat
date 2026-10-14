// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Every alert in force here, in full.
//
// A sheet over the page for the same reason PlacePicker is one: reading a
// warning is a detour, not a destination. You come back to the weather you were
// looking at, and on the desktop there is nothing to push a screen onto.
//
// ---- what it says when it cannot say everything -----------------------------
//
// Two admissions live at the bottom of this sheet, and they are separate
// sentences because they are separate facts:
//
//   "Some alert sources could not be reached"  - a whole service did not
//       answer. There may be warnings here we have never seen. This is
//       AlertSet::complete, and it is the reason alerts fan out across every
//       covering provider instead of falling through a chain.
//
//   "Last confirmed HH:MM"  - we are showing an alert whose issuer was due to
//       refresh it and whom we could not reach. The alert is real; our copy of
//       it is old.
//
// Neither is decoration. docs/06-roadmap.md §6.6's requirement is that no
// *ended* alert is displayed; this is the other half of the same promise -
// never silently keep an alert, and never silently drop one.
//
// `Bound` because this file has Repeater delegates that read ids from the file
// around them, which qmllint reports as unqualified access and qmlcachegen
// cannot ahead-of-time compile. Bound scoping is what makes those lookups
// resolvable; the delegates already declare their model roles as `required`,
// which is the other half of what it asks for.
pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    property bool open: false

    // The gallery hands in a list; the app reads the model. Same arrangement as
    // AlertBanner, and for the same reason: a sheet that can only be seen by
    // having severe weather is a sheet nobody reviews.
    property var alerts: Alerts.list
    property bool complete: Alerts.complete
    property bool unconfirmed: Alerts.unconfirmed
    property string confirmedLabel: Alerts.confirmedLabel
    property string sourceName: Alerts.sourceName

    signal dismissed()

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    enabled: open

    Behavior on opacity {
        NumberAnimation { duration: Theme.motion.view; easing.type: Easing.OutCubic }
    }

    Keys.onEscapePressed: root.dismissed()

    Rectangle {
        anchors.fill: parent
        color: Theme.overlay.scrim
        TapHandler { onTapped: root.dismissed() }
    }

    Rectangle {
        id: panel
        width: Math.min(parent.width - 32, 520)
        height: Math.min(parent.height - 64 - WindowInsets.top - WindowInsets.bottom,
                         32 + content.implicitHeight + 20)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        // Below the status bar on a phone that draws this window under it.
        anchors.topMargin: WindowInsets.top + Math.min(56, parent.height * 0.06)
        radius: Theme.metric.cardRadius

        // Opaque, the same §10.1 exception PlacePicker takes and for the same
        // reason: a sheet is the one surface that has to hide what it covers,
        // and an alert body read through a temperature is not read.
        color: Theme.page.bg
        border.width: 1
        border.color: Theme.line.card

        TapHandler { onTapped: {} }

        Flickable {
            id: scroll
            anchors.fill: parent
            anchors.margins: 1
            clip: true
            contentWidth: width
            contentHeight: content.implicitHeight + 52
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: content
                x: 16
                y: 16
                width: scroll.width - 32
                spacing: 10

                Item {
                    width: parent.width
                    height: heading.height

                    Text {
                        id: heading
                        anchors.left: parent.left
                        color: Theme.ink.primary
                        font.pixelSize: Theme.type.sectionTitle
                        font.weight: Font.DemiBold
                        text: root.alerts.length === 1
                              ? qsTr("1 alert")
                              //: %1 is a count of active severe-weather alerts
                              : qsTr("%1 alerts").arg(root.alerts.length)
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.verticalCenter: heading.verticalCenter
                        color: Theme.ink.dim
                        font.pixelSize: Theme.type.label
                        text: qsTr("Close")

                        TapHandler { onTapped: root.dismissed() }
                    }
                }

                Repeater {
                    model: root.alerts

                    AlertRow {
                        required property var modelData
                        required property int index

                        width: content.width
                        alert: modelData

                        // The worst one open, the rest closed. A sheet that
                        // opened everything would be a wall of NWS boilerplate
                        // - their descriptions run to a dozen wrapped lines -
                        // and one that opened nothing would make the reader tap
                        // to find out what the banner was about.
                        expanded: index === 0
                    }
                }

                // ---- the two admissions ------------------------------------
                Text {
                    width: parent.width
                    visible: !root.complete
                    wrapMode: Text.WordWrap
                    color: Theme.state.caution
                    font.pixelSize: Theme.type.label
                    text: qsTr("Some alert sources could not be reached. "
                               + "There may be warnings here that are not shown.")
                }

                Text {
                    width: parent.width
                    visible: root.unconfirmed
                    wrapMode: Text.WordWrap
                    color: Theme.ink.dim
                    font.pixelSize: Theme.type.label
                    text: root.confirmedLabel
                }

                // Who said so. R12's rule is that a credit cannot go stale
                // because it is generated; this is the per-screen half of it,
                // naming the services that answered THIS request rather than
                // every provider the app has.
                Text {
                    width: parent.width
                    visible: root.sourceName !== ""
                    wrapMode: Text.WordWrap
                    color: Theme.ink.dim
                    font.pixelSize: Theme.type.label
                    //: %1 is a list of weather service names
                    text: qsTr("Issued by %1").arg(root.sourceName)
                }
            }
        }
    }
}
