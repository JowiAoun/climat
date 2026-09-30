// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// Design tokens for Climat, named by the role they play rather than by what they
// happen to look like today. Keep them in one place so the whole app can be
// re-skinned without touching layout code.
//
// **Nothing in the tree reads this file directly any more - read Theme.qml.**
// This is the table; Theme.qml is the singleton that republishes it as QML
// properties, which is what lets a binding on a token be re-evaluated when the
// token changes. A `.pragma library` is evaluated once per engine and never
// notifies anyone of anything, so a colour read out of here is a colour decided
// at first paint and never revisited.
//
// It stays a plain library for one reason: gallery.js is a `.pragma library`
// too, it reads `ramp` from here, and a `.pragma library` cannot import a QML
// singleton. Every value and every argument for a value belongs here; the
// declarations that make them observable belong there.
//
// ---- why the colour groups are roles and not one flat list -------------------
//
// There used to be a single `color` group of 67 tokens, and its names described
// *appearance*: `surfaceRaised` was literally "#1affffff", a white wash. 24 of
// the 67 were `#XXffffff` and three more were `#XX141d33` dark tints, so the
// "white over dark" assumption was baked into the value and the name agreed with
// it. A light theme cannot be produced by re-valuing a table like that, because
// half of it is not a decision - it is a description of the decision that was
// already made somewhere else.
//
// §10.1's constraint is the one to design against: *the surface ladder decides
// the background, not the other way round*. So the groups below name jobs -
// what is a surface, what is ink, what is a line, what is a control, what is
// drawn over content - and the values are answers to those questions. Swapping
// the answers is now a change to this file and nothing else.
//
// The groups, and what belongs in each:
//
//   page       the ground everything else is composited on
//   surface    the §10.1 wash ladder, plus the opaque exceptions §10.12 allows
//   ink        text
//   line       anything one pixel wide that separates or measures
//   accent     the one saturated colour, and the only ink legible on it
//   control    interactive chrome: toggles, pagers, nav glyphs
//   overlay    drawn *over* content it must not let through
//   state      a verdict - a trend direction, a good/caution/poor band
//   glyph      the paints a weather glyph is drawn in
//   badge      the day/night disc behind a glyph
//   scaffold   deliberately off-palette: something not built yet
//
// `metric`, `type`, `motion` and `scale` are theme-invariant - a 14 px radius is
// 14 px in any palette - so they are not roles and did not move.
.pragma library

// Surfaces are translucent, not painted.
//
// The reference has no opaque cards at all: the page is one vertical gradient
// and every surface is a thin white wash over it, so a card's actual colour is
// whatever the gradient is doing behind it at that scroll depth. Flat fills
// cannot reproduce that - they are one colour everywhere, and the page stops
// reading as a single lit surface with things resting on it.
//
// Alpha ladder, matching the reference's three levels: 0.05 recedes, 0.07 is
// the default surface, 0.10 is raised or hovered. In #AARRGGBB those are 0d,
// 12 and 1a.
//
// The reference also puts backdrop-blur(60px) under each card. That is
// deliberately not reproduced: blurring a smooth vertical gradient returns
// almost exactly the same gradient, so it would cost an offscreen pass and a
// blur per card to change nothing. It earns its keep on their radar map, and
// that is where to revisit it.
var surfaceAlpha = {
    recede: "#0d",   // 0.05
    base:   "#12",   // 0.07
    raised: "#1a"    // 0.10
};

// ---- page --------------------------------------------------------------------
// The ground. The gradient itself is `sky`, five stops keyed by phase; this is
// the flat fallback for the one case that cannot take a gradient.
//
// There used to be `pageStop0…4` here as well, a second copy of `sky.dusk.stops`
// left over from when dusk was the only sky there was. Nothing read them - the
// desktop page and the phone both go through `PageBackdrop`, which reads
// `sky[phase].stops` - so they were deleted rather than renamed. A palette with
// two spellings of the same five colours is a palette that will eventually
// disagree with itself.
var page = {
    bg: "#27284f"    // ~the dusk gradient's midpoint
};

// ---- surface -----------------------------------------------------------------
// The §10.1 ladder, and the three surfaces that are allowed not to be on it.
//
// `recede`, `base` and `raised` are the whole ladder. A card is `base`; an
// unselected day is `recede`; hover, selection and emphasis are `raised`. There
// were five more names for exactly these three values - `cardBg`, `dayCardBg`,
// `stripBg`, `stripPast`, `pillHover`, `switchActive` - each naming the *place*
// a wash was used rather than the level it sits at, which is how a ladder with
// three rungs grows nine names and then drifts apart one rung at a time.
//
// `nav` and `menu` are §10.12's exception: chrome that floats over scrolling
// content may be opaque, because a wash there lets the chart the reader is
// scrolling slide visibly through the labels. `rowNow` is the accent showing
// through a surface, which is what marks the current hour in a list.
var surface = {
    recede: "#0dffffff",
    base:   "#12ffffff",
    raised: "#1affffff",

    // The chart sits directly on the card: a second wash inside the first
    // would composite to 0.135, a panel darker than anything in the reference.
    panel:  "transparent",

    rowAlt: "#0affffff",
    rowNow: "#1fffd24a",

    nav:    "#f2101832",
    menu:   "#f71c2450"   // more opaque than the nav: the nav sits at a screen
                          // edge with a hairline holding it down, a menu floats
                          // in the middle of the page with only its own weight
};

// ---- ink ---------------------------------------------------------------------
// Text, in three weights of presence. Ink for anything sitting *on* the accent
// is `accent.ink` and lives with the fill it has to be legible on - see there
// for why the two are one pair rather than two tokens in two groups.
var ink = {
    primary: "#ffffff",
    muted:   "#c5ccda",
    dim:     "#9ba4b9"
};

// ---- line --------------------------------------------------------------------
// Everything a pixel wide. Three jobs, and they are not interchangeable:
// gridlines *measure*, dividers and hairlines *separate*, and `track` is the
// unfilled remainder of a gauge - which is a reading, not a decoration (§10.7).
//
// These are the same white washes the surfaces are, at today's palette, and they
// are deliberately not the same tokens. A light theme darkens a line and keeps a
// surface a wash; sharing a value now would mean unpicking them then.
var line = {
    grid:     "#1cffffff",
    gridWeak: "#10ffffff",

    // The unfilled part of a gauge - a dial track, a bar's empty remainder. It
    // has to be present enough that the filled part reads as a fraction of
    // something, which `grid` at 0.11 is not.
    track:    "#2effffff",

    // The rule marking the present hour, and the same rule the crosshair
    // follows: both answer "which hour is this", and a chart with two different
    // whites for that is a chart with a bug in it.
    now:      "#5fffffff",

    // A forecast stretch is the same line as the observed one, drawn with less
    // certainty. Same value as `now` and deliberately a separate name: they
    // mean different things and will not always want the same alpha.
    forecast: "#5fffffff",

    // The dashed comparison line a metric may draw over its own series - gust
    // over wind, apparent over actual. The series proper is a ramp rather than
    // a token, which is why only its companion is here.
    series:   "#8cffffff",

    card:     "#1affffff",
    divider:  "#1affffff",
    nav:      "#1affffff",
    menu:     "#26ffffff",

    // The outline that says a control is live: a selected tab, a focused field,
    // the home pill, a device frame in the gallery. §10.1 bans borders at a
    // junction; this is not a junction, it is a state.
    control:  "#33ffffff"
};

// ---- accent ------------------------------------------------------------------
// The one saturated colour in the palette, and the only ink that is legible on
// it. They are one group because they are one decision: `ink` was once set to
// the card background, which was fine while a card was opaque navy and invisible
// the moment it became a white wash. A fill that can change without its ink
// changing with it is that bug waiting to be reintroduced.
//
// Naming it `ink` here rather than `onAccent` in the ink group also removes a
// QML trap that cost a day: a member called `onAccent` on an object that also
// has `accent` is parsed as a signal handler, binds silently to nothing, and
// paints black. Theme.qml carried a `Binding` by name to work around it. Roles
// made the workaround unnecessary rather than tidier.
var accent = {
    fill: "#ffd02c",   // measured off the reference's selected pill
    ink:  "#141d33"
};

// ---- control -----------------------------------------------------------------
// Interactive chrome that is neither a surface nor a line: the parts of a
// control that carry its own colour.
//
// The pager buttons float over the chart, so they stay more opaque than a
// surface - but still tinted rather than painted, or they punch a flat hole in
// the gradient. `navGlyph` is an inactive tab icon; it is the same value as
// `ink.muted` today and stays its own token, because a tab bar and a paragraph
// are not obliged to dim by the same amount in every palette.
var control = {
    toggleTrack:    "#26ffffff",
    toggleKnob:     "#c6cede",

    pagerFill:      "#99141d33",
    pagerFillHover: "#b3141d33",
    pagerGlyph:     "#e8edf7",

    navGlyph:       "#98a4be"
};

// ---- overlay -----------------------------------------------------------------
// Drawn *over* content, and opaque enough to survive whatever is under it. Each
// of these exists because something legible had to sit on something arbitrary.
var overlay = {
    // The past, on the chart: veiled, then hatched, so "there is no forecast
    // here" reads as deliberate rather than as a rendering fault.
    past:      "#14ffffff",
    pastHatch: "#1effffff",

    // HatchPattern's own default, for an instance nobody has told what it is
    // hatching - which today is the gallery specimen and nothing else. A caller
    // that means the chart's past passes `pastHatch`.
    hatch:     "#16ffffff",

    // The scrim behind a small label on the plot: a sun-event marker, a
    // precipitation caption. A caption sits over whatever the series happens to
    // be doing, and grey text over an orange AQI bar is not text.
    caption:   "#99111a2b",

    // The crosshair's value panel. Heavier than `caption` because it carries a
    // reading rather than a word, and a reading has to be exact at a glance.
    readout:   "#e6141d33",

    // A modal dim over the whole window - the place picker. Darker and cooler
    // than anything else here on purpose: it is the one overlay whose job is to
    // put the page *away* rather than to keep something on it readable.
    scrim:     "#99060b18"
};

// ---- state -------------------------------------------------------------------
// Colour as a verdict rather than as a value (§10.5).
//
// Trend colours are fixed: up is warm, down is cool, steady is neutral. A rising
// temperature and a rising pressure use the same up colour; the card's words say
// whether that is good news.
//
// `good`, `caution` and `poor` are pollen bands and the activity list's dots.
// Three, because a fourth level is a scale and a scale wants a ramp.
var state = {
    trendUp:     "#ff9d5c",
    trendDown:   "#7fb6e8",
    trendSteady: "#c6cede",

    good:        "#4ec98a",
    caution:     "#e8c93f",
    poor:        "#f0654f"
};

// ---- glyph -------------------------------------------------------------------
// The paints a weather glyph is drawn in. Not a per-visualisation palette: these
// are shared by the hero, the hourly chart, the day strip and four mobile cards,
// which is exactly the case §10's "a colour that belongs to one visualisation"
// exception does *not* cover.
var glyph = {
    sunWarm:   "#ffd97a",
    sunCool:   "#f2952f",

    moon:      "#f2e3b8",
    // The unlit limb, on the small glyph. DetailMoonCard draws its own, darker,
    // because at 14 px the shadowed side has to be well below the surface or the
    // phase stops being visible at all.
    moonShade: "#38425e",

    cloudTop:    "#ffffff",
    cloudBottom: "#c1cddf",
    // Clouds are drawn white, which vanishes on the pale day badge - these keep
    // them readable there without changing them everywhere else.
    cloudTopOnLight:    "#fbfdff",
    cloudBottomOnLight: "#7b95bb",

    // ---- the marks that fall ------------------------------------------------
    //
    // Five paints, not eight, because the six precipitating kinds are built out
    // of each other rather than each getting a colour of its own. Rain and
    // drizzle are the same drop at two lengths; sleet is a `rain` drop and a
    // `snow` pellet alternating, which is the whole of what sleet is; hail is
    // `snow` pellets under a `bolt`, because WMO 96 and 99 - the only two codes
    // that reach ConditionKind::Hail - are thunderstorms with hail in them and
    // not a separate sky.
    //
    // Each mark needs a second value for the one pale ground in the app: the
    // day plate under DayIconBadge. This is the same argument cloudTopOnLight
    // makes one block up, and the numbers say it louder here - a `rain` drop on
    // dark's #fdfefe plate measures 2.13:1, under the 3:1 an essential mark
    // owes its background, which is why the selected day card in the ten-day
    // strip has been drawing raindrops nobody could see.
    rain:         "#7fb6e8",
    rainOnLight:  "#3f7fc4",

    // Ice reads as near-white against the sky and cannot on the plate. Snow is
    // the one mark whose two values are a shade apart rather than a tint apart,
    // for the reason the light palette gives about snow drops further down.
    snow:         "#e8f2ff",
    snowOnLight:  "#6a83aa",

    // The only warm thing that falls. Deliberately not `sunWarm` reused: a bolt
    // is drawn *over* the white cloud it comes out of, so it is the one mark
    // whose ground is another glyph, and it wants more saturation than a sun
    // that is only ever seen against the page.
    bolt:         "#ffd15c",
    boltOnLight:  "#b26400",

    droplet:      "#93c6f2",

    // ---- and the marks for the night plate ----------------------------------
    //
    // The same five values in both schemes, which is not a copy-paste: the
    // ground is the same in both schemes. `badge.nightTop`/`nightBottom` is one
    // deep blue disc whether the app is light or dark, so the ink that reads on
    // it is one ink, and a light-theme reader looking at a night badge is
    // looking at exactly what a dark-theme reader sees.
    //
    // That is the whole reason these exist rather than the card values being
    // reused. In dark they WOULD be the card values; in light the card values
    // are dark ink for a pale card, and dark ink on a deep blue disc is the
    // defect this set was added to fix, one plate over.
    cloudTopOnNight:    "#ffffff",
    cloudBottomOnNight: "#c1cddf",
    rainOnNight:        "#7fb6e8",
    snowOnNight:        "#e8f2ff",
    boltOnNight:        "#ffd15c",
    moonOnNight:        "#ffe9a8",
};

// ---- badge -------------------------------------------------------------------
// The disc a day's glyph sits on in the ten-day strip: pale for a day, deep blue
// for a night, so the strip reads as a row of days before it reads as weather.
var badge = {
    dayTop:      "#fdfefe",
    dayBottom:   "#dde5f0",
    // Deepened, and the reason is measured rather than aesthetic. At the old
    // #6d9ae8 → #3f63bd this plate sat in the middle of the luminance range,
    // where neither a pale mark nor a dark one clears 3:1 - `glyph.rain` on it
    // measured **1.31:1**, so the night half of a rainy day card had raindrops
    // that were, arithmetically, not visible. A plate has to commit to being
    // light or dark for anything to be legible on it, and a night plate that
    // commits to dark is also the one that looks like night. Every mark in the
    // OnNight set above clears 3:1 against the PALER of these two stops, which
    // is the harder one for a pale ink.
    nightTop:    "#2f4d96",
    nightBottom: "#16265c"
};

// ---- scaffold ----------------------------------------------------------------
// Something the app has not built yet, drawn so it cannot be mistaken for
// something it has. Deliberately off-palette: the map placeholder must read as
// scaffolding at a glance, and a placeholder in the house colours reads as a
// finished screen with no content.
var scaffold = {
    ink:    "#8f9dbb",
    stroke: "#4d6a8fd8"
};

// ---- severity ----------------------------------------------------------------
// The five CAP grades a severe-weather alert can carry, as a table keyed by
// libclimat's own severity key - "extreme", "severe", "moderate", "minor",
// "unknown".
//
// A FOURTH CATEGORICAL GROUP, NOT A STRETCH OF state.good/caution/poor
//
// §10.5 says a published authority's bands get their own categorical palette,
// and CAP's five levels are exactly that: somebody else's scale, with somebody
// else's meaning, which we display rather than compute. The status trio is a
// three-point verdict this app makes about a number it is holding; stretching
// it to five would turn a set of named states into a scale, which is the thing
// detaildata.js's own header warns against.
//
// The shape is precip's and for precip's reason: this is read as
// `Theme.severity[key].edge` with the key coming from data, so it is a table
// rather than twenty flat tokens.
//
// FOUR TOKENS, BECAUSE COLOUR ALONE MAY NOT CARRY IT
//
// §4.10 forbids colour-only encoding, so a severity is always drawn as a glyph
// AND a word AND a colour. Hence `glyph` and `ink` beside `wash` and `edge`
// rather than one hue used four ways at four opacities.
//
//   wash    the banner plate, over page.bg. Alpha rises with severity, so an
//           Extreme reads as more present than a Minor before any word is read.
//   edge    the rail down the leading edge, and the hairline. Saturated: this
//           is the ECCC-recognisable red/orange/yellow.
//   glyph   the severity mark. Same value as `edge` today and a separate token,
//           because a rail and a symbol are not obliged to weigh the same in
//           every palette.
//   ink     the severity word. A tinted label rather than plain ink.primary,
//           because that is what makes the banner read as an alert instead of
//           as another card.
//
// Measured against the composited plate - every `ink` clears 4.5:1 and every
// `edge`/`glyph` clears 3:1 in both schemes, and every plate separates from the
// page by at least 1.3:1. tests/qml/tst_theme.qml asserts all three, because
// unlike the audited colour roles this table is keyed by data and the gallery's
// contrast column walks flat groups.
//
// One measurement worth knowing before it looks like a mistake: in DARK,
// moderate's plate (1.56) sits a hair above extreme's (1.51). Amber on navy is
// simply brighter than red on navy, and buying the ordering back would mean a
// red so pale it stops reading as red. Both are far above the floor, and the
// rail, the glyph and the word all order correctly.
var severity = {
    extreme:  { wash: "#5ce8503a", edge: "#ffff8a70", glyph: "#ffff8a70", ink: "#ffffc0b2" },
    severe:   { wash: "#4af0842c", edge: "#ffffb060", glyph: "#ffffb060", ink: "#ffffd0a0" },
    moderate: { wash: "#33f0c53c", edge: "#ffffd24a", glyph: "#ffffd24a", ink: "#ffffe490" },

    // Blue rather than a fourth warm hue: Minor is informational, and a ladder
    // that stays warm all the way down leaves nothing for "this is not an
    // emergency" to look like.
    minor:    { wash: "#2e6f9be8", edge: "#ff8fc0ff", glyph: "#ff8fc0ff", ink: "#ffb7d6ff" },

    // The issuer declined to grade. Six of the nine alerts in
    // tests/fixtures/alerts/ arrive this way, so this is not the rare case it
    // looks like. Neutral, and deliberately NOT the quietest thing on the
    // screen - an ungraded alert is still an alert.
    unknown:  { wash: "#24aab4c8", edge: "#ffc0c8d8", glyph: "#ffc0c8d8", ink: "#ffd6dce8" }
};

// Precipitation effect. Two layers: a `wash` under the chart marking the hours
// it falls in, and `drop`/`splash` particles over it saying what is falling.
//
// Opaque colours with the alpha applied at use, unlike everything above, and
// for a reason: six types times three intensities is eighteen washes, and
// eighteen hand-written #AARRGGBB literals is a table nobody can check. Type
// chooses the hue, intensity chooses the alpha, and the two are independent -
// which is the actual design, so it is what the tokens should say.
//
// The rain hue and its mid alpha are measured off the reference: its rainy
// stretch composites to #394e77 over a #333659 plot, which is #4f9ad4 at 0.20.
var precip = {
    wash: {
        drizzle: "#4f9ad4",
        rain:    "#4f9ad4",
        sleet:   "#7ba6cf",
        snow:    "#a9c8e8",
        hail:    "#b8d2ec",
        thunder: "#5d7ed0"
    },

    // Deliberately a narrow ladder. The wash answers "is it raining here",
    // which must read the same at every level; how hard it is raining is the
    // field's job, and it has far more range to say it with.
    washAlpha: { light: 0.13, moderate: 0.20, heavy: 0.27 },

    // A wash tells you roughly when. A line on its first and last minute tells
    // you exactly, and "exactly when" is the whole point of the feature.
    edge: "#3dbcd9f2",

    drop: {
        drizzle: "#cfe2f5",
        rain:    "#d8e8f8",
        sleet:   "#e6f1fd",
        snow:    "#f6faff",
        hail:    "#ffffff",
        thunder: "#dcecfd"
    },
    splash: "#c6dcf2",

    // Lightning: the storm band brightening for a frame, not a drawn bolt. A
    // bolt at this size is four pixels of noise; a flash is unmistakable.
    flash:  "#e8f0ff"
};

// The sky, by time of day.
//
// Five stops each, the same five positions PageBackdrop declares - QML cannot
// generate GradientStop elements from a Repeater, so the *positions* are
// written out there and only the colours come from here.
//
// **Every phase is dark.** That is the constraint the whole palette is built
// on and it is not a stylistic preference: §10.1's surfaces are white washes
// at 0.05-0.10, and a wash is only a surface if there is something darker
// behind it. A literal daylight sky would make every card on every screen
// invisible at once. So "day" is a clean deep blue rather than a bright one -
// the difference between phases is hue and clarity, not lightness, and it
// reads as time of day because the four are seen against each other.
//
// `dusk` is the palette this prototype has always had. The desktop page is dusk
// permanently, so nothing about it changes; the phone is the screen that follows
// the clock.
var sky = {
    night: { stops: ["#0c1738", "#141f4a", "#1a2350", "#131a3e", "#0a0f2c"], stars: 1.00 },
    dawn:  { stops: ["#132352", "#33386e", "#5a4470", "#3a3560", "#1b1f45"], stars: 0.45 },
    day:   { stops: ["#1d3d80", "#2a4f96", "#31568f", "#2a4070", "#1c2c50"], stars: 0.00 },
    dusk:  { stops: ["#203580", "#443e73", "#443a66", "#27284f", "#171e44"], stars: 0.50 }
};

var star = {
    ink:  "#ffffff",
    // The figures are drawn fainter than their own vertices: a constellation
    // is stars first and a line second, and a line that competes with them
    // turns a sky into a diagram.
    //
    // 0.13, and it was 0.24 first. At that weight the Plough drew a visible
    // line straight through "Expect sunny skies" - the sky is the one thing on
    // the screen that has to lose every contest it enters.
    line: "#22c8d8ff",

    // The halo on the handful of brightest stars, as four stops of a radial
    // gradient. PageBackdrop writes the *positions* out, for the same reason
    // the sky above does; only the colours are here.
    //
    // Never fully white. These sit behind cards, and a star that reaches full
    // opacity behind a 0.07 wash reads as a rendering fault in the card.
    //
    // Dark-theme values, and the only tokens in this file that a light theme
    // may reasonably answer with nothing at all: there are no stars in a
    // daylight sky, and `sky.day.stars` is already 0.
    glow: ["#d9ffffff", "#66ffffff", "#1affffff", "#00ffffff"]
};

var metric = {
    hourWidth:        48,

    // The narrowest a chart column may be, which is a different number from the
    // one above and a much smaller one. `hourWidth` is a column the reader is
    // meant to scroll through; this is the floor under a plot that is fitting a
    // whole day into whatever width it has, and it is set by what a two-column
    // header entry has to hold: a 27 px condition glyph, an "11 PM" and a "23°",
    // all centred in 2 x this.
    //
    // 48 was the floor to begin with, and it put a cliff at 1286 px of window:
    // below that the desktop stopped fitting the day and started scrolling -
    // with no scroll control, because the arrows step days. 24 moves the cliff
    // to 734, which is below the width the desktop shell exists at at all.
    minHourWidth:     24,
    plotHeight:       252,
    axisTopPad:       12,
    headerBandHeight: 78,
    gutterWidth:      40,
    stripHeight:      28,
    stripGap:         12,
    panelPadding:     14,
    cardPadding:      22,
    cardRadius:       14,
    panelRadius:      12,
    controlRadius:    8,
    filletRadius:     18,

    // Weather-detail grid, measured off the reference: a 300x250 card with a
    // 12px radius and 16/20 padding, giving every visualisation the same
    // 260-wide box to work in.
    detailCardWidth:  300,
    detailCardHeight: 250,
    detailRadius:     12,
    detailPadH:       20,
    detailPadV:       16,
    detailGap:        16,

    // Between two sections of the page - the hero and the hourly block, the
    // hourly block and the details grid. Wider than any gap inside a section,
    // which is what makes them read as separate things without a rule between
    // them.
    sectionGap:       30,
    pageMargin:       22,

    // ---- mobile shell ----------------------------------------------------
    // A phone has no room for the desktop page's 22 px gutter on both sides:
    // at 390 px that is 11 % of the screen spent on nothing, and the hourly
    // strip loses a whole column to it. 14 is the narrowest inset that still
    // keeps a card's corner radius clear of the screen edge.
    mobileMargin:     14,
    mobileGap:        14,

    // A mobile card's inset. Its own tokens rather than the detail card's,
    // which are named for the twelve-card grid and measured off it - sharing
    // them would mean a change to that grid silently re-padding every screen
    // on the phone.
    mobileCardPadH:   16,
    mobileCardPadV:   14,

    // The bottom nav. 58 is the bar itself; `navSafeArea` is the strip below
    // it that a phone's gesture bar occupies, drawn as part of the nav so the
    // page's bottom padding is one number rather than two that must agree.
    navHeight:        58,
    navSafeArea:      12,

    // The same nav turned on its side, for a tablet in landscape. 76 rather
    // than 58, because a rail's constraint is the label's width where a bar's
    // is its own height: "Monthly" at the axis size is 46 px, and 76 is the
    // narrowest strip that clears it on both sides and still holds a 44 px
    // target.
    navRailWidth:     76,

    // Where a single content column stops growing. The mobile shell runs at
    // 834 px too, and a hero row stretched that wide puts the temperature and
    // the condition at opposite ends of the screen. This is a cap per COLUMN,
    // not per page: two columns are allowed 620 each.
    mobileContentMax: 620,

    // The smallest a control may be in either direction, in device-independent
    // pixels. Not a rounded-up version of what the mouse needs - a pointer is
    // a pixel and a fingertip's contact patch is about 8 mm, so every platform
    // guideline that has measured it lands within a few px of the same number:
    // Apple says 44, Google says 48 dp, the WCAG 2.2 target-size floor is 24
    // and its enhanced level is 44.
    //
    // It applies to the TARGET and not to the mark. A dismiss cross stays 11 px
    // wide because 44 px of drawn cross is a shape shouting at the reader; what
    // grows is the invisible area around it, which is why raising this floor
    // moves nothing on screen.
    hitMin:           44
};

// Type sizes, as tokens rather than as a table in a document, because twelve
// independently-written cards produced seven different sizes for the same role
// when the only thing binding them was prose.
//
// `font.pixelSize` is an int in Qt. A fractional value fails object creation and
// Qt reports it only as `Type X unavailable` from the *parent* file.
//
// Sizes only. `Theme.type.family` - the face these sizes are set in - exists and
// is deliberately not here: the name of the bundled typeface is already written
// down inside the font file, and a second copy in this table is a copy that can
// disagree with it. Theme.qml reads it off the running application instead, and
// says why at more length.
var type = {
    // A section heading on the page: "Hourly", "Weather details". One token,
    // because the two sections had picked 18 and 15 independently and nobody
    // saw it until they were stacked on one page.
    sectionTitle: 18,

    cardTitle:   15,
    detailTitle: 14,

    // The reading: the one number a card exists to show. A card carrying two
    // co-equal readings - sunrise and sunset, speed and gust - uses the pair
    // size for both. There is no third option: a card wanting one is really
    // asking for a different layout.
    reading:     34,
    readingPair: 26,

    status:      14,
    body:        12,
    label:       12,
    axis:        11,

    // The page headline, in the current-conditions card. Deliberately far above
    // `reading`: a detail card's number is one of twelve competing for a sweep
    // of the eye, this one is the answer to the question the page was opened to
    // ask, and it should be readable across a room.
    heroReading: 64,
    heroUnit:    34,   // the degree suffix riding on it
    heroCaption: 32,   // the condition beside it
    heroCaptionLong: 24, // the same, when it runs to two lines on a phone
    heroDetail:  18,   // the outlook sentence, and each value in the slug row
    heroLabel:   14    // a slug's label
};

// Motion, as tokens for exactly the reason type sizes are tokens: the first
// pass at this gave *ranges* in a document - "140-160 ms", "190 ms" - and the
// components that bothered to animate at all came back with 130, 140, 150, 160,
// 170, 190, 340 and 430. Eight durations for four jobs. A range is not a rule.
//
// Easing is not in here because `Easing.OutCubic` is a QML enum and this is a
// plain JS library. **OutCubic unless there is a stated reason** - things
// decelerate into place because they are arriving, not departing.
//
// Theme.qml can hold the enum and now does, as `Theme.motion.easing`. The sixty
// call sites that spell it out are still spelling it out: sweeping them is a
// commit of its own, not a rider on the one that made the tokens observable.
var motion = {
    // A fill, a text colour, a border. Should feel instant rather than
    // animated: you are meant to notice the new colour, not the crossfade.
    tint:    150,

    // Something moved or changed size. Long enough to follow, short enough
    // that you are never waiting for it.
    move:    190,

    // One view becoming another - chart to list, a card opening.
    view:    340,

    // A value finding its place: a dial sweeping to its reading, a bar
    // growing from its baseline, a curve drawing itself in. Slower than
    // `move` on purpose, because the eye is meant to read the journey and
    // not merely notice that something happened.
    reveal:  520,

    // Between one sibling's reveal and the next. Deliberately small: twelve
    // cards at 45 ms is a 500 ms wave that reads as one gesture, where the
    // same wave at 150 ms reads as twelve separate things going off.
    stagger:  45
};

var scale = {
    tempMin:  0,
    tempMax:  40,
    tickStep: 10
};

// Colour ramps, keyed by *normalised axis position*: p = 0 at the top of the value
// axis, p = 1 at the bottom. Keying on position rather than on a unit is what lets
// one gradient implementation serve °C, %, km/h and hPa alike.
//
// The idea itself is the good part of MSN's design: colour encodes the absolute
// value, so a 19° hour reads green and a 27° hour reads tan at the same place on
// the same chart. Alpha falls off downward so the gridlines stay legible through
// the fill.
//
// Every ramp has exactly eight stops, because QML cannot generate GradientStop
// elements from a Repeater and the gradient is therefore declared statically.
var ramp = {
    temp: {
        fill: [{ p: 0.00, c: "#cce0764f" }, { p: 0.20, c: "#ccd59a5e" },
               { p: 0.35, c: "#ccbba083" }, { p: 0.45, c: "#c2a2ab82" },
               { p: 0.55, c: "#b886ab80" }, { p: 0.70, c: "#a05f9670" },
               { p: 0.85, c: "#88508a66" }, { p: 1.00, c: "#6e497f5f" }],
        line: [{ p: 0.00, c: "#f0f5a283" }, { p: 0.20, c: "#f0efc08f" },
               { p: 0.35, c: "#f0decbae" }, { p: 0.45, c: "#f0c8d0a9" },
               { p: 0.55, c: "#f0b1d3a9" }, { p: 0.70, c: "#f094cb9d" },
               { p: 0.85, c: "#f082c391" }, { p: 1.00, c: "#f076b988" }]
    },
    wind: {
        fill: [{ p: 0.00, c: "#cce2684f" }, { p: 0.18, c: "#ccd98a55" },
               { p: 0.34, c: "#ccc9a664" }, { p: 0.48, c: "#c2a8ae76" },
               { p: 0.62, c: "#b88aa48e" }, { p: 0.74, c: "#a06098a0" },
               { p: 0.88, c: "#88528ea6" }, { p: 1.00, c: "#6e4a83a4" }],
        line: [{ p: 0.00, c: "#f0f59a84" }, { p: 0.18, c: "#f0efb78e" },
               { p: 0.34, c: "#f0e2cf9d" }, { p: 0.48, c: "#f0c8d3b2" },
               { p: 0.62, c: "#f0aad0c6" }, { p: 0.74, c: "#f092c8d4" },
               { p: 0.88, c: "#f083c0da" }, { p: 1.00, c: "#f078b6d8" }]
    },
    humidity: {
        fill: [{ p: 0.00, c: "#cc4f8ed6" }, { p: 0.18, c: "#cc5893cf" },
               { p: 0.34, c: "#cc6699c6" }, { p: 0.48, c: "#c2789fb9" },
               { p: 0.62, c: "#b88fa5aa" }, { p: 0.74, c: "#a0a1a79c" },
               { p: 0.88, c: "#88b0a58c" }, { p: 1.00, c: "#6ebc9f7c" }],
        line: [{ p: 0.00, c: "#f08fbdee" }, { p: 0.18, c: "#f097c0e9" },
               { p: 0.34, c: "#f0a2c4e2" }, { p: 0.48, c: "#f0b0c8d9" },
               { p: 0.62, c: "#f0c0cbce" }, { p: 0.74, c: "#f0cdccc3" },
               { p: 0.88, c: "#f0d8cbb6" }, { p: 1.00, c: "#f0e0c8aa" }]
    },
    cloud: {
        fill: [{ p: 0.00, c: "#ccb9c4d6" }, { p: 0.18, c: "#ccaabbd0" },
               { p: 0.34, c: "#cc99b1c9" }, { p: 0.48, c: "#c286a6c2" },
               { p: 0.62, c: "#b8749bbc" }, { p: 0.74, c: "#a06390b6" },
               { p: 0.88, c: "#885585b0" }, { p: 1.00, c: "#6e4a7bab" }],
        line: [{ p: 0.00, c: "#f0e2e9f4" }, { p: 0.18, c: "#f0d6e0f0" },
               { p: 0.34, c: "#f0c8d7ec" }, { p: 0.48, c: "#f0b9cee8" },
               { p: 0.62, c: "#f0a9c5e4" }, { p: 0.74, c: "#f09abce0" },
               { p: 0.88, c: "#f08cb3dc" }, { p: 1.00, c: "#f080abd8" }]
    },
    pressure: {
        fill: [{ p: 0.00, c: "#cc9b86d8" }, { p: 0.18, c: "#cc9089d4" },
               { p: 0.34, c: "#cc848ccf" }, { p: 0.48, c: "#c2788fc9" },
               { p: 0.62, c: "#b86d92c4" }, { p: 0.74, c: "#a06395bf" },
               { p: 0.88, c: "#885a97ba" }, { p: 1.00, c: "#6e5299b6" }],
        line: [{ p: 0.00, c: "#f0cbbdef" }, { p: 0.18, c: "#f0c3c0ec" },
               { p: 0.34, c: "#f0bbc3e9" }, { p: 0.48, c: "#f0b3c6e6" },
               { p: 0.62, c: "#f0abc9e3" }, { p: 0.74, c: "#f0a4cce0" },
               { p: 0.88, c: "#f09dcedd" }, { p: 1.00, c: "#f096d0da" }]
    },
    visibility: {
        fill: [{ p: 0.00, c: "#cc74b8d8" }, { p: 0.18, c: "#cc78b2ce" },
               { p: 0.34, c: "#cc7fabc2" }, { p: 0.48, c: "#c288a4b5" },
               { p: 0.62, c: "#b8939ca8" }, { p: 0.74, c: "#a09e949a" },
               { p: 0.88, c: "#88a88c8d" }, { p: 1.00, c: "#6eb08581" }],
        line: [{ p: 0.00, c: "#f0aadcf0" }, { p: 0.18, c: "#f0aed7e9" },
               { p: 0.34, c: "#f0b4d1e0" }, { p: 0.48, c: "#f0bacbd7" },
               { p: 0.62, c: "#f0c1c5ce" }, { p: 0.74, c: "#f0c8bfc4" },
               { p: 0.88, c: "#f0ceb9bb" }, { p: 1.00, c: "#f0d4b4b2" }]
    },
    // Bar ramps. Sampled per value rather than used as a gradient, so the stops
    // are the category boundaries of the published scale where one exists.
    precip: {
        fill: [{ p: 0.00, c: "#ff3f6fd8" }, { p: 0.18, c: "#ff4879d9" },
               { p: 0.34, c: "#ff5285db" }, { p: 0.48, c: "#ff5d91dd" },
               { p: 0.62, c: "#ff699edf" }, { p: 0.74, c: "#ff77abe2" },
               { p: 0.88, c: "#ff86b8e5" }, { p: 1.00, c: "#ff96c5e8" }],
        line: [{ p: 0.00, c: "#ff6f9be8" }, { p: 1.00, c: "#ffb4d8f2" }]
    },
    // European AQI bands: good, fair, moderate, poor, very poor.
    aqi: {
        fill: [{ p: 0.00, c: "#ff8c1e4b" }, { p: 0.20, c: "#ffc03050" },
               { p: 0.40, c: "#ffe85a50" }, { p: 0.55, c: "#fff08a45" },
               { p: 0.70, c: "#ffe8c93f" }, { p: 0.82, c: "#ffa8d15a" },
               { p: 0.92, c: "#ff5cc79a" }, { p: 1.00, c: "#ff4ec3c8" }],
        line: [{ p: 0.00, c: "#ffb04a70" }, { p: 1.00, c: "#ff8fd8dc" }]
    },
    // WHO UV bands: low, moderate, high, very high, extreme.
    uv: {
        fill: [{ p: 0.00, c: "#ff8b5fc4" }, { p: 0.17, c: "#ffa155bd" },
               { p: 0.33, c: "#ffd6484e" }, { p: 0.50, c: "#fff07038" },
               { p: 0.63, c: "#fff5a02f" }, { p: 0.75, c: "#ffe8c73c" },
               { p: 0.88, c: "#ffa9cf55" }, { p: 1.00, c: "#ff5ec18a" }],
        line: [{ p: 0.00, c: "#ffb98ede" }, { p: 1.00, c: "#ff8fd4ac" }]
    }
};

// ---------------------------------------------------------------------------
// Performance tiers
// ---------------------------------------------------------------------------
// How much of the night sky is built. Not how it is drawn - every tier draws
// the same stars the same way - but how many of them there are.
//
// The sky is the only thing in this app whose cost is a *count* rather than a
// consequence of the data: 130 Rectangles, 9 Shapes with a radial gradient
// each, and 3 more Shapes with a stroked polyline, every one of them
// constructed before the first frame of a phone screen at night. On a desktop
// that is nothing. On a mid-range handset it is the difference between the
// window appearing and the window appearing to hesitate.
//
// The counts are prefixes and not samples, and that is the whole reason this
// works: sky.js seeds every star from its own index, so `field(70)` is the
// first seventy of the same hundred and thirty. A reduced sky is the same sky
// with fewer stars in it, in the same places - so a golden image per tier is
// stable, and switching tiers cannot move a constellation.
var perf = {
    full: {
        starField:      130,
        starBeacons:    9,
        constellations: 3
    },
    // Roughly half the field, half the beacons, and one figure. The figures are
    // the expensive third - a stroked Shape plus its own vertex stars - and one
    // of them still says "those points make a shape", which is the entire job.
    reduced: {
        starField:      70,
        starBeacons:    4,
        constellations: 1
    }
};
