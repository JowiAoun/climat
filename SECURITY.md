<!-- SPDX-FileCopyrightText: 2026 Climat contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Security policy

## Reporting a vulnerability

Use GitHub's private reporting: **Security → Report a vulnerability** on
[the repository](https://github.com/JowiAoun/climat/security/advisories/new).
That opens a channel only the maintainers can read.

Please do not open a public issue for a suspected vulnerability.

Expect an acknowledgement within a week. This is a small project with no
security team, so that is a realistic commitment rather than an aspirational
one. If a fix is warranted it ships in the next release, credited to you unless
you would rather it were not.

## Supported versions

The most recent release. There is no long-term support branch and there will not
be one until the project is large enough for that to mean something.

## What the attack surface actually is

Worth stating plainly, because it is smaller than a weather app's usually is and
that changes what is worth your time:

- **There is no server.** Not "we do not talk about it" - there is no
  infrastructure of ours anywhere. Every request goes from the user's machine
  directly to a public weather service.
- **There is no account, no login and no credential of any kind.** No API key
  either: every provider in use is keyless.
- **There is no telemetry**, no analytics, no crash reporting, and no update
  check that phones home.
- **No user file is read or written** beyond the app's own settings and cache.
  The Flatpak carries no `--filesystem` permission at all.

So the interesting surface is what comes back over the network, and what is
already on disk:

1. **Parsing untrusted provider responses.** JSON from Open-Meteo and MET
   Norway, GeoJSON from ECCC and the NWS. These are trusted sources over TLS,
   but a compromised or hostile response is the most plausible route to
   misbehaviour, and a malformed one must not crash the app. Parsing is in
   `libclimat/providers/`.
2. **The SQLite cache** at `QStandardPaths::AppDataLocation`. It stores raw
   payloads, so anything that can write there can feed the parsers above.
3. **The bundled GeoNames index**, a compressed binary blob decoded at startup
   in `libclimat/providers/geocoding/`.
4. **The Qt attack surface** underneath all of it. Report Qt issues to the Qt
   Project; we will pick up the fix by moving the floor or the runtime version.

## What is not a vulnerability

- **The Windows build is unsigned**, so SmartScreen warns. This is known,
  deliberate and [documented](docs/known-gaps.md) with the mitigations. A report
  telling us it is unsigned tells us nothing new; a report of a *specific*
  exploitation of it is very welcome.
- **Alerts may be late or missing.** This app is not a life-safety system and
  says so. Severe weather warnings are shown as the issuing authority published
  them, but the app checks only while it is running: every few minutes with the
  window open, and every 15 minutes with it hidden only if you turned on
  warnings in Preferences. There is no background delivery on Android at all.
  Do not rely on it for a tornado warning - use the authority's own channel.
- **Coordinates are sent to weather services.** That is how a forecast is
  fetched. They are sent at four decimal places to the providers named under
  Data sources in Preferences and nowhere else.
