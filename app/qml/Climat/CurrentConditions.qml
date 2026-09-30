// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Current conditions - the page headline.
//
// The one card that answers the question the app was opened to ask, so it is
// allowed to be loud in a way nothing else on the page is: a 64 px number, a
// 72 px glyph, and a plain-language sentence. Everything below it is detail.
//
// It reads every number from `Detail` rather than carrying its own, so the
// headline and the detail card for the same measurable cannot drift apart.
//
// Anatomy, top to bottom:
//   label + timestamp     what this is, and how fresh
//   glyph, reading, unit  the headline, with the condition and feels-like beside
//   summary               one sentence of outlook
//   slug row              six measurables, each also a card further down
//
// ---- motion: none, and that is the decision rather than an omission ----------
//
// This card has no interaction and no changing data, so it has no state to
// transition between. That leaves arrival, and §10.7 is precise about what
// earns an arrival: a reveal has to show where a value sits on its scale
// rather than merely assert it. This card draws no scale. It asserts a number,
// a condition, a sentence and six readings, and an assertion is instant - there
// is no journey for the eye to read, so animating one would be inventing one.
//
// The two ideas that come up every time, and why neither survives:
//
//   The temperature counting up from zero. §10.6's "the reader can read it at
//   rest position zero" is exactly this case, and this is the worst component
//   on the page to break it in: a hero that reads 0° for half a second is not
//   revealing the temperature, it is briefly reporting the wrong one, and it is
//   the single number the app was opened to answer. A count-up also has to be
//   re-triggered to be seen, and §10.6 forbids re-triggering a reveal.
//
//   The wind slug's bearing arrow rotating to its heading. This is the one
//   element here that genuinely is a value's position on a scale, so it is the
//   only tempting case - but it is a 16 px glyph, and sweeping it into place
//   would put the page's slowest motion on its smallest element while the 64 px
//   number beside it sits still. DetailWindCard is where a bearing gets to be
//   drawn at a size worth watching; this row is the glance, that card is the
//   answer.
//
// Nothing responds to resize either, and deliberately: the slug grid re-columns
// 6 → 3 → 2 and the high/low pair drops out below 620 px. Both pop, because
// §10.6 is clear that reflow is not a transition - a window drag that made this
// card ease between column counts would read as lag, not as a layout.
import QtQuick
import QtQuick.Shapes
import "chartmath.js" as ChartMath

Item {
    id: root

    readonly property real pad: Theme.metric.cardPadding

    // The six measurables MSN puts under the summary. Each is also a card in
    // the grid below: this row is the glance, that grid is the answer.
    //
    // `dot` and `arrow` are the two that carry more than a number - air quality
    // is meaningless without its band, and a wind speed without a bearing is
    // half a reading.
    readonly property var slugs: [
        // The dot is the band's colour, so it goes when there is no band: a
        // place with no air-quality product showed a dash beside a green dot.
        { label: qsTr("Air quality"), value: Detail.airQuality.reading,
          dot: Detail.airQuality.band !== "",  arrow: -1 },
        { label: qsTr("Wind"),        value: Detail.wind.reading,
          dot: false, arrow: Detail.wind.directionDeg },
        { label: qsTr("Humidity"),    value: Detail.humidity.reading,
          dot: false, arrow: -1 },
        { label: qsTr("Visibility"),  value: Detail.visibility.reading,
          dot: false, arrow: -1 },
        { label: qsTr("Pressure"),    value: Detail.pressure.reading,
          dot: false, arrow: -1 },
        { label: qsTr("Dew point"),   value: Detail.humidity.dewReading,
          dot: false, arrow: -1 }
    ]

    readonly property color aqiColor: ChartMath.sampleRamp(
        Detail.bands.aqi, Detail.airQuality.value / Detail.airQuality.max)

    implicitHeight: slugGrid.y + slugGrid.height + pad
    height: implicitHeight

    // No border. Contrast against the page defines the card - see
    // docs/10-design-system.md §10.1.
    Rectangle {
        anchors.fill: parent
        radius: Theme.metric.cardRadius
        color: Theme.surface.base
    }

    // ---- label and timestamp ---------------------------------------------
    Text {
        id: heading
        text: qsTr("Current weather")
        color: Theme.ink.primary
        font.pixelSize: Theme.type.cardTitle
        font.bold: true
        x: root.pad
        y: root.pad
    }

    // ---- how old this is, and where it came from ---------------------------
    //
    // docs/04-architecture.md §4.5 asks for "a subtle 'updated 25 min ago'" on
    // every row it ticks stale-while-revalidate for, and this is it. Three
    // facts share one line because they answer one question - how much should I
    // trust this:
    //
    //   the time where the weather is   12:28 PM
    //   how long ago we fetched it      Updated 4 minutes ago
    //   who answered, when it was not   via MET Norway
    //       the primary
    //
    // The third appears only when the fallback served, because "via Open-Meteo"
    // on every screen is noise that trains the reader to stop seeing the line
    // the one day it matters.
    //
    // A failed refresh does not blank any of this. It appends a sentence and
    // tints the line, which is the §4.1 rule made visible: stale with a
    // timestamp, never a spinner and never an empty screen.
    Text {
        id: stamp
        text: [Engine.localTime,
              Engine.updatedLabel,
              Engine.fromFallback ? qsTr("via %1").arg(Engine.sourceName) : "",
              Engine.problem].filter(function (s) { return s !== "" }).join("  ·  ")
        color: Engine.stale || Engine.problem !== "" ? Theme.state.caution
                                                     : Theme.ink.muted
        Behavior on color {
            ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
        }
        font.pixelSize: Theme.type.body
        anchors.left: heading.left
        anchors.top: heading.bottom
        anchors.topMargin: 3
    }

    // ---- the headline -----------------------------------------------------
    Item {
        id: summaryGroup
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.pad
        anchors.rightMargin: root.pad
        anchors.top: stamp.bottom
        anchors.topMargin: 18
        height: Math.max(glyph.height, reading.implicitHeight)

        WeatherGlyph {
            id: glyph
            kind: Detail.current.conditionKind
            glyphSize: 72
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
        }

        // Regular weight, not bold. At 64 px the reference sets this in book
        // weight and it is right: bold at that size stops reading as a number
        // and starts reading as a shout.
        //
        // Blank rather than a number when there is no forecast behind the card,
        // and it has to be asked here because this is the one place that prints
        // the figure separated from its unit: everywhere else the `reading`
        // field carries both, and `readingOf()` decides. `value` is the number
        // the ramps and the bars scale against - conditionsdata.cpp's
        // "absent is not zero" note - and printing it unconditionally put a
        // 64 px zero on a screen with nothing behind it.
        Text {
            id: reading
            text: Engine.hasData ? String(Detail.temperature.value) : ""
            color: Theme.ink.primary
            font.pixelSize: Theme.type.heroReading
            anchors.left: glyph.right
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
        }

        // Rides at the top of the digits rather than on their baseline, which
        // is where a degree suffix belongs.
        Text {
            id: unit
            text: Detail.current.unitLabel
            color: Theme.ink.primary
            font.pixelSize: Theme.type.heroUnit
            anchors.left: reading.right
            anchors.leftMargin: 2
            anchors.top: reading.top
            anchors.topMargin: Math.round(Theme.type.heroReading * 0.13)
        }

        Column {
            id: conditionColumn
            spacing: 4
            anchors.left: unit.right
            anchors.leftMargin: 30
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: Detail.cloudCover.condition
                color: Theme.ink.primary
                font.pixelSize: Theme.type.heroCaption
                font.bold: true
            }

            Row {
                spacing: 8
                Text {
                    text: qsTr("Feels like")
                    color: Theme.ink.muted
                    font.pixelSize: Theme.type.heroLabel
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: Detail.feelsLike.reading
                    color: Theme.ink.primary
                    font.pixelSize: Theme.type.heroDetail
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        // Today's range, right-aligned.
        //
        // A divergence: the reference fills this space with a radar map, which
        // we have nothing to draw. Left empty, a card this wide reads as a
        // layout that ran out of content - and the high and low are the two
        // numbers the summary sentence gestures at without stating.
        Row {
            id: rangeRow
            spacing: 22
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: parent.width > 620

            Repeater {
                model: [{ label: qsTr("High"), value: Detail.temperature.high },
                        { label: qsTr("Low"),  value: Detail.temperature.low }]

                delegate: Column {
                    required property var modelData
                    spacing: 2

                    Text {
                        text: modelData.label
                        color: Theme.ink.muted
                        font.pixelSize: Theme.type.heroLabel
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                    Text {
                        // Same rule as the hero above: no forecast, no figure.
                        text: Engine.hasData ? modelData.value + Detail.temperature.unit : ""
                        color: Theme.ink.primary
                        font.pixelSize: Theme.type.heroCaption
                        anchors.horizontalCenter: parent.horizontalCenter
                    }
                }
            }
        }
    }

    // ---- outlook ----------------------------------------------------------
    Text {
        id: outlook
        text: Detail.current.summary
        color: Theme.ink.primary
        font.pixelSize: Theme.type.heroDetail
        elide: Text.ElideRight
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.pad
        anchors.rightMargin: root.pad
        anchors.top: summaryGroup.bottom
        anchors.topMargin: 20
    }

    // ---- slug row ---------------------------------------------------------
    // Equal columns rather than the reference's natural widths: at 620 px its
    // six slugs sit shoulder to shoulder, and at our width the same treatment
    // leaves them huddled against the left edge of a very wide card. Equal
    // columns stretch, and they wrap when the window is too narrow to hold six.
    Grid {
        id: slugGrid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.pad
        anchors.rightMargin: root.pad
        anchors.top: outlook.bottom
        anchors.topMargin: 22

        // Only counts that divide six, so the last row is never one orphan slug
        // sitting under five. At 760 px the naive "as many as fit" gave 5 + 1,
        // which reads as a layout accident rather than as a wrap.
        readonly property int fits: Math.floor(width / 132)
        columns: fits >= 6 ? 6 : (fits >= 3 ? 3 : 2)
        readonly property real cellWidth: Math.floor(width / columns)
        rowSpacing: 14

        Repeater {
            model: root.slugs

            delegate: Item {
                id: slug
                required property var modelData

                width: slugGrid.cellWidth
                height: slugLabel.height + slugValue.height + 4

                Text {
                    id: slugLabel
                    text: slug.modelData.label
                    color: Theme.ink.muted
                    font.pixelSize: Theme.type.heroLabel
                    elide: Text.ElideRight
                    width: parent.width - 8
                }

                Row {
                    id: slugValue
                    spacing: 7
                    anchors.top: slugLabel.bottom
                    anchors.topMargin: 4

                    Rectangle {
                        visible: slug.modelData.dot
                        width: 11
                        height: 11
                        radius: 5.5
                        color: root.aqiColor
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: slug.modelData.value
                        color: Theme.ink.primary
                        font.pixelSize: Theme.type.heroDetail
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    // Points where the wind is going, matching the wedge in
                    // DetailWindCard: the bearing names where it comes from, so
                    // the arrow is that bearing turned around.
                    Item {
                        visible: slug.modelData.arrow >= 0
                        width: 16
                        height: 16
                        anchors.verticalCenter: parent.verticalCenter

                        // The notch is deep on purpose. A shallow one leaves a
                        // squat triangle that reads as a generic marker at this
                        // size, and the whole point of the glyph is that you can
                        // tell instantly which way it points.
                        Shape {
                            anchors.fill: parent
                            preferredRendererType: Shape.CurveRenderer
                            rotation: slug.modelData.arrow + 180
                            ShapePath {
                                fillColor: Theme.ink.primary
                                strokeColor: "transparent"
                                PathSvg { path: "M 8 0.6 L 12.3 15.4 L 8 12.1 L 3.7 15.4 Z" }
                            }
                        }
                    }
                }
            }
        }
    }
}
