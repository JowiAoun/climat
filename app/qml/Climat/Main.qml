// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The window, and deliberately almost nothing else.
//
// This file used to be five hundred lines, and four hundred of them were an
// argv scraper and five capture timers. Those are C++ now - AppOptions parses
// the command line, Settings remembers what should outlive the process, and
// ScreenshotController owns the shutters. What is left is the product: a
// backdrop, a shell, and the two rules that choose the shell.
import QtQuick
import QtQuick.Window

Window {
    id: win

    visible: true
    width: 1340
    height: 762

    // A phone is 390 px wide, so the old 680 floor would have made the layout
    // this window exists to show unreachable by dragging. The floor now only
    // has to keep the narrowest screen readable.
    minimumWidth: 360
    minimumHeight: 480
    color: Theme.page.bg
    title: qsTr("Climat")

    // ---- which shell -------------------------------------------------------
    // The window's width decides, and Viewports owns the thresholds so the
    // gallery frames a specimen at exactly the width the app would switch at.
    // `--viewport mobile` pins it and resizes to the preset, which is what a
    // headless grab needs: a shell chosen from a width that a screenshot flag
    // then changes is a shell chosen from the wrong width.
    property string forcedViewport: ""

    // A handheld is never a desktop, whatever its width. A tablet held in
    // landscape is 1112 px across - past the desktop threshold - and the
    // desktop page is the wrong answer for it twice over: a hover crosshair
    // nobody can hover, and a twelve-card grid of 300 px cards laid out for a
    // pointer. `Viewports.classOf` cannot know this, because a 1112 px window
    // on a laptop *is* a desktop and the geometry is identical. The platform
    // is the signal the geometry does not carry.
    readonly property bool handheld:
        Qt.platform.os === "android" || Qt.platform.os === "ios"

    readonly property string viewportClass: {
        if (forcedViewport !== "")
            return forcedViewport
        var byWidth = Viewports.classOf(win.width)
        return win.handheld && byWidth === "desktop" ? "tablet" : byWidth
    }
    readonly property bool mobile: Viewports.usesMobileShell(viewportClass)

    // ---- the sky -----------------------------------------------------------
    // The background follows the clock: a deep blue by day, a starred indigo at
    // night, warmed at the two crossings. Or it does not, and holds at `dusk` -
    // the palette this prototype has always had - which is what
    // `Settings.dynamicBackground` switches between and what the preferences
    // screen's first row is.
    //
    // Clock owns what time it is and which of the four phases that falls in, so
    // that the component gallery's window - which paints this same backdrop -
    // reads the hour from the same place rather than from its own copy of the
    // same three arguments.
    //
    // ---- three sources, and the order is the policy ------------------------
    //
    //   1. --sky, which wins outright
    //   2. a capture with no --sky, which is pinned to what this app did before
    //      the preference existed
    //   3. the reader's own choice
    //
    // Rule 2 is the same rule `resolvedScheme` states below, for the same
    // reason and with the same cost. A golden image is taken with whatever is in
    // the INI of the machine that took it, so a capture that read this
    // preference would be a picture that changes when a developer flips a switch
    // - the failure would appear in CI, on an unrelated pull request, with
    // nothing in the diff to explain it. The scheme has been pinned under
    // `--grab` since the day the theme landed; this is that decision applied to
    // the other half of the backdrop.
    //
    // What it costs is that `--grab` cannot photograph the preference switched
    // off. `--sky dusk` is that photograph, which is why rule 1 outranks
    // everything: the flag that pins a phase is the same flag a reviewer uses to
    // see what pinning looks like.
    readonly property bool dynamicSky:
        AppOptions.capturing ? win.mobile : Settings.dynamicBackground

    readonly property string skyPhase:
        AppOptions.sky !== "" ? AppOptions.sky
                              : (win.dynamicSky ? Clock.skyPhase : "dusk")

    // ---- the colour scheme -------------------------------------------------
    //
    // Pushed into the Theme singleton rather than read out of it, because Theme
    // is where every component already looks and adding a second place to ask
    // would mean two answers.
    //
    // Four sources, and the order is the whole of the policy:
    //
    //   1. --scheme, which wins outright
    //   2. a capture with no --scheme, which is pinned to dark
    //   3. the user's own choice, when they made one
    //   4. the desktop's preference, and dark under that
    //
    // Rule 2 is the one worth defending. `Settings.appearance` defaults to
    // "system", so without it a golden image would come out in whatever theme
    // the machine that ran CI happened to be in - the same image passing on one
    // runner and failing on the next, for a reason nothing in the diff would
    // show. A capture already restores no window geometry for exactly this
    // reason; the scheme is the same argument about a different property.
    readonly property string resolvedScheme: {
        if (AppOptions.scheme !== "")
            return AppOptions.scheme
        if (AppOptions.capturing)
            return "dark"
        if (Settings.appearance === "light" || Settings.appearance === "dark")
            return Settings.appearance
        return SystemAppearance.colorScheme
    }

    // A Binding rather than an assignment, because the desktop can change its
    // mind while the app is open - that is the entire point of subscribing to
    // the portal - and an assignment made once at startup would leave this
    // window the only one on the screen still dark at sunrise.
    Binding {
        target: Theme
        property: "scheme"
        value: win.resolvedScheme
    }

    // How much of the sky to build.
    //
    // A handheld by default, because that is where the cost is: 130 Rectangles
    // and 12 Shapes constructed before a phone's first night frame, on hardware
    // with a fraction of a desktop's fill rate. `--perf` overrides it, which is
    // the only way the reduced tier can be reviewed or photographed on a
    // machine that is not a phone.
    Binding {
        target: Theme
        property: "perfTier"
        value: AppOptions.perf !== "" ? AppOptions.perf
                                      : (win.handheld ? "reduced" : "full")
    }

    // Stillness is two unrelated requests that happen to want the same thing.
    // A still capture holds still so the shutter cannot catch a transition -
    // --film is exempt, since a contact sheet of eight identical frames is not
    // a review of anything. And a reader who has asked their desktop for less
    // movement has asked this window too; precipitation's standing animation is
    // the first thing that has to stop.
    Binding {
        target: Theme
        property: "stillness"
        value: (AppOptions.grab !== "" && AppOptions.film === "")
               || SystemAppearance.reduceMotion
    }

    // ---- what the alert poll is told -----------------------------------------
    //
    // AlertsData's schedule - three minutes focused, ten idle, stopped when
    // hidden - is only a schedule if something tells it which of those the
    // window is in. Nothing did. The view model's defaults are "visible and
    // focused", so from the day alerts landed the app polled every three
    // minutes from launch to quit, minimised or not: precisely the bill
    // alertsdata.h's arithmetic says it refuses to run up, and in Canada, where
    // nothing revalidates, several megabytes a day for a window in the dock.
    //
    // Pushed rather than read, for the reason the view model gives - it can be
    // tested without a QWindow. Minimised counts as hidden: a window in the
    // dock is a window nobody is looking at, and it is the case that costs.
    readonly property bool exposed:
        win.visibility !== Window.Hidden && win.visibility !== Window.Minimized

    onExposedChanged: Alerts.setWindowState(win.exposed, win.active)
    onActiveChanged: Alerts.setWindowState(win.exposed, win.active)

    // The page background is painted as an item, not left to Window.color.
    // grabToImage() captures contentItem, which does not include the window's
    // clear colour - so every headless screenshot came out with a black page
    // behind the cards, which is not what is on screen.
    // The starfield is still the phone's alone, and that is a separate question
    // from which phase the gradient is in. A 1340 px window is mostly cards -
    // the background is a rim around them - and the reader never sees enough of
    // it for a constellation to be anything but noise behind a chart. A phone is
    // the opposite: the hero sits directly on the sky with no card at all. So a
    // desktop that has followed the clock into the night gets the night
    // gradient and no stars, which is the arrangement `--sky night` has always
    // produced on the desktop for anyone who asked for it, and `desktop-night`
    // is the golden image of it.
    PageBackdrop {
        anchors.fill: parent
        phase: win.skyPhase
        stars: win.mobile || AppOptions.sky !== ""
    }

    // The app itself. Two shells, one product: `WeatherPage` is the desktop's
    // single scrolling column, `MobileShell` is the phone's five tabs under a
    // nav bar, and which one runs is a function of the window width and nothing
    // else - there is no "mobile build".
    //
    // A Loader rather than both in the tree with one hidden. Keeping both would
    // build five phone screens on every desktop launch to show none of them.
    Loader {
        id: shellLoader
        anchors.fill: parent
        sourceComponent: win.mobile ? mobileShell : desktopShell
    }

    Component {
        id: desktopShell
        WeatherPage { }
    }

    Component {
        id: mobileShell
        // Assigned on completion rather than bound through the constructor:
        // MobileShell's default tab is mobiletabs.js's first entry, and naming
        // that entry a second time out here is how the two come to disagree.
        //
        // `viewportClass` IS bound, because it is not a starting value - it is
        // the answer to a question the window keeps being asked. Dragging a
        // window from 834 to 1112 px changes it, and so does turning a tablet.
        MobileShell {
            viewportClass: win.viewportClass
            Component.onCompleted: if (AppOptions.tab !== "") tab = AppOptions.tab
        }
    }

    // The shutters. Everything it needs is bound - the scene it photographs and
    // the flags that say what to photograph - so it still points at the right
    // shell after `--viewport mobile` has swapped one for the other, and so it
    // does not have to know that AppOptions is where this app's flags happen to
    // live. climat-gallery binds the same properties off a different parser.
    //
    // `gallery` is left unbound: there is no gallery in this executable any
    // more, which is the whole point of the commit that moved it out.
    ScreenshotController {
        id: capture
        window: win
        shell: shellLoader.item as Item
        mobile: win.mobile

        grab:   AppOptions.grab
        film:   AppOptions.film
        frames: AppOptions.frames
        every:  AppOptions.every

        pokes:  AppOptions.pokes
        scroll: AppOptions.scroll
        metric: AppOptions.metric
        day:    AppOptions.day
        list:   AppOptions.list
    }

    // ---- window geometry ---------------------------------------------------
    // Three sources, and the order between them is the whole design:
    //
    //   1. a remembered size, weakest, and only when nothing else spoke
    //   2. --viewport's preset
    //   3. --size, which overrides the preset so `--viewport mobile --size
    //      390x1600` grabs a tall phone rather than fighting over the window
    //
    // A capture restores nothing at all. A golden image whose size depends on
    // where the developer last left the window is not a golden image, and the
    // symmetry matters as much as the rule: a run that did not restore does not
    // save either, so `--grab` cannot quietly rewrite the size the reader will
    // get next time they open the app.
    readonly property bool geometryFromFlags:
        AppOptions.hasSize || AppOptions.viewport !== ""
    readonly property bool geometryRemembered:
        !AppOptions.capturing && !win.geometryFromFlags

    Component.onCompleted: {
        // The scheme and stillness are not set here. They are Bindings above,
        // which apply during this object's completion and so are in force
        // before the first frame - and, unlike the assignments they replaced,
        // stay in force when the desktop changes underneath them.
        // The poll schedule's starting state, belt and braces.
        //
        // `onExposedChanged` does in fact fire during this window's own
        // completion, before this line runs - measured, against the offscreen
        // platform, after an earlier version of this comment claimed otherwise
        // - so on this Qt the push has already happened. It is kept because
        // that is a property of when Qt happens to evaluate the binding rather
        // than a promise, and the cost of being wrong about it is a poll that
        // never starts.
        Alerts.setWindowState(win.exposed, win.active)

        // The status bar and the gesture strip, on a phone that draws this
        // window under them. Zero on a desktop.
        WindowInsets.watch(win)

        if (win.geometryRemembered && Settings.hasWindowSize) {
            // Size only. Position is stored, and restoring it is a lie on
            // Wayland - the compositor places windows, and a client that
            // assigns x/y there is assigning to nothing.
            win.width = Settings.windowWidth
            win.height = Settings.windowHeight
        }

        var preset = AppOptions.viewport !== "" ? Viewports.byId(AppOptions.viewport) : null
        if (preset !== null) {
            // The preset's CLASS, not its id. Three of the four are the same
            // word; `tablet-landscape` is the one that is not, and forcing its
            // id would put a class nothing has heard of into every layout
            // question the shell asks.
            win.forcedViewport = preset.cls
            win.width = preset.w
            win.height = preset.h
        } else if (AppOptions.viewport !== "") {
            // The parser accepted an id Viewports has never heard of, which
            // means the two lists have drifted. Say so here rather than opening
            // a desktop window and letting the flag look inert.
            console.warn("viewport:", AppOptions.viewport, "is not in Viewports.presets")
        }

        if (AppOptions.hasSize) {
            win.width = AppOptions.sizeWidth
            win.height = AppOptions.sizeHeight
        }

        // Last, and that is the point: every flag above can change which shell
        // is loaded, and a state flag applied to a shell that is about to be
        // destroyed looks exactly like a flag that does nothing.
        capture.start()
    }

    Component.onDestruction:
        if (win.geometryRemembered)
            Settings.saveWindowGeometry(win.x, win.y, win.width, win.height)
}
