// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Where the map goes. There is no map.
//
// The mobile shell has five tabs because the reference has five, and a shell
// reviewed with four of them is a shell whose nav bar has never been seen at
// its real width. So the Maps tab exists and says, unmistakably, that it is
// scaffolding.
//
// Unmistakably is the whole specification. A tasteful empty state in the house
// colours - a centred glyph, a grey sentence - is exactly what a *finished*
// screen with no data looks like, and six weeks later somebody files a bug
// about the map not loading. So this is deliberately off-palette and
// deliberately ugly: hatched, dash-outlined, and labelled in words.
//
// The hatch is the same one the chart uses for the past, and it means the same
// thing in both places: there is no data here and that is on purpose.
//
// What replaces it is MapLibre Native over a vector basemap - see
// docs/03-tech-stack.md, decision D4. Its chrome (the layer legend, the
// timeline scrubber, the locate button) is not drawn here either, on the
// grounds that chrome around a placeholder is a mock of a mock.
import QtQuick
import QtQuick.Shapes

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        radius: Theme.metric.cardRadius
        color: Theme.surface.recede

        HatchPattern {
            anchors.fill: parent
            anchors.margins: 1
            lineColor: Theme.scaffold.stroke
            spacing: 14
            lineWidth: 1
        }
    }

    // Dashed, because a solid border is what a real panel has.
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            fillColor: "transparent"
            strokeColor: Theme.scaffold.stroke
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [6, 4]

            PathRectangle {
                x: 1; y: 1
                width: root.width - 2
                height: root.height - 2
                radius: Theme.metric.cardRadius
            }
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 320)
        spacing: 14

        Item {
            width: 92
            height: 92
            anchors.horizontalCenter: parent.horizontalCenter

            NavGlyph {
                kind: "maps"
                glyphSize: 72
                tint: Theme.scaffold.ink
                anchors.centerIn: parent
            }

            // Struck through - the glyph alone is a map icon, which is exactly
            // what a *working* map tab would put on its own button.
            //
            // The stroke overshoots the glyph's box by ten pixels at both ends
            // and is drawn heavier than the outline it crosses. Both are
            // deliberate: the first version was the same weight and stopped at
            // the icon's corners, and it read as another fold line. A strike
            // has to visibly not belong to the thing it crosses.
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    fillColor: "transparent"
                    strokeColor: Theme.scaffold.ink
                    strokeWidth: 5
                    capStyle: ShapePath.RoundCap
                    PathSvg { path: "M 6 86 L 86 6" }
                }
            }
        }

        Text {
            text: qsTr("No map yet")
            color: Theme.ink.primary
            font.pixelSize: Theme.type.cardTitle
            font.bold: true
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Text {
            // Written for the person holding the phone. The panel is for them
            // as much as for a reviewer, and a store build puts it in front of
            // them, so it names no file and no decision number.
            text: qsTr("A radar map belongs on this tab, and it is not built yet. "
                     + "Until then, the Hourly tab shows when rain is coming.")
            color: Theme.ink.muted
            font.pixelSize: Theme.type.body
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            width: parent.width
        }

        // Said in a word as well as in a sentence, for the reader who is
        // scanning a screenshot rather than reading it.
        Rectangle {
            width: chip.width + 20
            height: 24
            radius: 12
            color: "transparent"
            border.width: 1
            border.color: Theme.scaffold.stroke
            anchors.horizontalCenter: parent.horizontalCenter

            Text {
                id: chip
                text: qsTr("NOT BUILT YET")
                color: Theme.scaffold.ink
                font.pixelSize: Theme.type.axis
                font.bold: true
                font.letterSpacing: 1.2
                anchors.centerIn: parent
            }
        }
    }
}
