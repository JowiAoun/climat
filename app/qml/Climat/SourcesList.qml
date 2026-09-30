// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Every data source the app reads, credited in the words its licence asks for.
//
// One list in two places: the phone's Me tab puts it in a card and the desktop's
// preferences sheet puts it in a group. The desktop had no copy of it, so the
// most-used shell showed Open-Meteo's forecasts and GeoNames' place names with
// neither credited, which both CC BY licences require.
import QtQuick

Column {
    id: root

    spacing: 12

    Repeater {
        model: Engine.sources

        delegate: Column {
            required property var modelData

            width: root.width
            spacing: 2

            Text {
                // The exact sentence the licence asks for, not a paraphrase
                // built from the provider's name. Open-Meteo wants "Weather
                // data by Open-Meteo.com", and ECCC's required wording is a
                // sentence nobody would guess.
                text: modelData.creditLine
                color: Theme.ink.primary
                font.pixelSize: Theme.type.status
                width: parent.width
                wrapMode: Text.WordWrap
            }

            Text {
                text: modelData.licenceName + "  \u00b7  " + modelData.homepage
                color: Theme.ink.muted
                font.pixelSize: Theme.type.label
                width: parent.width
                elide: Text.ElideRight
            }

            Text {
                // §2.9 requires the model owners behind an aggregator to be
                // named separately, which is the part a paraphrase would drop.
                visible: modelData.upstream.length > 0
                text: qsTr("Models: ") + modelData.upstream.join(", ")
                color: Theme.ink.dim
                font.pixelSize: Theme.type.label
                width: parent.width
                wrapMode: Text.WordWrap
            }

            Text {
                visible: modelData.note !== ""
                text: modelData.note
                color: Theme.ink.dim
                font.pixelSize: Theme.type.label
                width: parent.width
                wrapMode: Text.WordWrap
            }
        }
    }
}
