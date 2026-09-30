// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The weather-detail grid: twelve cards, one per measurable.
//
// The cards are deliberately uniform - same box, same anatomy, same rhythm -
// so the eye can sweep the grid and land only where a value is unusual. Twelve
// differently-shaped cards would each demand to be read.
//
// The grid lays out in full and reports its height; it does not scroll itself.
// It used to, which was right while it was the only thing on screen and wrong
// the moment it became one section of a scrolling page: a scroll area inside a
// scroll area gives the reader two things to drag and no way to tell which one
// they got. The page owns the scrolling, and with it the layer that keeps these
// cards' Shapes inside the viewport - see docs/10-design-system.md §10.8.
pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    readonly property int columns: Math.max(1, Math.floor(
        (width + Theme.metric.detailGap)
        / (Theme.metric.detailCardWidth + Theme.metric.detailGap)))

    // The cards share the row. At their bare 300 px, whatever the columns left
    // over sat as a gap on the right: 48 px at 1340, so the grid stopped short of
    // the hero and the chart above it. The card width stays the smallest a card
    // is drawn at and decides how many fit.
    readonly property int cellWidth: Math.max(Theme.metric.detailCardWidth, Math.floor(
        (width - (columns - 1) * Theme.metric.detailGap) / columns))

    implicitHeight: grid.y + grid.height
    height: implicitHeight

    SectionHeader {
        id: header
        title: qsTr("Weather details")

        // Qualified, unlike the card above, because the two are different
        // readings and used to be the same one. That card carries the clock at
        // the place and it ticks; this carries the hour these twelve cards
        // describe, which moves only when a new observation lands. Two bare
        // times a finger apart, disagreeing by half an hour and neither saying
        // which it is, is a worse screen than either of them alone.
        stamp: Detail.observedAt === "" ? "" : qsTr("as of %1").arg(Detail.observedAt)
    }

    Grid {
        id: grid
        anchors.top: header.bottom
        anchors.topMargin: 14
        anchors.left: parent.left
        columns: root.columns
        spacing: Theme.metric.detailGap

        Repeater {
            // Fixed order, so the layout does not depend on key order.
            model: ["Temperature", "FeelsLike", "CloudCover", "Precipitation",
                    "Wind", "Humidity", "Uv", "AirQuality", "Visibility",
                    "Pressure", "Sun", "Moon", "MoonPhase"]

            delegate: Loader {
                required property string modelData
                required property int index
                source: "Detail" + modelData + "Card.qml"
                width: root.cellWidth

                // Stagger the reveal across the grid. Twelve cards at one
                // stagger apart is a half-second wave that reads as a single
                // gesture; the same wave three times slower reads as twelve
                // separate things going off, which is a fidget rather than a
                // reveal. Set here rather than in the card because only the
                // grid knows where a card sits in the order.
                onLoaded: if (item !== null)
                    item.revealDelay = index * Theme.motion.stagger
                // A card that fails to load leaves a hole rather than
                // collapsing the grid, so the gap names the culprit.
                onStatusChanged: if (status === Loader.Error)
                    console.warn("details: card failed to load -", source)

                Rectangle {
                    anchors.fill: parent
                    visible: parent.status === Loader.Error
                    radius: Theme.metric.detailRadius
                    color: Theme.surface.recede
                    Text {
                        anchors.centerIn: parent
                        text: parent.parent.modelData
                        color: Theme.ink.dim
                        font.pixelSize: Theme.type.body
                    }
                }
            }
        }
    }
}
