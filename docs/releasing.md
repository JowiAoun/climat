<!--
SPDX-FileCopyrightText: 2026 Jowi Aoun
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Releasing

How a version of Climat gets from `main` to a download. Most of it is automatic;
the parts that are not are the parts that should not be.

## Two settings this depends on, outside the repository

Both are one click and neither is in a file, so both are recorded here.

**Actions must be allowed to open pull requests.** Settings, Actions, General,
Workflow permissions, "Allow GitHub Actions to create and approve pull
requests". Without it `release-please` runs, works out the version, pushes its
branch, and then fails on the last step with *GitHub Actions is not permitted to
create or approve pull requests*. It did that on every push from the day it was
added until 2026-09-19, which is why there is no release yet.

**The repository name has to match the links in the tree.** Sixteen files carry
the project URL: the AppStream component a software centre shows, the bug
tracker in the issue templates, the Play listing, the privacy policy, the
written offer for Qt's source, the Debian copyright file. They all say the same
thing, and a release checks that they say what the repository is actually
called. `scripts/check-urls.sh` is that check, the `preflight` job runs it with
`--strict`, and a release stops rather than publishing a store page that 404s.

## The short version

1. Merge work into `main` with conventional-commit messages. You already do.
2. `release-please` keeps a pull request open titled **chore(main): release
   X.Y.Z**. It accumulates every commit since the last tag.
3. **The release PR will be red.** Add the AppStream release note (below).
4. Merge it. `release-please` tags `vX.Y.Z` and creates the GitHub Release.
5. `release-please` then starts `release.yml` on that tag, which builds every
   artefact and attaches them to the release. It has to start it by hand: a
   tag made with the workflow's own token starts no workflow by itself.

There is no manual `git tag`. If you find yourself typing one, something above
has gone wrong and tagging by hand will hide it.

## The one manual step, and why it is not automated

`release-please` rewrites exactly one thing: the version on the line in
`CMakeLists.txt` marked `# x-release-please-version`. Everything downstream -
`CLIMAT_VERSION`, the `.deb` version, the MSI `ProductVersion`, the MET Norway
User-Agent - reads from there, so one edit moves all of them.

It does **not** write the AppStream release note, and `packaging/CMakeLists.txt`
fails the configure step until somebody does:

```
climat: version disagreement. project() says 0.2.0, the newest <release> in
packaging/linux/climat.metainfo.xml.in says 0.1.0.
```

So the review of a release PR is: read the generated changelog, then write the
human version of it into `packaging/linux/climat.metainfo.xml.in` as a new
`<release>` block at the top of `<releases>`.

That is deliberate. The `<releases>` block is what GNOME Software and KDE
Discover show a user deciding whether to update - curated prose, in the voice
of the product, not a list of commit subjects. Generating it would produce
"fix(net): keep the reason a 4xx gave in the error message" on a store page.
And a version bump with no note is invisible until it is published, which is
the worst moment to notice it.

## What a release carries

| Artefact | Built by | State |
|---|---|---|
| `climat_X.Y.Z_amd64.deb` | `debian:trixie` container | verified |
| `climat-X.Y.Z-x86_64.flatpak` | `flatpak-builder` | verified |
| `climat-X.Y.Z-windows-x64.msi` | WiX v5 on `windows-latest` | **never run** |
| `climat-X.Y.Z-windows-x64.zip` | `Compress-Archive` on the staged install | **never run** |
| `climat-X.Y.Z-x86_64.AppImage` | `linuxdeploy` on `ubuntu-22.04` | **never run**, `continue-on-error` |
| `climat-X.Y.Z-android-arm64-v8a.aab` | Qt 6.11.1 for Android, NDK 27.2 | built locally 2026-09-17; signed only when the upload key is in the secrets |
| `climat-X.Y.Z-android-arm64-v8a.apk` | same build | for a phone on a cable, or F-Droid |
| `LICENSE-OpenSSL.txt` | `scripts/android-openssl.sh` | the Android package carries OpenSSL |
| `SHA256SUMS` | `sha256sum` | |
| `climat.spdx` | `reuse spdx` | SBOM, from the SPDX headers CI already gates |
| `THIRD-PARTY-LICENCES.txt` | `scripts/licence-bundle.sh` | |
| `QT-SOURCE-OFFER.txt` | committed, copied | LGPLv3 obligation |
| `COPYING.LESSER` | committed, copied | the LGPLv3 text, beside every build that bundles Qt |
| build provenance | `actions/attest-build-provenance` | `gh attestation verify`, over everything in `dist/` |
| the store screenshots | `scripts/store-shots.sh`, deployed to Pages | the four URLs the AppStream component declares |

The publish job prints which artefacts arrived and marks any that did not, into
both the job summary and the release body. A missing file and a file nobody
promised look identical on a release page otherwise.

## The obligations, in one place

Two of the attachments are not optional and both are easy to drop by accident.

**`THIRD-PARTY-LICENCES.txt`.** `climat` is statically linked and carries Inter,
the GeoNames place index and recorded ECCC, NWS and Open-Meteo payloads inside
the executable. Five licences that are not ours, in a program that otherwise
appears to be GPL and nothing else. Generated from `packaging/linux/copyright`
and `LICENSES/`, both of which `reuse lint` gates, so it cannot describe a set
of components that is not the set shipped.

**`QT-SOURCE-OFFER.txt`.** The Windows artefacts, the AppImage and the
Android package bundle Qt, which makes them LGPLv3 conveyances. A link to
qt.io does not discharge that - GPLv3 §6 permits pointing at a third party's
server only when the recipient got the object code from that same server. The
offer is valid three years and the `.deb` and Flatpak are explicitly outside
it, because they convey no Qt.

This is also why `cmake/ClimatCPack.cmake` defines no Windows generator. A
`cpack -G ZIP` would produce a Qt-bundling archive carrying neither file, from
one command, on anybody's machine.

## Android and Google Play

The release workflow builds the bundle Play takes (`.aab`) and the APK a
reader installs by hand, from the same `scripts/android.sh` a laptop uses.
`packaging/android/README.md` says what is in the package and
`packaging/android/play/` holds the store listing.

### The upload key

Play signs what it ships with a key Google holds. What we hold is the upload
key, which signs the bundle we send. It is never in this repository. Make it
once, keep it somewhere that is backed up, and put it in the repository's
secrets:

1. Run `keytool -genkeypair -keystore upload.keystore -alias upload -keyalg RSA -keysize 2048 -validity 10000`.
2. Run `base64 -w0 upload.keystore` and paste the output into the secret `ANDROID_UPLOAD_KEYSTORE_BASE64`.
3. Add `ANDROID_UPLOAD_KEY_ALIAS` (`upload`), `ANDROID_UPLOAD_KEYSTORE_PASSWORD` and `ANDROID_UPLOAD_KEY_PASSWORD`.

Without the first secret the job still runs and the release still carries a
bundle, marked unsigned in the log and on the release page. Play refuses an
unsigned bundle, so a release cut without the key is a release that cannot be
uploaded, and it says so rather than pretending.

Losing the upload key is not fatal: Play App Signing lets you register a new
one. Losing the Play signing key is not possible, because Google holds it.
That is the reason to enrol in Play App Signing on the first upload, and the
console makes it the default.

### The first upload

1. Create the app in the Play Console with the name, package name and
   category from `packaging/android/play/listing.md`.
2. Fill in the store listing from the same file, the data safety form from
   `data-safety.md`, and point the privacy policy at the published
   `privacy-policy.md`.
3. Upload `climat-X.Y.Z-android-arm64-v8a.aab` from the GitHub release to an
   internal testing track first. Install it on a phone from the testing link.
4. Promote it to production when it has been on a phone.

The version code Play sees is computed from the version in `CMakeLists.txt`
(`0.1.0` is `100`), so a release bump moves it and nothing has to be typed.

## Rehearsing without releasing

```sh
scripts/preflight.sh            # every gate CI runs, first
scripts/check-urls.sh --strict  # what the release refuses to publish without
scripts/store-shots.sh          # the four screenshots a software centre shows
scripts/deb.sh inspect          # the .deb, in debian:trixie, with its control file
scripts/flatpak.sh deps         # once: the runtime and SDK
scripts/flatpak.sh bundle       # the single-file .flatpak
scripts/licence-bundle.sh       # THIRD-PARTY-LICENCES.txt
scripts/android.sh deps         # once: Qt for Android, the SDK and the NDK
scripts/android.sh openssl      # once per OpenSSL bump
scripts/android.sh aab          # the bundle; unsigned unless the key variables are set
nix develop --command actionlint
```

`release.yml` also accepts `workflow_dispatch`. Run it on a branch and it builds
and attests everything and publishes nothing. Run it on a tag and it is a
release, which is how `release-please` starts it.

## The first release

Nothing has been released yet, so `.release-please-manifest.json` says `0.0.0`.
`initial-version` in `.github/release-please-config.json` makes the first
release `0.1.0`, and `bootstrap-sha` starts its changelog at 2026-09-19, when
the pipeline first worked. Without it the changelog would be every commit since
the prototype. Both settings only apply while no release exists.

The AppStream note for `0.1.0` is already written. Before merging the first
release PR, set its `date` to the day you merge.

## When Flathub happens

The manifest in `packaging/flatpak/` builds a `dir` source - this working tree
- which is what makes `scripts/flatpak.sh build` useful on a branch. A Flathub
submission is a **separate repository**, `flathub/io.github.JowiAoun.Climat`,
whose manifest is the same file with a `git` source pinned to a tag and a
commit. A published build has to be reproducible from something immutable.

Flathub also runs `appstreamcli validate` and requires screenshots at stable
URLs. `scripts/check-packaging.sh` runs the same validation locally; the
screenshots are rendered by `scripts/store-shots.sh` and deployed to GitHub
Pages by the release workflow, un-bezelled, because Flathub's linter reads a
marketing composite with a device frame around it as excessive whitespace.
