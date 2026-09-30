<!-- SPDX-FileCopyrightText: 2026 Jowi Aoun -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# 02 - Data Sources, Licensing and Attribution

This is the most important research document in the repo. Data availability - not Qt,
not C++ - is what will decide whether Climat reaches MSN parity.

Headline conclusions:

1. **Open-Meteo is the right primary provider.** No key, global, 18+ national models,
   free for non-commercial use under CC-BY 4.0, and self-hostable if we ever outgrow it.
2. **Open-Meteo has no severe-weather alerts product.** Alerts must be region-routed
   across NWS (US), MeteoAlarm (EU/UK/IL), ECCC (CA) and others. This is real work.
3. **Radar is the hardest problem.** The convenient global option (RainViewer) is
   licensed for *personal/educational use only* and therefore cannot be our default in a
   widely distributed app. We must region-route open national composites, or self-host.
4. Everything we need is obtainable **without any API key and without a backend service**,
   which keeps the app installable and private by default.

---

## 2.1 Forecast API comparison

| API | Coverage | Key? | Cost / limits | Licence | Models exposed | Alerts | Verdict |
|---|---|---|---|---|---|---|---|
| **Open-Meteo** | Global | ❌ none | Free non-commercial: <10 000/day, 5 000/h, 600/min | CC-BY 4.0 | **18+** (ECMWF IFS/AIFS, GFS, HRRR, ICON, ARPEGE, AROME, UKMO, GEM, JMA, KMA, BOM, CMA, …) | ❌ | ✅ **Primary** |
| **MET Norway Locationforecast 2.0** | Global | ❌ | 20 req/s; **mandatory identifying User-Agent** or 403 | CC-BY 4.0 | MET Nordic / ECMWF blend | Norway only | ✅ **Fallback** |
| **NWS api.weather.gov** | US + territories | ❌ | Polite use; UA with contact requested | US public domain | NDFD / NBM | ✅ **CAP** | ✅ US alerts + obs |
| **Bright Sky** (DWD wrapper) | Germany + DWD domain | ❌ | Free | MIT code / DWD data (GeoNutzV) | ICON-D2/EU | DWD warnings | ⭕ Optional DE |
| **ECCC GeoMet / MSC** | Canada | ❌ | Free | Open Government Licence - Canada | GDPS/RDPS/HRDPS | ✅ | ⭕ CA alerts + radar |
| OpenWeatherMap | Global | ✅ | 1 000/day free, then paid | Proprietary | Own blend | Paid tier | ❌ key friction |
| WeatherAPI.com | Global | ✅ | 1M/mo free | Proprietary | Own blend | ✅ | ❌ key friction |
| Tomorrow.io | Global | ✅ | 500/day free | Proprietary | Own | ✅ | ❌ key friction |
| Xweather (Aeris) | Global | ✅ | Paid | Proprietary | Multi | ✅ global CAP aggregation | ⭕ only if alerts routing fails |

**Decision:** Open-Meteo primary, MET Norway as automatic fallback, NWS/ECCC/DWD as
regional enrichment. Never require an API key to use the app.

## 2.2 Open-Meteo product matrix - what we consume and where

| Endpoint | What it gives us | Which Climat screen |
|---|---|---|
| `/v1/forecast` | Current + hourly + daily, up to **16 days**, 50+ variables | Home, Hourly, 10-Day |
| `minutely_15` params | 15-minute resolution - **Central Europe + North America only**, elsewhere interpolated hourly; lightning potential in HRRR regions | Next-hour precipitation ribbon |
| Model-specific endpoints (`/v1/ecmwf`, `/v1/gfs`, `/v1/dwd-icon`, …) | Individual model runs | **Model comparison view** |
| `/v1/ensemble` | Ensemble members + mean (incl. Google WeatherNext) | **Confidence bands / spaghetti plot** |
| `/v1/air-quality` | PM2.5, PM10, O₃, NO₂, SO₂, CO, dust, AOD, UV, US + European AQI; **pollen (alder, birch, grass, mugwort, olive, ragweed) - Europe only, 4-day** | Air Quality tab |
| `/v1/archive` (Historical Weather) | ERA5 reanalysis from 1940 | **30-year same-day history** (MSN parity) |
| Historical Forecast API | Past *forecasts* as issued | **Forecast-accuracy scoreboard** (our differentiator) |
| `/v1/marine` | Wave height/period/direction | Coastal / marine card |
| `/v1/climate` | CMIP6 downscaled projections | "Climate" long-view (post-1.0) |
| `/v1/flood` | GloFAS river discharge | Flood card (post-1.0) |
| `/v1/seasonal` | Long-range | Post-1.0 |
| Satellite Radiation API | Solar irradiance from satellite | Solar card (niche) |
| **Geocoding API** | GeoNames-backed global search, any language, postcodes, ≤100 results, protobuf option | Location search |
| **Elevation API** | 90 m DEM | Elevation-corrected display |

Notes captured from the docs: automatic grid-cell selection using 90 m DEM with
statistical downscaling for elevation; seamless stitching of model runs so there are no
gaps; multi-location requests via comma-separated coordinates; JSON/CSV/XLSX and
protobuf/FlatBuffers-style binary on some endpoints; `timezone=auto` resolves from
coordinates; WMO weather codes 0-99.

## 2.3 Forecast models - what we can put in the comparison view

| Model | Provider | Coverage | Resolution | Horizon | Notes |
|---|---|---|---|---|---|
| **IFS** | ECMWF | Global | 9 km (O1280 native via Open-Meteo) | 15 d | Consensus best physics model |
| **AIFS** | ECMWF | Global | ~0.25° | 15 d | First operational AI model (Feb 2025); ≈10 % better on large-scale patterns, +12-24 h medium-range skill, ~20 % better TC tracks; **weaker on heavy rainfall** |
| **GFS** | NOAA | Global | 13 km | 16 d | Free, frequent runs |
| **HRRR** | NOAA | CONUS | 3 km | 48 h | Convection-allowing; lightning potential |
| **ICON** / ICON-EU / ICON-D2 | DWD | Global / EU / DE | 11 / 7 / 2 km | 7 d | Excellent European short-range |
| **ARPEGE / AROME** | Météo-France | Global / EU / FR | 25 / 2.5 / 1 km | - | Best-in-class France |
| **UKMO** | Met Office | Global / UK | 10 / 2 km | - | |
| GEM, JMA, KMA, BOM, CMA | national | regional/global | varies | varies | Round out the ensemble |

This table *is* the feature. MSN gives you one number; Climat can show you that ICON-D2
says 4 mm and AIFS says 0.2 mm, and that is information the user genuinely wants.

## 2.4 Radar sources - the hard problem

| Source | Coverage | Licence / terms | Format | Suitable as default? |
|---|---|---|---|---|
| **RainViewer** | 1 200+ radars, 150+ countries, 5-min refresh, 2 h past + nowcast | **Free tier: "personal or educational use only"**, attribution link required; no availability guarantee | XYZ raster tiles + `weather-maps.json` timeline | ❌ **No** - licence forbids it for a distributed app. Offer as opt-in only. |
| **LibreWXR** | US (IEM), Canada (ECCC), pan-European (EUMETNET OPERA), global ECMWF; plus global alerts | **CC-BY 4.0**, free with attribution; self-hostable drop-in RainViewer replacement | RainViewer-compatible | ✅ **Best default candidate** |
| **Iowa Environmental Mesonet (IEM)** | US NEXRAD mosaic, 5-min, 1995→now | Academic public service; attribute Iowa State; be polite | **WMS + WMS-T** (`n0r`, `n0r-t`, `daa`, `dta`) | ✅ US |
| **ECCC GeoMet** | Canada, 1 km | Open Government Licence - Canada | WMS (`RADAR_1KM_RSNO`, …) | ✅ Canada |
| **DWD Open Data** | Germany (RADOLAN/RADVOR) | GeoNutzV, attribution | Binary grids / GeoTIFF | ✅ Germany (needs decoding work) |
| **EUMETNET OPERA** | Pan-European composite | Via LibreWXR / national portals | - | ✅ Europe (indirect) |
| **NOAA MRMS** | CONUS, 2-min, quantitative | US public domain | GRIB2 | ⭕ heavy; server-side only |
| Self-hosted Climat relay | Wherever we ingest | Ours | XYZ tiles | ⭕ Escape hatch (see §2.8) |

**Design consequence:** radar must be an *abstracted, region-routed, replaceable*
provider from day one - `IRadarProvider` with a source registry keyed by bounding box.
Do not hardcode RainViewer into the map layer. This is the single most likely place for
a naive implementation to create a licensing incident.

## 2.5 Severe-weather alerts

Open-Meteo does **not** offer alerts. There is no single free global source. Plan a
region-routed chain over CAP (Common Alerting Protocol):

| Region | Source | Access | Licence |
|---|---|---|---|
| USA | NWS `api.weather.gov/alerts` (+ `weather.gov/alerts`) | Free, no key, CAP XML + JSON, ATOM digest | US public domain |
| Europe + UK + Israel | **MeteoAlarm** (EUMETNET) | Atom/CAP feeds per country and all-Europe; REST API portal at `api.meteoalarm.org` is aimed at member services - **confirm terms for third-party clients** ⚠️ | Per-country |
| Canada | ECCC / MSC | CAP via MSC Open Data / GeoMet | OGL-Canada |
| Norway | MET Alerts (api.met.no) | Free, UA required | CC-BY 4.0 |
| Global fallback | WMO Severe Weather Information Centre (severeweather.wmo.int) CAP source list | Aggregated national CAP endpoints | Per-issuer |
| Commercial fallback | Xweather alerts (NWS + EC + MeteoAlarm + UKMO + JMA + BOM + CMA in one API) | Paid | Proprietary |

Because CAP is a standard, one parser plus a per-region feed registry covers most of the
world. Budget this as its own milestone - it is not a weekend task.

## 2.6 Air quality, pollen, UV

| Data | Source | Coverage limits |
|---|---|---|
| PM2.5, PM10, O₃, NO₂, SO₂, CO, dust, AOD, NH₃ | Open-Meteo Air Quality (CAMS) | Europe 11 km / 4 d; global 45 km / 5 d, 12-hourly updates; up to 7 d requestable |
| US AQI (0-500) and European AQI (0-100+) | Open-Meteo, both provided | Global |
| **Pollen**: alder, birch, grass, mugwort, olive, ragweed | Open-Meteo (CAMS Europe) | **Europe only, in season, 4-day** - MSN has a US pollen tab we cannot match from open data ⚠️ |
| UV index | Open-Meteo forecast + air-quality endpoints | Global |

Pollen outside Europe is a **known parity gap**. Options: leave it region-gated (honest),
or investigate open US sources later. Do not fake it.

## 2.7 Basemap tiles and geocoding

| Need | Choice | Licence / notes |
|---|---|---|
| Vector basemap | **OpenFreeMap** (hosted, no key, no registration, no stated usage limit) with **Protomaps `.pmtiles`** as self-host/offline fallback | Data is OpenStreetMap → **ODbL attribution mandatory** |
| Style | Custom MapLibre style JSON, dark/light variants, deliberately desaturated so radar reads clearly | Ours |
| Renderer | MapLibre Native Qt (see [`03-tech-stack.md`](03-tech-stack.md)) | BSD-2 |
| Forward geocoding | Open-Meteo Geocoding (GeoNames) | CC-BY 4.0; ≥2 chars exact, ≥3 fuzzy; supports postcodes and localisation |
| Reverse geocoding | **Not offered by Open-Meteo. Decided: offline.** Nominatim returned HTTP 403 to the *first* request from a client with an identifying User-Agent and contact (tested 2026-07-31), so a bundled GeoNames `cities15000` index is used instead - `tools/geonames/` | CC-BY 4.0, GeoNames. 412 KiB packed; deterministic, works offline, and yields the **same `geonames_id`** as the forward geocoder, so "search Toronto" and "detect Toronto" are one place |
| Timezones | `timezone=auto` from Open-Meteo; local TZ database via Qt | - |
| Elevation | Open-Meteo Elevation API (90 m DEM) | CC-BY 4.0 |

## 2.8 Do we need a backend? No - but keep the option

Because each user's client calls Open-Meteo from their own IP, the 10 000/day non-commercial
limit is per-user and generous (our worst case is ~150 calls/day/user). The app therefore
ships with **no mandatory server**, which is a privacy and trust feature we should
advertise loudly.

An optional `climat-relay` becomes worthwhile only for: radar tile normalisation,
CAP alert aggregation, and rate-limit shielding if we ever go commercial. Self-hosting
Open-Meteo is documented and realistic:

| Aspect | Figure |
|---|---|
| Deployment | Official Docker image or Ubuntu 22.04 package; Swift/Vapor single binary |
| Storage | Full global: **500 GB+**, 2 TB+/day ingest. Selective single-VPS deployment observed at **~50 GB** steady, DEM ~10 GB one-off |
| Runtime | ~1.1 GiB RAM, ~4 % of one core at steady state |
| Data sync | `sync` command pulls model+variable subsets from AWS S3 (e.g. `ecmwf_ifs025`, `dwd_icon`) |

## 2.9 Attribution obligations - must ship in an "About → Data sources" screen

| Provider | Required credit |
|---|---|
| Open-Meteo (forecast, AQI, archive, geocoding, elevation) | "Weather data by Open-Meteo.com" + link to CC-BY 4.0; underlying model owners (ECMWF, NOAA, DWD, Météo-France, …) named |
| MET Norway | CC-BY 4.0 credit + **identifying User-Agent** `Climat/<version> (+https://…; contact@…)` - generic UA gets 403/blocked |
| NWS / NOAA | Public domain; UA with contact still expected |
| ECCC | "Contains information licensed under the Open Government Licence - Canada" |
| DWD | GeoNutzV attribution |
| IEM | Credit Iowa State University / Iowa Environmental Mesonet |
| LibreWXR | CC-BY 4.0 credit |
| OpenStreetMap (via OpenFreeMap/Protomaps) | "© OpenStreetMap contributors", ODbL |
| GeoNames | CC-BY 4.0 |
| Meteocons icons | MIT |
| Qt | LGPLv3 notice + licence text + relink information (see [`03-tech-stack.md`](03-tech-stack.md)) |

Build this screen in **M1**, not at the end. It is a licence obligation, not polish.

## Sources

- [Open-Meteo docs](https://open-meteo.com/en/docs) · [Air Quality API](https://open-meteo.com/en/docs/air-quality-api) · [Geocoding API](https://open-meteo.com/en/docs/geocoding-api) · [ECMWF API](https://open-meteo.com/en/docs/ecmwf-api) · [Ensemble API](https://open-meteo.com/en/docs/ensemble-api) · [Terms](https://open-meteo.com/en/terms) · [Pricing](https://open-meteo.com/en/pricing) · [open-meteo/open-meteo](https://github.com/open-meteo/open-meteo) · [Self-host getting started](https://github.com/open-meteo/open-meteo/blob/main/docs/getting-started.md)
- [MET Norway Locationforecast 2.0](https://api.met.no/weatherapi/locationforecast/2.0/documentation) · [Terms of Service](https://api.met.no/doc/TermsOfService) · [FAQ](https://docs.api.met.no/doc/FAQ.html)
- [api.weather.gov FAQs](https://weather-gov.github.io/api/general-faqs) · [NWS CAP](https://vlab.noaa.gov/web/nws-common-alerting-protocol) · [NWS alerts](https://www.weather.gov/alerts)
- [MeteoAlarm API portal](https://api.meteoalarm.org/) · [WMO SWIC CAP sources](https://severeweather.wmo.int/sources.html)
- [RainViewer API](https://www.rainviewer.com/api.html) · [rainviewer-api-example](https://github.com/rainviewer/rainviewer-api-example)
- [LibreWXR](https://librewxr.net/)
- [IEM GIS RADAR](https://mesonet.agron.iastate.edu/GIS/radview.phtml) · [IEM OGC services](https://mesonet.agron.iastate.edu/ogc/) · [NEXRAD mosaics](https://mesonet.agron.iastate.edu/docs/nexrad_mosaic/)
- [ECCC MSC radar readme](https://eccc-msc.github.io/open-data/msc-data/obs_radar/readme_radar_en/)
- [OpenFreeMap](https://openfreemap.org/) · [Protomaps](https://protomaps.com/) · [OpenMapTiles](https://openmaptiles.org/)
- [AIFS update (arXiv 2509.18994)](https://arxiv.org/pdf/2509.18994) · [Forecast models overview - RainViewer blog](https://www.rainviewer.com/blog/forecast-models-around-the-world.html)
- [Meteocons (MIT)](https://github.com/basmilius/meteocons) · [Weather Icons (SIL OFL 1.1)](https://erikflowers.github.io/weather-icons/)
