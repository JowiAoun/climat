<!--
SPDX-FileCopyrightText: 2026 Jowi Aoun
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Climat privacy policy

Climat is a weather app, written and published by Jowi Aoun. This page says
what it does with your data, which is very little, and it is short because
there is little to say.

## What Climat sends

To show you a forecast, Climat asks a weather service for the weather at a
place. The request carries the place's coordinates, the units you want and a
User-Agent that names the app and a contact address, as some services require.
It does not carry your name, an account, a device identifier, an advertising
identifier or anything else about you.

The services it asks, and their own policies:

- Open-Meteo (open-meteo.com), for the forecast and air quality
- MET Norway (met.no), as a fallback for the forecast
- Environment and Climate Change Canada (weather.gc.ca), for warnings in Canada
- The United States National Weather Service (weather.gov), for warnings in
  the United States

There is no server of ours in between. Climat talks to those services
directly, and nothing about your use of the app reaches us.

## Your location

Climat only reads your location when you tap "Use my location", and then it
asks Android for an approximate fix. The fix is turned into a city name on
your phone, using a place list built into the app. The city's coordinates are
what the forecast request carries. The fix itself is not saved and not sent
anywhere.

You can use the app without ever granting location: search for a place by
name instead.

## What Climat stores

Your preferences, the places you have chosen and a copy of the last forecast,
in the app's private storage on your phone. Uninstalling the app removes them.
Nothing is stored anywhere else.

## What Climat does not do

No ads. No analytics. No crash reporting. No account. No tracking. No sale or
sharing of data, because there is none to sell or share.

## Changes

If this policy changes, the change will appear in this document's history in
the project's repository, which is public.

## Contact

Open an issue at https://github.com/JowiAoun/climat/issues, or write to the
contact address on the app's Google Play page.
