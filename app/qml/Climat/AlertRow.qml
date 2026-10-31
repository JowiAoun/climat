// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// One alert in the sheet: everything the issuer wrote, expandable.
//
// The banner is a glance and this is the reading. So this component makes the
// opposite trade at every point: it shows the issuer's headline in full rather
// than the event name, it keeps their paragraph breaks, and it separates the
// description from the instruction because those are two different things -
// what is happening, and what you are being asked to do about it.
//
// ---- the body is the issuer's, and it is not reflowed ------------------------
//
// NWS descriptions arrive as wrapped plain text with hard newlines and `*
// WHAT...` bullets; ECCC's carry "Locations:", "Time span:", "Remarks:" on
// their own lines. Both are meant to be read as written. `Text.WordWrap` with
// the newlines preserved is what keeps that, and `Text.RichText` would be worse
// than useless - an alert body is untrusted text from the internet, and it is
// the one string in this app that would be worth someone's while to inject
// markup into.
import QtQuick

Item {
    id: root

    required property var alert
    property bool expanded: false

    readonly property string severity: alert && alert.severityKey ? alert.severityKey : "unknown"
    readonly property var tones: Theme.severity[severity] || Theme.severity.unknown

    implicitHeight: plate.implicitHeight
    height: implicitHeight

    Behavior on implicitHeight {
        enabled: !Theme.stillness
        NumberAnimation { duration: Theme.motion.reveal; easing.type: Easing.OutCubic }
    }

    Rectangle {
        id: plate
        anchors.fill: parent
        radius: Theme.metric.cardRadius
        color: root.tones.wash
        clip: true
        implicitHeight: body.implicitHeight + 26

        Rectangle {
            id: rail
            width: 4
            height: parent.height
            color: root.tones.edge
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.expanded = !root.expanded
        }

        Column {
            id: body
            anchors.left: rail.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.top: parent.top
            anchors.topMargin: 13
            spacing: 6

            // ---- the head ---------------------------------------------------
            Item {
                width: parent.width
                height: Math.max(glyph.height, title.height)

                SeverityGlyph {
                    id: glyph
                    severity: root.severity
                    glyphSize: 16
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: 2
                }

                Text {
                    id: title
                    anchors.left: glyph.right
                    anchors.leftMargin: 10
                    anchors.right: parent.right
                    wrapMode: Text.WordWrap
                    color: root.tones.ink
                    font.pixelSize: Theme.type.detailTitle
                    font.weight: Font.DemiBold
                    textFormat: Text.PlainText

                    // The headline where there is one, the event name where
                    // there is not. ECCC publishes no headline at all, and
                    // manufacturing one out of the other fields would put a
                    // sentence in their mouth.
                    text: root.alert
                          ? (root.alert.headline ? root.alert.headline : root.alert.event)
                          : ""
                }
            }

            // ---- the slug ---------------------------------------------------
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.ink.muted
                font.pixelSize: Theme.type.label
                textFormat: Text.PlainText
                text: {
                    if (!root.alert)
                        return ""
                    var parts = []
                    if (root.alert.issuerLabel)
                        parts.push(root.alert.issuerLabel)
                    if (root.alert.area)
                        parts.push(root.alert.area)
                    if (root.alert.when)
                        parts.push(root.alert.when)
                    if (root.alert.sender)
                        parts.push(root.alert.sender)
                    return parts.join(" · ")
                }
            }

            // ---- the body ---------------------------------------------------
            Text {
                width: parent.width
                visible: root.expanded && text !== ""
                wrapMode: Text.WordWrap

                // Plain text, deliberately. See the header: this is untrusted
                // text from a network, and RichText would parse it.
                textFormat: Text.PlainText
                color: Theme.ink.muted
                font.pixelSize: Theme.type.body
                lineHeight: 1.35
                text: root.alert && root.alert.description ? root.alert.description : ""
            }

            // What you are being asked to do. Kept apart from the description
            // and given the severity's own ink, because it is the only part of
            // an alert that is addressed to the reader.
            Text {
                width: parent.width
                visible: root.expanded && text !== ""
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: root.tones.ink
                font.pixelSize: Theme.type.body
                lineHeight: 1.35
                text: root.alert && root.alert.instruction ? root.alert.instruction : ""
            }

            Text {
                width: parent.width
                visible: !root.expanded
                color: Theme.ink.dim
                font.pixelSize: Theme.type.label
                text: qsTr("Tap to read")
            }
        }
    }
}
