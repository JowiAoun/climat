// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The place picker: what the location bar's chevron opens.
//
// Three things, in the order somebody reaches for them:
//
//   search        type a name, get places
//   saved         the places already kept, with the home marker on one of them
//   use my location
//
// Search is first and not last because a picker is opened to go somewhere new;
// the saved list is what you already have and is one glance away underneath.
// "Use my location" is last because it is the one row that can fail for reasons
// that are nothing to do with the weather - no service, no permission, no fix -
// and a row that sometimes answers with an error belongs where an error can be
// read rather than at the top where it displaces the list.
//
// ---- it is a sheet over the page, not a screen ------------------------------
//
// Choosing a place is a detour and not a destination: you come back to the page
// you were on, looking at somewhere else. A pushed screen would make it a
// journey with a back button, and on the desktop there is nothing to push it
// onto. So it is a scrim and a panel, dismissed by the scrim, by Escape, and by
// choosing something.
//
// ---- motion -----------------------------------------------------------------
//
// The panel fades and the scrim fades with it, and nothing slides. §10.6 wants
// a component legible at rest position zero, and a list of place names sliding
// up under a search field the reader is about to type into is a list they
// cannot read and a field they cannot aim at. `Theme.motion.view` because this
// is one view arriving over another, which is the token's whole job.
import QtQuick
import QtQuick.Shapes

Item {
    id: root

    // Not `visible`: the panel animates its own opacity and a hard toggle would
    // cut the fade off at both ends.
    property bool open: false

    signal dismissed()

    anchors.fill: parent
    visible: opacity > 0
    opacity: open ? 1 : 0
    enabled: open

    Behavior on opacity {
        NumberAnimation { duration: Theme.motion.view; easing.type: Easing.OutCubic }
    }

    onOpenChanged: {
        if (open) {
            field.forceActiveFocus()
        } else {
            // Cleared on the way out rather than on the way in, so that
            // reopening does not flash the previous query for a frame before
            // emptying it.
            field.text = ""
            Engine.search.query = ""
            problem.text = ""
        }
    }

    Keys.onEscapePressed: root.dismissed()

    Connections {
        target: Engine
        function onLocationFailed(reason) { problem.text = reason }
    }

    // ---- the scrim ---------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        color: Theme.overlay.scrim
        TapHandler { onTapped: root.dismissed() }
    }

    // ---- the panel ---------------------------------------------------------
    Rectangle {
        id: panel
        width: Math.min(parent.width - 32, 420)
        height: Math.min(parent.height - 64 - WindowInsets.top - WindowInsets.bottom,
                         30 + column.implicitHeight + 20)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        // Below the status bar on a phone that draws this window under it.
        anchors.topMargin: WindowInsets.top + Math.min(72, parent.height * 0.08)
        radius: Theme.metric.cardRadius

        // OPAQUE, which is a deliberate exception to §10.1's rule that a
        // surface is a wash over the page gradient. Every other surface in
        // Climat sits on the background; this one sits on top of the hero card
        // and the chart, and a 7% white wash over those reads as a smear rather
        // than as a panel - the place names come out interleaved with the
        // temperature behind them. A sheet is the one thing that has to hide
        // what it covers.
        color: Theme.page.bg
        border.width: 1
        border.color: Theme.line.card

        // The panel swallows taps so that hitting it does not dismiss through
        // the scrim underneath.
        TapHandler { onTapped: {} }

        Column {
            id: column
            x: 16
            y: 14
            width: parent.width - 32
            spacing: 12

            // ---- search ----------------------------------------------------
            Rectangle {
                width: parent.width
                height: 38
                radius: Theme.metric.controlRadius
                color: Theme.surface.raised
                border.width: 1
                border.color: field.activeFocus ? Theme.line.control : Theme.line.grid

                Behavior on border.color {
                    ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
                }

                TextInput {
                    id: field
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    verticalAlignment: Text.AlignVCenter
                    color: Theme.ink.primary
                    font.pixelSize: Theme.type.status
                    selectByMouse: true
                    clip: true

                    // The debounce is PlaceSearchModel's, not this field's -
                    // 250 ms, and it belongs there because the model is what
                    // knows a request is about to be made. Assigning on every
                    // keystroke is correct: the model decides when to send.
                    onTextChanged: Engine.search.query = text
                    onAccepted: Engine.search.searchNow()

                    Text {
                        visible: field.text === ""
                        text: qsTr("Search for a town or city")
                        color: Theme.ink.dim
                        font.pixelSize: Theme.type.status
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            // ---- results ---------------------------------------------------
            //
            // The list is shown only while there is a query. An empty result
            // list under an empty box would be a permanent blank panel where the
            // saved places should be.
            Column {
                width: parent.width
                visible: Engine.search.count > 0

                Repeater {
                    model: Engine.search

                    delegate: Item {
                        id: result
                        required property int index
                        required property string label
                        required property string region

                        width: column.width
                        height: 46

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -6
                            radius: Theme.metric.controlRadius
                            color: resultHover.hovered ? Theme.surface.raised : "transparent"
                            Behavior on color {
                                ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                text: result.label
                                color: Theme.ink.primary
                                font.pixelSize: Theme.type.status
                            }
                            Text {
                                text: result.region
                                color: Theme.ink.muted
                                font.pixelSize: Theme.type.label
                            }
                        }

                        HoverHandler { id: resultHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                Engine.chooseSearchResult(result.index)
                                root.dismissed()
                            }
                        }
                    }
                }
            }

            // A search that found nothing says so. Silence reads as a request
            // still in flight, and the reader retypes the word they spelled
            // correctly the first time.
            Text {
                width: parent.width
                visible: field.text.length >= 2 && Engine.search.count === 0
                         && !Engine.search.searching
                text: qsTr("No places match “%1”.").arg(field.text)
                color: Theme.ink.muted
                font.pixelSize: Theme.type.body
                wrapMode: Text.WordWrap
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.line.gridWeak
                visible: Engine.places.count > 0
            }

            // ---- saved -----------------------------------------------------
            Column {
                width: parent.width
                visible: Engine.places.count > 0

                Repeater {
                    model: Engine.places

                    delegate: Item {
                        id: saved
                        required property int index
                        required property string label
                        required property string region
                        required property bool isHome

                        width: column.width
                        height: 46

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -6
                            radius: Theme.metric.controlRadius
                            color: savedHover.hovered ? Theme.surface.raised : "transparent"
                            Behavior on color {
                                ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                text: saved.label
                                color: saved.index === Engine.places.currentIndex
                                           ? Theme.accent.fill : Theme.ink.primary
                                font.pixelSize: Theme.type.status
                            }
                            Text {
                                text: saved.region
                                color: Theme.ink.muted
                                font.pixelSize: Theme.type.label
                            }
                        }

                        // The home marker, on every row rather than only on the
                        // one that is home: a marker that appears only where it
                        // is already set is a marker nobody can use to move it.
                        Item {
                            width: 24
                            height: 24
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter

                            Shape {
                                anchors.centerIn: parent
                                width: 14
                                height: 14
                                preferredRendererType: Shape.CurveRenderer
                                ShapePath {
                                    fillColor: saved.isHome ? Theme.ink.primary
                                                            : Theme.line.grid
                                    Behavior on fillColor {
                                        ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
                                    }
                                    strokeColor: "transparent"
                                    PathSvg {
                                        path: "M 7 2.4 L 12.8 7.6 L 11.3 7.6 L 11.3 12.4 "
                                            + "L 8.4 12.4 L 8.4 9.3 L 5.6 9.3 L 5.6 12.4 "
                                            + "L 2.7 12.4 L 2.7 7.6 L 1.2 7.6 Z"
                                    }
                                }
                            }

                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: Engine.toggleHome(saved.index) }
                        }

                        HoverHandler { id: savedHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: {
                                Engine.selectPlace(saved.index)
                                root.dismissed()
                            }
                        }
                    }
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.line.gridWeak
            }

            // ---- use my location -------------------------------------------
            //
            // Shown even where it cannot work, disabled, with the reason under
            // it. Hiding it would leave a reader wondering whether Climat has the
            // feature at all; saying "no location service here" answers that in
            // four words and never comes back.
            Item {
                width: parent.width
                height: 34

                Text {
                    text: qsTr("Use my location")
                    color: Engine.locationAvailable() ? Theme.ink.primary
                                                      : Theme.ink.dim
                    font.pixelSize: Theme.type.status
                    anchors.verticalCenter: parent.verticalCenter
                }

                HoverHandler {
                    enabled: Engine.locationAvailable()
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    enabled: Engine.locationAvailable()
                    onTapped: {
                        problem.text = ""
                        Engine.useMyLocation()
                    }
                }
            }

            Text {
                id: problem
                width: parent.width
                visible: text !== ""
                text: Engine.locationAvailable()
                          ? "" : qsTr("This system has no location service Climat can use.")
                color: Theme.ink.muted
                font.pixelSize: Theme.type.label
                wrapMode: Text.WordWrap
            }
        }
    }
}
