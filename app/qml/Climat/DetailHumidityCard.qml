// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Humidity detail card.
//
// Eight hours as a row of filling gauges: each column is drawn full height in
// the track colour - the whole 0-100% range - with the lower part filled in
// blue to the hour's relative humidity. Reading the array against a fixed 0-100
// scale is what makes it a visualisation rather than a decoration: 45% is a bar
// filled just under halfway, and it would be visibly a different picture at 80%.
//
// The two readings sit to the right at the pair size: relative humidity, and
// the dew point that explains how it feels. Neither is subordinate to the
// other, so neither gets shrunk to make the point.
//
// On arrival the fills grow off the floor of their tracks, one stagger apart,
// left to right - the array assembles the way the day runs. The tracks do not
// grow: they are the 0-100 scale each fill is read against (§10.7), and a track
// that arrived with its fill would leave the first frames with eight bars and
// nothing to judge them by.
import QtQuick

DetailCard {
    id: root

    readonly property var d: Detail.humidity

    // The fill belongs to this visualisation alone, so it lives here rather
    // than in theme.js (design system §10.1). The empty part of each gauge does
    // not: `trackLine` is the token for exactly that, and a locally tinted
    // track was one more thing for twelve cards to disagree about. The blue
    // cast it used to carry was family resemblance, not meaning.
    readonly property color barFill: "#6e9cff"

    title: qsTr("Humidity")
    status: d.status
    trend: d.trend
    body: d.body

    content: Item {
        id: viz

        readonly property var series: root.d.series
        readonly property int count: series.length

        // Measured off the reference: bars about half as wide as their pitch,
        // so the array reads as a comb rather than as a solid block.
        readonly property real barW: 9
        readonly property real barGap: 7
        readonly property real arrayW: count * barW + (count - 1) * barGap

        // Gauges are scaled over the full 0-100% range, not over the series'
        // own span. Relative humidity has a meaningful zero and a meaningful
        // ceiling; stretching eight readings across the box would make a calm
        // afternoon look like a storm.
        readonly property real trackH: Math.min(112, height)

        // ---- the arrival wave ----------------------------------------------
        // One column's own growth is a change of size, so it takes `move`; the
        // gap between one column and the next is `stagger`, the same token the
        // grid staggers whole cards by.
        //
        // The wave steps once per column that has a fill to draw. Every hour
        // here does, so this is `index`; it is written the same way
        // DetailPrecipitationCard writes it so that an hour at 0% costs the
        // wave a beat of silence rather than a hole in it.
        function drawnRank(i) {
            var r = 0
            for (var k = 0; k < i && k < count; ++k)
                if (series[k] > 0) r++
            return r
        }
        readonly property int drawnCount: drawnRank(count)

        // The step is tightened when a series is long enough that a full
        // stagger apiece would run past the card's own reveal - eight columns
        // fit at the full 45, twelve would not. Whatever the count, the last
        // column has landed within `Theme.motion.reveal` of the first starting,
        // so a card that ripples still arrives on the grid's wave rather than
        // trailing a third of a second behind it.
        readonly property int growSpan: Theme.motion.move
        readonly property int growStep: Math.round(Math.min(
            Theme.motion.stagger,
            (Theme.motion.reveal - growSpan) / Math.max(1, drawnCount - 1)))

        Row {
            spacing: viz.barGap
            anchors.verticalCenter: parent.verticalCenter

            Repeater {
                model: viz.series

                Item {
                    id: bar

                    required property int index
                    required property var modelData

                    width: viz.barW
                    height: viz.trackH

                    readonly property real barValue: modelData

                    // 0 → 1 once, this column's turn into the card's reveal. A
                    // value source rather than a binding on `root.reveal`: the
                    // card's reveal is a single eased ramp, and slicing an eased
                    // ramp into eight windows gives eight differently-eased
                    // columns crushed into its first third. Nothing restarts it -
                    // the card's reveal only ever goes 0 → 1, so the guard latches.
                    property real barGrow: 0

                    SequentialAnimation on barGrow {
                        running: root.reveal > 0
                        PauseAnimation { duration: viz.drawnRank(bar.index) * viz.growStep }
                        NumberAnimation {
                            to: 1
                            duration: viz.growSpan
                            easing.type: Easing.OutCubic
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: viz.barW / 2
                        color: Theme.line.track
                    }

                    Rectangle {
                        // An hour at 0% has nothing to draw. Clamping the height
                        // up to the bar width instead would print a 9px stub for
                        // a reading that is not there - the same guard
                        // DetailPrecipitationCard puts on its chance strip.
                        visible: bar.barValue > 0
                        width: viz.barW
                        // A hairline floor below that, so a reading small enough
                        // to round to nothing is still drawn as the something it
                        // is. Nothing in the current series comes near it.
                        height: Math.max(2, parent.height * bar.barValue / 100)
                               * bar.barGrow
                        y: parent.height - height
                        radius: viz.barW / 2
                        color: root.barFill
                    }
                }
            }
        }

        Column {
            spacing: 10
            x: viz.arrayW + 18
            width: viz.width - x
            // Bottom-anchored, not centred: nine of the twelve cards put their
            // reading on the floor of the content box, and a side-by-side layout
            // that centres its readout column instead is what made row 2 the row
            // with no baseline.
            anchors.bottom: parent.bottom

            Column {
                spacing: 0
                width: parent.width

                Text {
                    text: root.d.reading
                    color: Theme.ink.primary
                    font.pixelSize: Theme.type.readingPair
                    font.bold: true
                }

                Text {
                    text: qsTr("Relative humidity")
                    color: Theme.ink.muted
                    font.pixelSize: Theme.type.label
                    width: parent.width
                    elide: Text.ElideRight
                }
            }

            Column {
                spacing: 0
                width: parent.width

                Text {
                    text: root.d.dewReading
                    color: Theme.ink.primary
                    font.pixelSize: Theme.type.readingPair
                    font.bold: true
                }

                Text {
                    text: qsTr("Dew point")
                    color: Theme.ink.muted
                    font.pixelSize: Theme.type.label
                    width: parent.width
                    elide: Text.ElideRight
                }
            }
        }
    }
}
