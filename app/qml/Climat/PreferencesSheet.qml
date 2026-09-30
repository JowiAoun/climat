// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Preferences, on the desktop.
//
// A sheet over the page for the reason PlacePicker and AlertSheet are: changing
// a setting is a detour, not a destination. You come back to the weather you
// were looking at, and on the desktop there is nothing to push a screen onto -
// WeatherPage is one scrolling column and there is no navigation stack anywhere
// in this shell.
//
// ---- the phone does not use this ----------------------------------------------
//
// It shows the same two groups inline on the Me tab, which is the destination
// that already exists for settings. A sheet there would be a modal over a screen
// whose entire content is the thing the modal contains.
//
// That asymmetry is the point rather than an inconsistency: the groups -
// PrefGeneral and PrefUnits - are one definition used twice, and what differs is
// only how each shell presents them. The desktop had no settings surface at all
// before this file; every preference in the app was reachable only by making the
// window narrow enough to trigger the phone layout.
//
// ---- why the desktop is where this was missing --------------------------------
//
// docs/04-architecture.md's shell split says the two shells are one product and
// which one runs is a function of window width. That was true of everything
// except the settings, which lived on a tab only one of them has. A desktop user
// could not change their units.
import QtQuick

Item {
    id: root

    property bool open: false

    signal dismissed()

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    enabled: open

    Behavior on opacity {
        NumberAnimation { duration: Theme.motion.view; easing.type: Easing.OutCubic }
    }

    // The sheet takes focus while it is up, so Escape reaches it rather than the
    // shell underneath. Handled on the press, which is the stage MobileShell's
    // back handler also uses - one key must not close this on the way down and
    // change a tab on the way up.
    onOpenChanged: if (open) forceActiveFocus()
    Keys.onEscapePressed: function (event) {
        root.dismissed()
        event.accepted = true
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.overlay.scrim
        TapHandler { onTapped: root.dismissed() }
    }

    Rectangle {
        id: panel
        width: Math.min(parent.width - 32, 560)
        height: Math.min(parent.height - 64, 32 + content.implicitHeight + 20)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Math.min(56, parent.height * 0.06)
        radius: Theme.metric.cardRadius

        // Opaque, the same §10.1 exception PlacePicker and AlertSheet take: a
        // sheet is the surface that has to hide what it covers, and a switch
        // read through a temperature chart is not read.
        color: Theme.page.bg
        border.width: 1
        border.color: Theme.line.card

        // Swallows taps that reach the panel rather than a control on it, so a
        // click on the padding between two groups does not dismiss the sheet
        // through it.
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
                spacing: 14

                Item {
                    width: parent.width
                    height: heading.height

                    Text {
                        id: heading
                        anchors.left: parent.left
                        text: qsTr("Preferences")
                        color: Theme.ink.primary
                        font.pixelSize: Theme.type.sectionTitle
                        font.weight: Font.DemiBold
                    }

                    // The word rather than a cross, which is what AlertSheet
                    // does - one dismiss affordance spelled one way, and a
                    // cross would be the only glyph in the app whose meaning is
                    // learned rather than read.
                    Text {
                        id: closeLabel
                        anchors.right: parent.right
                        anchors.verticalCenter: heading.verticalCenter
                        text: qsTr("Close")
                        color: closeTarget.hovered ? Theme.ink.primary : Theme.ink.dim
                        font.pixelSize: Theme.type.label

                        Behavior on color {
                            ColorAnimation {
                                duration: Theme.motion.tint
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    TouchTarget {
                        id: closeTarget
                        area: closeLabel
                        onTapped: root.dismissed()
                    }
                }

                PrefGeneral { width: parent.width }
                PrefUnits { width: parent.width }

                // The list the phone shows on its Me tab, inset the way a row
                // is. Not a row itself: a credit is three or four lines and
                // nothing to press.
                PrefGroup {
                    width: parent.width
                    title: qsTr("Data sources")

                    Item {
                        width: parent.width
                        implicitHeight: sources.implicitHeight + 24
                        height: implicitHeight

                        SourcesList {
                            id: sources
                            x: 16
                            y: 12
                            width: parent.width - 32
                        }
                    }
                }

                PrefAbout { width: parent.width }

                // Where the file is. The first question of every support
                // conversation, and the answer is one a person can act on: open
                // it, read line 4, delete it to start again.
                //
                // Not under a capture, for two unrelated reasons that happen to
                // want the same thing. It is an absolute path under the home
                // directory, so it is the one string on this sheet that differs
                // between two machines - a golden image of it could never be
                // stable, because scripts/golden.sh redirects XDG_CONFIG_HOME
                // into a fresh scratch directory on every run. And `--grab` is
                // what the issue template asks a reporter to attach, which makes
                // this the one line in the app that would put somebody's
                // username into a public bug report.
                Text {
                    width: parent.width
                    visible: !AppOptions.capturing
                    text: qsTr("Saved in %1").arg(Settings.filePath())
                    color: Theme.ink.dim
                    font.pixelSize: Theme.type.label
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
