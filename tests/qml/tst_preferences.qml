// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The preferences screen: the two things about it a picture cannot check.
//
// The golden images cover what it looks like - `desktop-preferences` on the
// sheet, `mobile-me` and `tablet-me` inline. What they cannot cover is that the
// controls are *wired*: a switch bound to a preference and a switch bound to
// nothing photograph identically, and so do a row that writes the setting and a
// row that writes its own copy of it.
//
// That second failure is the one this file exists for, and it is not
// hypothetical. PrefSwitch deliberately does not toggle itself, because
// assigning to `checked` would destroy the binding to the preference - after
// which the control shows its own state for ever and the screen keeps working
// right up until something else writes the setting. The unit preset is exactly
// that something else: `Units.applySystem` writes five preferences at once, and
// a row whose binding had been destroyed would not follow.
//
// So: tap things, then ask Settings what it holds, then write Settings and ask
// the controls what they show. Both directions, because only one of them is
// broken by the mistake.
import QtQuick
import QtQuick.Window
import QtTest
import Climat

TestCase {
    name: "Preferences"
    when: windowShown

    // A window of this file's own, shown, as tst_hittargets has. Two reasons,
    // and the second one cost an afternoon.
    //
    // `Item.visible` is effective visibility, so an item in a view that was
    // never shown reports false all the way down and every assertion about a
    // control being drawn would be an assertion about the harness.
    //
    // And a click has to have somewhere to land. Built into the TestCase's own
    // item instead - the arrangement tst_shell uses, which is fine there because
    // a KEY goes to whatever has focus and does not care where it is - every
    // mouseClick in this file hit nothing. Measured, not deduced: with that host
    // all three click tests failed with the setting unchanged, and with this one
    // they pass. tst_hittargets reached the same conclusion from the other side
    // and its header says why a shown window is not optional.
    Window {
        id: host
        width: 900
        height: 800
        visible: true

        Item {
            id: stage
            anchors.fill: parent
        }
    }

    property var built: []

    function build(type, props) {
        var c = Qt.createComponent("Climat", type)
        verify(c !== null && c.status !== Component.Error,
               c === null ? "no such type" : c.errorString())
        var o = c.createObject(stage, props)
        verify(o !== null, type + " did not instantiate")
        built.push(o)
        wait(0)
        return o
    }

    // Every test starts from the shipped defaults. The settings file is process
    // wide, so without this the suite's result would depend on its order - and
    // on what the developer last left in the build tree's XDG_CONFIG_HOME.
    function init() {
        Settings.clockFormat = "12h"
        Settings.dynamicBackground = true
        Settings.appearance = "system"
        Units.applySystem("metric")
    }

    function cleanup() {
        for (var i = 0; i < built.length; ++i)
            if (built[i])
                built[i].destroy()
        built = []
    }

    // ---- the row is the button ----------------------------------------------
    //
    // Not the switch. Tapping the words has to do what tapping the control does,
    // which is the arrangement every platform's settings list uses and the one
    // PrefRow's `activated()` exists for.
    function test_tappingAnywhereOnTheRowChangesTheSetting() {
        var group = build("PrefGeneral", { width: 520 })
        var row = group.children[group.children.length - 1]

        // The rows are the group's Column's children; walk to the first one,
        // which is Dynamic background. Found by title rather than by index so
        // that adding a row above it does not silently move this test onto a
        // different setting.
        var target = null
        for (var i = 0; i < group.children.length; ++i) {
            var col = group.children[i]
            for (var j = 0; col.children !== undefined && j < col.children.length; ++j)
                if (col.children[j].title === "Dynamic background")
                    target = col.children[j]
        }
        verify(target !== null, "no Dynamic background row")

        compare(Settings.dynamicBackground, true)

        // Left of centre, which is over the words and not over the switch.
        mouseClick(target, 40, target.height / 2)
        compare(Settings.dynamicBackground, false)

        mouseClick(target, 40, target.height / 2)
        compare(Settings.dynamicBackground, true)
    }

    // ---- the phone is told what it will not get -----------------------------
    //
    // docs/known-gaps.md's one condition for shipping Android without
    // background alerts: the settings screen has to say so. On a desktop the
    // row does not exist, so a picture of the desktop cannot check it, and a
    // picture of a phone would need a phone.
    function findRow(group, title) {
        for (var i = 0; i < group.children.length; ++i) {
            var col = group.children[i]
            for (var j = 0; col.children !== undefined && j < col.children.length; ++j)
                if (col.children[j].title === title)
                    return col.children[j]
        }
        return null
    }

    function test_aHandheldIsToldWarningsArriveOnlyWhileTheAppIsOpen() {
        var desktop = build("PrefGeneral", { width: 520, handheld: false })
        var row = findRow(desktop, "Severe weather warnings")
        verify(row !== null, "no Severe weather warnings row")
        compare(row.visible, false)
        compare(row.height, 0)

        var phone = build("PrefGeneral", { width: 520, handheld: true })
        row = findRow(phone, "Severe weather warnings")
        verify(row !== null, "no Severe weather warnings row")
        // Only where the notification switch is absent. Where it is present the
        // switch's own subtitle says what happens off screen, and two rows
        // about the same thing would contradict each other on a build that
        // had both.
        compare(row.visible, !Engine.notificationsAvailable)
        if (row.visible) {
            verify(row.height > 0)
            verify(row.subtitle.indexOf("while Climat is open") !== -1, row.subtitle)
        }
    }

    // ---- the control is bound, not stateful ---------------------------------
    //
    // Written from the outside, the way `applySystem` writes it, and the switch
    // has to follow. A control that toggled itself would have destroyed this
    // binding on the click above and would now be showing the wrong thing.
    function test_theSwitchFollowsAWriteItDidNotMake() {
        // A real binding, not `createObject(..., { checked: … })` - that is an
        // assignment, and asserting against it would prove only that a property
        // holds what it was set to. Every call site in the app writes the
        // binding; Qt.binding is how a test writes the same thing.
        var sw = build("PrefSwitch", {})
        sw.checked = Qt.binding(function () { return Settings.dynamicBackground })
        compare(sw.checked, true)

        Settings.dynamicBackground = false
        compare(sw.checked, false)

        Settings.dynamicBackground = true
        compare(sw.checked, true)
    }

    // ---- the preset writes all five, and the rows follow --------------------
    function test_thePresetReachesEveryUnitRow() {
        var group = build("PrefUnits", { width: 520 })

        compare(Units.system, "metric")
        compare(Units.bareSymbol(Units.Temperature), "°C")
        compare(Units.bareSymbol(Units.Wind), "km/h")

        Units.applySystem("imperial")

        compare(Units.system, "imperial")
        compare(Units.bareSymbol(Units.Temperature), "°F")
        compare(Units.bareSymbol(Units.Wind), "mph")
        compare(Units.bareSymbol(Units.Precipitation), "in")

        // And the override, which is the row promoted out of the five. Turning
        // it off leaves the other four imperial, which is `custom`.
        Settings.precipitationUnit = "mm"
        compare(Units.system, "custom")
        compare(Units.bareSymbol(Units.Temperature), "°F")
    }

    // ---- the segment ---------------------------------------------------------
    //
    // A value that is none of the options selects nothing - index -1 - rather
    // than falling back to the first cell. That is the `custom` units state, and
    // a segment that rounded it to "metric" would be a control claiming a
    // preference the reader does not have.
    function test_theSegmentSelectsNothingForAValueItDoesNotHave() {
        var seg = build("PrefSegment", {
            options: [{ id: "24h", label: "24 hour" }, { id: "12h", label: "AM / PM" }],
            currentId: "12h"
        })
        compare(seg.currentIndex, 1)

        seg.currentId = "24h"
        compare(seg.currentIndex, 0)

        seg.currentId = "beats"
        compare(seg.currentIndex, -1)

        // And it has a width before anything is clicked. `cellWidth` is assigned
        // out of remeasure() rather than bound - see PrefSegment - so a broken
        // trigger would leave the whole control zero wide and invisible, which
        // is a failure a golden image of a *sheet* would show as a gap nobody
        // could name.
        verify(seg.cellWidth > 0, "the segment never measured its labels")
        verify(seg.width > seg.cellWidth, "the segment is narrower than one cell")
    }

    function test_tappingASegmentCellEmitsTheIdRatherThanTheIndex() {
        var seg = build("PrefSegment", {
            options: [{ id: "24h", label: "24 hour" }, { id: "12h", label: "AM / PM" }],
            currentId: "12h"
        })

        var chosen = ""
        seg.selected.connect(function (id) { chosen = id })

        mouseClick(seg, seg.cellWidth / 2, seg.height / 2)
        compare(chosen, "24h")

        // It does not move itself. Same rule as the switch, and the same reason:
        // `currentId` is bound to a preference at every call site.
        compare(seg.currentId, "12h")
    }

    // ---- the group's last row is not ruled ----------------------------------
    //
    // A hairline against the bottom edge of a card is a second card edge one
    // pixel inside the first. PrefGroup decides this rather than the caller, so
    // the assertion is that it decided - a group that never ran markLast() looks
    // right in every screenshot taken before somebody adds a row.
    function test_theGroupUnrulesItsLastRowAndOnlyItsLast() {
        var group = build("PrefUnits", { width: 520 })

        var rows = []
        for (var i = 0; i < group.children.length; ++i) {
            var col = group.children[i]
            for (var j = 0; col.children !== undefined && j < col.children.length; ++j)
                if (col.children[j].ruled !== undefined)
                    rows.push(col.children[j])
        }

        verify(rows.length > 2, "PrefUnits should have more rows than this")

        // The bottom row by GEOMETRY, not by position in `children`. Those two
        // orders are the same today and a Repeater is what could separate them:
        // its delegates are inserted into the parent's child list rather than
        // appended, so a group that mixed a Repeater with declared rows could
        // have a "last child" that is not the last row on screen. Asserting
        // against the same order the group iterates would be asserting that a
        // loop agrees with itself.
        var bottom = rows[0]
        for (var k = 1; k < rows.length; ++k)
            if (rows[k].y > bottom.y)
                bottom = rows[k]

        for (var m = 0; m < rows.length; ++m)
            compare(rows[m].ruled, rows[m] !== bottom,
                    "row \"" + rows[m].title + "\" at y=" + rows[m].y)
    }

    // ---- the licences travel with the app -----------------------------------
    //
    // Qt's LGPL asks for its text to go with every copy, and a phone gets only
    // what is inside the package. So the two rows unfold the copies built into
    // the app: a tap has to show text, and it has to be the licence it names.
    function test_theLicencesUnfoldFromTheCopiesBuiltIn() {
        var group = build("PrefAbout", { width: 520 })

        verify(Engine.licenceText.indexOf("GNU GENERAL PUBLIC LICENSE") === 0,
               Engine.licenceText.slice(0, 80))
        verify(Engine.qtLicenceText.indexOf("GNU LESSER GENERAL PUBLIC LICENSE") === 0,
               Engine.qtLicenceText.slice(0, 80))

        // One paragraph to a line. COPYING.LESSER is wrapped at 72 columns, and
        // a line break left inside a paragraph is a break a phone draws twice.
        verify(!/[^\n]\n[^\n]/.test(Engine.qtLicenceText), "a paragraph still has a line break in it")

        var licence = findRow(group, "Licence")
        var qt = findRow(group, "Built with Qt")
        verify(licence !== null && qt !== null, "no licence rows")

        // Measured once the rows are laid out. A Column places its children on
        // the next polish, and read before it the group was 48 px short on CI.
        waitForPolish(host)
        var folded = group.height

        // The LGPL first, from the lower row. The GPL unfolded pushes that row
        // thousands of pixels down, off this window, where a click lands on
        // nothing.
        mouseClick(qt, 40, qt.height / 2)
        compare(group.reading, "lgpl")
        tryVerify(function() { return group.height > folded + 300 }, 1000,
                  "the LGPL did not unfold under its row")

        // One open at a time, and a second tap folds it away again.
        mouseClick(licence, 40, licence.height / 2)
        compare(group.reading, "gpl")
        tryVerify(function() { return group.height > folded + 1000 }, 1000,
                  "the GPL did not unfold under its row")
        mouseClick(licence, 40, licence.height / 2)
        compare(group.reading, "")
        tryCompare(group, "height", folded)
    }

    // ---- the sheet ----------------------------------------------------------
    //
    // Closed is not merely transparent: `enabled` has to go with it, or a sheet
    // faded out still takes the clicks aimed at the page behind it - which on
    // this one means the whole page under a full-screen scrim.
    //
    // `opacity` and `enabled` rather than `visible`. `visible` here is a
    // *derived* property - `opacity > 0` - and it is also effective visibility,
    // so it reports the state of every ancestor as well. Asserting on it would
    // be asserting about the test's own scaffolding half the time.
    function test_theSheetIsInertWhenItIsClosed() {
        // No width or height: the sheet anchors to fill its parent, and
        // assigning a size over an anchor is the one thing that would make this
        // specimen a different object from the one WeatherPage builds.
        var sheet = build("PreferencesSheet", {})

        compare(sheet.open, false)
        compare(sheet.enabled, false)
        compare(sheet.opacity, 0)

        sheet.open = true
        tryCompare(sheet, "opacity", 1)
        compare(sheet.enabled, true)
    }
}
