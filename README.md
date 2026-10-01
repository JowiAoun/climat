<!-- SPDX-FileCopyrightText: 2026 Climat contributors -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Climat

> A native, ad-free, open-source weather app for Linux and Windows. Two weeks of
> forecast as charts you can read, and official severe-weather warnings on every
> screen where they matter.

![Climat on a phone, a tablet and a desktop](docs/images/hero.png)

**Status: early. Version 0.1.0, and the version number is honest.** This
installs, runs and shows you real weather, and several things the plan calls for
are not built yet. [What is missing](docs/known-gaps.md) is written down rather
than left to be discovered.

Every image in this README is rendered from the running application by
`scripts/shots.sh`, and CI re-renders and diffs them. They cannot drift from
what the app actually draws.

## What it does today

- **Hourly and ten-day forecasts:** temperature, precipitation, wind, humidity,
  pressure, visibility, UV and air quality, as charts rather than a grid of
  numbers.
- **Severe weather warnings**, live, from Environment and Climate Change Canada
  and the United States National Weather Service. No API key, no account, no
  server of ours in the middle. Off by default, they can also **interrupt you**:
  a desktop notification when a warning arrives and Climat is not on screen,
  through the XDG portal where there is one and the notification service where
  there is not.
- **Detail cards** for sunrise and sunset, the moon phase, and each measurement
  in turn.
- **A dark and a light theme**, following the desktop's own setting through the
  XDG portal.
- **Preferences:** a gear beside the place name on the desktop, or the Me tab
  on a phone. The background can follow the sky over the place on screen or hold
  still; the clock is 24-hour or AM/PM everywhere at once, tiles included; and
  units are °C or °F as a pair of presets **over** five independent preferences,
  so °C with mph or inches of rain with millimetres of visibility is one tap and
  not an impossibility.
- **Phone, tablet and desktop layouts:** one product, three arrangements,
  chosen by the width of the window.
- **Ten desktop widgets**, drawn from the same components as the app, pinned
  under your windows on GNOME by a shell extension that draws no weather itself,
  and on KDE, Sway, Hyprland and every other wlroots compositor by the tiles
  asking for it directly. One process fetches; every tile and the top-bar
  indicator read from it over the session bus, so a desktop full of widgets is
  one client of the forecast service and not eight.
- **`climat-cli`:** the forecast for a status bar, a script or a terminal.
  `climat-cli now`, `hourly`, `daily`, `places`; `--json` and `--csv` for a
  script, in canonical units so a preference nobody opened cannot move a number
  something parses. It reads the same cache the app fills, so a status bar
  polling every minute is not a second client of the forecast service.

- **"Use my location"** through GeoClue2 on a desktop and through the
  `org.freedesktop.portal.Location` portal inside a Flatpak. Your own desktop
  asks you, once, and the app is told a city and never a street.

- **Offline-first.** It renders from its cache and reconciles with the network,
  so a service being down is never an empty screen.
- **No ads, no news feed, no telemetry, no account, no API key.**

## What it does not do yet

Stated here rather than at the bottom, because a README that lists a roadmap as
if it were a feature list is the thing this project is trying not to be.

- **No radar or map.** The Maps tab renders a deliberate placeholder that says
  so. This is the single largest gap against the goal in `docs/01-landscape.md`.
- **No model comparison.** Showing ECMWF against GFS against ICON with ensemble
  ranges is the differentiator this project exists for, and it is not built.
- **No macOS build to download.** It compiles and its tests run on macOS in CI;
  what is missing is notarisation, which needs an Apple Developer ID, and
  without it Gatekeeper refuses a downloaded app rather than warning about it.
- **The Windows build is unsigned**, so SmartScreen warns on first run.
- **Android warns you only while the app is open.** The package builds, signs,
  installs and runs on an emulator, and has not yet been checked on a physical
  phone. Delivering an alert to a sleeping phone is the gate, and Qt has no
  answer for it. The desktop answer above does not port: it is a session bus
  and a window that can be hidden.

- **No translations.** Every string is marked and the catalogue is kept current
  by CI (322 of them), and not one language has been translated. A machine
  translation would be worse than none.
- **The widgets have never been pinned on a KDE session.** They pin themselves
  on GNOME, which was measured by hand, and on wlroots, which is measured in CI
  against a headless compositor. KWin implements the same protocol and nobody
  has run it there.

All of them, with what would close each: [`docs/known-gaps.md`](docs/known-gaps.md).

## Install

Every build is on the [latest release](https://github.com/JowiAoun/climat/releases/latest),
with `SHA256SUMS` and build provenance beside it.

### Linux

**Flatpak** is the primary channel. It brings its own Qt, so it works on any
distribution, including Ubuntu 24.04, which ships Qt 6.4 and cannot run the
`.deb`.

```sh
flatpak install --user ./climat-0.1.0-x86_64.flatpak
flatpak run io.github.JowiAoun.Climat
```

**Debian 13 / Ubuntu 26.04 or newer**, using the distribution's own Qt:

```sh
sudo apt install ./climat_0.1.0_amd64.deb
```

It declares `libqt6core6t64 (>= 6.8.2)` and apt will refuse to install it on
anything older. That is the intended behaviour: a package that installed and
then would not start is worse than one that says why.

### Windows

Download the `.msi` and run it. It installs per-user, so it needs no
administrator rights.

**Windows will show an "unknown publisher" warning.** The build is not
code-signed, because signing requires a certificate this project does not have.
The [mitigations](docs/known-gaps.md) are listed in the order they should be
attempted; in the meantime every release carries `SHA256SUMS` and GitHub build
provenance:

```sh
gh attestation verify climat-0.1.0-windows-x64.msi --repo JowiAoun/climat
```

That proves the file came out of this repository's release workflow at a named
commit. It is weaker than a signature in one way (Windows does not
check it) and stronger in another, since it names the source revision.

## What it looks like

| | |
|---|---|
| **Desktop:** one scrolling column, the chart card open on Overview <br> ![](docs/images/desktop.png) | **Phone:** five destinations under a nav bar <br> ![](docs/images/phone.png) |
| **Tablet:** two content columns, bottom bar <br> ![](docs/images/tablet.png) | **Tablet, turned:** the nav becomes a left rail <br> ![](docs/images/tablet-landscape.png) |

**Desktop widgets:** six of the ten. On GNOME a shell extension launches
Climat's own Qt process and pins its window below everything else; it draws none
of this itself, because GNOME Shell cannot host a QML surface. On KDE and every
wlroots compositor the same binary asks for a desktop-layer surface and there is
no extension at all. [`docs/widgets.md`](docs/widgets.md) has both mechanisms
and the measurements.

![](docs/images/widgets.png)

The tablet is not a third layout. It is the phone's shell with two questions
answered differently: how many content columns the width buys, and whether the
navigation is a bar or a rail. See
[`docs/10-design-system.md`](docs/10-design-system.md) §10.12.

## Build from source

Everything goes through a Nix devshell, which pins Qt, the compiler, FreeType
and fontconfig by store hash, which is what makes the golden images reproducible
on a machine that is not this one.

```sh
nix develop --command cmake --preset dev
nix develop --command cmake --build build/dev
scripts/dev-run.sh                             # builds if needed, finds Qt itself
```

`scripts/dev-run.sh` is the one entry point that works from a plain shell.
Running `build/dev/app/climat` directly outside `nix develop` exits silently,
because Qt is only in the Nix store and the binary cannot find its QML imports.

Try the layouts and the themes:

```sh
scripts/dev-run.sh --viewport mobile
scripts/dev-run.sh --viewport tablet-landscape --scheme light
scripts/dev-run.sh --place "Halifax"           # live ECCC alerts
scripts/dev-run.sh --fixture seattle           # four NWS alerts, offline
```

The desktop tiles are a second binary reading from a third: a weather service
the session bus starts on demand once Climat is installed. Nothing is installed
in a build tree, so this starts one beside them:

```sh
scripts/widgets-run.sh                         # the tiles, with a daemon to read
CLIMAT_FIXTURE=toronto scripts/widgets-run.sh   # …from recorded data
```

Without a Nix devshell, any Qt 6.8 or newer works;
[`BUILDING.md`](BUILDING.md) has the distribution package lists and the
CMake options.

## Testing

```sh
nix develop --command ctest --test-dir build/dev --output-on-failure
```

42 tests, 318 QML assertions among them, and 53 golden images compared byte
for byte.
The component gallery is a second binary and the fastest way to look at
anything in isolation:

```sh
CLIMAT_BINARY=build/dev/gallery/climat-gallery scripts/dev-run.sh
```

It has a touch-target overlay that draws every tap area a layout leaves under
44 px, which is how nine undersized controls were found, including a 14×14
disclosure chevron on every phone screen.

## Stack

C++20 engine (`libclimat`, GUI-free and enforced by a CMake assertion) under a
**Qt 6.8+ / QML** interface, with a hand-written scene-graph chart kit.
Open-Meteo is the primary forecast provider with MET Norway as a fallback.
In Canada, temperature, humidity and wind are the average of Open-Meteo's best
match, ECCC's GEM and ECMWF's IFS. Scored against ECCC's own station
records, that cut the temperature error by about a fifth
([`docs/02-data-sources.md`](docs/02-data-sources.md) §2.10). Alerts are
region-routed to ECCC and the NWS. Reverse geocoding is offline, from
a 412 KiB bundled GeoNames index, so turning a coordinate into "Toronto,
Ontario" never leaves the machine.

There is no server. There never will be: every request goes from your machine
to a public weather service, and nothing goes anywhere else.

## Data and attribution

Weather from [Open-Meteo](https://open-meteo.com/) (CC-BY 4.0), which aggregates
ECMWF, NOAA, DWD, Météo-France, the UK Met Office and fourteen more national
services; in Canada, ECCC's GEM and ECMWF's IFS through Open-Meteo as well;
[MET Norway](https://api.met.no/) as fallback; alerts from
[ECCC](https://api.weather.gc.ca/) ([ECCC Data Server End-use Licence](https://eccc-msc.github.io/open-data/licence/readme_en/)) and
the [US National Weather Service](https://api.weather.gov/) (public domain);
place names from [GeoNames](https://www.geonames.org/) (CC-BY 4.0).

Every source is credited at runtime under **Data sources**, in Preferences on
the desktop and on the Me tab on a phone, generated from the provider registry
rather than maintained by hand. The full record is in
[`docs/02-data-sources.md`](docs/02-data-sources.md) §2.9 and §2.10.

## Licence

- `libclimat`: **MPL-2.0**, so the engine stays reusable outside a GPL program
- the application: **GPL-3.0-or-later**
- authored assets and documentation: **CC-BY-SA-4.0**
- bundled Inter: **OFL-1.1**; bundled GeoNames data: **CC-BY 4.0**

The repository is [REUSE 3.3](https://reuse.software/) compliant: every file
carries an SPDX header, and `reuse lint` gates every commit. Every release
carries an SBOM and a third-party licence bundle generated from that same
metadata, plus a written offer for Qt's corresponding source.

Contributions are by **DCO sign-off**. There is no CLA.

## Contributing

Start with [`CONTRIBUTING.md`](CONTRIBUTING.md). The short version: conventional
commits, `git commit -s`, and if a change moves a golden image or a README
image, the pull request has to say why the pixels moved.

The planning documents in [`docs/`](docs/README.md) are a frozen baseline,
amended in place with dated corrections when the code contradicts them;
`docs/07-packaging.md` §7.6 is a good example of what that looks like.
