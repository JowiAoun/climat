// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The severe-weather banner: one alert, never a stack.
//
// ---- one, and a count -------------------------------------------------------
//
// Seattle had four alerts in force at one coordinate on the afternoon they were
// recorded, and a banner that stacked them would have pushed the temperature
// below the fold to tell a reader about three air quality advisories and a heat
// advisory at equal weight. So the banner shows the highest-ranked alert and
// says "+3 more"; the sheet is where the rest live.
//
// Ranking is libclimat's - AlertSet::displayableAt() sorts by severity, then
// urgency, then certainty, then onset - so this file makes no decision about
// which alert is the important one. It only draws the answer.
//
// ---- dismissal is acknowledgement -------------------------------------------
//
// The close control collapses this to a one-line strip. It does not remove the
// alert, and the strip is still tappable, still names the event and still
// carries the severity colour and glyph. An alert that could be made to vanish
// is an alert whose banner people learn to dismiss reflexively; a strip that
// stays is a reminder that costs one line of the screen.
//
// An update that RAISES the severity un-acknowledges - see
// app/viewmodels/alertsdata.h, which owns that rule and the storage behind it.
//
// ---- where this is not ------------------------------------------------------
//
// Not inside WeatherPage's Flickable, and not inside any of the five mobile
// pages. On the desktop the Flickable sets `layer.enabled: true` and a banner
// inside it would scroll away from a warning the reader has not read yet. On
// the phone MobileShell destroys and rebuilds its page on every tab change, so
// a per-page banner would be constructed five times in a session and would
// re-run its entrance each time - a tornado warning that re-animates when you
// look at the map.
//
// ---- motion -----------------------------------------------------------------
//
// The height change on collapse, and nothing else. §10.6's rule is that a
// component is readable at rest position zero, and here that is not a style
// preference: an alert that arrives with an entrance animation is an alert that
// is unreadable for 200 ms, and the reader who most needs it is the one glancing
// at the screen. So it appears at full opacity, at full height, immediately.
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

    // Everything comes from the model. Held as a property rather than read
    // through the singleton at each site so the gallery can hand in a specimen -
    // Alerts is a singleton with a network behind it, and a component that can
    // only be seen by having weather is a component nobody reviews.
    property var alert: Alerts.top
    property int moreCount: Alerts.moreCount
    property bool acknowledged: Alerts.acknowledged
    property bool complete: Alerts.complete
    property bool unconfirmed: Alerts.unconfirmed
    property string confirmedLabel: Alerts.confirmedLabel

    // The sheet asks to open. Emitted rather than opening one here: the sheet
    // has to cover the nav bar on the phone, so it belongs to the shell.
    signal opened()

    readonly property string severity: alert && alert.severityKey ? alert.severityKey : "unknown"
    readonly property var tones: Theme.severity[severity] || Theme.severity.unknown

    // An empty map is what the model publishes when there is nothing in force,
    // and it is the only visibility test. Not `Alerts.count > 0` - that would be
    // a second source of truth for the same question.
    readonly property bool present: alert !== undefined && alert !== null
                                    && alert.event !== undefined && alert.event !== ""

    // The event name, or an empty string. A property rather than
    // `alert.event` at each site, because `{}` - which is exactly what
    // `Alerts.top` publishes when nothing is in force - is TRUTHY, so
    // `alert ? alert.event : ""` evaluates to `undefined` and QML says
    // "Unable to assign [undefined] to QString" twice on every launch of a
    // place with no weather warnings. `visible: present` hides the banner and
    // does nothing about its bindings, which still run.
    //
    // Found by tests/qml/tst_specimen.qml, which fails a component that builds
    // with any warning at all. That is the whole reason it exists: this is the
    // same defect as the 469 undefined lines W4 spent a day on, and it would
    // have shipped looking perfect.
    readonly property string eventName: present ? alert.event : ""

    visible: present
    // Column computes its own implicitHeight from its children, so the padding
    // is added here rather than assigned there - `Column.implicitHeight` is
    // read-only and assigning it is a load error, not a warning.
    implicitHeight: present ? (acknowledged ? strip.implicitHeight
                                            : full.implicitHeight + 26) : 0
    height: implicitHeight

    Behavior on implicitHeight {
        enabled: !Theme.stillness
        NumberAnimation { duration: Theme.motion.reveal; easing.type: Easing.OutCubic }
    }

    // ---- the plate ---------------------------------------------------------
    Rectangle {
        id: plate
        anchors.fill: parent
        radius: Theme.metric.cardRadius
        color: root.tones.wash
        clip: true

        // The rail. §10.1 bans borders at a junction; this is not a junction and
        // not a border - it is the one place the saturated severity colour is
        // allowed to be a solid, and it is what makes the banner scannable in a
        // column of cards without reading a word of it.
        Rectangle {
            id: rail
            width: 4
            height: parent.height
            color: root.tones.edge
        }

        // Anywhere that is not the dismiss control opens the sheet.
        //
        // Declared HERE, before the content, and that ordering is the whole of
        // it: QML stacks later siblings above earlier ones, so this written last
        // would sit over the dismiss control and swallow every click on it. The
        // close button would have looked present and done nothing - which is the
        // kind of defect that survives review, because the banner is obviously
        // fine and the one control on it is obviously there.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            onClicked: {
                if (root.acknowledged)
                    Alerts.reveal()
                root.opened()
            }
        }

        // ---- collapsed ------------------------------------------------------
        Item {
            id: strip
            anchors.left: rail.right
            anchors.right: parent.right
            anchors.top: parent.top
            opacity: root.acknowledged ? 1 : 0
            visible: opacity > 0

            // The floor. The collapsed strip IS the target - the whole thing is
            // tappable and tapping it is how an acknowledged alert is read
            // again - so here the control's size is the affordance and it grows
            // rather than growing an invisible area around itself.
            implicitHeight: Theme.metric.hitMin

            Behavior on opacity {
                enabled: !Theme.stillness
                NumberAnimation { duration: Theme.motion.tint }
            }

            SeverityGlyph {
                id: stripGlyph
                severity: root.severity
                glyphSize: 14
                anchors.left: parent.left
                anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                anchors.left: stripGlyph.right
                anchors.leftMargin: 10
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                color: root.tones.ink
                font.pixelSize: Theme.type.label
                font.weight: Font.DemiBold
                textFormat: Text.PlainText
                text: root.moreCount > 0
                      //: %1 is an alert name, %2 a count of further alerts
                      ? qsTr("%1 · +%2 more").arg(root.eventName).arg(root.moreCount)
                      : root.eventName
            }
        }

        // ---- expanded -------------------------------------------------------
        Column {
            id: full
            anchors.left: rail.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.top: parent.top
            anchors.topMargin: 13
            spacing: 5
            opacity: root.acknowledged ? 0 : 1
            visible: opacity > 0

            Behavior on opacity {
                enabled: !Theme.stillness
                NumberAnimation { duration: Theme.motion.tint }
            }

            // ---- the headline row ------------------------------------------
            Item {
                width: parent.width
                height: Math.max(headGlyph.height, headText.height, dismiss.height)

                SeverityGlyph {
                    id: headGlyph
                    severity: root.severity
                    glyphSize: 18
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    id: headText
                    anchors.left: headGlyph.right
                    anchors.leftMargin: 10
                    anchors.right: more.visible ? more.left : dismiss.left
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    color: root.tones.ink
                    font.pixelSize: Theme.type.cardTitle
                    font.weight: Font.DemiBold
                    textFormat: Text.PlainText
                    text: root.eventName
                }

                // ---- the count ----------------------------------------------
                //
                // Its own item in the head row rather than a clause on the
                // sentence below, and that is not a layout preference. It was in
                // the sentence first - and Seattle's area description is 180
                // characters of county names, so the line elided long before it
                // and the banner told a reader about one of four alerts with no
                // sign the other three existed. The one fact a banner cannot
                // afford to lose to elision is how much it is not showing.
                Rectangle {
                    id: more
                    visible: root.moreCount > 0
                    width: moreText.implicitWidth + 16
                    height: 20
                    radius: 10
                    color: Theme.surface.raised
                    anchors.right: dismiss.left
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: moreText
                        anchors.centerIn: parent
                        color: root.tones.ink
                        font.pixelSize: Theme.type.label
                        font.weight: Font.DemiBold
                        //: %1 is how many further alerts are in force
                        text: qsTr("+%1 more").arg(root.moreCount)
                    }
                }

                // ---- dismiss ------------------------------------------------
                // 32 px of drawn control around an 11 px mark, and 44 px of
                // target around that. The item stays 32 because it is what sets
                // the head row's height, and a 44 px row would push the whole
                // banner 12 px taller to hold a cross nobody wants bigger.
                //
                // The target grows past the item on all four sides. It is 6 px
                // clear of the plate's right edge and well inside its top, which
                // matters because the plate clips - and clipping in Qt Quick
                // takes input with it, so a target that overflowed a clipped
                // edge would silently be smaller than it says.
                Item {
                    id: dismiss
                    width: 32
                    height: 32
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter

                    Rectangle {
                        anchors.centerIn: parent
                        width: 22
                        height: 22
                        radius: 11
                        color: dismissArea.hovered ? Theme.surface.raised : "transparent"
                        Behavior on color {
                            enabled: !Theme.stillness
                            ColorAnimation { duration: Theme.motion.tint }
                        }
                    }

                    Repeater {
                        model: [45, -45]

                        Rectangle {
                            required property int modelData

                            width: 11
                            height: 1.6
                            radius: 0.8
                            anchors.centerIn: parent
                            rotation: modelData
                            color: root.tones.ink
                        }
                    }

                    TouchTarget {
                        id: dismissArea
                        onTapped: Alerts.acknowledge()
                    }
                }
            }

            // ---- the sentence ----------------------------------------------
            //
            // The issuer's own grading, then the window, then the area - and
            // that order is the whole of the design here, because this line
            // elides.
            //
            // Area LAST, which was not the first arrangement. NWS area
            // descriptions are lists of every zone the alert covers: Seattle's
            // Heat Advisory names eleven, at 180 characters, so an area put
            // second consumed the entire line and pushed "Until 10:00 PM" off
            // the end. The grading and the window are one phrase each and both
            // fit; the area is the part a reader can lose and still know what
            // they have been told.
            Text {
                width: parent.width
                elide: Text.ElideRight
                color: Theme.ink.muted
                font.pixelSize: Theme.type.body
                textFormat: Text.PlainText
                text: {
                    if (!root.alert)
                        return ""
                    var parts = []
                    if (root.alert.issuerLabel)
                        parts.push(root.alert.issuerLabel)
                    if (root.alert.when)
                        parts.push(root.alert.when)
                    if (root.alert.area)
                        parts.push(root.alert.area)
                    return parts.join(" · ")
                }
            }

            // ---- what we could not check ------------------------------------
            //
            // Two different admissions, and they must not be collapsed into one
            // sentence: `unconfirmed` means we are showing an alert its author
            // has already been due to refresh and we could not hear them;
            // `!complete` means a whole service did not answer and there may be
            // warnings here we have never seen. Silence on either is the failure
            // this feature exists to avoid.
            Text {
                width: parent.width
                visible: text !== ""
                elide: Text.ElideRight
                color: Theme.ink.dim
                font.pixelSize: Theme.type.label
                text: {
                    if (root.unconfirmed)
                        return root.confirmedLabel
                    if (!root.complete)
                        return qsTr("Some alert sources could not be reached")
                    return ""
                }
            }
        }
    }
}
