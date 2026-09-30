// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The phone's headline: the answer to the question the app was opened to ask.
//
// The desktop's CurrentConditions with three things changed, and each of them
// is a consequence of the width rather than a restyling:
//
//   No card.      It sits directly on the page gradient. On the desktop the
//                 hero is one of four sections and a wash is what separates
//                 it from the others; here it is the top of a screen with
//                 nothing above it to be separated from, and a wash would
//                 only hide the sky it is drawn on.
//   No high/low.  The desktop puts them right-aligned in the space the
//                 reference fills with a radar map. There is no such space at
//                 390 px, and the ten-day card two rows down states today's
//                 pair anyway.
//   Slugs wrap.   Six across needs about 790 px. Three by two is the only
//                 arrangement that divides six and fits.
//
// It reads every number from `Detail`, same as the desktop hero, so the
// two cannot drift.
//
// ---- motion: none ------------------------------------------------------------
// Same reasoning as CurrentConditions, and it applies harder here. This card
// asserts a number, a condition and six readings; an assertion is instant.
// A phone hero that counts up from zero is a hero that briefly reports the
// wrong temperature on the one screen the app exists to show.
import QtQuick
import QtQuick.Shapes
import "chartmath.js" as ChartMath

Item {
    id: root

    // The six the desktop shows, without the disclosure chevrons the
    // reference puts on each. A chevron is a promise that tapping the slug
    // opens the measurable, and this prototype has no per-measurable screen on
    // the phone to open - LocationBar's own note about the non-rotating
    // chevron is the same rule: do not draw an affordance the app cannot keep.
    readonly property var slugs: [
        { label: qsTr("Air quality"), value: Detail.airQuality.reading,
          dot: true,  arrow: -1 },
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

    implicitHeight: slugGrid.y + slugGrid.height
    height: implicitHeight

    // ---- label and timestamp ----------------------------------------------
    Text {
        id: heading
        text: qsTr("Current weather")
        color: Theme.ink.primary
        font.pixelSize: Theme.type.cardTitle
        font.bold: true
        anchors.left: parent.left
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
    //
    // Right-aligned against a heading on a 390 px phone, so it wraps rather
    // than eliding: an age that has been cut off in the middle is worse than no
    // age at all.
    Text {
        text: [Engine.localTime,
              Engine.updatedLabel,
              Engine.fromFallback ? qsTr("via %1").arg(Engine.sourceName) : "",
              Engine.problem].filter(function (s) { return s !== "" }).join("  ·  ")
        color: Engine.stale || Engine.problem !== "" ? Theme.state.caution
                                                     : Theme.ink.muted
        Behavior on color {
            ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
        }
        font.pixelSize: Theme.type.label
        horizontalAlignment: Text.AlignRight
        wrapMode: Text.WordWrap
        anchors.right: parent.right
        anchors.left: heading.right
        anchors.leftMargin: 12
        anchors.baseline: heading.baseline
    }

    // ---- the headline ------------------------------------------------------
    Item {
        id: headline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.topMargin: 14
        height: Math.max(glyph.height, reading.implicitHeight)

        WeatherGlyph {
            id: glyph
            kind: Detail.current.conditionKind
            glyphSize: 52
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
        }

        // Blank rather than a number when there is no forecast behind the card.
        // CurrentConditions.qml carries the argument; the phone hero is the same
        // figure split from the same unit and needs the same guard.
        Text {
            id: reading
            text: Engine.hasData ? String(Detail.temperature.value) : ""
            color: Theme.ink.primary
            font.pixelSize: Theme.type.heroReading
            anchors.left: glyph.right
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
        }

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

        // Takes whatever is left. "Sunny" fits on one line at full size, and
        // on a 390 px phone "Mainly sunny" did not: it elided to "Mainly s…",
        // and so would most names past seven letters. A name that does not fit
        // steps down to `heroCaptionLong` and may take two lines, which still
        // sits inside the number's height, so nothing beside it moves.
        Column {
            id: conditionColumn
            spacing: 2
            anchors.left: unit.right
            anchors.leftMargin: 14
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter

            TextMetrics {
                id: conditionAtFullSize
                text: Detail.cloudCover.condition
                font.pixelSize: Theme.type.heroCaption
                font.bold: true
            }

            Text {
                text: Detail.cloudCover.condition
                color: Theme.ink.primary
                font.pixelSize: conditionAtFullSize.advanceWidth <= conditionColumn.width
                                ? Theme.type.heroCaption : Theme.type.heroCaptionLong
                font.bold: true
                width: parent.width
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                text: qsTr("Feels like %1").arg(Detail.feelsLike.reading)
                color: Theme.ink.muted
                font.pixelSize: Theme.type.heroLabel
                width: parent.width
                elide: Text.ElideRight
            }
        }
    }

    // ---- outlook -----------------------------------------------------------
    Text {
        id: outlook
        text: Detail.current.summary
        color: Theme.ink.primary
        font.pixelSize: Theme.type.heroDetail
        wrapMode: Text.WordWrap
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headline.bottom
        anchors.topMargin: 14
    }

    // ---- slug row ----------------------------------------------------------
    // Three columns, always. The desktop grid picks a count that divides six
    // from the width it is given; here the width is a phone and the answer is
    // three at every size the mobile shell runs at, so choosing is a branch
    // that would only ever be taken one way.
    Grid {
        id: slugGrid
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: outlook.bottom
        anchors.topMargin: 18

        columns: 3
        rowSpacing: 14
        readonly property real cellWidth: Math.floor(width / columns)

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
                    width: parent.width - 6
                }

                Row {
                    id: slugValue
                    spacing: 6
                    anchors.top: slugLabel.bottom
                    anchors.topMargin: 3

                    Rectangle {
                        visible: slug.modelData.dot
                        width: 10
                        height: 10
                        radius: 5
                        color: root.aqiColor
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: slug.modelData.value
                        color: Theme.ink.primary
                        font.pixelSize: Theme.type.heroDetail
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    // Points where the wind is going: the bearing names where
                    // it comes from, so the arrow is that bearing turned round.
                    // Same glyph and same reasoning as the desktop hero.
                    Item {
                        visible: slug.modelData.arrow >= 0
                        width: 14
                        height: 14
                        anchors.verticalCenter: parent.verticalCenter

                        Shape {
                            anchors.fill: parent
                            preferredRendererType: Shape.CurveRenderer
                            rotation: slug.modelData.arrow + 180
                            ShapePath {
                                fillColor: Theme.ink.primary
                                strokeColor: "transparent"
                                PathSvg { path: "M 7 0.5 L 10.8 13.5 L 7 10.6 L 3.2 13.5 Z" }
                            }
                        }
                    }
                }
            }
        }
    }
}
