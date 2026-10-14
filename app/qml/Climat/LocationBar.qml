// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The place the page is about: name, a disclosure chevron, a home marker.
//
// It sits directly on the page gradient, not on a surface. Wrapping it in its
// own wash would put a fourth surface immediately above the hero card with
// nothing between them, and two washes that close together read as one badly
// aligned panel rather than as two things.
import QtQuick
import QtQuick.Shapes

Item {
    id: root

    // Before the first place is chosen the bar says what to do, and tapping
    // it opens the picker that does it.
    property string label: Engine.hasPlace ? Detail.location.label : qsTr("Choose a place")
    property bool isHome: Detail.location.isHome

    // Whether the picker this bar opens is currently up. Bound from outside -
    // the picker is a sheet over the whole page and the bar is inside it, so
    // the bar cannot own it - and read here for one thing only: which way the
    // chevron points.
    property bool disclosed: false

    signal changeRequested()
    signal homeToggled()

    implicitWidth: bar.width

    // Still 26, which is the height of the row this bar draws. The cells inside
    // it are 44 and overflow it by 9 px top and bottom, into the section gap
    // that is already there and that nothing else is aiming at.
    //
    // The alternative was to make the bar 44 and it moved every page in the app
    // down by 18 px - a whole-page diff on eleven golden images to make room for
    // air around two marks that had not changed size. The rule this file ended
    // up on is the one TouchTarget states: the target grows, the layout does
    // not. Here it could not be TouchTarget itself, because the two marks are
    // too close together for two 44 px areas - so the cells grew instead, and
    // only across.
    implicitHeight: 26
    height: implicitHeight

    // ---- spacing is the touch fix here ---------------------------------------
    //
    // The chevron and the home marker are two different actions - open the
    // place list, make this place home - and they used to sit 10 px apart, with
    // a 14 px target and a 24 px one. Two 44 px targets need 88 px between their
    // outer edges and cannot be conjured out of 48; growing them in place would
    // have made each one steal half the other's taps, which is worse than
    // leaving both small.
    //
    // So the cells are the floor and the spacing goes to zero, which puts 15 px
    // of air between the name and the chevron where there were 10, and 25 px
    // between the chevron and the home ring where there were 10. That reads as
    // a deliberate separation of two unlike controls rather than as a gap - and
    // it is the only part of this bar that moved. See `implicitHeight`.
    Row {
        id: bar
        spacing: 0
        anchors.verticalCenter: parent.verticalCenter

        Text {
            id: placeName
            // The name keeps its own 10 px of air from the chevron cell, which
            // the Row no longer provides. Right-padded rather than spaced,
            // because the two gaps in this bar are now different on purpose.
            rightPadding: 10
            text: root.label
            color: Theme.ink.primary
            font.pixelSize: Theme.type.sectionTitle
            anchors.verticalCenter: parent.verticalCenter

            // The name opens the picker too. "Choose a place" reads as a
            // button, and a first tap on it that did nothing was the first
            // thing a new reader met on a phone. As tall as the chevron's
            // cell, so it clears the same touch floor.
            Item {
                width: parent.width
                height: Theme.metric.hitMin
                anchors.verticalCenter: parent.verticalCenter
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.changeRequested() }
            }
        }

        // ---- disclosure chevron ------------------------------------------
        // It rotates now, and for most of this prototype's life it deliberately
        // did not: `changeRequested()` went nowhere, and a chevron that turns
        // over and reveals nothing is an animation making a promise the app
        // cannot keep.
        //
        // The promise is kept. The signal opens PlacePicker - search, saved
        // places, use my location - so the chevron is doing what a disclosure
        // chevron means: pointing at the thing it opened, and pointing back
        // when it closes. `disclosed` is bound from whichever page owns the
        // sheet, so the arrow is a readout of the picker's state rather than a
        // toggle of its own, and it cannot end up upside down over a closed
        // panel.
        //
        // `Theme.motion.move` and not `view`: the chevron is one small object
        // travelling, not a screenful arriving.
        Item {
            width: Theme.metric.hitMin
            height: Theme.metric.hitMin
            anchors.verticalCenter: parent.verticalCenter

            Shape {
                width: 14
                height: 14
                anchors.centerIn: parent
                rotation: root.disclosed ? 180 : 0
                Behavior on rotation {
                    NumberAnimation { duration: Theme.motion.move; easing.type: Easing.OutCubic }
                }
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: chevronHover.hovered ? Theme.ink.primary
                                                      : Theme.ink.muted
                    // Hover is colour only (§10.6), and the stroke was the one
                    // hover on this bar that snapped while the ring beside it
                    // faded.
                    Behavior on strokeColor {
                        ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
                    }
                    strokeWidth: 1.6
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: "M 2.5 5.5 L 7 10 L 11.5 5.5" }
                }
            }

            HoverHandler { id: chevronHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.changeRequested() }
        }

        // ---- home marker --------------------------------------------------
        // A ring rather than a filled plate: a filled circle here would be the
        // only opaque thing on the page.
        Item {
            width: Theme.metric.hitMin
            height: Theme.metric.hitMin
            anchors.verticalCenter: parent.verticalCenter

            Rectangle {
                width: 24
                height: 24
                anchors.centerIn: parent
                radius: width / 2
                color: homeHover.hovered ? Theme.surface.raised : "transparent"
                border.width: 1
                border.color: root.isHome ? Theme.line.control : Theme.line.grid
                Behavior on color {
                    ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
                }
                // The marker is a toggle - `homeToggled()` is its whole reason
                // for having a tap target - so the ring must not snap between
                // states while the wash behind it fades.
                Behavior on border.color {
                    ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
                }
            }

            Shape {
                anchors.centerIn: parent
                width: 14
                height: 14
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    fillColor: root.isHome ? Theme.ink.primary : Theme.ink.muted
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

            HoverHandler { id: homeHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.homeToggled() }
        }
    }
}
