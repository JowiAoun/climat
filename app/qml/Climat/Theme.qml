// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The design tokens, as something a binding is allowed to watch.
//
// Every value, and every argument for it, is still in theme.js. This file does
// not restate either: it declares one QML property per token and binds it to
// that table. The indirection *is* the feature.
//
// A `.pragma library` is evaluated once per engine and reading a property out
// of one produces no change notification. So
//
//     color: Theme.ink.primary
//
// used to be evaluated exactly once - when the binding was created - and never
// again. Swapping the table at runtime repainted nothing, which is why
// dark/light was not merely unbuilt but unbuildable, and why every component
// written against the JS namespace in the meantime would have had to be
// migrated twice. A QML property has a NOTIFY signal, so the identical line
// above is now a live binding: when a second table and a mode to choose between
// them arrive, all ~60 components repaint and not one of them is edited.
//
// ---- why the groups are objects and not prefixes ---------------------------
//
// `surface`, `ink`, `line`, `metric`, `type` and the rest are sub-objects rather
// than flattened names (`inkPrimary`) for one reason beyond spelling: their
// identity never changes. A theme switch will assign new values to the
// properties *inside* these objects; it will never replace an object. Nothing
// that holds `Theme.ink` - a binding, a cached reference in a Canvas paint
// handler, a property alias - is ever left pointing at a corpse.
//
// Each group is an *inline component* instantiated once, rather than an
// anonymous `QtObject { … }`, and that is a tooling decision with real teeth.
// A property declared `QtObject` tells qmllint and qmlls that the thing on the
// other side has QObject's members and nothing else - so every one of the six
// hundred token reads in the tree came back as `Member "primary" not found on
// type "QObject"`. Six hundred warnings in the lint target and a squiggle under
// every colour in the editor is not a cost worth paying for two fewer lines
// here. Naming the type restores the check: a typo in a token name is a lint
// error again, which is exactly what it was when this was a JS object literal.
//
// ---- groups and tables -----------------------------------------------------
//
// Two shapes live here and the difference is what the keys are.
//
//   A *group* is keyed by token name: the eleven colour roles, plus `metric`,
//   `type`, `motion`, `scale`, `star`, `surfaceAlpha`. Someone wrote each of
//   those names down on purpose, and a component reads them one at a time, so
//   each is a property with its own notification.
//
//   A *table* is keyed by a data value: `sky` by a phase, `ramp` by a metric
//   id, `precip.wash` by a precipitation type. The keys are not tokens - they
//   are the domain - and the lookups in the tree are all dynamic
//   (`Theme.ramp[metric.ramp]`, `Theme.sky[phase]`). Declaring one property per
//   weather type would buy nothing that `Tokens.precip.wash[type]` does not
//   already give, so these stay whole objects behind a single `var`. They are
//   still reactive: re-binding the `var` notifies, which is the granularity a
//   whole-table swap needs anyway.
//
// ---- why theme.js is still on disk -----------------------------------------
//
// Its sibling viewports.js was absorbed into Viewports.qml and deleted, and the
// obvious question is why this one was not. gallery.js is why: it is a
// `.pragma library`, it reads `Theme.ramp` to give the chart specimens
// something to draw, and a `.pragma library` cannot import a QML singleton.
// Absorbing this file would mean either editing the catalogue to route three
// ramp lookups through QML, or keeping a second copy of a seventy-line colour
// table in step with the first by hand. A binding per token is the cheaper of
// the three, and it puts the values and the essays about them in one place -
// which is where they were.
//
// ---- what this file does not decide ----------------------------------------
//
// The role names, the deletions and the seven absorbed literals are all
// arguments made in theme.js, next to the values they are about. This file
// republishes whatever is there. The one thing it adds is `motion.easing`,
// which cannot live in a plain JS library because it is a QML enum, and the one
// thing it *asks* rather than states is `type.family` - see below.
pragma Singleton

import QtQuick
import "theme.js" as Tokens
import "themelight.js" as LightTokens

QtObject {
    id: theme

    // ---- the active scheme -------------------------------------------------
    //
    // The one writable property on this singleton, and the whole of the theme
    // switch. Everything below reads it exactly once, at the point each group
    // is instantiated, and hands the chosen table down; nothing else in the
    // tree ever asks which theme is running.
    //
    // The groups are QObjects whose identity never changes - only the `src`
    // they read from does - so a switch re-evaluates the bindings inside them
    // and destroys none of them. That is the property `.pragma library` could
    // not offer and the reason Theme became a singleton in the first place: a
    // JS library value produces no change notification, so
    // `color: Theme.ink.primary` was evaluated once and never again.
    //
    // Default dark, deliberately. It is where this design system started, it is
    // what every committed screenshot shows, and it is the answer when the
    // desktop has no preference to report.
    property string scheme: "dark"
    readonly property bool isLight: scheme === "light"

    // Startup shape check, because the failure it catches is silent. The light
    // table is a separate file carrying the same key set, so a key added to one
    // and not the other is a binding that quietly resolves to undefined - which
    // in QML is a transparent colour, not an error. One pass at construction
    // costs nothing and turns that into a line on stderr naming the key.
    Component.onCompleted: {
        for (var i = 0; i < colorRoles.length; ++i) {
            var role = colorRoles[i]
            var dark = Tokens[role]
            var lit  = LightTokens[role]
            if (lit === undefined) {
                console.warn("Theme: themelight.js has no '" + role + "' group")
                continue
            }
            for (var key in dark) {
                if (lit[key] === undefined)
                    console.warn("Theme: themelight.js is missing " + role + "." + key)
            }
        }
    }

    // The token names in a group, in declaration order.
    //
    // The gallery's palette and type pages enumerate a group rather than
    // transcribing it, so that a token added to theme.js shows up there without
    // anyone remembering to add it. `Object.keys()` on a plain JS object
    // returned exactly the tokens; on a QObject it also returns `objectName`
    // and a `<name>Changed` entry per property. That is a wart of the port and
    // not something every caller should have to know, so it is filtered here
    // and only here.
    // `src` and `still` are the inputs a group is *constructed* from, not
    // tokens in it - the table a theme-varying group reads, and the flag the
    // motion group collapses on. They are properties like any other as far as
    // Object.keys is concerned, so without this line `Theme.names(Theme.page)`
    // answers `["bg", "src"]` and any page generated from it grows a column for
    // a design token that does not exist. Found by the test that compares this
    // list against theme.js, which is also what now keeps it true.
    readonly property var plumbing: ["objectName", "src", "still"]

    function names(group) {
        return Object.keys(group).filter(function (key) {
            return theme.plumbing.indexOf(key) < 0 && !/Changed$/.test(key)
        })
    }

    // The colour roles, in the order the design system introduces them.
    //
    // This is the one list in the project that has to be maintained by hand, and
    // it is here rather than in the gallery because it is a fact about the
    // palette rather than about the tool that draws it. The palette page walks
    // it and then walks `names()` inside each role, so a *token* added to a role
    // still appears without anyone doing anything; only a whole new role costs a
    // line. That trade is deliberate - a new role is a design decision worth
    // one line of bookkeeping, and a new token is not.
    readonly property var colorRoles: [
        "page", "surface", "ink", "line", "accent",
        "control", "overlay", "state", "glyph", "badge", "scaffold"
    ]

    // ---- what each token owes its background --------------------------------
    //
    // The contract the gallery's contrast column audits against. It is here and
    // not in the gallery for the same reason `colorRoles` is: what a token is
    // *for* is a design decision, and the tool that measures it should not also
    // be the place it is written down.
    //
    // One blanket threshold was tried first and it was worthless. Held to 3:1,
    // the shipped dark palette came out almost entirely red - `line.grid` at
    // 1.40:1, `overlay.hatch` at 1.19:1 - and every one of those is the design
    // working as intended, because a chart gridline is *supposed* to be barely
    // there. An instrument that flags the correct answer is one nobody reads,
    // and a page of red rows hides the four that are real.
    //
    // So a token declares two things. `on` is the background it is actually
    // composited over - a token measured against a ground it never touches is
    // measuring nothing, which is what made `accent.ink` (the label *on* the
    // pill) look broken at 1.48:1 against a card it is never drawn on. And
    // `duty` is which of three jobs it does:
    //
    //   text        draws glyphs of text at body size                    4.5:1
    //   essential   you must see it to read the content or to work a     3.0:1
    //               control - WCAG 1.4.11's "graphical objects required
    //               to understand content" and "UI components"
    //   incidental  scaffolding, washes, decoration. No minimum. The
    //               ratio is still computed and still shown, because a
    //               number you can see is how you notice it drifting.
    //
    // `pair` names the other stop of a two-stop gradient - a badge plate, a
    // cloud, the sun. The requirement belongs to the pair and not to either
    // stop: a plate is legible if *either* end of it separates from the card,
    // and which end does that flips between the schemes. `badge.nightBottom` is
    // the deep end of a blue plate; on a dark card it nearly vanishes and the
    // light top carries it, on a light card the reverse. Scored per token it
    // reads as broken in one scheme and fine in the other while being the same
    // plate, which is a false alarm and a missed alarm from one rule.
    readonly property var contrastDefaults: ({
        "page":     { duty: "incidental", on: null           },
        "surface":  { duty: "incidental", on: "page.bg"      },
        "ink":      { duty: "text",       on: "surface.base" },
        "line":     { duty: "incidental", on: "surface.base" },
        "accent":   { duty: "essential",  on: "surface.base" },
        "control":  { duty: "essential",  on: "surface.base" },
        "overlay":  { duty: "incidental", on: "surface.base" },
        "state":    { duty: "essential",  on: "surface.base" },
        "glyph":    { duty: "essential",  on: "surface.base" },
        "badge":    { duty: "essential",  on: "surface.base" },
        "scaffold": { duty: "essential",  on: "surface.base" }
    })

    // Only the tokens that differ from their role's default need a line here,
    // which is what keeps this a short list rather than a second copy of the
    // palette. Two kinds of entry: a ground correction, where the token sits on
    // something other than a card, and a duty correction, where one token in a
    // role does a different job from its neighbours.
    readonly property var contrastOverrides: ({
        // A row tint is painted on the card, not on the page.
        "surface.rowAlt":  { on: "surface.base" },
        "surface.rowNow":  { on: "surface.base" },

        // Three of the eleven lines carry meaning. `now` says where the present
        // is, `forecast` says where the recording stops and the prediction
        // starts, and `series` is the data itself - you cannot read the chart
        // without them. The other eight are ruling.
        "line.now":        { duty: "essential" },
        "line.forecast":   { duty: "essential" },
        "line.series":     { duty: "essential" },
        // The hairline light mode adds exists to separate the card from the
        // page, so the page is the only ground it can be measured against.
        "line.card":       { on: "page.bg" },
        "line.nav":        { on: "surface.nav" },
        "line.menu":       { on: "surface.menu" },

        // The label on the selected pill, on the pill.
        "accent.ink":      { duty: "text", on: "accent.fill" },

        // A switch is found by its knob, not by the boundary of its track, so
        // the track is not what has to reach 3:1 - the knob against the track
        // is. Same for the pager: the scrim behind the chevron is decoration
        // and the chevron is the control.
        "control.toggleTrack":    { duty: "incidental" },
        "control.toggleKnob":     { on: "control.toggleTrack" },
        "control.pagerFill":      { duty: "incidental" },
        "control.pagerFillHover": { duty: "incidental" },
        "control.pagerGlyph":     { on: "control.pagerFill" },
        "control.navGlyph":       { on: "surface.nav" },

        // Gradient stops. The sun, the cloud and the two badge plates are each
        // one object drawn with two colours; the cloud on a day badge is drawn
        // on the badge rather than on the card.
        "glyph.sunWarm":            { pair: "glyph.sunCool" },
        "glyph.sunCool":            { pair: "glyph.sunWarm" },
        "glyph.cloudTop":           { pair: "glyph.cloudBottom" },
        "glyph.cloudBottom":        { pair: "glyph.cloudTop" },
        "glyph.cloudTopOnLight":    { pair: "glyph.cloudBottomOnLight", on: "badge.dayTop" },
        "glyph.cloudBottomOnLight": { pair: "glyph.cloudTopOnLight",    on: "badge.dayTop" },

        // Three marks that fall, in their day-plate form. Not paired with their
        // card values: a drop and its OnLight twin are never drawn together, so
        // "the better of the two" would let the plate variant pass on the
        // strength of a colour that is not on the plate.
        "glyph.rainOnLight":        { on: "badge.dayTop" },
        "glyph.snowOnLight":        { on: "badge.dayTop" },
        "glyph.boltOnLight":        { on: "badge.dayTop" },

        // And the same six for the other plate, which had nothing measuring it
        // at all until this table gained these lines - the entry in
        // docs/known-gaps.md that said so is closed by them.
        //
        // Against `badge.nightTop` and not `nightBottom`, and that is the
        // strict choice rather than the convenient one: these inks are pale,
        // and the harder ground for a pale ink is the PALER of the plate's two
        // stops. Scoring them against the bottom stop would flatter every one
        // of them by two or three points.
        "glyph.cloudTopOnNight":    { pair: "glyph.cloudBottomOnNight", on: "badge.nightTop" },
        "glyph.cloudBottomOnNight": { pair: "glyph.cloudTopOnNight",    on: "badge.nightTop" },
        "glyph.rainOnNight":        { on: "badge.nightTop" },
        "glyph.snowOnNight":        { on: "badge.nightTop" },
        "glyph.boltOnNight":        { on: "badge.nightTop" },
        "glyph.moonOnNight":        { on: "badge.nightTop" },
        // The shaded limb is what makes a crescent read as a crescent, so it is
        // measured against the lit face rather than against the card behind it.
        "glyph.moonShade":          { on: "glyph.moon" },

        // The four badge stops are the plate a weather glyph is drawn on, and
        // the plate is not what has to be seen - the glyph is. That requirement
        // is already carried, one role up, by glyph.cloud*OnLight measured
        // against badge.dayTop.
        //
        // Scoring the plate against the card as well looked rigorous and was
        // not. Rendered in light mode the day badge is a pale gold disc on a
        // pale grey card: 1.11:1, and plainly visible, because almost all of
        // the separation is hue and a WCAG ratio is luminance only. Two
        // honest readings of that: the plate is decoration and the number does
        // not apply, or the plate leans on a channel the number cannot see and
        // should not be trusted alone. Both land here - no floor, ratio still
        // printed - rather than on a threshold that would have been satisfied
        // by turning a sunny day's badge into a bronze one.
        "badge.dayTop":      { duty: "incidental" },
        "badge.dayBottom":   { duty: "incidental" },
        "badge.nightTop":    { duty: "incidental" },
        "badge.nightBottom": { duty: "incidental" },

        // The map placeholder's linework is a frame around the label, not the
        // label.
        "scaffold.stroke":   { duty: "incidental" }
    })

    // The two above, merged, for one token. Returns `{ duty, on, pair }` with
    // `pair` undefined for the great majority of tokens that are not half of a
    // gradient.
    function contrastRule(path) {
        var role = path.split(".")[0]
        var base = contrastDefaults[role]
        var over = contrastOverrides[path]
        if (base === undefined)
            return { duty: "incidental", on: null }
        if (over === undefined)
            return { duty: base.duty, on: base.on }
        return {
            duty: over.duty !== undefined ? over.duty : base.duty,
            on:   over.on   !== undefined ? over.on   : base.on,
            pair: over.pair
        }
    }

    // The floor each duty has to clear. 0 means "no minimum" rather than
    // "always passes", and the gallery draws those rows without a verdict at
    // all so that an incidental token cannot be mistaken for one that passed.
    function contrastFloor(duty) {
        if (duty === "text")      return 4.5
        if (duty === "essential") return 3.0
        return 0
    }

    // ---- reading the palette from outside the running scheme ----------------
    //
    // Everything above hands components the table for whichever scheme is on.
    // The gallery needs both at once - a light column beside a dark one is the
    // whole point of a palette page - so these two hand back a raw table by
    // name.
    //
    // Nothing in the app may call them. Reading a colour for a scheme you are
    // not running is precisely the bug the grouped properties exist to prevent,
    // and the only defensible caller is a tool whose subject is the palette
    // itself rather than the weather.
    function tokensFor(role, scheme) {
        return (scheme === "light" ? LightTokens : Tokens)[role]
    }

    function rampsFor(scheme) {
        return (scheme === "light" ? LightTokens : Tokens).ramp
    }

    // The ramps whose hues are published authority bands rather than our
    // choice - European AQI, WHO UV, and the precipitation scale - so light
    // mode passes them through unchanged instead of inverting their lightness
    // like the six continuous ones. `tools/theme/ramp-light.mjs` holds the same
    // list, because it is a one-shot generator that reads theme.js as data and
    // never loads this singleton; if a fourth categorical ramp is ever added,
    // both say so. The gallery labels each ramp from this list.
    readonly property var categoricalRamps: ["precip", "aqi", "uv"]

    // ---- surfaces ----------------------------------------------------------
    // The alpha ladder, as the leading pair of an #AARRGGBB literal. Nothing
    // reads these - the composed `surface.*` values below are what the tree
    // uses - and they stay exported because they are the ladder the design
    // system quotes, not a leftover.
    component SurfaceAlphaTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string recede: src.recede
        readonly property string base:   src.base
        readonly property string raised: src.raised
    }
    readonly property SurfaceAlphaTokens surfaceAlpha: SurfaceAlphaTokens { src: theme.isLight ? LightTokens.surfaceAlpha : Tokens.surfaceAlpha }

    // ---- colour ------------------------------------------------------------
    // Eleven roles. Every one of them is declared `string` rather than `color`,
    // which is not laziness. A `color` property round-trips through QColor, so
    // `"transparent"` reads back as `"#00000000"` and `"#ffffff"` as
    // `"#ffffffff"` - and the gallery's palette page prints these values as text
    // beside each swatch. Assigning a string to a `color` property at the call
    // site is a conversion QML already does everywhere, so nothing downstream
    // can tell the difference; the palette page can.

    // The ground everything is composited on. The gradient itself is `sky`.
    component PageTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string bg: src.bg
    }
    readonly property PageTokens page: PageTokens { src: theme.isLight ? LightTokens.page : Tokens.page }

    // §10.1's three-rung ladder, plus §10.12's opaque exceptions.
    component SurfaceTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string recede: src.recede
        readonly property string base:   src.base
        readonly property string raised: src.raised
        readonly property string panel:  src.panel
        readonly property string rowAlt: src.rowAlt
        readonly property string rowNow: src.rowNow
        readonly property string nav:    src.nav
        readonly property string menu:   src.menu
    }
    readonly property SurfaceTokens surface: SurfaceTokens { src: theme.isLight ? LightTokens.surface : Tokens.surface }

    // Text. Ink for anything on the accent is `accent.ink`.
    component InkTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string primary: src.primary
        readonly property string muted:   src.muted
        readonly property string dim:     src.dim
    }
    readonly property InkTokens ink: InkTokens { src: theme.isLight ? LightTokens.ink : Tokens.ink }

    // Anything a pixel wide: it measures, it separates, or it is the unfilled
    // remainder of a gauge.
    component LineTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string grid:     src.grid
        readonly property string gridWeak: src.gridWeak
        readonly property string track:    src.track
        readonly property string now:      src.now
        readonly property string forecast: src.forecast
        readonly property string series:   src.series
        readonly property string card:     src.card
        readonly property string divider:  src.divider
        readonly property string nav:      src.nav
        readonly property string menu:     src.menu
        readonly property string control:  src.control
    }
    readonly property LineTokens line: LineTokens { src: theme.isLight ? LightTokens.line : Tokens.line }

    // The one saturated colour, and the only ink legible on it.
    //
    // This pair used to be `color.accent` and `color.onAccent` on one object,
    // and the second of those could not be written like its neighbours: any
    // binding whose name is `on` + a capital letter is parsed as a signal
    // handler first, so `onAccent:` resolved against a member called `accent` -
    // which existed - and the value went to the signal rather than to the
    // property.
    //
    // How that failed is why it is worth recording. With a literal it is at
    // least an error: `Cannot assign a value to a signal`. With an *expression*
    // - which every token here is, since they all read out of theme.js - a
    // script assigned to a signal is perfectly legal QML, so it compiled clean,
    // ran clean, and left the token as the empty string. An empty string is a
    // valid colour to QML: it paints black. The whole symptom was a 9x9 patch of
    // the trend badge's arrow going from #141d33 to #000000 on four
    // screenshots, with nothing on stderr. The workaround was a `Binding` that
    // named the property as a *string*, taking the parser out of it, at the cost
    // of the property's `readonly`.
    //
    // Naming the pair by role deleted the problem instead of working around it.
    // `fill` and `ink` say what they are for, they sit together because they are
    // one decision, and neither of them starts with "on".
    component AccentTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string fill: src.fill
        readonly property string ink:  src.ink
    }
    readonly property AccentTokens accent: AccentTokens { src: theme.isLight ? LightTokens.accent : Tokens.accent }

    // Interactive chrome that carries its own colour rather than a surface's.
    component ControlTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string toggleTrack:    src.toggleTrack
        readonly property string toggleKnob:     src.toggleKnob
        readonly property string pagerFill:      src.pagerFill
        readonly property string pagerFillHover: src.pagerFillHover
        readonly property string pagerGlyph:     src.pagerGlyph
        readonly property string navGlyph:       src.navGlyph
    }
    readonly property ControlTokens control: ControlTokens { src: theme.isLight ? LightTokens.control : Tokens.control }

    // Drawn over content it must not let through.
    component OverlayTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string past:      src.past
        readonly property string pastHatch: src.pastHatch
        readonly property string hatch:     src.hatch
        readonly property string caption:   src.caption
        readonly property string readout:   src.readout
        readonly property string scrim:     src.scrim
    }
    readonly property OverlayTokens overlay: OverlayTokens { src: theme.isLight ? LightTokens.overlay : Tokens.overlay }

    // Colour as a verdict rather than as a value (§10.5).
    component StateTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string trendUp:     src.trendUp
        readonly property string trendDown:   src.trendDown
        readonly property string trendSteady: src.trendSteady
        readonly property string good:        src.good
        readonly property string caution:     src.caution
        readonly property string poor:        src.poor
    }
    readonly property StateTokens state: StateTokens { src: theme.isLight ? LightTokens.state : Tokens.state }

    // The paints a weather glyph is drawn in, shared by six screens.
    component GlyphTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string sunWarm:            src.sunWarm
        readonly property string sunCool:            src.sunCool
        readonly property string moon:               src.moon
        readonly property string moonShade:          src.moonShade
        readonly property string cloudTop:           src.cloudTop
        readonly property string cloudBottom:        src.cloudBottom
        readonly property string cloudTopOnLight:    src.cloudTopOnLight
        readonly property string cloudBottomOnLight: src.cloudBottomOnLight
        readonly property string rain:               src.rain
        readonly property string rainOnLight:        src.rainOnLight
        readonly property string snow:               src.snow
        readonly property string snowOnLight:        src.snowOnLight
        readonly property string bolt:               src.bolt
        readonly property string boltOnLight:        src.boltOnLight
        readonly property string droplet:            src.droplet

        readonly property string cloudTopOnNight:    src.cloudTopOnNight
        readonly property string cloudBottomOnNight: src.cloudBottomOnNight
        readonly property string rainOnNight:        src.rainOnNight
        readonly property string snowOnNight:        src.snowOnNight
        readonly property string boltOnNight:        src.boltOnNight
        readonly property string moonOnNight:        src.moonOnNight
    }
    readonly property GlyphTokens glyph: GlyphTokens { src: theme.isLight ? LightTokens.glyph : Tokens.glyph }

    // The disc a day's glyph sits on in the ten-day strip.
    component BadgeTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string dayTop:      src.dayTop
        readonly property string dayBottom:   src.dayBottom
        readonly property string nightTop:    src.nightTop
        readonly property string nightBottom: src.nightBottom
    }
    readonly property BadgeTokens badge: BadgeTokens { src: theme.isLight ? LightTokens.badge : Tokens.badge }

    // Deliberately off-palette: something not built yet.
    component ScaffoldTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property string ink:    src.ink
        readonly property string stroke: src.stroke
    }
    readonly property ScaffoldTokens scaffold: ScaffoldTokens { src: theme.isLight ? LightTokens.scaffold : Tokens.scaffold }

    // ---- precipitation -----------------------------------------------------
    // Three tokens and three tables in one group. `edge`, `splash` and `flash`
    // are single decisions; `wash`, `washAlpha` and `drop` are keyed by
    // precipitation type and intensity, which is data, and every read of them
    // in the tree is already a dynamic lookup.
    component PrecipTokens: QtObject {
        // The group's values for the active scheme, handed in rather than
        // looked up: an inline component cannot see the enclosing file's id,
        // so the choice is made once at the instantiation below.
        required property var src

        readonly property var    wash:      src.wash
        readonly property var    washAlpha: src.washAlpha
        readonly property var    drop:      src.drop
        readonly property string edge:      src.edge
        readonly property string splash:    src.splash
        readonly property string flash:     src.flash
    }
    readonly property PrecipTokens precip: PrecipTokens { src: theme.isLight ? LightTokens.precip : Tokens.precip }

    // ---- severity ----------------------------------------------------------
    // The five CAP grades, keyed by libclimat's own severity key. A plain table
    // rather than a token group, for precip's reason: every read of it is
    // `Theme.severity[alert.severityKey]`, with the key coming from data.
    //
    // Not in `colorRoles`, and therefore not on the gallery's contrast page,
    // which walks flat groups. It is audited instead by tests/qml/tst_theme.qml,
    // which knows the shape and checks all twenty values against the plate they
    // are actually drawn on - see theme.js for the numbers and why the ground
    // is the composited plate and not the page.
    readonly property var severity: theme.isLight ? LightTokens.severity : Tokens.severity

    // The grades, worst first. The order the gallery lays specimens out in and
    // the order a reader expects to meet them; nothing sorts by it at run time,
    // because libclimat has already ranked what the banner shows.
    readonly property var severityKeys: ["extreme", "severe", "moderate", "minor", "unknown"]

    // ---- the sky -----------------------------------------------------------
    // A table keyed by phase - `night`, `dawn`, `day`, `dusk` - because that is
    // how it is read: `Theme.sky[phase]`, with the phase computed from the
    // clock. Main.qml also leans on a miss returning `undefined`, which is how
    // `--sky nonsense` is rejected rather than obeyed.
    readonly property var sky: theme.isLight ? LightTokens.sky : Tokens.sky

    // `glow` is four gradient stops rather than one colour, so it is a `var`
    // for the same reason a ramp is: PageBackdrop indexes it.
    component StarTokens: QtObject {
        readonly property string ink:  Tokens.star.ink
        readonly property string line: Tokens.star.line
        readonly property var    glow: Tokens.star.glow
    }
    readonly property StarTokens star: StarTokens { }

    // ---- measurements ------------------------------------------------------
    // Theme-invariant: a 14 px radius is 14 px in any palette. These are not
    // roles and they did not move when the colours became roles.
    component MetricTokens: QtObject {
        readonly property int hourWidth:        Tokens.metric.hourWidth
        readonly property int minHourWidth:     Tokens.metric.minHourWidth
        readonly property int plotHeight:       Tokens.metric.plotHeight
        readonly property int axisTopPad:       Tokens.metric.axisTopPad
        readonly property int headerBandHeight: Tokens.metric.headerBandHeight
        readonly property int gutterWidth:      Tokens.metric.gutterWidth
        readonly property int stripHeight:      Tokens.metric.stripHeight
        readonly property int stripGap:         Tokens.metric.stripGap
        readonly property int panelPadding:     Tokens.metric.panelPadding
        readonly property int cardPadding:      Tokens.metric.cardPadding
        readonly property int cardRadius:       Tokens.metric.cardRadius
        readonly property int panelRadius:      Tokens.metric.panelRadius
        readonly property int controlRadius:    Tokens.metric.controlRadius
        readonly property int filletRadius:     Tokens.metric.filletRadius

        readonly property int detailCardWidth:  Tokens.metric.detailCardWidth
        readonly property int detailCardHeight: Tokens.metric.detailCardHeight
        readonly property int detailRadius:     Tokens.metric.detailRadius
        readonly property int detailPadH:       Tokens.metric.detailPadH
        readonly property int detailPadV:       Tokens.metric.detailPadV
        readonly property int detailGap:        Tokens.metric.detailGap

        readonly property int sectionGap:       Tokens.metric.sectionGap
        readonly property int pageMargin:       Tokens.metric.pageMargin

        readonly property int mobileMargin:     Tokens.metric.mobileMargin
        readonly property int mobileGap:        Tokens.metric.mobileGap
        readonly property int mobileCardPadH:   Tokens.metric.mobileCardPadH
        readonly property int mobileCardPadV:   Tokens.metric.mobileCardPadV
        readonly property int navHeight:        Tokens.metric.navHeight
        readonly property int navSafeArea:      Tokens.metric.navSafeArea
        readonly property int navRailWidth:     Tokens.metric.navRailWidth
        readonly property int mobileContentMax: Tokens.metric.mobileContentMax
        readonly property int hitMin:           Tokens.metric.hitMin
    }
    readonly property MetricTokens metric: MetricTokens { }

    // ---- performance -------------------------------------------------------
    //
    // "full" or "reduced". Pushed in from Main, like `scheme` and `stillness`
    // and for the same reason: the answer belongs to the running application -
    // it is the platform on a handset and a flag under review - and Theme is
    // where every component already looks.
    //
    // A tier and not a device check at each site. There is one consumer today
    // (PageBackdrop) and the temptation is to write `Qt.platform.os` there;
    // that would put the decision in the file that draws rather than in the
    // file that decides, and the second thing to become tier-aware would make
    // its own copy of the rule.
    property string perfTier: "full"

    component PerfTokens: QtObject {
        required property string tier

        readonly property var table:
            Tokens.perf[tier] !== undefined ? Tokens.perf[tier] : Tokens.perf.full

        readonly property int starField:      table.starField
        readonly property int starBeacons:    table.starBeacons
        readonly property int constellations: table.constellations
    }
    readonly property PerfTokens perf: PerfTokens { tier: theme.perfTier }

    // ---- type --------------------------------------------------------------
    // `font.pixelSize` is an int in Qt and a fractional value fails object
    // creation, reported only as `Type X unavailable` from the *parent* file.
    // Declaring these `int` here turns that into a value that was rounded on
    // the way in rather than a component that silently does not exist.
    component TypeTokens: QtObject {
        // The face, and the second token in this file that does not come from
        // theme.js. `easing` is there because a QML enum cannot live in a plain
        // JS library; this one is there because the *right* answer is not a
        // string at all - it is a question, asked of the running application.
        //
        // app/appfont.cpp registers the bundled Inter faces and makes the family
        // they declare the application font. That is what every one of the 158
        // Text items in the tree already renders in, without one of them naming
        // a family, and it is what a settings surface would change when D9's
        // "use the system UI font" arrives: one call to QGuiApplication::setFont
        // and the whole tree follows, because that is where a Text resolves its
        // family from.
        //
        // Reading it back here rather than writing "Inter" into theme.js means
        // there is one spelling of the family in the project and it is the one
        // inside the font file. The failure the duplicate would have caused is
        // quiet and slow: swap the bundled face, C++ picks up the new family
        // automatically, theme.js still says the old one, and every component
        // that referenced the token falls back to the host's font - half the
        // screen in the right face and half in the wrong one.
        //
        // Nothing in the tree needs to set `font.family` today; the application
        // font covers it. The token is for the cases that are coming: text drawn
        // into a Canvas or a QPainter, which take a font by name and inherit
        // nothing, and any surface that has to opt back out to the platform.
        //
        // Not live, one caveat. This is read once when the singleton is created,
        // and Qt.application.font has no change notification we can rely on, so
        // a runtime font switch will need this to become a property on a C++
        // singleton with a NOTIFY of its own. That is a later problem, and the
        // call sites do not change when it is solved.
        //
        // The suppression is a gap in qmllint's type data rather than a gap in
        // Qt: `Qt.application.font` is documented and works - the gallery's Type
        // page prints "family · Inter" off this very property - but the QML type
        // description for QQmlApplication does not list `font`, so the linter
        // reports a member that is there. Scoped to the one line, because
        // `missing-property` is the category that catches real typos in a token
        // name and it should keep doing that everywhere else in this file.
        readonly property string family: Qt.application.font.family // qmllint disable missing-property

        readonly property int sectionTitle: Tokens.type.sectionTitle
        readonly property int cardTitle:    Tokens.type.cardTitle
        readonly property int detailTitle:  Tokens.type.detailTitle
        readonly property int reading:      Tokens.type.reading
        readonly property int readingPair:  Tokens.type.readingPair
        readonly property int status:       Tokens.type.status
        readonly property int body:         Tokens.type.body
        readonly property int label:        Tokens.type.label
        readonly property int axis:         Tokens.type.axis
        readonly property int heroReading:  Tokens.type.heroReading
        readonly property int heroUnit:     Tokens.type.heroUnit
        readonly property int heroCaption:  Tokens.type.heroCaption
        readonly property int heroCaptionLong: Tokens.type.heroCaptionLong
        readonly property int heroDetail:   Tokens.type.heroDetail
        readonly property int heroLabel:    Tokens.type.heroLabel
    }
    readonly property TypeTokens type: TypeTokens { }

    // ---- motion ------------------------------------------------------------
    // `easing` is the one thing this file adds rather than republishes. theme.js
    // says outright that it cannot hold `Easing.OutCubic` because a QML enum
    // cannot live in a plain JS library, so the house rule - **OutCubic unless
    // there is a stated reason** - is written literally at some sixty call sites
    // and enforced by nothing. A singleton can hold it.
    // ---- motion ------------------------------------------------------------
    //
    // Every duration collapses to zero when `stillness` is set, and that one
    // switch serves two callers that turn out to want exactly the same thing.
    //
    // A READER who has asked their desktop for reduced motion. docs/04 requires
    // honouring it, and §10.11's standing precipitation field - the only
    // infinite animation in the product - is the first thing such a reader wants
    // stopped.
    //
    // A CAPTURE. --grab waits a fixed interval and photographs whatever has
    // settled by then, which works until something has not settled. PagerButton
    // fades its opacity over `tint`; when the shutter and the fade land at the
    // same moment the chevron is caught mid-fade, and the result is a handful of
    // pixels a shade out. That surfaced as two alternating outputs for one
    // command, 35 pixels apart on a 1340x900 frame - invisible to look at, and
    // fatal to a golden image, which compares bytes.
    //
    // A longer settle would have made it rarer rather than impossible: it is a
    // race, and races lose eventually. Zero durations remove the race instead of
    // outrunning it, for every animated property at once rather than for the one
    // that happened to be caught.
    //
    // --film is deliberately exempt: it exists to photograph motion.
    property bool stillness: false

    component MotionTokens: QtObject {
        required property bool still

        readonly property int tint:    still ? 0 : Tokens.motion.tint
        readonly property int move:    still ? 0 : Tokens.motion.move
        readonly property int view:    still ? 0 : Tokens.motion.view
        readonly property int reveal:  still ? 0 : Tokens.motion.reveal
        readonly property int stagger: still ? 0 : Tokens.motion.stagger

        readonly property int easing:  Easing.OutCubic
    }
    readonly property MotionTokens motion: MotionTokens { still: theme.stillness }

    component ScaleTokens: QtObject {
        readonly property int tempMin:  Tokens.scale.tempMin
        readonly property int tempMax:  Tokens.scale.tempMax
        readonly property int tickStep: Tokens.scale.tickStep
    }
    readonly property ScaleTokens scale: ScaleTokens { }

    // ---- ramps -------------------------------------------------------------
    // A table keyed by ramp name, which is a metric's choice and not a token:
    // the chart reads `Theme.ramp[root.metric.ramp].fill` and the registry is
    // what decides the key. gallery.js reads this table too, and it reads it
    // straight out of theme.js - a `.pragma library` cannot import a QML
    // singleton, which is the other half of why theme.js is still the file the
    // values live in.
    readonly property var ramp: theme.isLight ? LightTokens.ramp : Tokens.ramp
}
