# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The .deb, via CPack.
#
# Included from the top-level CMakeLists.txt and not from packaging/, because
# CPack reads its variables out of the directory that includes it and a
# subdirectory's `set` does not reach the parent scope. A CPack configured one
# directory down produces a package with a default name, no dependencies and no
# description, and reports success.
#
# ==== who this .deb is for, and who it is not for ============================
#
# It installs on Debian 13 and Ubuntu 26.04 or newer, and it does not install on
# Ubuntu 24.04. That is not a limitation to be worked around: 24.04 ships
# Qt 6.4.2, our floor is 6.8, and a package that installs and then fails to
# start is worse than one that refuses. 24.04 users get the Flatpak, which
# carries its own Qt - see packaging/flatpak/, and docs/known-gaps.md, which
# says so in the place a user will look.
#
# The dependency list is not written here. CPACK_DEBIAN_PACKAGE_SHLIBDEPS reads
# the built binary's DT_NEEDED entries and asks dpkg which packages provide
# them, so the list is derived from what we actually linked rather than from
# what somebody believed we linked. It follows that THIS MUST BE BUILT AGAINST
# SYSTEM QT. Run it under Nix and shlibdeps resolves libQt6Quick.so.6 to a
# /nix/store path, finds no Debian package owning it, and either fails or emits
# a package that depends on nothing. CI builds it in debian:trixie for exactly
# this reason; scripts/deb.sh does the same thing locally through docker.

set(CPACK_PACKAGE_NAME "climat")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Jowi Aoun")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
# Not PROJECT_DESCRIPTION, which reads "A native Qt 6 weather app". Debian
# policy §3.4.1 asks a synopsis not to begin with an article and to read as the
# continuation of "climat is a…", and lintian has a tag for it.
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "native weather app with charts and severe weather warnings")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

# Debian's Maintainer field has to parse as "Name <address>", so the issue
# tracker that CLIMAT_CONTACT holds cannot be reused here. A GitHub noreply
# address parses as one and is not anybody's personal inbox, which is the
# right default for a package that strangers will file bugs about. It does not
# receive mail, so the bug tracker is the way in.
# A cache variable because a distribution rebuilding this is the maintainer of
# their build, not us, and lintian will tell them so.
set(CLIMAT_MAINTAINER "Jowi Aoun <JowiAoun@users.noreply.github.com>" CACHE STRING
    "Debian Maintainer field. Packagers: put yourself here")
set(CPACK_PACKAGE_CONTACT "${CLIMAT_MAINTAINER}")

set(CPACK_STRIP_FILES ON)

set(CPACK_GENERATOR "DEB")

# ---- the Debian control fields ----------------------------------------------
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)

# The EXTENDED description only, unindented, and both of those were learned
# the hard way. CPack writes the synopsis itself from
# CPACK_PACKAGE_DESCRIPTION_SUMMARY and then appends this, so a synopsis line
# at the top of this string appears twice in the built package; and CPack adds
# the leading space that Debian's format requires, so a string that already has
# one comes out indented by two on every line but the last.
#
# The lone "." is Debian's paragraph separator and passes through as-is.
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
"Climat shows the next two weeks of forecast as charts rather than a grid of
numbers, and puts official severe-weather warnings from Environment and
Climate Change Canada and the United States National Weather Service on every
screen where they matter.
.
No ads, no news feed, no telemetry, no account and no API key. Weather data
comes from Open-Meteo with MET Norway as a fallback, and every source is
credited in the app under About, Data sources.")

include(CPack)
