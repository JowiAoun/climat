// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The highest-ranked warning in force, or an honest silence.
//
// ============================================================================
// THREE STATES, AND TWO OF THEM LOOK ALIKE IF YOU ARE CARELESS
//
//   "No warnings in force"   we asked and there are none.
//   "Warnings unavailable"   we have not managed to ask.
//
// Those are not the same sentence and a tile that prints the first when it
// means the second is telling somebody there is no tornado warning on the
// strength of a failed HTTP request. The wire keeps them apart with
// `alertsKnown` alongside the list - see libclimat/wire/snapshot.cpp - and this
// is the tile that exists to respect the distinction.
//
// ============================================================================
// ONE ALERT, NOT A STACK
//
// The daemon already sorted by `Alert::outranks`, so index 0 is the one that
// matters. A tile 320 px wide showing three warnings shows none of them; "+2
// more" and a click into the app is the honest amount of room.
//
// ============================================================================
// NEVER COLOUR ALONE
//
// Severity carries a glyph, a colour and the word. docs/04-architecture.md
// §4.10 forbids colour-only encoding, and this is the tile where getting it
// wrong matters most - a red bar means nothing to a reader who cannot see red,
// and this is the one tile that is trying to tell them to take shelter.

import QtQuick

import "wire.js" as Wire

WidgetSurface {
    id: root
    widgetId: "alerts"

    readonly property var alerts: Wire.arr(Wire.at(root.snap, "alerts"))
    readonly property bool known: Wire.at(root.snap, "alertsKnown") === true
    // `worst` and not `top`: QQuickItem already declares a FINAL anchor-line
    // property called `top`, and shadowing it is a hard load failure - "Cannot
    // override FINAL property", reported against this file from the Loader two
    // files away. The daemon has already sorted by Alert::outranks, so index 0
    // is the one that matters.
    readonly property var worst: root.alerts.length > 0 ? Wire.obj(root.alerts[0]) : ({})
    readonly property string severity: root.worst.severity !== undefined
                                       ? root.worst.severity : "unknown"

    // ---- something in force ------------------------------------------------

    Item {
        anchors.fill: parent
        visible: root.alerts.length > 0

        // The severity wash, which is a fourth token group rather than a
        // stretch of statusGood/Caution/Poor. Published authority bands get
        // their own categorical palette (docs/10-design-system.md §10.5), and a
        // fourth level of a three-level status scale would have made it a scale.
        Rectangle {
            anchors.fill: parent
            radius: 6
            color: Theme.severity[root.severity] !== undefined
                   ? Theme.severity[root.severity] : Theme.severity.unknown
            opacity: 0.18
        }

        SeverityGlyph {
            id: mark
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            severity: root.severity
            glyphSize: 22
        }

        Column {
            anchors.left: mark.right
            anchors.leftMargin: 10
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1

            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: root.worst.event !== undefined ? root.worst.event : ""
                color: Theme.ink.primary
                font.pixelSize: Theme.type.label
                font.bold: true
                elide: Text.ElideRight
            }

            // Where it applies, which is the one thing a reader needs from a
            // tile: whether this is about them. The issuer's own grading -
            // "yellow warning" - is on the wire and is deliberately *not* here,
            // because for the NWS it is the word "Moderate", which is what the
            // severity glyph beside it already says. The app's banner has room
            // to show both; a 320 px tile does not, and repeating the severity
            // in words would cost the line that says which county.
            Text {
                width: parent.width
                textFormat: Text.PlainText
                text: {
                    var area = root.worst.areaDescription !== undefined
                               ? root.worst.areaDescription : ""
                    if (area !== "")
                        return area
                    return root.worst.sender !== undefined ? root.worst.sender : ""
                }
                color: Theme.ink.muted
                font.pixelSize: Theme.type.axis
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: root.alerts.length > 1
                text: qsTr("+%n more", "", root.alerts.length - 1)
                color: Theme.ink.dim
                font.pixelSize: Theme.type.axis
            }
        }
    }

    // ---- nothing in force, or nothing known --------------------------------

    Text {
        anchors.fill: parent
        visible: root.alerts.length === 0
        verticalAlignment: Text.AlignVCenter
        text: root.known ? qsTr("No warnings in force") : qsTr("Warnings unavailable")
        color: root.known ? Theme.ink.muted : Theme.ink.dim
        font.pixelSize: Theme.type.label
        wrapMode: Text.WordWrap
    }
}
