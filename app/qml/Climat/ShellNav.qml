// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The mobile shell's navigation: five destinations from mobiletabs.js, one
// selected, along the bottom of a phone and down the left of a landscape
// tablet.
//
// It is the only piece of persistent chrome this shell has, so it is also the
// only thing on the screen allowed to be more opaque than a wash.
//
// ---- one component, two orientations ------------------------------------------
// This was BottomNav until a tablet in landscape needed the same five targets
// somewhere else. A second file would have been the obvious split and it would
// have been two copies of the pill, the model, the tokens and the tint
// animation kept in step by hand - and the pill is the part that is easiest to
// get subtly wrong and hardest to notice.
//
// So the *arrangement* is the property and everything else is shared.
// `Viewports.navStyle` decides which arrangement, not this file: the same
// question is asked about the content columns, and one place has to answer
// both or a rail can appear beside a layout that did not make room for it.
//
// ---- why this is not translucent ----------------------------------------------
// §10.1 says every surface is a thin white wash over the page gradient, and
// that rule is right everywhere the surface sits *in* the page. This one sits
// *over* it: the page scrolls underneath, so at 0.07 the reader would watch a
// temperature curve slide through the word "Hourly". The pager buttons already
// established the exception - a thing that floats over moving content is
// tinted and mostly opaque - and `surface.nav` is that token for this bar.
//
// The hairline along the leading edge is the second half of the same decision.
// §10.1 warns off borders because a border across a junction is the seam the
// junction exists to hide, but this is not a junction: it is the edge where a
// floating bar stops and scrolling content begins, and it is the only cue that
// the content continues behind it. Which edge that is follows the arrangement
// - the top of a bottom bar, the right of a left rail.
//
// ---- motion ------------------------------------------------------------------
// The pill slides between tabs rather than cutting, at `move`, because it is
// the one element here that is genuinely the same object in a new place. It
// slides along whichever axis the bar runs. The glyphs and labels tint at
// `tint`. Nothing else moves, and nothing moves on a timer - the bar is at
// rest until it is touched.
//
// `Bound` because the cell is a Component instantiated by two Repeaters, and a
// Component is a scope of its own - every `root.` inside it reads as unqualified
// access to qmllint and cannot be ahead-of-time compiled by qmlcachegen. Bound
// scoping is what makes those lookups resolvable; the delegate already declares
// its model role as `required`, which is the other half of what it asks for.
pragma ComponentBehavior: Bound

import QtQuick
import "mobiletabs.js" as Tabs

Item {
    id: root

    property string currentId: Tabs.list[0].id

    // Qt.Horizontal is the bottom bar, Qt.Vertical the left rail. Named for
    // the axis the destinations run along rather than for the edge it sits on,
    // because that is the axis the pill travels and the axis the cells divide.
    property int orientation: Qt.Horizontal

    readonly property bool rail: orientation === Qt.Vertical

    // The bar's own extent, without the strip beyond it. The shell needs both
    // numbers - one to place the bar, one to pad the page - and they must come
    // from the same place or the last row of a page hides under the nav.
    readonly property real barHeight: Theme.metric.navHeight

    // The gesture strip below a phone's bottom bar. A rail has no equivalent:
    // a system gesture area on a landscape device is along the bottom, which
    // for a rail is its far end rather than its leading edge, and 12 px of
    // padding there would push the fifth destination up for no reason. The
    // phone's own strip when it reports one, which a Qt 6.9 build on Android
    // does; `navSafeArea` otherwise, the deterministic stand-in that the
    // gallery's device frames and the golden images are measured against.
    readonly property real safeArea: rail ? 0 : Math.max(Theme.metric.navSafeArea,
                                                         WindowInsets.bottom)

    signal selected(string id)

    // One dimension each. A bar states its height and takes its width from
    // whatever is placing it; a rail is the other way round. The shell assigns
    // both outright, which is what a rotation needs - see MobileShell.
    implicitWidth: rail ? Theme.metric.navRailWidth : 0
    implicitHeight: rail ? 0 : barHeight + safeArea
    width: implicitWidth
    height: implicitHeight

    // Glyph, gap and label: the extent of what a cell actually draws.
    readonly property real contentExtent: root.pillHeight + 7 + 13

    // How much of the run each destination gets, along the axis it runs on.
    //
    // A bar divides its width by five, because the bar spans the screen and a
    // gap between two destinations there is a gap in a row the thumb sweeps.
    // A rail does NOT divide its height: 834 px in five parts puts 167 px
    // between one destination and the next, which stops reading as one control
    // with five positions and starts reading as five buttons that happen to
    // share an edge - and it puts the last of them at the bottom corner, which
    // is the one place a hand holding a tablet cannot reach.
    readonly property real cellExtent:
        rail ? root.contentExtent + 14 : width / Tabs.list.length

    // Where the run of five starts. Centred down a rail rather than pinned to
    // the top: a desktop rail is a menu and belongs under the title, and this
    // one is a touch target for whichever hand is holding the device.
    readonly property real runOrigin:
        rail ? Math.max(0, (height - cellExtent * Tabs.list.length) / 2) : 0

    // The pill is a horizontal lozenge in both arrangements - it sits behind a
    // glyph with a label under it, and that shape does not rotate with the bar.
    readonly property real pillWidth:
        Math.min(64, (rail ? width : cellExtent) - 8)
    readonly property real pillHeight: 30

    // Where a cell's contents start, measured from the cell's leading edge. In
    // a bar that is 6 px of top padding inside a 58 px cell; a rail's cell is
    // sized around its contents, so the padding is what is left over.
    readonly property real cellPad:
        rail ? (cellExtent - root.contentExtent) / 2 : 6

    Rectangle {
        anchors.fill: parent
        color: Theme.surface.nav
    }

    // The hairline, on whichever edge faces the content. Placed rather than
    // anchored: an edge that moves with the arrangement needs all four numbers
    // to change together, and half-anchored geometry is where an item ends up
    // obeying an anchor that the other branch left behind.
    Rectangle {
        x: root.rail ? root.width - 1 : 0
        y: 0
        width: root.rail ? 1 : root.width
        height: root.rail ? root.height : 1
        color: Theme.line.nav
    }

    // The selection, drawn once and moved, rather than one pill per cell
    // switched on and off. That is what lets it travel: five pills fading in
    // and out is a crossfade, and a crossfade between two positions reads as
    // two objects rather than as one that moved.
    Rectangle {
        id: pill
        width: root.pillWidth
        height: root.pillHeight
        radius: height / 2
        color: Theme.accent.fill

        readonly property real slot:
            Math.max(0, Tabs.indexOf(root.currentId)) * root.cellExtent

        x: root.rail ? (root.width - width) / 2
                     : slot + (root.cellExtent - width) / 2
        y: root.rail ? root.runOrigin + slot + root.cellPad : 6

        Behavior on x {
            NumberAnimation { duration: Theme.motion.move; easing.type: Easing.OutCubic }
        }
        Behavior on y {
            NumberAnimation { duration: Theme.motion.move; easing.type: Easing.OutCubic }
        }
    }

    // A Column for the rail and a Row for the bar, rather than one positioner
    // with a `flow` property: Row and Column are the two positioners that exist,
    // Flow's wrapping is not wanted here, and a Loader would rebuild five cells
    // on every rotation to produce the same five cells.
    Row {
        anchors.fill: parent
        visible: !root.rail

        Repeater {
            model: Tabs.list
            delegate: navCell
        }
    }

    Column {
        x: 0
        y: root.runOrigin
        width: root.width
        visible: root.rail

        Repeater {
            model: Tabs.list
            delegate: navCell
        }
    }

    Component {
        id: navCell

        Item {
            id: cell
            required property var modelData

            readonly property bool isCurrent: modelData.id === root.currentId

            width: root.rail ? root.width : root.cellExtent
            height: root.rail ? root.cellExtent : root.barHeight

            NavGlyph {
                id: glyph
                kind: cell.modelData.glyph
                glyphSize: 21
                tint: cell.isCurrent ? Theme.accent.ink : Theme.control.navGlyph
                anchors.horizontalCenter: parent.horizontalCenter
                y: root.cellPad + (root.pillHeight - height) / 2
            }

            Text {
                text: cell.modelData.label
                color: cell.isCurrent ? Theme.ink.primary : Theme.ink.muted
                font.pixelSize: Theme.type.axis
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: glyph.bottom
                anchors.topMargin: 7

                Behavior on color {
                    ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
                }
            }

            // The whole cell is the target, not the pill. A 30 px pill is
            // under every thumb-size guideline there is, and the label
            // below it is part of what the reader is aiming at.
            TapHandler {
                onTapped: root.selected(cell.modelData.id)
            }
        }
    }
}
