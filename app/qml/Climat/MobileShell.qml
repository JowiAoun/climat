// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The phone and tablet shell: five destinations under a bottom nav.
//
// The desktop answer to "what is the weather doing" is one tall scrolling page
// - WeatherPage - because a 1340 px window can afford to put the hero, the
// hourly chart and twelve detail cards in the same column and let the reader
// scroll. A phone cannot: the same page at 390 px is roughly eight screens
// deep, and the fourth of them is unreachable in any sense that matters.
//
// So the phone splits the page rather than shrinking it, and the split is the
// reference's: today, the rest of today, the rest of the month, the map, and
// settings. mobiletabs.js is the list.
//
// ---- motion ------------------------------------------------------------------
// The nav pill slides; the page does not transition. That is a decision and
// not an omission.
//
// A fade-through between two full screens is the obvious thing to add here and
// it is the wrong thing to add here. §10.6's rule is that the reader can read
// the component at rest position zero, and the whole content area of a phone
// is the worst possible place to break it: a reader who tapped "Hourly" is
// waiting on the one thing they asked for. The pill is what has to move,
// because the pill is what changed - it is the same object in a new place, and
// watching it travel is what tells you the bar has five positions and you are
// now at the second. The page underneath is not a transition, it is a
// destination.
import QtQuick
import "mobiletabs.js" as Tabs

Item {
    id: root

    property string tab: Tabs.list[0].id

    // ---- how much room this is ----------------------------------------------
    //
    // Which viewport class the app decided on, pushed down from Main. Not
    // derived from this item's width, and that is the whole reason it is a
    // property: `--viewport mobile --size 900x844` means review the phone at
    // 900 px, and a shell that re-derived the class from its own width would
    // give the reviewer a tablet instead. Android pins it too - see Main.
    property string viewportClass: "mobile"

    readonly property string navStyle:
        Viewports.navStyle(root.viewportClass, root.width, root.height)
    readonly property bool railed: navStyle === "rail"

    // Forwarded to whichever page wants them, so `--metric`, `--day`,
    // `--list` and `--poke feels=` still reach the chart now that a screen
    // sits between them and Main. A page that does not have a chart ignores
    // them; the properties live here because the shell outlives the page and
    // a metric chosen on the hourly screen should survive a trip to the map.
    property string metricId: "overview"
    property bool listView: false
    property bool feelsLike: false

    // The day is the exception, and it is not forwarded to anything.
    //
    // The other three are the shell's to remember because nothing else holds
    // them; the day the chart is of belongs to `Data`, which every strip in
    // both shells reads and writes - see DayStrip, which says why that number
    // cannot be kept in two places. This is the entry point `--day` and
    // `--poke day` write to and nothing else, so it pushes and reads back the
    // same way a strip does, and the clamped answer wins.
    //
    // A remembered copy here was actively wrong twice over. It survived a place
    // change, so a shell still holding "5" pushed it back over the new
    // location's today the next time the Hourly tab was built; and its default
    // of 1 was a guess at `Data.todayIndex`, right only for a provider that
    // sends a past day. MET Norway sends none, so on the fallback path the
    // phone opened the Hourly screen on Tomorrow.
    property int dayIndex: Data.selectedDay
    onDayIndexChanged: Data.selectedDay = root.dayIndex
    Binding {
        target: root
        property: "dayIndex"
        value: Data.selectedDay
        restoreMode: Binding.RestoreNone
    }

    // Ambient motion, forwarded the same way. The precipitation field behind
    // the hourly chart is the only thing under this shell that moves when
    // nothing has changed, and `--grab` clears this so a headless frame is the
    // same frame every run - frozen it still draws rain, because precip.js
    // seeds every drop from its hour.
    //
    // It travels with the four above rather than being reached for directly
    // from Main, and for the same reason they do: the screen that owns the
    // chart is destroyed and rebuilt on every tab change, so the shell is the
    // only thing here that a flag parsed once at startup can still address.
    property bool animated: true

    // The place picker, opened by the location bar's chevron and by
    // `--poke picker=1`. On the shell rather than on the page because the sheet
    // has to cover the nav bar too - see the PlacePicker at the bottom of this
    // file.
    property bool pickerOpen: false
    onPickerOpenChanged: picker.open = pickerOpen

    // Scrolling, forwarded to the current page.
    //
    // Write-only, deliberately. Main assigns `contentY` to place the view for
    // a headless grab and reads `maxContentY` to clamp it; making this a live
    // two-way mirror of a Flickable that is destroyed and rebuilt on every tab
    // change buys nothing and costs a binding loop. Reading it back gives you
    // the last value assigned, not where the reader has scrolled to.
    property real contentY: 0
    onContentYChanged: if (pageLoader.item) pageLoader.item.contentY = contentY

    readonly property real maxContentY: pageLoader.item ? pageLoader.item.maxContentY : 0
    function flickBy(velocity) { if (pageLoader.item) pageLoader.item.flickBy(velocity) }

    readonly property var currentTab: Tabs.byId(tab)

    // The page that is up, read-only.
    //
    // It exists so that a test can hold the object and check it is still the
    // same one after the window has been turned, which is the one thing about
    // this shell that cannot be asserted from the outside and is the easiest
    // thing to break: `source` is bound to the tab and to nothing else, and a
    // geometry change that reached it would rebuild the page, replay every
    // card's entrance and throw away the reader's scroll position - for the
    // crime of rotating the device. See tests/qml/tst_shell.qml.
    readonly property Item currentPage: pageLoader.item as Item

    // Placed rather than anchored, because the nav moves. Anchoring the page to
    // `nav.top` was right while there was only one place a nav could be; with a
    // rail the page has to give up width on the left instead of height at the
    // bottom, and a conditional anchor line leaves the anchor from the other
    // branch still attached.
    //
    // `source` is untouched by any of this, and it has to stay that way: it is
    // bound to the current tab and nothing else, so a rotation moves the page
    // and does not rebuild it. A Loader whose source changed on rotation would
    // replay every card's entrance and lose the reader's scroll position for
    // the crime of turning the device.
    Loader {
        id: pageLoader
        x: root.railed ? nav.x + nav.width : 0
        y: 0
        width: root.width - x
        height: root.height - (root.railed ? 0 : nav.height)

        source: root.currentTab.page

        onLoaded: {
            root.pushLayout(item)
            root.adopt(item)
        }

        // The rail swap and every resize. `pushLayout` is a push and not a
        // binding - see below - so nothing arrives at the page unless something
        // calls it, and a tablet turned on its side is exactly the case where
        // the column count and both insets all change at once.
        onWidthChanged: root.pushLayout(item)
        onHeightChanged: root.pushLayout(item)

        onStatusChanged: if (status === Loader.Error)
            console.warn("mobile shell: could not load", source)
    }

    // The status bar and the gesture strip move when the phone turns, and the
    // first answer arrives after the first page has loaded.
    Connections {
        target: WindowInsets
        function onChanged() { root.pushLayout(pageLoader.item) }
    }

    // ---- shell state, page state -------------------------------------------
    // Values are *pushed* down and requests come back up as signals. The
    // obvious alternative - Qt.binding() from the shell into the page - was
    // written first and is quietly broken: the moment the reader touches the
    // control the page assigns its own property, which destroys the binding,
    // and every later push from the shell silently stops arriving. A poke that
    // works until someone clicks the thing first is worse than one that never
    // works, because it passes review.
    //
    // Each property is offered rather than assumed. Assigning one a page does
    // not have is an error in QML, and this is what keeps the map and settings
    // screens from having to declare four chart properties to be loadable.
    function adopt(page) {
        if (page === null)
            return

        push(page)

        if (page.navigate !== undefined)
            page.navigate.connect(root.go)
        if (page.metricRequested !== undefined)
            page.metricRequested.connect(function (id) { root.metricId = id })
        if (page.pickerRequested !== undefined)
            page.pickerRequested.connect(function () { picker.open = !picker.open })
    }

    // Everything about the shape of the page that the shell decides: the chrome
    // that floats over it, in the units the page pads by, and how many columns
    // the room is worth splitting into.
    //
    // A function rather than three `Qt.binding()`s, and the comment above says
    // why: a binding pushed into a page is destroyed the first time the page
    // assigns the property itself, and every later push silently stops. The
    // banner makes that failure reachable in a way the nav never did - the nav's
    // height is fixed, and the banner's changes when it is dismissed, when the
    // severity changes, and when a second alert arrives.
    //
    // Called from onLoaded, from every one of those changes, and from the
    // page's own resize - which is what a rotation is, and where all three of
    // these change at once.
    function pushLayout(page) {
        if (page === null)
            return
        if (page.bottomInset !== undefined)
            page.bottomInset = root.railed ? WindowInsets.bottom : nav.height
        if (page.topInset !== undefined)
            page.topInset = WindowInsets.top
                            + (banner.visible ? banner.height + Theme.metric.mobileGap : 0)
        if (page.viewportClass !== undefined)
            page.viewportClass = root.viewportClass
    }

    function push(page) {
        if (page === null)
            return
        if (page.pickerOpen !== undefined) page.pickerOpen = picker.open
        if (page.metricId !== undefined)  page.metricId = root.metricId
        if (page.listView !== undefined)  page.listView = root.listView
        if (page.feelsLike !== undefined) page.feelsLike = root.feelsLike
        if (page.animated !== undefined)  page.animated = root.animated
    }

    onMetricIdChanged: push(pageLoader.item)
    onListViewChanged: push(pageLoader.item)
    onFeelsLikeChanged: push(pageLoader.item)
    onAnimatedChanged: push(pageLoader.item)

    // A card header's link is a request to go somewhere, not a scroll. The
    // page emits it and the shell is the only thing that knows the tab bar
    // exists.
    function go(id) {
        if (Tabs.indexOf(id) >= 0)
            root.tab = id
    }

    // ---- back ----------------------------------------------------------------
    //
    // Android's back gesture, and this shell is the only thing in the app with
    // a navigation stack to pop. Any tab but the first goes to the first; on
    // the first the event is left unaccepted, which closes the app. That is the
    // convention every Android launcher-facing screen follows, and the
    // alternative - swallowing it on the home screen - leaves a reader holding
    // a gesture that does nothing at all.
    //
    // A sheet outranks a tab: the picker and the alert sheet cover the nav bar,
    // so back has to close what is on top before it moves what is underneath.
    //
    // `Keys.onPressed` and a Qt.Key_Back test rather than `Keys.onBackPressed`,
    // which is the Qt 5 spelling: it is deprecated, and it only ever fired for
    // the hardware key that handsets stopped shipping years ago.
    //
    // The PRESS, and that is not arbitrary - it was `onReleased` first, and the
    // picker's own `Keys.onEscapePressed` accepts the press and leaves the
    // release unaccepted. So one tap of Escape closed the sheet on the way down
    // and changed the tab on the way up: two navigations from one key, and the
    // second one invisible in a screenshot. Handling the same stage the sheets
    // handle is what makes "accepted" mean anything between them.
    focus: true
    Keys.onPressed: function (event) {
        if (event.key !== Qt.Key_Back && event.key !== Qt.Key_Escape)
            return
        if (sheet.open) {
            sheet.open = false
        } else if (picker.open) {
            picker.open = false
        } else if (root.tab !== Tabs.list[0].id) {
            root.tab = Tabs.list[0].id
        } else {
            return      // unaccepted: on Android this closes the app
        }
        event.accepted = true
    }

    // ---- the alert banner ---------------------------------------------------
    //
    // HERE, on the shell, and not on any of the five pages. MobileShell destroys
    // and rebuilds its page on every tab change, so a per-page banner would be
    // constructed five times in a session - re-running its reveal each time,
    // and losing a dismissal on every tap of the nav bar. It also has to be
    // visible from all five destinations, which a page cannot arrange.
    //
    // It floats over the page rather than displacing it: every mobile page
    // paints its own sky, and a banner that pushed the page down would leave a
    // strip of window background above the gradient. `topInset` is how the page
    // moves its content out from under it.
    AlertBanner {
        id: banner
        x: pageLoader.x + Theme.metric.mobileMargin
        y: WindowInsets.top + Theme.metric.mobileMargin
        width: pageLoader.width - Theme.metric.mobileMargin * 2
        onOpened: sheet.open = true

        onVisibleChanged: root.pushLayout(pageLoader.item)
        onHeightChanged: root.pushLayout(pageLoader.item)
    }

    // Over the nav bar as well as over the page, and that is the reason it is
    // here rather than on the page: a picker that a tab bar could be tapped
    // through is a picker that can be left half-open behind another screen.
    PlacePicker {
        id: picker
        onDismissed: open = false
        onOpenChanged: {
            root.pickerOpen = open
            root.push(pageLoader.item)
            // Focus comes back when the sheet goes. The picker takes it on the
            // way in - its search field calls forceActiveFocus() so you can
            // type a place name immediately - and without this it keeps it on
            // the way out, on a field nobody can see any more. Every key the
            // shell handles is dead from then on, which on Android is the back
            // gesture.
            if (!open)
                root.forceActiveFocus()
        }
    }

    // Over the nav bar too, for the picker's reason.
    AlertSheet {
        id: sheet
        onDismissed: open = false
        onOpenChanged: if (!open) root.forceActiveFocus()
    }

    // Bottom bar or left rail, and one component either way - see ShellNav for
    // why that is not two files. Placed with numbers rather than anchors so
    // that turning the device moves it instead of leaving it anchored to an
    // edge the other arrangement does not use.
    ShellNav {
        id: nav
        orientation: root.railed ? Qt.Vertical : Qt.Horizontal
        // A rail keeps clear of the status bar above it, the gesture strip
        // below it and a camera cutout beside it. A bar's own padding takes
        // the strip; see ShellNav.
        x: root.railed ? WindowInsets.left : 0
        y: root.railed ? WindowInsets.top : root.height - height
        width: root.railed ? implicitWidth : root.width
        height: root.railed ? root.height - WindowInsets.top - WindowInsets.bottom
                            : implicitHeight
        currentId: root.tab
        onSelected: function (id) { root.tab = id }
    }
}
