// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The card every tile is drawn on, and the four states none of them may skip.
//
// A widget file declares what it draws and nothing else - no subscription, no
// staleness rule, no empty state:
//
//     WidgetSurface {
//         widgetId: "uv-dial"
//         ...one Item that draws a dial...
//     }
//
// The subscription comes from the catalogue entry for `widgetId`, so a
// widget's contract with the daemon is `widgets/catalogue.json` and not a field
// list repeated in QML. Change what a tile shows and you change one file.
//
// ============================================================================
// THE FOUR STATES, IN THE ORDER THEY OUTRANK EACH OTHER
//
//   1. INCOMPATIBLE   the daemon speaks a schema this build cannot read. The
//                     only state where the tile refuses to draw, because
//                     everything under it would be a guess about a shape.
//
//   2. WAITING        nothing has arrived yet. A quiet skeleton, no numbers.
//                     Never a zero, never a dash where a reading will go -
//                     docs/README.md ranks not fabricating above everything
//                     else, and a brief lie is still the thing that rule is
//                     about.
//
//                     The skeleton means "a snapshot is on its way" and it is
//                     only ever shown when one is. When nothing is coming -
//                     no daemon, no place, a daemon that has stopped answering
//                     - the tile says which, in `feed.waitingReason`.
//
//                     That distinction is the whole of this state and it was
//                     missing: four skeletons on a desktop, no message on
//                     screen and none in the journal, for the most ordinary
//                     situation there is. "Loading forever" is a lie of the
//                     same family as a fabricated number, told with a picture
//                     rather than a digit.
//
//   3. STALE          there is data and it is not current: the daemon went
//                     away, or it is serving from its cache. The tile draws
//                     everything it has and says how old it is. This is the
//                     state the whole design exists for and it must never
//                     become state 2 - a tile that blanks when the daemon
//                     restarts is worse than one that is ten minutes behind.
//
//   4. LIVE           the footer says nothing at all. An "updated just now" on
//                     every tile all day is a line the eye stops reading, and
//                     then it stops reading it on the day it says 40 minutes.
//
// ============================================================================
// WHY THE FOOTER IS INSIDE THE CARD AND NOT A ROW UNDER IT
//
// Because the shell adopts one window and the user resizes it. A footer that
// appears when a reading goes stale must not change the tile's height, or a
// desktop reflows itself the moment the network hiccups.

import QtQuick

import "wire.js" as Wire

Rectangle {
    id: root

    // ---- what this tile is -------------------------------------------------

    required property string widgetId

    property string place: WidgetOptions.place

    // The catalogue entry. Everything below reads its size and its field list
    // from here; nothing restates them.
    readonly property var spec: DaemonLink.widget(root.widgetId)

    property string title: Wire.obj(root.spec).title !== undefined
                           ? Wire.obj(root.spec).title : root.widgetId

    // ---- what a widget body reads ------------------------------------------

    readonly property alias feed: feed
    readonly property var snap: feed.snapshot

    // True when there is something real to draw. A body is instantiated
    // regardless and simply gets no numbers until this is true, because
    // rebuilding the whole tile on the first snapshot would animate every
    // reveal in it a second time.
    readonly property bool ready: feed.hasData && DaemonLink.incompatibility === ""

    readonly property bool stale: feed.hasData
                                  && (feed.state !== "live" || !DaemonLink.available)

    // The one line this tile shows instead of a body, or "" when it has one to
    // show. Both states that replace a body come through here, in the order the
    // header ranks them: a shape this build cannot read outranks everything,
    // and a reason to be empty only applies while the tile is empty.
    //
    // Written as one property rather than as two visibilities so that the
    // ranking is stated once. Two `visible:` expressions that each have to
    // exclude the other is how a tile ends up drawing both.
    readonly property string message:
        DaemonLink.incompatibility !== "" ? DaemonLink.incompatibility
                                          : (root.ready ? "" : feed.waitingReason)

    // Where a body puts its children.
    default property alias content: body.data

    // ---- the card ----------------------------------------------------------

    implicitWidth: Wire.arr(Wire.obj(root.spec).size).length === 2
                   ? Wire.obj(root.spec).size[0] : 240
    implicitHeight: Wire.arr(Wire.obj(root.spec).size).length === 2
                    ? Wire.obj(root.spec).size[1] : 140

    // ---- a tile carries its own page ---------------------------------------
    //
    // The app's card is `Theme.surface.base`, which is a 7 % white wash - it is
    // a *lift* off the page behind it, not a colour. That works everywhere in
    // the app because there is always a page behind it. On a desktop there is
    // not: there is a wallpaper the user chose, and nothing this process gets
    // to know about.
    //
    // Rendered as-is, a dark-mode tile came out as 7 % white over a photograph
    // with white text on top. Legible over some wallpapers, invisible over the
    // rest, and impossible to fix from the theme because the theme is right -
    // the card token means what it says.
    //
    // So the tile paints the page itself. `Theme.page.bg` is the app's own
    // background colour, at 92 %: enough of the wallpaper survives that a tile
    // still looks like it belongs to the desktop, and the text on top has the
    // same contrast it has in the app because it is sitting on the same colour.
    color: Qt.alpha(Theme.page.bg, 0.92)
    radius: Theme.metric.cardRadius

    // The hairline is a light-mode exception in the app and is unconditional
    // here. docs/10-design-system.md §10.1 bans borders because contrast
    // against the page defines a card - which is exactly the premise a
    // wallpaper removes. A tile has to have an edge of its own or it has none.
    border.width: 1
    border.color: Theme.line.card

    // The card wash, on top of the page the tile brought with it. Same token,
    // same 7 %, same result as in the app.
    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: root.radius - 1
        color: Theme.surface.base
    }

    // ---- the subscription --------------------------------------------------

    WidgetFeed {
        id: feed
        place: root.place
        fields: Wire.arr(Wire.obj(root.spec).fields)
        hours: Wire.numOr(Wire.obj(root.spec).hours, 0)
        days: Wire.numOr(Wire.obj(root.spec).days, 0)
    }

    // ---- header ------------------------------------------------------------

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.metric.cardPadding
        anchors.rightMargin: Theme.metric.cardPadding
        anchors.topMargin: Theme.metric.cardPadding * 0.7
        height: titleText.implicitHeight

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.ink.muted
            font.pixelSize: Theme.type.label
            font.letterSpacing: 0.4
        }

        // The place, when there is one on the wire. A tile is often the only
        // thing on a desktop that says which city it is talking about, and a
        // second place added later must not silently change what the first one
        // means.
        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: titleText.right
            anchors.leftMargin: 8
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            textFormat: Text.PlainText
            text: Wire.obj(Wire.at(root.snap, "place")).name !== undefined
                  ? Wire.obj(Wire.at(root.snap, "place")).name : ""
            color: Theme.ink.dim
            font.pixelSize: Theme.type.label
            visible: text !== ""
        }
    }

    // ---- the body ----------------------------------------------------------

    Item {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.metric.cardPadding
        anchors.rightMargin: Theme.metric.cardPadding
        anchors.topMargin: 4
        anchors.bottomMargin: Theme.metric.cardPadding * 0.7

        opacity: root.ready ? 1 : 0
        Behavior on opacity {
            NumberAnimation {
                duration: Theme.motion.tint
                easing.type: Theme.motion.easing
            }
        }
    }

    // ---- state 2: waiting --------------------------------------------------
    //
    // Three bars at the weights a reading, a caption and a series would have
    // had. Not a spinner, because a spinner is a claim about work in progress
    // and this is a claim about a request in flight - which, now that `message`
    // covers the case where there is no request, is a claim that is true
    // whenever these bars are on screen.

    Column {
        anchors.left: body.left
        anchors.top: body.top
        anchors.topMargin: 6
        spacing: 8
        visible: !root.ready && root.message === ""

        Rectangle {
            width: Math.min(96, body.width * 0.5)
            height: 18
            radius: 4
            color: Theme.surface.raised
        }
        Rectangle {
            width: Math.min(150, body.width * 0.8)
            height: 10
            radius: 3
            color: Theme.surface.raised
            opacity: 0.7
        }
        Rectangle {
            width: body.width
            height: 10
            radius: 3
            color: Theme.surface.raised
            opacity: 0.45
        }
    }

    // ---- state 1, and the half of state 2 that has something to say ---------
    //
    // One Text for both, because they are the same thing on screen: a sentence
    // where a body would have been. Which sentence is `message`'s decision, up
    // at the top of this file with the other state.
    //
    // Vertically centred rather than top-aligned. It is the tile's whole
    // content, and the smallest one in the catalogue is 110 px tall - a line of
    // text under the title with a drop of empty space beneath it reads as a
    // tile that failed to finish drawing, which is the impression this line
    // exists to correct.
    Text {
        anchors.fill: body
        visible: root.message !== ""
        text: root.message
        color: Theme.ink.muted
        font.pixelSize: Theme.type.label
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
    }

    // ---- state 3: stale ----------------------------------------------------
    //
    // Bottom-right, over the body rather than beside it, so that appearing and
    // disappearing never changes the tile's height. It is dim on purpose: this
    // is a caveat on the number above it, not a second reading.

    // A scrim under it, because on a twelve-column hourly strip the bottom-right
    // corner already has a temperature in it. Without one the two texts overlap
    // and the tile reads as broken at exactly the moment it is trying to say
    // something careful about its data.
    Rectangle {
        anchors.fill: age
        anchors.margins: -3
        radius: 3
        color: Theme.overlay.readout
        visible: age.visible
    }

    Text {
        id: age
        anchors.right: body.right
        anchors.bottom: body.bottom
        visible: root.stale && text !== ""
        // `feed.ageMinutes` is re-notified once a minute by the one timer
        // DaemonLink owns, so this binding re-evaluates on its own. That is
        // what makes a tile whose daemon died keep counting up rather than
        // freezing on whatever number it was showing when the bus went quiet.
        text: {
            if (DaemonLink.source === "file")
                return qsTr("recorded")
            var age = Wx.ago(feed.ageMinutes)
            return age === "" ? qsTr("no reading") : age
        }
        color: Theme.ink.dim
        font.pixelSize: Theme.type.axis
    }
}
