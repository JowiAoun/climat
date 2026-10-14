<!--
SPDX-FileCopyrightText: 2026 Jowi Aoun
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Android packaging

What androiddeployqt takes from this directory, and what it does not.

`app/CMakeLists.txt` names this directory as `QT_ANDROID_PACKAGE_SOURCE_DIR`.
Everything in it is copied over Qt's own Gradle project template before the
package is built, so a file here with the same path as one of Qt's replaces
it. `scripts/android.sh` is the build; `docs/releasing.md` is the release.

## Files

- `AndroidManifest.xml`: Qt 6.11.1's template with three edits. The icon,
  because Qt's CMake has no property for the launcher icon. Three permissions
  Qt's own modules add and this app does not use, taken back out. And
  location hardware marked optional, so Play does not hide the app from a
  device without it. The package name, version code, version name and
  permissions stay as `%%INSERT_…%%` placeholders and are filled in from
  `app/CMakeLists.txt`.
- `res/mipmap-anydpi-v26/ic_launcher.xml`: the adaptive icon, two layers the
  launcher composes and masks itself on Android 8 and later.
- `res/mipmap-<density>/ic_launcher.png`: the square icon for Android 7 and
  older, 48 dp at five densities.
- `res/mipmap-<density>/ic_launcher_{background,foreground}.png`: the two
  layers, 108 dp at five densities. The background is the sky gradient, full
  bleed; the foreground is the sun, the cloud and the stars, with the master's
  tile mapped onto the 66 dp safe zone.

Every PNG is cut from `packaging/icons/climat.svg` by `scripts/icons.sh`, with
`scripts/android-icon-layers.py` splitting the layers. `scripts/icons.sh check`
runs in CI and fails when any of them drifts from the drawing. Do not edit a
PNG by hand.

## What is not here

- **OpenSSL.** Qt for Android carries no TLS library, and every weather
  service is HTTPS. `scripts/android-openssl.sh` builds OpenSSL from a pinned
  tarball into `build/android-openssl/`, and `cmake/ClimatAndroidTls.cmake`
  refuses to configure a package without it.
- **The upload key.** Never in this repository. `docs/releasing.md` says where
  it lives and how the release workflow gets it.
- **The Play listing.** Text, the feature graphic and the store screenshots
  are in `play/`, beside this file, because they go to the Play Console and
  not into the package. `scripts/play.sh` sends them.

## When Qt's template changes

Diff this manifest against the one in the Android kit and carry over what
moved. The `android:icon` line, the three removed permissions and the
`uses-feature` line are ours.

```sh
diff ~/Qt/6.11.1/android_arm64_v8a/src/android/templates/AndroidManifest.xml \
     packaging/android/AndroidManifest.xml
```
