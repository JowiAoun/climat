// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Me: preferences, places, where the data comes from.
//
// The reference has no screenshot of this tab and the nav bar has five slots,
// so this is the one screen here that is a proposal rather than a rebuild.
//
// It used to end with a line saying "nothing on this screen is wired up", and
// listed three things as not built: the unit switch that re-renders every
// screen, the place picker LocationBar's chevron would open, and the provider
// chooser. The first two are here. The third is not, and is deliberately not
// listed as missing any more - a provider chooser is a feature for somebody who
// already knows which model they prefer, and the fallback chain means the app
// answers that question for everybody else.
//
// ---- the preferences here are the same objects the desktop opens ------------
// `PrefGeneral` and `PrefUnits` are two files in this module, and the desktop's
// PreferencesSheet puts the same two on a sheet. Not a copy: this screen used to
// carry its own Appearance and Units cards, written here, and the moment the
// desktop needed settings at all that would have been two screens to keep in
// step - with the phone's the one people would have kept editing, because it is
// the one anybody could reach.
//
// They are two components rather than one for this page's benefit: MobilePage
// lays its children out in a Flow, so on a tablet two half-width cards sit side
// by side and one tall card could only ever be a column.
//
// Everything downstream - the hero, the chart axis, the hourly list, the day
// strip - is bound to the same Units singleton, so the whole app changes on the
// tap.
//
// ---- the data sources card is GENERATED -------------------------------------
// docs/08-risks.md R12 is "a new provider gets added without its credit", and
// its mitigation is that this card comes out of the provider registry. It does:
// `Engine.sources` is ProviderRegistry::attributions(), and the registry
// REFUSES to hold a provider whose Attribution is incomplete - an uncredited
// provider is not added, and never added means its data cannot reach this
// screen either.
//
// So there is nothing to keep in step here. Registering MET Norway put MET
// Norway on this card; registering the fixture provider put the recording on
// it, saying which afternoon it is.
import QtQuick

MobilePage {
    id: root

    // The row shape for the two cards below. It used to serve four of them -
    // Appearance and Units are PrefGroup/PrefRow now, which is a richer row with
    // a subtitle and a control slot - and it stays for the two that are a name
    // and a value with nothing to explain.
    //
    // Not replaced by PrefRow as well, which was the tempting tidy-up: a place
    // is not a preference. These rows are a list of things the reader added,
    // where the trailing text says which one is home rather than what a control
    // is set to, and a card of them inside a preferences group would say the
    // wrong thing about what they are.
    component SettingRow: Item {
        id: settingRow

        property string text
        property string value
        property bool last: false
        property bool tappable: false

        signal activated()

        width: parent ? parent.width : 0

        // The floor, and it used to be 42. A settings row is a target whose
        // size is its affordance, so two more pixels is the fix and not a
        // workaround for one - which is the same call the metric picker's menu
        // rows make, and the reason they are now the same height.
        height: Theme.metric.hitMin

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: -8
            anchors.rightMargin: -8
            radius: Theme.metric.controlRadius
            color: rowHover.hovered ? Theme.surface.raised : "transparent"
            Behavior on color {
                ColorAnimation { duration: Theme.motion.tint; easing.type: Easing.OutCubic }
            }
        }

        Text {
            text: settingRow.text
            color: Theme.ink.primary
            font.pixelSize: Theme.type.status
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: settingRow.value
            color: Theme.ink.muted
            font.pixelSize: Theme.type.status
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            visible: !settingRow.last
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.line.gridWeak
        }

        HoverHandler {
            id: rowHover
            enabled: settingRow.tappable
            cursorShape: Qt.PointingHandCursor
        }
        TapHandler {
            enabled: settingRow.tappable
            onTapped: settingRow.activated()
        }
    }

    Text {
        width: root.spanWidth(2)
        text: qsTr("Me")
        color: Theme.ink.primary
        font.pixelSize: Theme.type.sectionTitle
        font.bold: true
    }

    PrefGeneral { width: root.spanWidth(1) }

    PrefUnits { width: root.spanWidth(1) }

    MobileCard {
        width: root.spanWidth(1)
        title: qsTr("Places")
        content: Column {
            Repeater {
                model: Engine.places

                delegate: SettingRow {
                    required property int index
                    required property string label
                    required property bool isHome

                    text: label
                    value: isHome ? qsTr("Home") : ""
                    last: index === Engine.places.count - 1
                    tappable: true
                    onActivated: Engine.selectPlace(index)
                }
            }
        }
    }

    MobileCard {
        width: root.spanWidth(1)
        title: qsTr("Data sources")
        content: SourcesList { }
    }

    // What this run is doing, said outright. Under `--fixture` every time on
    // screen is a recorded afternoon's, and a reviewer holding a screenshot
    // deserves to be told that by the screenshot rather than by the command
    // that produced it.
    Text {
        width: root.spanWidth(2)
        visible: Engine.fixtureMode
        text: qsTr("Showing the recorded “%1” fixture at its frozen clock. "
                 + "No network request was made for this forecast.").arg(Engine.fixtureName)
        color: Theme.ink.dim
        font.pixelSize: Theme.type.body
        wrapMode: Text.WordWrap
    }
}
