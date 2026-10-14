<!--
SPDX-FileCopyrightText: 2026 Jowi Aoun
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Google Play listing

The text and the pictures the Play Console asks for, kept here so that the
store page is reviewed like the README is. Everything under `screenshots/`
and `feature-graphic.png` is rendered by a script and gated in CI; the words
are edited by hand.

`scripts/play.sh` sends all of it: the text in `en-CA/`, the icon, the feature
graphic, the screenshots and the release note. The release workflow does that
on every tag once the Play key is in the secrets, and `scripts/play.sh check`
holds it to Play's limits on every pull request. The rest of this page is what
is typed into the Console once.

## App details

| Field | Value |
|---|---|
| App name | `en-CA/title.txt` |
| Package name | `io.github.JowiAoun.Climat` |
| Default language | English (Canada) |
| Category | Weather |
| Free or paid | Free |
| Contains ads | No |
| App icon | `packaging/icons/climat-512.png` |
| Feature graphic | `feature-graphic.png` (1024 by 500) |
| Phone screenshots | `screenshots/01-today.png` to `06-hourly-light.png` (540 by 960) |
| Privacy policy | https://jowiaoun.github.io/climat/privacy-policy/ |
| Contact email | an address that receives mail, typed into the Console |
| Website | https://github.com/JowiAoun/climat |

The contact address is shown on the store page. The package's Maintainer
field holds a GitHub noreply address, which does not receive mail, so it is
not the one to use here.

## Short description

`en-CA/short_description.txt`, 80 characters at most.

## Full description

`en-CA/full_description.txt`, 4000 characters at most. Plain text: Play
ignores Markdown and keeps every line break, so each paragraph is one line.

## Content rating

The IARC questionnaire: no user-generated content, no violence, no sexual
content, no gambling, no controlled substances, no user interaction, no
location sharing with other users. Expected rating: Everyone.

## Target audience

18 and over. Not designed for children, and not a "family" app: that
programme has its own review and Climat gains nothing from it.

## Data safety

`data-safety.md` beside this file has the answers, one per question.

## Release notes

Play's "What's new" is the release's AppStream note in
`packaging/linux/climat.metainfo.xml.in`, turned into plain text, so it says
the same thing a Linux software centre says. A release whose note talks about
the desktop can have a Play note of its own in
`en-CA/changelogs/<version code>.txt`, which wins. 500 characters at most.
