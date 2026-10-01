<!-- SPDX-FileCopyrightText: 2026 Jowi Aoun -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Recorded severe-weather alerts

Bytes two live services actually sent, captured with `curl` on **2026-08-05** and committed.
`record.sh` in this directory is the command that captured them; it is here so the provenance is
executable rather than remembered, and it is **not** run by the build or by CI.

Nothing in `tests/` may reach a network - `docs/04-architecture.md` §4.11, enforced at run time by
`tests/support/networkguard.h` and statically by `tests/tst_sourcerules.cpp`.

Licensing is in `REUSE.toml`, and the two services differ: Canada's is ECCC's Data Server End-use
Licence, and the United States' is a government work with no copyright at all under
17 U.S.C. §105. Neither is Creative Commons, which is why they are annotated separately from every
other fixture in this repository.

## These are harder to re-record than the forecast fixtures

A forecast can be captured anywhere at any time. **An alert can only be captured where one is
actually in force**, so the coordinates in `record.sh` were chosen on the afternoon of the
recording by asking each service what it had and keeping the interesting answers. Run it tomorrow
at the same points and you will very likely record empty collections.

## The files

| File | What it is | What it is evidence of |
|---|---|---|
| `eccc/annapolis-heat.json` | 44.6487,-65.2007 · one warning · 10.5 kB | The commonest Canadian shape: `risk_colour_en` yellow, `alert_type` warning, `status_en` continued. Also **`expiration_datetime` two days before `event_end_datetime`** - the expires-before-ends trap is not an American peculiarity. |
| `eccc/fraser-valley-air-quality.json` | 49.2552,-121.9686 · one warning | **Orange**, and the only feature in the whole 58-alert national set that afternoon with `impact_en` High. A severity mapping tested against one colour is not tested. |
| `eccc/toronto-clear.json` | 43.6532,-79.3832 · 850 B | An empty `FeatureCollection`, HTTP 200. The answer a fall-through provider chain would mistake for "done". |
| `nws/seattle-four.json` | 47.6062,-122.3321 · four alerts · 20.5 kB | The banner's "+3 more". A Heat Advisory graded Moderate and three Air Quality Alerts graded **`Unknown`**; all three have **`ends: null`**; and **two of them share `event`, `senderName` and their entire SAME geocode list**, differing only in `effective`. That last pair is what disproves identifying an alert by what it is about. |
| `nws/siskiyou-heat-advisory.json` | 41.5,-122.5 · one alert · 6.3 kB | **The expiry fixture.** `onset` 2026-08-05T14:15-07:00, `expires` 08-06T05:00-07:00, `ends` 08-06T23:00-07:00. Eighteen hours between the message going stale and the heat stopping. |
| `nws/phoenix-extreme-heat.json` | 33.4484,-112.0740 | severity `Severe` - the tier above everything else here. |
| `nws/denver-air-quality.json` | 39.7392,-104.9903 · two alerts | Two ungraded alerts at one point, from one office. |
| `nws/minneapolis-clear.json` | 44.9778,-93.2650 · 232 B | Nothing in force. |
| `nws/out-of-bounds.json` | 44.7,-65.3 · **HTTP 400** | A Canadian coordinate. `{"detail":"Parameter \"point\" is invalid: out of bounds"}` - **not** an empty list. This is why `NwsAlertProvider` maps 400 to `ErrorKind::Unsupported` and why the fan-out does not count a declining provider as a missing one. |

## Two things the research got backwards, and one it could not have known

Recorded here because they cost real time and would cost it again.

**The CQL2 prefix is inverted.** The plan this work was built from recorded that
`INTERSECTS(properties.geometry, …)` was required and that a bare attribute name returned
`200 matched:0`. Measured live:

```
filter=INTERSECTS(properties.geometry,POINT(-65.3 44.7))   HTTP 500  "query error (check logs)"
filter=INTERSECTS(geometry,POINT(-65.3 44.7))              HTTP 200  1 match
```

Exactly backwards. Whichever way round it is on the day you read this, the useful fact is that the
convention **moved**, silently, in an OGC API - Features **Part 3** extension. So the provider uses
`bbox`, which is **Part 1: Core** - a zero-area bounding box, verified to return the same single
alert for 10,459 bytes against the filter's 10,546.

**GeoMet sends no validator.** No `ETag`, no `Last-Modified`, no `Cache-Control` - only
`Vary: Accept-Encoding`. The conditional GET that `HttpClient` sends everywhere else is a no-op
there, so every Canadian poll is a full ~10 kB transfer. `api.weather.gov` does send an `ETag`, so
most American polls are a 304. The plan's "~264 KB/day" assumed revalidation on both.

**`severity` is optional in practice.** Six of the nine alerts recorded here say `Unknown`, and all
six are Air Quality Alerts. It means the issuer declined to grade, not that the grade is low.

## Re-recording

Don't, unless a field is being added. Every assertion in `tests/tst_alertproviders.cpp` names a
specific county, a specific colour and a specific minute. If you must, run `record.sh`, expect to
re-choose the coordinates, and expect every literal in that file to need re-checking by hand.

`libclimat/providers/fixture/data/seattle/alerts.json` is byte-identical to `nws/seattle-four.json`
on purpose: the parser test and the app's own banner are assertions about the same four alerts, one
at the boundary and one on the screen.
