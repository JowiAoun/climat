// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// A month of forecasts, four days to a row.
//
// Not a seven-column calendar grid, and that is the interesting decision here.
// A week grid puts the weekday alignment first: Mondays stack under Mondays,
// and the shape of the month is legible. It also gives every cell a seventh of
// the screen - 52 px on a phone - which is enough for a date and nothing else.
//
// This is a forecast, not a diary. What the reader wants from it is "when is
// the warm stretch" and "which days are wet", and both are read by scanning
// numbers and icons rather than by looking up a Tuesday. Four columns give
// each day 90 px, which fits a condition, a high and a low; the weekday is
// written in the cell rather than implied by the column. The reference makes
// the same trade.
//
// Today is marked with a border and an accent date, not a lighter fill. A
// raised wash inside the card's own would composite to 0.165 - the stacked
// surface §10.1 exists to prevent - and this is the case its note about
// borders allows for: nothing else will do.
import QtQuick

Item {
    id: root

    // This month's days unless the page hands it the next month's.
    property var days: Data.monthDays

    readonly property int columns: 4
    readonly property real cellWidth: Math.floor(width / columns)
    readonly property real cellHeight: 84

    readonly property int rows: Math.ceil(root.days.length / columns)

    implicitHeight: rows * cellHeight
    height: implicitHeight

    Grid {
        anchors.fill: parent
        columns: root.columns

        Repeater {
            model: root.days

            delegate: Item {
                id: cell
                required property var modelData

                width: root.cellWidth
                height: root.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.metric.controlRadius
                    color: "transparent"
                    border.width: cell.modelData.isToday ? 1 : 0
                    border.color: Theme.accent.fill
                }

                Row {
                    id: dateRow
                    spacing: 5
                    x: 8
                    y: 8

                    Text {
                        text: String(cell.modelData.date)
                        color: cell.modelData.isToday ? Theme.accent.fill
                                                      : Theme.ink.primary
                        font.pixelSize: Theme.type.status
                        font.bold: true
                        anchors.bottom: parent.bottom
                    }

                    Text {
                        text: cell.modelData.weekday
                        color: Theme.ink.dim
                        font.pixelSize: Theme.type.axis
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 1
                    }
                }

                WeatherGlyph {
                    id: cellGlyph
                    kind: cell.modelData.icon
                    glyphSize: 26
                    x: 8
                    y: dateRow.y + dateRow.height + 6
                }

                // A day beyond the forecast horizon draws its date and nothing
                // else. The mock generated a plausible seasonal high for the
                // 29th and it looked completely convincing, which is exactly
                // the objection: a number nobody forecast is a number somebody
                // will plan around. docs/08-risks.md R9.
                Column {
                    visible: cell.modelData.known
                    spacing: 0
                    anchors.left: cellGlyph.right
                    anchors.leftMargin: 6
                    anchors.verticalCenter: cellGlyph.verticalCenter

                    Text {
                        text: Units.formatDisplay(Units.Temperature, cell.modelData.high)
                        color: Theme.ink.primary
                        font.pixelSize: Theme.type.label
                        font.bold: true
                    }

                    Text {
                        text: Units.formatDisplay(Units.Temperature, cell.modelData.low)
                        color: Theme.ink.muted
                        font.pixelSize: Theme.type.label
                    }
                }
            }
        }
    }
}
