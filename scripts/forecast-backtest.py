#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Scores Open-Meteo's models against what ECCC's stations measured, in Canada.
#
#   scripts/forecast-backtest.py                               the last 120 days
#   scripts/forecast-backtest.py --start 2027-01-01 --end 2027-03-31
#
# The forecasts are the ones each model actually issued 1, 3 and 5 days before
# each hour, from Open-Meteo's previous-runs API. The observations are ECCC's
# hourly and daily climate records at 14 airports, from GeoMet. Every model is
# scored on the same hours, so a model is never let off an hour another was
# scored on. Average error for temperature and wind; for rain, how often a day
# with 1 mm or more was called right.
#
# It reaches the internet and is not run by the build or by CI. The average it
# measures is the one libclimat/providers/openmeteo/openmeteoconsensus.h ships,
# and docs/02-data-sources.md §2.10 has the result it was written for. Run it
# again in winter: that measurement is June to September.

import argparse
import datetime as dt
import json
import time
import urllib.parse
import urllib.request

# Airports with hourly records, coast to coast and north. The offset is the
# local standard time ECCC's daily records are kept in.
STATIONS = {
    "Toronto": (43.6772, -79.6306, -5), "Montreal": (45.4706, -73.7408, -5),
    "Ottawa": (45.3225, -75.6692, -5), "Quebec": (46.7911, -71.3933, -5),
    "Halifax": (44.8808, -63.5086, -4), "St. John's": (47.6186, -52.7519, -3.5),
    "Winnipeg": (49.9100, -97.2399, -6), "Regina": (50.4333, -104.6667, -6),
    "Calgary": (51.1139, -114.0203, -7), "Edmonton": (53.3097, -113.5797, -7),
    "Vancouver": (49.1939, -123.1844, -8), "Whitehorse": (60.7096, -135.0674, -7),
    "Yellowknife": (62.4628, -114.4403, -7), "Iqaluit": (63.7531, -68.5558, -5),
}
MODELS = ["best_match", "gem_seamless", "gfs_seamless", "ecmwf_ifs"]
AVERAGED = ["best_match", "gem_seamless", "ecmwf_ifs"]   # what the app averages
LEADS = (1, 3, 5)
GEOMET = "https://api.weather.gc.ca/collections"
PREVIOUS_RUNS = "https://previous-runs-api.open-meteo.com/v1/forecast"


def get(url):
    for attempt in range(5):
        try:
            with urllib.request.urlopen(url, timeout=120) as response:
                return json.load(response)
        except OSError:
            if attempt == 4:
                raise
            time.sleep(3 + attempt * 5)


def observations(lat, lon, start, end):
    # The station nearest the point is the one with most hourly rows in a
    # small box around it: a box can catch a second, sparser station.
    box = f"{lon - 0.06},{lat - 0.06},{lon + 0.06},{lat + 0.06}"
    probe = get(f"{GEOMET}/climate-hourly/items?bbox={box}&datetime={end}T00:00:00Z/{end}T23:59:59Z"
                f"&f=json&limit=500")["features"]
    counts = {}
    for feature in probe:
        cid = feature["properties"]["CLIMATE_IDENTIFIER"]
        counts[cid] = counts.get(cid, 0) + 1
    if not counts:
        return None, {}, {}
    cid = max(counts, key=counts.get)
    window = f"{start}T00:00:00Z/{end}T23:59:59Z"
    hourly = {}
    for feature in get(f"{GEOMET}/climate-hourly/items?CLIMATE_IDENTIFIER={cid}&datetime={window}"
                       f"&f=json&limit=10000")["features"]:
        p = feature["properties"]
        hourly[p["UTC_DATE"][:13]] = (p.get("TEMP"), p.get("WIND_SPEED"))
    daily = {}
    for feature in get(f"{GEOMET}/climate-daily/items?CLIMATE_IDENTIFIER={cid}&datetime={window}"
                       f"&f=json&limit=1000")["features"]:
        p = feature["properties"]
        daily[p["LOCAL_DATE"][:10]] = p.get("TOTAL_PRECIPITATION")
    return cid, hourly, daily


def forecasts(lat, lon, start, end):
    names = ",".join(f"{v}_previous_day{n}" for v in ("temperature_2m", "wind_speed_10m", "precipitation")
                     for n in LEADS)
    query = urllib.parse.urlencode({"latitude": lat, "longitude": lon, "hourly": names,
                                    "models": ",".join(MODELS), "start_date": start,
                                    "end_date": end, "timezone": "UTC"})
    return get(f"{PREVIOUS_RUNS}?{query}")["hourly"]


def candidates():
    named = {"best match": ["best_match"], "GEM": ["gem_seamless"], "GFS": ["gfs_seamless"],
             "ECMWF IFS": ["ecmwf_ifs"], "average": AVERAGED}
    return named


def score_hourly(data, variable, index, lead):
    errors = {name: [] for name in candidates()}
    for station in data.values():
        fc = station["forecast"]
        for i, stamp in enumerate(fc["time"]):
            seen = station["hourly"].get(stamp[:13])
            if not seen or seen[index] is None:
                continue
            values = {m: fc.get(f"{variable}_previous_day{lead}_{m}", [None] * len(fc["time"]))[i]
                      for m in MODELS}
            if any(v is None for v in values.values()):
                continue
            for name, models in candidates().items():
                guess = sum(values[m] for m in models) / len(models)
                errors[name].append(abs(guess - seen[index]))
    return {name: sum(e) / len(e) for name, e in errors.items() if e}


def stations_won(data, variable, index, lead):
    """Stations where the average's error is below best match's, of those scored."""
    won = scored = 0
    for station in data.values():
        fc = station["forecast"]
        now = mean = 0.0
        hours = 0
        for i, stamp in enumerate(fc["time"]):
            seen = station["hourly"].get(stamp[:13])
            if not seen or seen[index] is None:
                continue
            values = [fc.get(f"{variable}_previous_day{lead}_{m}", [None] * len(fc["time"]))[i]
                      for m in MODELS]
            if any(v is None for v in values):
                continue
            hours += 1
            now += abs(values[MODELS.index("best_match")] - seen[index])
            mean += abs(sum(values[MODELS.index(m)] for m in AVERAGED) / len(AVERAGED) - seen[index])
        if hours:
            scored += 1
            won += mean < now
    return won, scored


def score_rain(data, lead):
    right = {name: 0 for name in candidates()}
    days = 0
    for station in data.values():
        fc, offset = station["forecast"], station["offset"]
        totals = {m: {} for m in MODELS}
        hours = {m: {} for m in MODELS}
        for i, stamp in enumerate(fc["time"]):
            # Precipitation at T is the hour ending at T, so it belongs to the
            # local day of T minus an hour.
            local = dt.datetime.fromisoformat(stamp) + dt.timedelta(hours=offset - 1)
            day = local.date().isoformat()
            for m in MODELS:
                v = fc[f"precipitation_previous_day{lead}_{m}"][i]
                if v is not None:
                    totals[m][day] = totals[m].get(day, 0.0) + v
                    hours[m][day] = hours[m].get(day, 0) + 1
        for day, measured in station["daily"].items():
            if measured is None or any(hours[m].get(day, 0) < 24 for m in MODELS):
                continue
            days += 1
            for name, models in candidates().items():
                guess = sum(totals[m][day] for m in models) / len(models)
                right[name] += (guess >= 1.0) == (measured >= 1.0)
    return {name: 100.0 * r / days for name, r in right.items()} if days else {}, days


def main():
    today = dt.date.today()
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0] if __doc__ else None)
    parser.add_argument("--start", default=(today - dt.timedelta(days=122)).isoformat())
    parser.add_argument("--end", default=(today - dt.timedelta(days=2)).isoformat())
    args = parser.parse_args()

    data = {}
    for name, (lat, lon, offset) in STATIONS.items():
        cid, hourly, daily = observations(lat, lon, args.start, args.end)
        if cid is None:
            print(f"{name:12s} no station with hourly records")
            continue
        data[name] = {"offset": offset, "hourly": hourly, "daily": daily,
                      "forecast": forecasts(lat, lon, args.start, args.end)}
        print(f"{name:12s} station {cid}: {len(hourly)} hours, "
              f"{sum(v is not None for v in daily.values())} days of rain records", flush=True)

    names = list(candidates())
    print(f"\n{args.start} to {args.end}, {len(data)} stations. "
          f"'average' is {', '.join(AVERAGED)}, as the app takes it.\n")
    print(f"{'':28s}" + "".join(f"{n:>12s}" for n in names))
    for variable, index, label, unit in (("temperature_2m", 0, "temperature", "°C"),
                                         ("wind_speed_10m", 1, "wind", "km/h")):
        for lead in LEADS:
            scores = score_hourly(data, variable, index, lead)
            won, scored = stations_won(data, variable, index, lead)
            row = f"{label}, {lead} day{'s' if lead > 1 else ''} ({unit})"
            print(f"{row:28s}" + "".join(f"{scores.get(n, float('nan')):12.2f}" for n in names)
                  + f"   average better at {won} of {scored} stations")
    for lead in LEADS:
        scores, days = score_rain(data, lead)
        row = f"rain day right, {lead}d (%)"
        print(f"{row:28s}" + "".join(f"{scores.get(n, float('nan')):12.1f}" for n in names)
              + f"   {days} station-days")


if __name__ == "__main__":
    main()
