<!--
SPDX-FileCopyrightText: 2026 Jowi Aoun
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Data safety form

What Climat does with data, in the shape the Play Console asks for it. Every
answer here is checkable against the source, and `docs/02-data-sources.md`
names each service the app talks to.

## Does your app collect or share any of the required user data types?

**Yes: approximate location, and nothing else.**

Nothing leaves the phone except a request to a weather service, and that
request carries a place's coordinates and nothing about the person. There is
no account, no analytics library, no crash reporter, no advertising SDK, and
no server of ours in the middle. The app's User-Agent names the app and a
contact address, as MET Norway's terms require, and nothing else.

Play counts any data the app sends off the phone as collected, "irrespective
of whether data is transmitted to you or a third-party server" (Play Console
Help, "Provide information for Google Play's Data safety section"). When a
reader taps "Use my location", the city it finds is what the forecast request
carries, so that is approximate location sent to a weather service. A place
typed into the search is not the reader's location and is not declared.

## Location

The app asks for approximate location (`ACCESS_COARSE_LOCATION`) only when the
reader taps "Use my location". The fix is turned into a city name on the phone
by a bundled place index, and the city's coordinates are what the forecast
request carries. The fix itself is not stored and not sent anywhere.

Under the form's definitions this is **collected, not shared**. It is sent
off the phone, so it is collected. It is not shared, because the form exempts
a transfer the reader starts and expects: they asked for the weather where
they are. It is not processed ephemerally, because the city is saved as a
place on the phone. It is optional, because a place typed into the search
works without it.

Precise location is not requested. Qt Positioning declares it and the manifest
removes it; `packaging/android/AndroidManifest.xml` says how.

## Network

`INTERNET` for the forecast, the air quality reading and the warnings.
`ACCESS_NETWORK_STATE` is declared by Qt Network and lets the app notice it is
offline rather than wait for a timeout.

## Data stored on the device

Preferences (units, clock format, theme) and a cache of the last forecast, in
the app's private storage. Nothing is written to shared storage. Uninstalling
the app removes all of it.

## Encryption in transit

Yes. Every service is HTTPS, and the package carries its own OpenSSL for the
purpose - see `scripts/android-openssl.sh`.

## Can users request that data be deleted?

No, because there is nothing of ours to delete: the coordinates go to the
weather service and to no server of ours. Clearing the app's storage in
Android's settings, or uninstalling it, removes the saved places, the cache
and the preferences.

## Independent security review

No.

## Answers, one per form section

| Section | Answer |
|---|---|
| Data collection and security: collects or shares | Yes |
| Data encrypted in transit | Yes |
| Users can request deletion | No |
| Location: approximate location | Collected, not shared |
| Approximate location: processed ephemerally | No |
| Approximate location: required or optional | Optional, users can choose |
| Approximate location: why collected | App functionality |
| Location: precise location | Not collected |
| Personal info, financial info, health, messages, photos, audio, files, calendar, contacts, activity, browsing, identifiers, installed apps, other | Not collected |
| Ads | None |
