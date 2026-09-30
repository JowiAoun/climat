// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
// The sky behind the page: which part of the day it is, and where the stars go.
//
// Geometry and phase logic only - the colours are in theme.js, because they
// are tokens like every other colour here.
//
// ---- everything in this file is deterministic --------------------------------
// There is no Math.random anywhere in it, and that is not a style preference.
// §10.6: "No Math.random, anywhere, including in timing. Determinism is not
// negotiable." A star field seeded from a random number generator would make
// every golden image of every mobile screen a different picture, which would
// quietly destroy the only regression net this prototype has.
//
// `_hash` is the standard sine-fract hash. It is not a good random number
// generator and does not need to be: what it has to do is scatter 120 points
// across a rectangle without a visible lattice, and it does.
.pragma library

function _hash(i) {
    var x = Math.sin(i * 12.9898 + 78.233) * 43758.5453;
    return x - Math.floor(x);
}

// ---------------------------------------------------------------------------
// Phase
// ---------------------------------------------------------------------------
// Four, because two is not enough and six is a gradient with names. Dawn and
// dusk exist because they are the hours a weather app is actually opened in
// and because they are the only ones where the sky is doing something a
// photograph would be about.
//
// The twilight bands are 70 minutes either side of the crossing - near enough
// to civil twilight at temperate latitudes, and deliberately a constant rather
// than a solar-depression calculation. The real thing belongs in libclimat with
// the rest of the ephemeris; here it decides a gradient.
var twilightMinutes = 70;

function phaseAt(nowMin, riseMin, setMin) {
    function near(t) {
        var d = Math.abs(((nowMin - t) % 1440 + 1440 + 720) % 1440 - 720);
        return d <= twilightMinutes;
    }
    if (near(riseMin))
        return "dawn";
    if (near(setMin))
        return "dusk";

    // Up-window taken modulo a day, so a rise-after-set pair still measures
    // daylight rather than a negative span.
    var up = ((setMin - riseMin) % 1440 + 1440) % 1440;
    var since = ((nowMin - riseMin) % 1440 + 1440) % 1440;
    return since < up ? "day" : "night";
}

// ---------------------------------------------------------------------------
// The star field
// ---------------------------------------------------------------------------
// Positions are normalised, so the field stretches with the window. Stars are
// points and a stretched point is still a point - which is exactly why the
// constellations below are *not* placed this way.
//
// `r` is in pixels and `a` is alpha. Both are drawn from the same hash as the
// position, so a star's size and brightness are properties of that star rather
// than of the order it happens to be created in.
function field(count) {
    var out = [];
    for (var i = 0; i < count; ++i) {
        var h1 = _hash(i * 3 + 1);
        var h2 = _hash(i * 3 + 2);
        var h3 = _hash(i * 3 + 3);

        // Most stars are faint and small. Cubing the brightness hash is what
        // stops the field reading as a regular dot screen: an even spread of
        // alpha gives 120 stars of roughly one weight, and a night sky is a
        // few bright ones over a haze of almost-nothing.
        //
        // The field is the haze. The bright few are the beacons and the
        // constellations' own stars, so a field star stops at 0.55 alpha and
        // 1.45 px. It went to 0.90 and 2 px, as bright as those, and the
        // content scrolls over a sky that does not move: sooner or later one
        // lands between two words and reads as a full stop.
        var bright = h3 * h3 * h3;
        out.push({
            x: h1,
            y: h2,
            r: 0.55 + bright * 0.9,
            a: 0.18 + bright * 0.37
        });
    }
    return out;
}

// A handful that get a glow rather than a hard dot. §10.1's test for a stacked
// wash is whether you can trace an edge, and these fade to zero alpha at the
// rim - which makes them glows and legal. The plain field above is small
// enough that its dots are points, not discs.
function beacons(count) {
    var out = [];
    for (var i = 0; i < count; ++i) {
        out.push({
            x: _hash(i * 7 + 101),
            y: _hash(i * 7 + 102),
            r: 1.6 + _hash(i * 7 + 103) * 1.1
        });
    }
    return out;
}

// ---------------------------------------------------------------------------
// Constellations
// ---------------------------------------------------------------------------
// Real asterisms, and placed differently from the field on purpose.
//
// A constellation is a *shape*, and normalised coordinates would stretch it:
// the Plough at 390x844 and the same Plough at 1340x762 would be two different
// figures, and the one thing everybody knows about the Plough is what it looks
// like. So the anchor is normalised - where on the screen it sits - and the
// points are in a square unit space scaled by one number, which keeps the
// figure rigid at any aspect ratio.
//
// Coordinates are eyeballed from a star chart rather than computed from right
// ascension and declination. They are recognisable, which is all this has to
// be; a correct projection of the northern sky for the reader's latitude and
// date is a thing libclimat could genuinely do later, and would be a much
// better feature than this is.
var constellations = [
    {
        name: "Plough",
        anchor: { x: 0.16, y: 0.07 },
        scale: 0.34,                 // of the smaller window dimension
        // Handle, then the bowl, closing back on Megrez.
        points: [
            { x: 0.00, y: 0.46 },    // Alkaid
            { x: 0.17, y: 0.30 },    // Mizar
            { x: 0.35, y: 0.22 },    // Alioth
            { x: 0.52, y: 0.27 },    // Megrez
            { x: 0.57, y: 0.50 },    // Phecda
            { x: 0.83, y: 0.46 },    // Merak
            { x: 0.78, y: 0.20 }     // Dubhe
        ],
        close: 3                     // last point joins back to Megrez
    },
    {
        name: "Cassiopeia",
        anchor: { x: 0.62, y: 0.30 },
        scale: 0.30,
        points: [
            { x: 0.00, y: 0.10 },
            { x: 0.20, y: 0.40 },
            { x: 0.42, y: 0.14 },
            { x: 0.64, y: 0.44 },
            { x: 0.88, y: 0.08 }
        ],
        close: -1
    },
    {
        name: "Orion's belt",
        anchor: { x: 0.24, y: 0.63 },
        scale: 0.22,
        points: [
            { x: 0.00, y: 0.06 },
            { x: 0.26, y: 0.20 },
            { x: 0.52, y: 0.34 }
        ],
        close: -1
    }
];

// The polyline for one constellation, in item pixels.
function figurePath(c, width, height) {
    var unit = Math.min(width, height) * c.scale;
    var ox = c.anchor.x * width;
    var oy = c.anchor.y * height;

    function px(p) { return (ox + p.x * unit).toFixed(2) + " " + (oy + p.y * unit).toFixed(2); }

    if (!c.points || c.points.length < 2)
        return "";

    var d = "M " + px(c.points[0]);
    for (var i = 1; i < c.points.length; ++i)
        d += " L " + px(c.points[i]);
    if (c.close >= 0 && c.close < c.points.length)
        d += " L " + px(c.points[c.close]);
    return d;
}

// Vertex positions for one constellation, so the figure's own stars can be
// drawn brighter than the field they sit in.
function figureStars(c, width, height) {
    var unit = Math.min(width, height) * c.scale;
    var out = [];
    for (var i = 0; i < c.points.length; ++i) {
        out.push({
            x: c.anchor.x * width + c.points[i].x * unit,
            y: c.anchor.y * height + c.points[i].y * unit
        });
    }
    return out;
}
