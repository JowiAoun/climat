// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Wind detail card - a compass rose on the left, the readings on the right.
//
// Wind is two facts that do not fit in one glyph: how hard it is blowing and
// where it is coming from. The numbers carry the first, because a speed is a
// quantity you read rather than estimate off a picture. The rose carries the
// second, because a bearing is a direction and a direction wants to be drawn.
//
// Three things on the rose are bound to the data and would look different for
// a different reading:
//
//   - the wedge's orientation is the bearing: its blunt end faces the quarter
//     the wind comes *from* (294°, WNW) and its point faces where it is going,
//     which is the only reading of "From WNW" that does not contradict itself;
//   - the wedge's reach scales with the mean speed, so a calm hour is a stub
//     near the middle and a gale nearly touches the ring;
//   - the green arc on the ring is the gust band, centred on the bearing and
//     widened by how far the gusts run ahead of the mean. Gusty air is air
//     whose direction is not settled, so a gustier hour gets a wider band.
//
// All three arrive rather than appear: the vane swings off north onto the
// bearing, the wedge grows out to its reach, and the gust band opens to its
// width - one gesture on the card's one-shot `reveal`. The two numbers do not
// move. A speed is a reading and a reading is legible from the first frame.
//
// And point at the card and the wedge sets off downwind, because a drawing of
// wind that holds perfectly still is missing the only thing everybody already
// knows about wind. That is the one hover gesture in the grid; the rule it is
// kept to is in DetailCard.qml and the mechanics are further down.
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

    readonly property var d: Detail.wind

    title: qsTr("Wind")
    status: d.status
    trend: d.trend
    body: d.body

    content: Item {
        id: viz

        // Belongs to this visualisation and to nothing else, so it lives here
        // rather than in theme.js - design system §10, the wind-rose exception.
        readonly property color windAccent: "#55b17e"

        readonly property real dirDeg: root.d.directionDeg

        // ---- the arrival -----------------------------------------------------
        // Where the vane swings *from* is the whole question, and north is the
        // only answer that carries meaning: a bearing is degrees from north, so
        // north is the reading's own zero and starting anywhere else would be
        // decoration dressed as data.
        //
        // It takes the short way round - 66° anticlockwise for a WNW wind, not
        // 294° the other way. A vane settles onto the wind; a near-full
        // revolution in half a second reads as a spinner, which is the one thing
        // an arrival on this page must not look like. `dirDelta` is the signed
        // turn, folded into (-180, 180].
        readonly property real dirDelta: ((dirDeg + 180) % 360) - 180
        readonly property real vaneDeg: {
            var a = dirDelta * root.reveal
            return (a % 360 + 360) % 360
        }

        // The cardinal labels sit *on* the ring, so the radius has to leave
        // half a line of type above N and below S.
        readonly property real ringR: Math.max(24, Math.min(48, height / 2 - 9))
        readonly property real roseX: ringR + 12
        readonly property real roseY: height / 2

        // Half the break in the ring at each cardinal, wide enough for an
        // 11px letter to sit in it without touching either arc end.
        readonly property real gapHalf: 12

        // Gust band. Half-width grows with the gusts' excess over the mean:
        // 24 against 13 km/h is a 46% excess, giving roughly ±22°. It opens from
        // nothing over the reveal, so the band widens as the vane settles rather
        // than dragging a finished arc around the ring ahead of it.
        readonly property real gustHalf: {
            var g = root.d.gust, s = root.d.speed
            var excess = g > 0 ? Math.max(0, g - s) / g : 0
            return (8 + 30 * Math.min(excess, 1)) * root.reveal
        }

        // ---- the hover: which way the air is going ----------------------------
        // The rose draws a bearing and the numbers draw two speeds. All three
        // are correct and all three are standing still, and standing still is
        // the one thing wind is not: a vane on a roof tells you the air is
        // *going* somewhere before you have read anything off it. Pointing at
        // this card asks that question, so the wedge does what the air does and
        // travels downwind.
        //
        // A drift and not a turn. The bearing is a measurement - turning the
        // wedge off it would be drawing a wind that is not blowing - so nothing
        // rotates. The wedge keeps its angle and moves along the one axis the
        // reading already names, from the upwind side of the dial to the
        // downwind side, and then starts again from where it came in. The reset
        // is instant and deliberately so; a wedge that eased back upwind would
        // be drawing air going the wrong way for half the loop.
        //
        // The travel is bounded by the dial rather than chosen: `driftBack` and
        // `driftOn` are the room actually left between the wedge and the ring at
        // either end, so it sweeps everything available to it and never crosses
        // out. That falls out well - a light wind is a stub with most of the
        // dial to cross, and a gale already fills it and barely moves, which is
        // the same fact the wedge's length is carrying.
        //
        // The rate is the speed. It is the honest binding and the one the still
        // card cannot make: 6 km/h ambles across and anything near the ceiling
        // crosses in a third of the time.
        readonly property real driftBack:
            Math.max(0, ringR * 0.95 - (wedgeBaseR + ringR * 0.26))
        readonly property real driftOn: Math.max(0, ringR * 0.95 - wedgeApexR)

        readonly property int driftPeriod:
            2600 - 1700 * Math.min(root.d.speed / root.d.scaleMax, 1)

        // 0 at the upwind limit, 1 at the downwind one. A property value source,
        // so it holds wherever it stopped; `hoverPhase` is what takes the
        // displacement back to nothing when the pointer leaves, and it runs on
        // `hoverPhase > 0` rather than on `hovered` so the wedge is still
        // drifting while that happens instead of being cut off mid-dial.
        property real drift: 0

        NumberAnimation on drift {
            running: root.hoverPhase > 0
            loops: Animation.Infinite
            from: 0
            to: 1
            duration: viz.driftPeriod
            // The one place in this file that is not OutCubic, and §10.6 asks
            // for the reason: air does not decelerate into the far side of a
            // compass rose. A drift that eased would be reporting a gust at one
            // end and a lull at the other, neither of which is in the data.
            easing.type: Easing.Linear
        }

        // How far downwind the wedge has been carried. Zero at rest, which is
        // what keeps the resting card the card the golden images hold.
        readonly property real driftBy:
            (-driftBack + (driftBack + driftOn) * drift) * root.hoverPhase

        // Wedge reach downwind, against the working ceiling `Detail.wind` carries -
        // a ceiling that decides what the reader sees is data, not styling. At
        // 30 km/h it puts 13 km/h a little past halfway out, and anything at or
        // above the ceiling stops just short of the ring.
        //
        // Only the speed's share of the reach is revealed. 0.46 is the scale's
        // own zero - where a dead calm leaves the wedge - so the reach grows off
        // that the way a bar grows off its baseline, rather than swelling out of
        // a point that means nothing.
        readonly property real wedgeApexR:
            ringR * (0.46 + 0.42 * Math.min(root.d.speed / root.d.scaleMax, 1)
                                 * root.reveal)
        readonly property real wedgeBaseR: ringR * 0.40
        readonly property real wedgeHalfW: ringR * 0.25

        function nf(v) { return Math.round(v * 100) / 100 }

        // Compass frame: 0° is north, angles run clockwise, y grows downward.
        function ptX(a, r) { return roseX + r * Math.sin(a * Math.PI / 180) }
        function ptY(a, r) { return roseY - r * Math.cos(a * Math.PI / 180) }

        function arcPath(a0, a1, r) {
            if (a1 - a0 < 0.5)
                return ""
            var large = (a1 - a0) > 180 ? 1 : 0
            return "M " + nf(ptX(a0, r)) + " " + nf(ptY(a0, r))
                 + " A " + nf(r) + " " + nf(r) + " 0 " + large + " 1 "
                 + nf(ptX(a1, r)) + " " + nf(ptY(a1, r))
        }

        // Four arcs, one per quadrant, broken where a cardinal label sits.
        function spansPath(a0, a1) {
            var spans = ChartMath.compassSpans(a0, a1, gapHalf)
            var p = ""
            for (var i = 0; i < spans.length; ++i)
                p += arcPath(spans[i].from, spans[i].to, ringR) + " "
            return p.trim()
        }

        readonly property string ringPath: spansPath(0, 360)

        // The band is the ring painted green wherever the gusts reach, so it is
        // literally the ring's own call with a narrower span - which is why the
        // two cannot disagree about where a letter sits. The argument, and the
        // bug that made it necessary, are in ChartMath.compassSpans.
        //
        // Where that leaves a stub on one side of a letter and nothing on the
        // other, the stub is the truth: the ring has no arc in the gap, so
        // neither can the band.
        readonly property string gustPath: spansPath(vaneDeg - gustHalf,
                                                     vaneDeg + gustHalf)

        // A leaf: rounded and broad on the upwind side, drawn to a point
        // downwind. Sides bow out very slightly so the tip reads as a taper
        // rather than as a triangle.
        readonly property string wedgePath: {
            var t = vaneDeg * Math.PI / 180
            var ux = Math.sin(t), uy = -Math.cos(t)      // toward where it blows from
            var px = -uy, py = ux                        // across that axis

            // The whole leaf is drawn about a centre that the hover carries
            // downwind. Offsetting the centre rather than each point is what
            // keeps this a translation: the shape is identical at every step,
            // which is the difference between air moving and a wedge growing.
            var cx = roseX - ux * driftBy, cy = roseY - uy * driftBy

            var bx = cx + ux * wedgeBaseR, by = cy + uy * wedgeBaseR
            var ax = cx - ux * wedgeApexR, ay = cy - uy * wedgeApexR
            var c1x = bx + px * wedgeHalfW, c1y = by + py * wedgeHalfW
            var c2x = bx - px * wedgeHalfW, c2y = by - py * wedgeHalfW

            var bow = ringR * 0.05
            var m1x = (c1x + ax) / 2 + px * bow, m1y = (c1y + ay) / 2 + py * bow
            var m2x = (c2x + ax) / 2 - px * bow, m2y = (c2y + ay) / 2 - py * bow
            var kx = bx + ux * ringR * 0.26, ky = by + uy * ringR * 0.26

            return "M " + nf(c1x) + " " + nf(c1y)
                 + " Q " + nf(m1x) + " " + nf(m1y) + " " + nf(ax) + " " + nf(ay)
                 + " Q " + nf(m2x) + " " + nf(m2y) + " " + nf(c2x) + " " + nf(c2y)
                 + " Q " + nf(kx) + " " + nf(ky) + " " + nf(c1x) + " " + nf(c1y)
                 + " Z"
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            // The ring is the scale the wedge is read against, so it is a gauge
            // track and takes the track colour. At gridLine's 0.11 it was too
            // faint to read as anything, and the wedge floated on nothing.
            ShapePath {
                fillColor: "transparent"
                strokeColor: Theme.line.track
                strokeWidth: 2.5
                capStyle: ShapePath.RoundCap
                PathSvg { path: viz.ringPath }
            }

            ShapePath {
                fillColor: "transparent"
                strokeColor: viz.windAccent
                strokeWidth: 4
                capStyle: ShapePath.RoundCap
                PathSvg { path: viz.gustPath }
            }

            ShapePath {
                strokeColor: "transparent"
                fillColor: viz.windAccent
                PathSvg { path: viz.wedgePath }
            }
        }

        Repeater {
            model: [{ label: qsTr("N"), deg: 0 }, { label: qsTr("E"), deg: 90 },
                    { label: qsTr("S"), deg: 180 }, { label: qsTr("W"), deg: 270 }]

            Text {
                required property var modelData
                text: modelData.label
                color: Theme.ink.dim
                font.pixelSize: Theme.type.axis
                x: viz.ptX(modelData.deg, viz.ringR) - width / 2
                y: viz.ptY(modelData.deg, viz.ringR) - height / 2
            }
        }

        // The readings. Bearing first, because it is what the rose is showing;
        // then the mean, then the gust, each with its unit and its name.
        //
        // Mean and gust are co-equal - neither is the number the card exists to
        // show on its own - so both take the pair size rather than one of them
        // taking the reading size and the other a scaled-down guess.
        Column {
            id: readout

            x: viz.roseX + viz.ringR + 20
            width: Math.max(0, viz.width - x)
            // Bottom-anchored, not centred: nine of the twelve cards put their
            // reading on the floor of the content box, and a side-by-side layout
            // that centres its readout column instead is what made row 2 the row
            // with no baseline.
            anchors.bottom: parent.bottom
            spacing: 4

            Text {
                text: qsTr("From %1 (%2°)").arg(root.d.directionLabel)
                                           .arg(root.d.directionDeg)
                color: Theme.ink.muted
                font.pixelSize: Theme.type.label
                width: readout.width
                elide: Text.ElideRight
            }

            Row {
                spacing: 7

                Text {
                    text: root.d.speedFigure
                    color: Theme.ink.primary
                    font.pixelSize: Theme.type.readingPair
                    font.bold: true
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: root.d.unit
                        color: Theme.ink.muted
                        font.pixelSize: Theme.type.label
                    }
                    Text {
                        text: qsTr("Wind speed")
                        color: Theme.ink.muted
                        font.pixelSize: Theme.type.label
                    }
                }
            }

            Row {
                spacing: 7

                Text {
                    text: root.d.gustFigure
                    color: Theme.ink.primary
                    font.pixelSize: Theme.type.readingPair
                    font.bold: true
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        text: root.d.unit
                        color: Theme.ink.muted
                        font.pixelSize: Theme.type.label
                    }
                    Text {
                        text: qsTr("Wind gust")
                        color: Theme.ink.muted
                        font.pixelSize: Theme.type.label
                    }
                }
            }
        }
    }
}
