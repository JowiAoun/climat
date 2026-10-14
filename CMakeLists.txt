# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The top of the build. Everything here is policy - versions, identity, the
# options that switch pieces of the tree on and off - and nothing here compiles
# anything. The targets live one directory down, in app/.
#
#   nix develop -c cmake --preset dev
#   nix develop -c cmake --build build/dev
#   nix develop -c ./build/dev/app/climat
#
# 3.21 is the floor and it is not arbitrary: it is the first release with the
# `qt_standard_project_setup` story we depend on and the first that ships
# `CMAKE_<LANG>_LINKER_LAUNCHER` for the whole toolchain. The devshell pins
# CMake 4.x, which has dropped compatibility with `cmake_minimum_required`
# below 3.5 - well under us, so this floor is safe. If a dependency ever trips
# that, the answer is to fix the dependency, not to lower this number.
cmake_minimum_required(VERSION 3.21)

project(climat
    # The single source of truth for the version, and the marker is load-bearing
    # rather than decorative: release-please's generic updater rewrites the
    # number on any line carrying that comment, which is how a merged release PR
    # bumps CLIMAT_VERSION, the AppStream <release>, the MSI ProductVersion and
    # the MET Norway User-Agent from one edit. See .github/release-please-config.json.
    VERSION 0.2.0 # x-release-please-version
    DESCRIPTION "A native Qt 6 weather app"
    HOMEPAGE_URL "https://github.com/JowiAoun/climat"
    LANGUAGES CXX
)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---- identity ---------------------------------------------------------------
# One variable, one spelling, everywhere. The reverse-DNS app ID is the name the
# desktop file, the AppStream metainfo, the D-Bus service, the icon theme entry
# and QStandardPaths all have to agree on, and every one of those is a separate
# file that would otherwise carry its own copy of the string. A cache variable
# rather than a plain `set` so a downstream packager can rename the app without
# patching sources - a fork, a nightly channel, a distro rebrand.
set(CLIMAT_APP_ID "io.github.JowiAoun.Climat" CACHE STRING
    "Reverse-DNS application ID: desktop file, AppStream, D-Bus, icon name")

# Where a weather service can reach a human about our traffic.
#
# This goes in the User-Agent every request carries, and it is an obligation
# rather than a courtesy: docs/02-data-sources.md §2.9 records MET Norway's
# requirement for an identifying User-Agent with a contact, and notes that a
# generic one gets 403. api.weather.gov enforces the same thing today - an empty
# User-Agent is refused outright.
#
# A cache variable, and this is the one that a downstream packager genuinely
# should override. A distribution shipping a rebuilt Climat will be the source of
# that traffic, and a rate-limit complaint about it should arrive in an inbox
# they read rather than in ours.
set(CLIMAT_CONTACT "https://github.com/JowiAoun/climat/issues" CACHE STRING
    "Contact channel in the outbound User-Agent. Packagers: point this at yours")

# The daemon's object path: the app ID with its dots turned into slashes.
#
# Computed here rather than in daemon/ because there are two ends of that bus
# and both have to spell it identically - climat-daemon exports the object and
# climat-widget calls it. A path they disagree about is a service that registers
# successfully and then answers nothing, which looks like a daemon bug from one
# side and a widget bug from the other. Both configure the same
# daemon/daemonconfig.h.in from this variable, so there is one spelling.
string(REPLACE "." "/" CLIMAT_DAEMON_PATH "/${CLIMAT_APP_ID}")
set(CLIMAT_DAEMON_PATH "${CLIMAT_DAEMON_PATH}/Daemon")

# ---- options ----------------------------------------------------------------
# Declared now, wired up as each lands. An option that appears the same day as
# the feature it guards is an option nobody remembered to check for, so they go
# in first and are honoured as the code arrives.
option(CLIMAT_DEV_TOOLS    "Build development-only tooling (gallery, harnesses)" ON)
option(CLIMAT_BUILD_GALLERY "Build the component gallery"                        ON)
option(CLIMAT_BUILD_TESTS  "Build the test suite"                                ON)

# ---- Qt ---------------------------------------------------------------------
# 6.8 is the LTS floor from D2. Ask for exactly the components the code uses:
# an unused `find_package` component is a dependency the packager has to satisfy
# for nothing, and - more to the point here - a chance to pull in a module whose
# licence we cannot accept. See cmake/ClimatLicenceGuard.cmake.
include(ClimatLicenceGuard)
include(ClimatEngineGuard)

# Network and Sql are libclimat's, not the app's. Test is the test suite's, and
# it is Qt Test rather than Catch2 or GoogleTest on purpose: D8 in
# docs/03-tech-stack.md rules out Conan and vcpkg so that a distribution
# packager can build this from what their distribution already ships, and a
# test framework fetched at configure time is exactly the dependency that rules
# out. Qt Test comes with the Qt they already have, and QSignalSpy and
# QTRY_COMPARE are the two shapes an asynchronous network client is tested with.
set(climat_qt_components Core Gui Quick Network Sql)
if(CLIMAT_BUILD_TESTS)
    # QuickTest as well as Test, because the 13,400 lines this project is mostly
    # made of are QML and a C++ test can only reach them through a keyhole. It
    # is the runner, not a second framework: QUICK_TEST_MAIN_WITH_SETUP builds a
    # QQmlEngine per test file and reports through the same QTestLib that every
    # other test here already reports through, so ctest sees one list.
    list(APPEND climat_qt_components Test QuickTest)
endif()

climat_guard_qt_components(${climat_qt_components})

find_package(Qt6 6.8 REQUIRED COMPONENTS ${climat_qt_components})
qt_standard_project_setup(REQUIRES 6.8)

# ---- the one optional Qt module ---------------------------------------------
#
# Qt Positioning backs "use my location" (§4.9: GeoClue2 on Linux, Windows
# Location, CoreLocation). It is QUIET and not REQUIRED, and both words are
# deliberate:
#
#   * Optional, because a packager should not have to satisfy a dependency for
#     a feature their users may never press. libclimat compiles the backend out
#     when this is absent and DeviceLocator::create() hands back a locator that
#     reports Unavailable - the same branch a user who denied permission takes,
#     so there is one code path and it is exercised either way.
#
#   * Here rather than in libclimat/, where the code that uses it lives, because
#     a find_package in a subdirectory creates targets only that subdirectory
#     can see. Qt Positioning brings plugin targets that Qt's own executable
#     finalizer resolves from app/ and gallery/, and those are siblings.
#
# It goes through the licence guard like everything else. Qt Positioning is
# LGPLv3-or-commercial, not one of the GPLv3-only modules.
climat_guard_qt_components(Positioning)
find_package(Qt6 6.8 QUIET COMPONENTS Positioning)

if(NOT TARGET Qt6::Positioning)
    message(STATUS
        "climat: Qt6::Positioning was not found, so \"use my location\" is compiled out. "
        "Everything else works; the user searches for a place by name.")
endif()

# ---- and the second one ------------------------------------------------------
#
# Qt D-Bus backs SystemAppearance: the XDG desktop portal is how an app learns
# that the session went dark, and it is the only route that works from inside a
# Flatpak. Optional for the same two reasons Positioning is, plus a third that
# is specific to it - there is no session bus on Windows, so requiring it would
# make one of our two shipping desktop targets unbuildable for a Linux feature.
#
# The fallbacks are real rather than nominal: QStyleHints::colorScheme() answers
# the colour question on Windows and on a portal-less Linux session, and dark is
# the floor under both. See app/platform/systemappearance.h for the order.
#
# LGPLv3-or-commercial, so it clears the guard.
climat_guard_qt_components(DBus)
find_package(Qt6 6.8 QUIET COMPONENTS DBus)

if(NOT TARGET Qt6::DBus)
    message(STATUS
        "climat: Qt6::DBus was not found, so the desktop's colour scheme and "
        "reduced-motion preference are read from QStyleHints alone. The three-way "
        "override in Settings still works.")
endif()

# Where qt_add_qml_module lays out its module directories. This is not a tidiness
# preference - it is what makes the build's own QML modules discoverable as
# modules rather than as resources baked into one executable:
#
#   * `all_qmllint` can resolve `import Climat`
#   * qmlls (the language server) resolves it too, so an editor stops
#     underlining every type in the module
#   * QML_IMPORT_PATH=build/dev/qml qml … loads our types without a build of the
#     app around them
#
# Without it those three fail in three different and individually confusing
# ways. Do not remove it.
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

# The engine first, because app/ links it. It is also the only directory here
# whose sources are MPL-2.0 rather than GPL-3.0-or-later - see D6, and
# libclimat/CMakeLists.txt, which explains why the build file itself is not.
add_subdirectory(libclimat)

add_subdirectory(app)

# The command-line client. Every desktop, because everything it needs is in
# libclimat and Qt Core: a status bar on Windows wants the forecast as much as
# waybar does. See cli/CMakeLists.txt. Not Android, where there is no shell to
# run it from and where Qt turns every executable into an app package of its
# own - the APK would have shipped a second, invisible app beside the first.
if(NOT ANDROID)
    add_subdirectory(cli)
endif()

# The daemon that widgets and the tray read from. Gated twice, and both gates
# are real: the whole thing is a session-bus service, and Windows has no
# session bus, so on Windows this is not a feature that is switched off but a
# concept that does not exist. A Linux build without Qt DBus is the same
# situation as a Linux build without Qt Positioning - the app is complete, and
# one optional thing beside it is not built.
#
# The app does not link it and does not know about it. A desktop with no daemon
# running is exactly the app that shipped before this directory existed.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET Qt6::DBus)
    add_subdirectory(daemon)

    # And the process that draws what it serves. Gated with the daemon rather
    # than separately, because a widget host with nothing to read from is not a
    # degraded feature - every number on its tiles comes over that bus, and it
    # would come up, register nothing, and show ten skeletons.
    add_subdirectory(widgets)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS
        "climat: Qt6::DBus was not found, so climat-daemon is not built and the "
        "desktop widgets have nothing to read from. The app is unaffected.")
endif()

# The component gallery is a separate executable and not a mode of the app, so
# `CLIMAT_BUILD_GALLERY=OFF` is a directory that is never entered rather than a
# preprocessor symbol threaded through the product. That is the whole point of
# it having moved: a storybook shipping inside a weather app is wrong on
# principle, and it also costs about eleven hundred lines against the <15 MB
# budget in docs/03-tech-stack.md §3.4.
#
# It depends on `climatqml`, which app/ defines, so it is entered second.
if(CLIMAT_BUILD_GALLERY)
    add_subdirectory(gallery)
endif()

# A harness that prints what the provider adapters make of a recorded fixture.
# The gallery is for looking at components; this is for looking at data, and it
# exists because the pollen gate in
# libclimat/providers/airquality/openmeteoairqualityprovider.h is the kind of
# rule a test can satisfy while the screen still says something untrue. Guarded
# by CLIMAT_DEV_TOOLS, which is what that option was declared for.
if(CLIMAT_DEV_TOOLS)
    add_subdirectory(tools/provider-probe)

    # And its opposite number: the same shape of report, built from a *live*
    # fetch rather than from a recording. The two answer different questions
    # and neither replaces the other - a recorded fixture guards against
    # regression in us, and only a real request notices Open-Meteo renaming a
    # field or changing a unit. See tools/openmeteo-probe/main.cpp.
    add_subdirectory(tools/openmeteo-probe)

    # And the geocoding one, which is two harnesses in a trenchcoat: a live
    # forward search, and an offline reverse lookup that prints the distance
    # and whether the point was inside the settlement's modelled footprint.
    # The second half is there because the ranking rule in
    # libclimat/providers/geocoding/geonamesindex.h is exactly the kind of
    # heuristic that passes four test coordinates and says something silly
    # about the fifth.
    add_subdirectory(tools/geonames)
endif()

# What a Linux install puts on disk beside the binary: the desktop entry, the
# AppStream component and the icon theme. Not guarded by an option - a build
# that cannot be installed is not a build anybody can package, and the
# directory returns immediately on every platform that has no use for it.
#
# It reads CLIMAT_APP_ID and PROJECT_VERSION and defines no targets, so it could
# go anywhere after the identity block above. It is here, last of the product
# directories, because it is the only one whose output is not code.
add_subdirectory(packaging)

# enable_testing() has to be called from the top-level directory or `ctest
# --test-dir build/dev` finds no tests at all and says so in a way that reads
# like the tests failed to build. It is guarded by the same option as the
# directory below so that a packaging build asks for neither.
if(CLIMAT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# The .deb. Last, and included here rather than from packaging/, because CPack
# reads its configuration out of the directory that includes it - a CPack set up
# one directory down produces a package with a default name and no dependencies,
# and says nothing about it. See the file for why it has to be built against
# system Qt.
# Linux only, and Windows is not an omission. Both Windows artefacts - the
# portable ZIP and the MSI - bundle Qt, which makes them LGPLv3 conveyances
# that have to carry the written source offer and the third-party licence
# bundle. A `cpack -G ZIP` here would produce an archive that satisfies neither,
# from a one-line invocation, on anybody's machine. The release workflow stages
# the install, adds those two files, and builds both artefacts from that one
# tree - so there is no route to a Windows archive that has not been through
# it. See .github/workflows/release.yml and packaging/QT-SOURCE-OFFER.txt.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(ClimatCPack)
endif()
