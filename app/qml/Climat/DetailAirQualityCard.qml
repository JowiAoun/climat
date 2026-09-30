// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Air-quality detail card.
//
// The same instrument as the UV card, and deliberately so: "an index against a
// published band scale" is the same question in both cards, so it gets the same
// answer, and the two sit in the same row of the grid where any difference
// between them would read as two authors rather than one.
//
// The ring *is* the reading. It is painted with the European AQI bands from the
// bottom of the scale up to the index, so 25 draws a short teal stub and 150
// draws the whole circle running teal, green, amber, red. The dot only marks
// where the paint stops.
//
// The previous version drew one flat arc for the whole ring and left the entire
// reading to a dot sliding along it: change the value and nothing moved but a
// 16px disc. That is a drawn scale with the reading left out - the thing
// docs/10-design-system.md §10.7 calls decoration rather than visualisation.
//
// Two smaller corrections that came with it. The unfilled remainder is
// `trackLine`, not `gridLine`: at 0.11 alpha the track was too faint to read as
// a scale, and a filled fraction needs to be a fraction *of* something visible.
// And the "PM2.5 4.4 µg/m³" caption is gone - the body sentence already names
// PM2.5 as the primary pollutant, and printing the same fact twice on one card
// is what the caption was doing. The card's own number is the index; the
// pollutant is context, and context is the body's job.
// A card is a `DetailCard { content: Item { id: viz } }`, so everything drawn
// here lives inside a Component and reaches the two ids around it - `root` for
// the card and `viz` for the visualisation - across that boundary. Without this
// pragma neither is resolvable at compile time: qmllint reports every one of
// them as an unqualified access, and qmlcachegen, which is the half that costs
// something, cannot ahead-of-time compile the binding and leaves it to be
// interpreted on every evaluation. That is the first-paint budget in
// docs/03-tech-stack.md §3.4 being spent on lookups the compiler could have
// done.
//
// Bound makes the enclosing scope's ids lexical, which is what they already
// read as. It is safe here because every delegate in this file declares its
// `required property` - that is the one thing Bound takes away, and none of
// these were relying on it.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import "chartmath.js" as ChartMath

DetailCard {
    id: root

    readonly property var d: Detail.airQuality

    title: qsTr("Air quality")
    status: d.status
    trend: d.trend
    body: d.body

    content: Item {
        id: viz

        // Angles run clockwise from 3 o'clock, y down, as in SVG - the same
        // convention and the same 310° dial with a 50° mouth at the bottom that
        // the UV card uses. Matching numbers rather than merely similar ones is
        // what makes the two rings the same size on screen.
        readonly property real startAngle: 115
        readonly property real sweepAngle: 310
        readonly property real ringWidth: 7
        // Segments per full ring, so a painted stretch is smooth at any length.
        readonly property int segments: 44

        // The marker is the widest thing on the dial, so it - not the stroke -
        // decides how much room the ring must leave around itself.
        readonly property real markSize: 14
        readonly property real rim: markSize / 2 + 1

        // The dial is not vertically symmetric: the mouth at the bottom means it
        // reaches a full radius above its centre but only sin(startAngle) below.
        // Fit the shape it actually is and hang it from the top of the box.
        readonly property real reachDown: Math.sin(startAngle * Math.PI / 180)
        readonly property real radius: Math.max(0, Math.min((height - rim * 2) / (1 + reachDown),
                                                            (width - rim * 2) / 2))
        readonly property real cx: width / 2
        readonly property real cy: rim + radius

        // The reading, as a fraction of the scale the data declares. Clamped, so
        // an index past the top of the scale fills the ring rather than running
        // off the end of it.
        readonly property real reading: ChartMath.clamp(root.d.value / root.d.max, 0, 1)

        // The head of the paint, running 0 → `reading` once on mount off the
        // shell's `reveal` hook, over `Theme.motion.reveal`. The same two lines
        // as the UV and cloud-cover dials, because the three share a geometry
        // and should not arrive three different ways; the long version of why
        // is in DetailUvCard.qml. The number in the middle does not count up.
        readonly property real t: reading * root.reveal

        readonly property real markAngle: startAngle + t * sweepAngle
        readonly property color markColor: ChartMath.sampleRamp(Detail.bands.aqi, t)

        function dialX(deg, r) { return cx + r * Math.cos(deg * Math.PI / 180) }
        function dialY(deg, r) { return cy + r * Math.sin(deg * Math.PI / 180) }

        // An arc between two positions on the scale, both in 0..1.
        function arcSeg(t0, t1) {
            var a0 = startAngle + t0 * sweepAngle
            var a1 = startAngle + t1 * sweepAngle
            return "M " + dialX(a0, radius).toFixed(2) + " " + dialY(a0, radius).toFixed(2)
                 + " A " + radius.toFixed(2) + " " + radius.toFixed(2)
                 + " 0 " + ((t1 - t0) * sweepAngle > 180 ? 1 : 0) + " 1 "
                 + dialX(a1, radius).toFixed(2) + " " + dialY(a1, radius).toFixed(2)
        }

        // A ShapePath can gradient-*fill* but not gradient-*stroke*, so the
        // painted stretch is a run of short arcs, each sampling the AQI bands at
        // its own midpoint - the trick SeriesBars uses per bar. The run is cut to
        // end exactly under the marker rather than at the nearest segment
        // boundary, so the dot sits on the end of the paint and not a few pixels
        // past it.
        //
        // Cut off the reading and not off the sweeping head, so the array is a
        // fixed length: a JS-array model that grows resets the whole Repeater,
        // which would tear down and rebuild every Shape on the ring on every
        // frame of the arrival. The segments are all there from the start and
        // each is clipped to the head instead.
        readonly property var litSegs: {
            var out = []
            if (reading <= 0.005)
                return out
            var n = Math.max(4, Math.round(reading * segments))
            for (var i = 0; i < n; ++i)
                out.push({ from: reading * i / n,
                           to:   reading * (i + 1) / n,
                           mid:  reading * (i + 0.5) / n })
            return out
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            // The scale, end to end, under everything: the reading only means
            // something as a fraction of the whole ring.
            ShapePath {
                fillColor: "transparent"
                strokeColor: Theme.line.track
                strokeWidth: viz.ringWidth
                capStyle: ShapePath.RoundCap
                PathSvg { path: viz.arcSeg(0, 1) }
            }
        }

        // The traversed stretch, in the bands themselves. Painted over the track
        // rather than instead of it, so the two always meet exactly.
        Repeater {
            model: viz.litSegs

            Shape {
                id: seg
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                required property var modelData

                // Clipped to the head of the sweep, and clamped at its own
                // start: an arc asked to end before it begins is not empty -
                // SVG goes the long way round and paints most of the ring.
                readonly property real segTo: Math.max(modelData.from,
                                                       Math.min(modelData.to, viz.t))
                // Not reached yet. `opacity` rather than `visible`, per §10.8;
                // it is a plain binding with no Behavior, so nothing fades.
                opacity: segTo > modelData.from ? 1 : 0

                ShapePath {
                    fillColor: "transparent"
                    // Sampled at the segment's own place on the scale, which
                    // does not move: the run grows, the bands stay put.
                    strokeColor: ChartMath.sampleRamp(Detail.bands.aqi, seg.modelData.mid)
                    strokeWidth: viz.ringWidth
                    capStyle: ShapePath.RoundCap
                    PathSvg { path: viz.arcSeg(seg.modelData.from, seg.segTo) }
                }
            }
        }

        // Where the paint stops. Ringed in white so it separates from the band it
        // has landed on instead of reading as a bulge in the ring. Not there at
        // all without a band, or a place with no air-quality product gets a
        // marker sitting on "good".
        Rectangle {
            visible: root.d.band !== ""
            width: viz.markSize; height: viz.markSize; radius: width / 2
            color: viz.markColor
            border.width: 2.5
            border.color: Theme.ink.primary
            x: viz.dialX(viz.markAngle, viz.radius) - width / 2
            y: viz.dialY(viz.markAngle, viz.radius) - height / 2
        }

        // In the middle of the dial, which is where a dial's reading goes: the
        // ring is a scale drawn *around* the number, and setting the number off
        // to one side leaves the ring circling nothing. This is the one stated
        // exception to the bottom-left rule (§10.7), and all three dials in the
        // grid take it.
        //
        // `reading` and not `value`: the same digits when there is an index,
        // and a dash when there is none. `value` is 0 there, which put a big
        // "0" over the words "No reading".
        Text {
            text: root.d.reading
            color: Theme.ink.primary
            font.pixelSize: Theme.type.reading
            font.bold: true
            x: viz.cx - width / 2
            y: viz.cy - height / 2
        }
    }
}
