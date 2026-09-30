// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The component catalogue behind `climat-gallery`.
//
// One entry per component, grouped. An entry names the file to instantiate and
// the properties to instantiate it with, so adding a component to the gallery
// is a few lines here rather than a new QML file - and a component that is in
// the tree but not in this list shows up as a gap you can see.
//
// `variants` renders the same component several times side by side. That is
// what the gallery is for: a toggle you can only see switched off is a toggle
// you have not checked, and the states no screen currently uses are exactly
// the ones that rot.
//
// Fields:
//   name     what to call it in the sidebar
//   file     the .qml to instantiate; omit for a `kind` page
//   kind     "palette" | "type" for the generated foundation pages
//   blurb    one line, shown above the stage
//   stage    { w, h } when the component has no implicit size of its own
//   fills    true for a screen or a shell: in a device frame it takes the
//            whole device rather than a content column inside it
//   variants [{ label, props }]; omit for a single default instance
//
// `stage.w` and the viewport frames interact, and the rule is worth stating:
// a stage width is a stand-in for a host that is not there, so when a frame
// *is* there the frame wins and the component gets the width that viewport's
// shell would actually give it. `stage.h` is left alone - a height in this
// catalogue is usually the component saying it has no opinion, and the frame
// has none either.
.pragma library

// One directory up, because that is where the Climat module is: this file is in
// Climat.Gallery, which maps to `qrc:/qt/qml/Climat/Gallery/`, and theme.js and
// precip.js are part of Climat at `qrc:/qt/qml/Climat/`. There is no
// module-qualified spelling of a JavaScript import in QML - `.import Climat 1.0`
// would bring in the module's *types*, and a `.pragma library` cannot reach a
// QML singleton anyway, which is the reason theme.js still exists as a data
// file at all (see Theme.qml).
//
// So it is a relative URL, and the relationship it encodes is the real one: a
// submodule's parent module is one directory up.
.import "../theme.js" as Theme
.import "../precip.js" as Precip

// A gentle curve for the series specimens, so the two chart primitives have
// something to draw. Sample input for a catalogue, not weather.
function _curve(n, w, h) {
    var pts = [];
    for (var i = 0; i < n; ++i) {
        var t = i / (n - 1);
        var y = 0.5 + 0.34 * Math.sin(t * Math.PI * 1.15 - 0.4);
        pts.push({ x: t * w, y: h - y * h });
    }
    return pts;
}

// Precipitation levels, as specimens. The page can only ever show the weather
// its mock data has, and at 18-28 °C that is rain - so every frozen level below
// exists solely here, which is the argument for having a gallery at all.
//
// `hours` wide of one level, with a dry hour at each end so the wash has an
// edge to draw and you can see where it decided the spell begins.
function _spell(type, intensity, hours) {
    var cells = [null];
    var wet = Precip.uniform(hours, type, intensity);
    for (var i = 0; i < wet.length; ++i)
        cells.push(wet[i]);
    cells.push(null);
    return cells;
}

function _levels(hours) {
    var out = [];
    // The six levels anyone would name first, then the four that only exist
    // because the model is a type crossed with an intensity rather than ten
    // hand-drawn pictures.
    var levels = [
        ["drizzle", "light"],
        ["rain", "light"], ["rain", "moderate"], ["rain", "heavy"],
        ["snow", "light"], ["snow", "moderate"], ["snow", "heavy"],
        ["sleet", "moderate"], ["hail", "moderate"], ["thunder", "heavy"]
    ];
    for (var i = 0; i < levels.length; ++i)
        out.push({
            label: Precip.label(levels[i][0], levels[i][1]),
            props: { cells: _spell(levels[i][0], levels[i][1], hours),
                     hourWidth: 48, contentWidth: 48 * (hours + 2) }
        });
    return out;
}

// ---- alert specimens ---------------------------------------------------------
//
// An alert map in the shape app/viewmodels/alertsdata.cpp publishes. Five of
// them, because the whole point of this group is the four grades the app cannot
// show you on demand: a Canadian summer produces heat warnings, and waiting for
// a tornado in order to review the palette is not a review process.
//
// The wording is real. Each row below is taken from an alert one of the two
// services actually issued - see tests/fixtures/alerts/ - rather than invented,
// because a specimen written to fit the layout is a specimen that proves the
// layout fits itself. NWS descriptions are long, wrapped and bulleted; ECCC's
// carry "Locations:" and "Time span:" on their own lines. Both shapes are here.
function _alert(severity, event, issuer, area, when, sender, description, instruction) {
    return {
        id: "specimen-" + severity,
        event: event,
        headline: event + " issued for " + area,
        description: description,
        instruction: instruction || "",
        area: area,
        sender: sender,
        severityKey: severity,
        severityName: severity.charAt(0).toUpperCase() + severity.slice(1),
        issuerLabel: issuer,
        phase: "active",
        when: when,
        pastDeadline: false,
        acknowledged: false,
        web: ""
    };
}

function _alerts() {
    return {
        extreme: _alert("extreme", "Tornado Warning", "Extreme",
                        "Southern Douglas County", "Until 4:45 PM", "NWS Omaha NE",
                        "At 411 PM CDT, a confirmed large and extremely dangerous tornado was\nlocated near Springfield, moving northeast at 30 mph.\n\nHAZARD...Damaging tornado.\n\nSOURCE...Weather spotters confirmed tornado.",
                        "TAKE COVER NOW! Move to a basement or an interior room on the\nlowest floor of a sturdy building."),
        severe:  _alert("severe", "air quality warning", "orange warning",
                        "Eastern Fraser Valley", "Until Sun 11:00 PM",
                        "Environment and Climate Change Canada",
                        "Locations: Eastern Fraser Valley.\n\nTime span: persisting through the weekend.\n\nRemarks: Wildfire smoke is causing poor air quality and reduced visibility.",
                        ""),
        moderate: _alert("moderate", "Heat Advisory", "Moderate",
                         "Foothills and Valleys of the North Cascades", "Until Fri 11:00 PM",
                         "NWS Seattle WA",
                         "* WHAT...Temperatures up to 95 expected.\n\n* WHERE...Foothills and valleys of the North Cascades.\n\n* WHEN...From noon today to 11 PM PDT Friday.",
                         "Drink plenty of fluids, stay in an air-conditioned room, and check\nup on relatives and neighbors."),
        minor:    _alert("minor", "Rip Current Statement", "Minor",
                         "Coastal Miami Dade County", "Until 8:00 PM", "NWS Miami FL",
                         "* WHAT...Dangerous rip currents expected.\n\n* WHERE...Atlantic beaches of Miami-Dade County.",
                         "Swim near a lifeguard. If caught in a rip current, relax and float."),
        unknown:  _alert("unknown", "Air Quality Alert", "Unknown",
                         "King; Pierce; Snohomish", "Until 5:00 PM", "NWS Seattle WA",
                         "The Puget Sound Clean Air Agency has issued an Air Quality Alert.\nFine particle pollution is expected to reach unhealthy levels.",
                         "")
    };
}

function _bannerVariants() {
    var a = _alerts();
    var out = [];
    var order = ["extreme", "severe", "moderate", "minor", "unknown"];

    for (var i = 0; i < order.length; ++i) {
        out.push({ label: order[i],
                   props: { alert: a[order[i]], moreCount: 0, acknowledged: false,
                            complete: true, unconfirmed: false } });
    }

    // The three states that are about the app rather than about the weather,
    // and the three a live screen almost never reaches.
    out.push({ label: "with more",
               props: { alert: a.moderate, moreCount: 3, acknowledged: false,
                        complete: true, unconfirmed: false } });
    out.push({ label: "acknowledged",
               props: { alert: a.severe, moreCount: 2, acknowledged: true,
                        complete: true, unconfirmed: false } });
    out.push({ label: "unconfirmed",
               props: { alert: a.extreme, moreCount: 0, acknowledged: false,
                        complete: true, unconfirmed: true,
                        confirmedLabel: "Last confirmed 2:05 PM" } });
    out.push({ label: "incomplete",
               props: { alert: a.moderate, moreCount: 0, acknowledged: false,
                        complete: false, unconfirmed: false } });
    return out;
}

var groups = [
    {
        name: "Foundations",
        items: [
            { name: "Colour", kind: "palette",
              blurb: "Every colour token in both schemes, on the background it is actually composited over, with the WCAG ratio it owes that background. Red is a token that does not clear the floor its duty sets." },
            { name: "Ramps", kind: "ramps",
              blurb: "The nine metric ramps in both schemes. Six are continuous and light inverts their lightness; three are published authority bands and light leaves their hues alone." },
            { name: "Type", kind: "type",
              blurb: "The type scale. Sizes are tokens: a range is not a rule." }
        ]
    },
    {
        name: "Alerts",
        items: [
            { name: "Banner", file: "AlertBanner.qml",
              blurb: "One alert, never a stack, plus a count of what it is not showing. The five CAP grades, then the three states that are about the app rather than the weather - dismissed, unconfirmed, and a partial answer.",
              stage: { w: 720 },
              variants: _bannerVariants() },

            { name: "Row", file: "AlertRow.qml",
              blurb: "The sheet's row: the issuer's headline, their paragraph breaks, and the instruction kept apart from the description because one says what is happening and the other says what to do.",
              stage: { w: 620 },
              variants: [
                  { label: "expanded", props: { alert: _alerts().moderate, expanded: true } },
                  { label: "collapsed", props: { alert: _alerts().extreme, expanded: false } },
                  { label: "no instruction", props: { alert: _alerts().severe, expanded: true } }
              ] },

            { name: "Severity glyph", file: "SeverityGlyph.qml",
              blurb: "A different SHAPE per grade, not one shape in five colours - §4.10 forbids colour-only encoding, and about one man in twelve cannot separate this red from this amber.",
              stage: { w: 120, h: 60 },
              variants: [
                  { label: "extreme",  props: { severity: "extreme",  glyphSize: 28 } },
                  { label: "severe",   props: { severity: "severe",   glyphSize: 28 } },
                  { label: "moderate", props: { severity: "moderate", glyphSize: 28 } },
                  { label: "minor",    props: { severity: "minor",    glyphSize: 28 } },
                  { label: "unknown",  props: { severity: "unknown",  glyphSize: 28 } }
              ] }
        ]
    },
    {
        name: "Weather details",
        items: [
            { name: "Card shell", file: "DetailCard.qml",
              blurb: "The anatomy the twelve share: title, visualisation slot, status line, context. Shown with the slot empty.",
              variants: [
                  { label: "with trend", props: { title: "Card title", status: "Status", body: "Two lines of context sit here, and the shell reserves both whether the sentence fills them or not.", trend: "up" } },
                  { label: "no trend", props: { title: "Card title", status: "Status", body: "One line.", trend: "none" } }
              ] },
            { name: "Temperature",   file: "DetailTemperatureCard.qml",   blurb: "Twelve-hour sparkline; observed solid, forecast dimmed." },
            { name: "Feels like",    file: "DetailFeelsLikeCard.qml",     blurb: "Two curves and the band between them - the card's subject is the distance." },
            { name: "Cloud cover",   file: "DetailCloudCoverCard.qml",    blurb: "Dial, ramped off the cloud palette: clear reads sky-blue, overcast near-white." },
            { name: "Precipitation", file: "DetailPrecipitationCard.qml", blurb: "The number says how much; the columns say when." },
            { name: "Wind",          file: "DetailWindCard.qml",          blurb: "Compass rose: blunt end into the wind, reach scales with speed." },
            { name: "Humidity",      file: "DetailHumidityCard.qml",      blurb: "Eight readings against full-height tracks." },
            { name: "UV",            file: "DetailUvCard.qml",            blurb: "WHO bands are the palette, so the ring itself carries the reading." },
            { name: "Air quality",   file: "DetailAirQualityCard.qml",    blurb: "The same dial as UV, on the European AQI bands." },
            { name: "Visibility",    file: "DetailVisibilityCard.qml",    blurb: "A sight line down the long axis of the box." },
            { name: "Pressure",      file: "DetailPressureCard.qml",      blurb: "Sparkline against a fixed 1005-1020 mb scale." },
            { name: "Sun",           file: "DetailSunCard.qml",           blurb: "Altitude as a sinusoid; the horizon is zero, so daylight's width is the day length." },
            { name: "Moon",          file: "DetailMoonCard.qml",          blurb: "The sun card's twin, on the night ramp." },
            { name: "Moon phase",    file: "DetailMoonPhaseCard.qml",     blurb: "The face rather than the night: the disc at the size a gibbous can be told from a quarter, and the date the month is navigated by." }
        ]
    },
    {
        name: "Screens",
        items: [
            { name: "Weather page", file: "WeatherPage.qml", stage: { w: 1300, h: 740 }, fills: true,
              blurb: "The whole thing: location, current conditions, hourly, details. Scrolls - its Flickable is layered, which is what keeps every chart on it inside the viewport." },
            // 980, not the page's own 1244: the gallery's rail takes 232 px, so a
            // full-width stage runs off the right of a default window and clips
            // the dew-point slug and the high/low - the two things furthest right.
            { name: "Current conditions", file: "CurrentConditions.qml", stage: { w: 980, h: 0 },
              blurb: "The page headline. Every number on it is read from the same place the detail card for that measurable reads from, so the two cannot drift." },
            { name: "Weather details grid", file: "WeatherDetails.qml", stage: { w: 1244, h: 0 },
              blurb: "All twelve, responsive columns. Lays out in full and reports its height; the page it sits on owns the scrolling." },
            { name: "Hourly overview", file: "HourlyOverview.qml", stage: { w: 1000, h: 430 },
              blurb: "The chart panel: crossed gradients, no stroked line.",
              variants: [
                  { label: "overview", props: { metricId: "overview" } },
                  { label: "wind",     props: { metricId: "wind" } },
                  { label: "uv",       props: { metricId: "uv" } }
              ] },
            { name: "Hourly list", file: "HourlyList.qml", stage: { w: 1000, h: 372 },
              blurb: "The same hours as cards along the day, for reading a number rather than a shape. Its arrows walk the hours and then step the day." },
            { name: "Day strip", file: "DayStrip.qml", stage: { w: 1100, h: 130 },
              blurb: "Seven days; the selected card abuts the panel below rather than overlapping it." },
            { name: "Metric tab bar", file: "MetricTabBar.qml", stage: { w: 900, h: 38 },
              blurb: "Metric pills and the chart/list switch." }
        ]
    },
    {
        // Everything below runs at 390 px in the app. Reviewing it at the
        // gallery's own stage width is reviewing a layout the phone never
        // draws - pick the Mobile viewport in the rail, or pass
        // `--viewport mobile` alongside `--gallery`.
        name: "Mobile screens",
        items: [
            { name: "Mobile shell", file: "MobileShell.qml", fills: true,
              blurb: "Five destinations under a bottom nav. The nav pill slides; the page behind it does not transition.",
              variants: [
                  { label: "today",   props: { tab: "today" } },
                  { label: "hourly",  props: { tab: "hourly" } },
                  { label: "monthly", props: { tab: "monthly" } }
              ] },
            { name: "Today screen", file: "MobileTodayPage.qml", fills: true,
              blurb: "The screen the app opens on: headline, hourly strip, ten days, sun & moon, pollen, activities." },
            { name: "Hourly screen", file: "MobileHourlyPage.qml", fills: true,
              blurb: "The desktop's chart card at 40 px columns and a shorter plot, with the ten metric pills replaced by one button." },
            { name: "Monthly screen", file: "MobileMonthlyPage.qml", fills: true,
              blurb: "A month of forecasts, four days to a row rather than seven." },
            { name: "Maps screen", file: "MobileMapsPage.qml", fills: true,
              blurb: "The placeholder. There is no map component; this says so in a way a screenshot cannot hide." },
            { name: "Me screen", file: "MobileMePage.qml", fills: true,
              blurb: "Units, places and attribution. The one screen here that is a proposal rather than a rebuild." }
        ]
    },
    {
        name: "Mobile parts",
        items: [
            { name: "Shell nav", file: "ShellNav.qml", stage: { w: 390, h: 0 },
              blurb: "The only persistent chrome the phone has, and the only thing on it allowed to be more opaque than a wash. A landscape tablet stands it on end.",
              variants: [
                  { label: "today", props: { currentId: "today" } },
                  { label: "maps",  props: { currentId: "maps" } },
                  { label: "me",    props: { currentId: "me" } }
              ] },
            // The same file on its side. Its own entry rather than two more
            // variants of the one above, because a stage is declared per entry
            // and a 76x620 rail beside a 390x70 bar is not one stage.
            { name: "Nav rail", file: "ShellNav.qml", stage: { w: 76, h: 620 },
              blurb: "A landscape tablet's navigation. Five targets across 1112 px is a row a thumb cannot cross, so they stand up instead.",
              variants: [
                  { label: "today", props: { currentId: "today", orientation: Qt.Vertical } },
                  { label: "maps",  props: { currentId: "maps",  orientation: Qt.Vertical } }
              ] },
            { name: "Mobile card", file: "MobileCard.qml", stage: { w: 362, h: 0 },
              blurb: "The shell the phone's cards are built in. Unlike DetailCard it grows to its body, because on a phone a card is as tall as what is in it.",
              variants: [
                  { label: "with link", props: { title: "Today", link: "Hourly" } },
                  { label: "plain",     props: { title: "Sun & Moon" } }
              ] },
            { name: "Mobile hero", file: "MobileCurrentWeather.qml", stage: { w: 362, h: 0 },
              blurb: "The desktop headline with no card, no high/low, and six slugs wrapped three by two." },
            { name: "Hour strip", file: "MobileHourStrip.qml", stage: { w: 362, h: 0 },
              blurb: "Twenty-four hours, two at a time. The band's top edge is the temperature - the reference draws it flat." },
            { name: "Ten-day strip", file: "MobileDailyStrip.qml", stage: { w: 362, h: 0 },
              blurb: "A readout, not a control. Highs bold, lows not: the pair is the reading and one of them has to lead." },
            { name: "Sun & moon", file: "MobileSunMoonCard.qml", stage: { w: 330, h: 0 },
              blurb: "Two arcs on one reveal. They have to leave together or it reads as a race." },
            { name: "Sky arc", file: "SkyArc.qml", stage: { w: 170, h: 0 },
              blurb: "Progress from rise to set. Not DetailSunCard's altitude sinusoid - at this size that is a bump with nothing to read.",
              variants: [
                  { label: "mid-morning", props: { riseMin: 364, setMin: 1243, nowMin: 620,
                                                   riseLabel: "6:04", riseSuffix: "AM", riseName: "Sunrise",
                                                   setLabel: "8:43", setSuffix: "PM", setName: "Sunset",
                                                   span: "14 hrs 39 mins" } },
                  { label: "just risen",  props: { riseMin: 364, setMin: 1243, nowMin: 400,
                                                   riseLabel: "6:04", riseSuffix: "AM", riseName: "Sunrise",
                                                   setLabel: "8:43", setSuffix: "PM", setName: "Sunset",
                                                   span: "14 hrs 39 mins" } },
                  { label: "already set", props: { riseMin: 364, setMin: 1243, nowMin: 1400,
                                                   riseLabel: "6:04", riseSuffix: "AM", riseName: "Sunrise",
                                                   setLabel: "8:43", setSuffix: "PM", setName: "Sunset",
                                                   span: "14 hrs 39 mins" } }
              ] },
            { name: "Pollen", file: "MobilePollenCard.qml", stage: { w: 330, h: 0 },
              blurb: "The headline is a word, because the published answer is a band and nobody reads grains per cubic metre." },
            { name: "Activities", file: "MobileActivitiesCard.qml", stage: { w: 330, h: 0 },
              blurb: "Five verdicts. The dot is last on the row, or a column of colours outreads the labels saying what they are about." },
            { name: "Week strip", file: "MobileWeekStrip.qml", stage: { w: 362, h: 0 },
              blurb: "Seven days across a phone, so a column is 48 px. That is why it is not DayStrip." },
            { name: "Metric picker", file: "MobileMetricPicker.qml",
              blurb: "Ten pills become one button and a list. Shown open, because a menu you have only seen shut is a menu you have not checked.",
              stage: { w: 0, h: 0 },
              variants: [
                  { label: "closed", props: { currentId: "overview", open: false } },
                  { label: "open",   props: { currentId: "wind",     open: true } }
              ] },
            { name: "Calendar", file: "MobileCalendar.qml", stage: { w: 330, h: 0 },
              blurb: "Four columns, not seven. A forecast is scanned for warm stretches, not looked up by weekday." },
            { name: "Map placeholder", file: "MapPlaceholder.qml", stage: { w: 362, h: 520 },
              blurb: "Deliberately off-palette and deliberately ugly. A tasteful empty state is what a finished screen with no data looks like." },
            { name: "Nav glyph", file: "NavGlyph.qml",
              blurb: "Outlines, not fills: at 22 px in a row of five, the silhouette is all there is to tell them apart.",
              variants: [
                  { label: "today",   props: { kind: "today",   glyphSize: 30 } },
                  { label: "hourly",  props: { kind: "hourly",  glyphSize: 30 } },
                  { label: "monthly", props: { kind: "monthly", glyphSize: 30 } },
                  { label: "maps",    props: { kind: "maps",    glyphSize: 30 } },
                  { label: "me",      props: { kind: "me",      glyphSize: 30 } }
              ] }
        ]
    },
    {
        name: "Controls",
        items: [
            { name: "Chevron", file: "ChevronGlyph.qml",
              blurb: "One path, rotated. Four hand-written ones is four chances for one to drift off its siblings.",
              variants: [
                  { label: "right", props: { direction: "right", glyphSize: 22 } },
                  { label: "down",  props: { direction: "down",  glyphSize: 22 } },
                  { label: "left",  props: { direction: "left",  glyphSize: 22 } },
                  { label: "up",    props: { direction: "up",    glyphSize: 22 } }
              ] },
            { name: "Location bar", file: "LocationBar.qml",
              blurb: "Sits on the page gradient rather than on a surface - there is nothing here to lift off the background.",
              variants: [
                  { label: "home",     props: { label: "Toronto, Ontario", isHome: true } },
                  { label: "not home", props: { label: "Reykjavík, Iceland", isHome: false } }
              ] },
            { name: "Section header", file: "SectionHeader.qml", stage: { w: 420, h: 0 },
              blurb: "One token for every section title on the page. The hourly and details sections had picked 18 and 15 independently.",
              variants: [
                  { label: "with stamp", props: { title: "Weather details", stamp: "12:28 PM" } },
                  { label: "plain",      props: { title: "Hourly" } }
              ] },
            { name: "Trend badge", file: "TrendBadge.qml",
              blurb: "The arrow tracks the number, not whether the news is good.",
              variants: [
                  { label: "up",     props: { direction: "up",     badgeSize: 22 } },
                  { label: "down",   props: { direction: "down",   badgeSize: 22 } },
                  { label: "steady", props: { direction: "steady", badgeSize: 22 } },
                  { label: "none",   props: { direction: "none",   badgeSize: 22 } }
              ] },
            { name: "Feels-like toggle", file: "FeelsLikeToggle.qml",
              blurb: "Both states, because a switch you have only seen off is a switch you have not checked.",
              variants: [
                  { label: "off", props: { checked: false } },
                  { label: "on",  props: { checked: true } }
              ] },
            { name: "Pager button", file: "PagerButton.qml",
              blurb: "Floats over the chart, so it stays more opaque than a surface.",
              variants: [
                  { label: "left",     props: { pointsLeft: true,  enabledState: true } },
                  { label: "right",    props: { pointsLeft: false, enabledState: true } },
                  { label: "disabled", props: { pointsLeft: true,  enabledState: false } }
              ] },
            { name: "Tab fillet", file: "TabFillet.qml", stage: { w: 40, h: 40 },
              blurb: "The concave corner where a selected tab meets its panel.",
              variants: [
                  { label: "left",  props: { filletRadius: 18, fillColor: "#33ffffff", mirrored: false } },
                  { label: "right", props: { filletRadius: 18, fillColor: "#33ffffff", mirrored: true } }
              ] }
        ]
    },
    {
        // The preferences screen, in parts. It is the one screen in this app
        // that is the same objects in both shells - PreferencesSheet puts these
        // groups on a desktop sheet and MobileMePage puts the same two on the
        // phone's Me tab - so reviewing them here is reviewing both.
        name: "Preferences",
        items: [
            { name: "General", file: "PrefGeneral.qml", stage: { w: 520, h: 0 },
              blurb: "Reads and writes the real preferences: switching one here changes the gallery's own window." },
            { name: "Units", file: "PrefUnits.qml", stage: { w: 520, h: 0 },
              blurb: "Two presets over five per-quantity preferences. Change one row and both radios empty - that state is `custom`, and it is the model being honest." },
            { name: "About", file: "PrefAbout.qml", stage: { w: 520, h: 0 },
              blurb: "Version, licence, source and issues. The Qt row is also the notice Qt's LGPL asks for, and both licences unfold from the copies built into the app.",
              variants: [
                  { label: "folded",        props: {} },
                  { label: "the LGPL open", props: { reading: "lgpl" } }
              ] },
            { name: "Data sources", file: "SourcesList.qml", stage: { w: 488, h: 0 },
              blurb: "Every source in its licence's own words. The desktop sheet and the Me tab show this same list." },
            { name: "Preference row", file: "PrefRow.qml", stage: { w: 460, h: 0 },
              blurb: "Title, sentence, control. The three shapes it takes; the control slot needs a Component, so the two groups above are where it is reviewed with one in it.",
              variants: [
                  { label: "with a sentence", props: { title: "Dynamic background", subtitle: "The page follows the sky over the place on screen - night, dawn, day and dusk." } },
                  { label: "title only",      props: { title: "Wind" } },
                  { label: "not interactive", props: { title: "Theme", subtitle: "Following the desktop, which is dark.", interactive: false } }
              ] },
            { name: "Preference switch", file: "PrefSwitch.qml",
              blurb: "FeelsLikeToggle without the caption: a preferences row has already said what the switch means, twice.",
              variants: [
                  { label: "off", props: { checked: false } },
                  { label: "on",  props: { checked: true } }
              ] },
            { name: "Segment", file: "PrefSegment.qml",
              blurb: "Both options visible at rest, which is the whole difference from a cycling row. `custom` selects nothing rather than lying about the first cell.",
              variants: [
                  { label: "two",    props: { options: [{ id: "24h", label: "24 hour" }, { id: "12h", label: "AM / PM" }], currentId: "12h" } },
                  { label: "three",  props: { options: [{ id: "system", label: "System" }, { id: "light", label: "Light" }, { id: "dark", label: "Dark" }], currentId: "system" } },
                  { label: "custom", props: { options: [{ id: "metric", label: "Metric" }, { id: "imperial", label: "Imperial" }], currentId: "custom" } }
              ] },
            { name: "Gear", file: "GearGlyph.qml",
              blurb: "The one pictogram in this app whose meaning is learned rather than read - which is why it is the only borrowed one.",
              variants: [
                  { label: "18", props: { glyphSize: 18 } },
                  { label: "28", props: { glyphSize: 28 } },
                  { label: "56", props: { glyphSize: 56 } }
              ] }
        ]
    },
    {
        name: "Glyphs",
        items: [
            { name: "Weather glyph", file: "WeatherGlyph.qml",
              blurb: "Condition icons, drawn rather than shipped as raster. All thirteen "
                     + "of ConditionKind - the six at the end were folded into their "
                     + "neighbours until the pictures existed.",
              variants: [
                  { label: "clear-day",    props: { kind: "clear-day",    glyphSize: 44 } },
                  { label: "clear-night",  props: { kind: "clear-night",  glyphSize: 44 } },
                  { label: "partly-day",   props: { kind: "partly-day",   glyphSize: 44 } },
                  { label: "partly-night", props: { kind: "partly-night", glyphSize: 44 } },
                  { label: "cloudy",       props: { kind: "cloudy",       glyphSize: 44 } },
                  { label: "rain",         props: { kind: "rain",         glyphSize: 44 } },
                  { label: "rain-night",   props: { kind: "rain-night",   glyphSize: 44 } },
                  { label: "fog",          props: { kind: "fog",          glyphSize: 44 } },
                  { label: "drizzle",      props: { kind: "drizzle",      glyphSize: 44 } },
                  { label: "sleet",        props: { kind: "sleet",        glyphSize: 44 } },
                  { label: "snow",         props: { kind: "snow",         glyphSize: 44 } },
                  { label: "thunder",      props: { kind: "thunder",      glyphSize: 44 } },
                  { label: "hail",         props: { kind: "hail",         glyphSize: 44 } }
              ] },
            { name: "Day icon badge", file: "DayIconBadge.qml",
              blurb: "The glyph on its pale day plate - every mark gets a darker variant "
                     + "there or it vanishes.",
              variants: [
                  { label: "day",     props: { kind: "partly-day",   night: false, badgeSize: 56 } },
                  { label: "night",   props: { kind: "partly-night", night: true,  badgeSize: 56 } },
                  // The three marks the plate is hardest on, staged so the
                  // OnLight tokens are reviewable rather than only measurable.
                  { label: "rain",    props: { kind: "rain",         night: false, badgeSize: 56 } },
                  { label: "snow",    props: { kind: "snow",         night: false, badgeSize: 56 } },
                  { label: "thunder", props: { kind: "thunder",      night: false, badgeSize: 56 } }
              ] },
            { name: "Moon glyph", file: "MoonGlyph.qml",
              blurb: "Illumination is a real parameter, not three fixed pictures.",
              variants: [
                  { label: "0.08", props: { illuminated: 0.08, glyphSize: 40 } },
                  { label: "0.50", props: { illuminated: 0.50, glyphSize: 40 } },
                  { label: "0.95", props: { illuminated: 0.95, glyphSize: 40 } }
              ] },
            { name: "Sun event glyph", file: "SunEventGlyph.qml",
              blurb: "Sunrise and sunset: the half-disc sits above or below the horizon line, so the two differ in shape and not only in colour.",
              variants: [
                  { label: "sunrise", props: { kind: "sunrise", glyphSize: 40 } },
                  { label: "sunset",  props: { kind: "sunset",  glyphSize: 40 } }
              ] },
            { name: "Droplet", file: "DropletGlyph.qml",
              blurb: "Precipitation marker.",
              variants: [ { label: "", props: { glyphSize: 34 } } ] }
        ]
    },
    {
        name: "Chart parts",
        items: [
            { name: "Series area", file: "SeriesArea.qml", stage: { w: 480, h: 150 },
              blurb: "Crossed gradients: colour across, alpha down. Sample curve, not weather.",
              variants: [
                  { label: "temperature", props: { points: _curve(13, 480, 150), baselineY: 150,
                                                   gradientTop: 0, gradientBottom: 150,
                                                   fillRamp: Theme.ramp.temp.fill, lineRamp: Theme.ramp.temp.line } },
                  { label: "humidity",    props: { points: _curve(13, 480, 150), baselineY: 150,
                                                   gradientTop: 0, gradientBottom: 150,
                                                   fillRamp: Theme.ramp.humidity.fill, lineRamp: Theme.ramp.humidity.line } }
              ] },
            { name: "Series bars", file: "SeriesBars.qml", stage: { w: 480, h: 150 },
              blurb: "For sums and bands. Sample values, not weather.",
              variants: [
                  { label: "cloud", props: { values: [12, 20, 34, 46, 58, 71, 66, 52, 44, 30], hourWidth: 48,
                                             axisTop: 8, axisBottom: 142, minValue: 0, maxValue: 100,
                                             ramp: Theme.ramp.cloud.fill } }
              ] },
            { name: "Precipitation strip", file: "PrecipitationStrip.qml", stage: { w: 480, h: 28 },
              blurb: "One cell per label interval; past hours are hatched, not blank." },
            { name: "Precipitation wash", file: "PrecipBands.qml", stage: { w: 480, h: 120 },
              blurb: "When it falls. Hue is the type, alpha the intensity - a narrow ladder on purpose, because \"is it raining here\" has to read the same at every level.",
              variants: _levels(8) },
            { name: "Precipitation field", file: "PrecipField.qml", stage: { w: 480, h: 200 },
              blurb: "What is falling, and how hard. Ten levels off one particle model: type picks the shape, intensity scales the count, size and speed. Shown without the wash it normally sits over.",
              variants: _levels(8) },
            { name: "Hatch", file: "HatchPattern.qml", stage: { w: 240, h: 90 },
              blurb: "\"The past - there is no forecast here\", so absent data reads as deliberate." }
        ]
    }
];
