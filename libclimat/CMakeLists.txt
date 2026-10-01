# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The engine. Qt Core, Qt Network, Qt Sql - and provably nothing that draws.
# Two optional Core-only modules join them below: Qt Positioning and Qt D-Bus,
# the two backends behind "use my location".
#
# Note the licence line above: this build file is GPL-3.0-or-later like the rest
# of the build, while every source file it compiles is MPL-2.0. That is not an
# oversight and it is the point of the split (D6). The MPL is file-scoped, so
# the licence that matters is the one in each source file's header; a build
# script is not part of the library it builds, and a downstream that vendors
# libclimat's sources under the MPL writes its own build anyway.
#
# ---- what this target may link ----------------------------------------------
#
# Core, Network, Sql. Not Gui, not Qml, not Quick, and climat_forbid_gui() at the
# bottom of this file makes that a configure-time error rather than a rule in a
# document - see cmake/ClimatEngineGuard.cmake for why the promise is worth a
# hundred lines of CMake to keep.
#
# ---- STATIC, and the target name --------------------------------------------
#
# The target is `libclimat` because that is what docs/04-architecture.md §4.3
# calls it and what every other document refers to, and OUTPUT_NAME is `climat`
# so that the archive is `libclimat.a` rather than `liblibclimat.a`. The
# executable in app/ is also called `climat`; the two do not collide because one
# is an archive with a `lib` prefix in build/dev/libclimat and the other is an
# executable in build/dev/app.
#
# STATIC for the same reason app/climatqml is static: one repository, one
# release, consumers always built together. The applet and the extension that
# will one day want a shared object are separate builds in separate
# repositories, and they will make that decision for themselves.

add_library(libclimat STATIC
    cache/cachepolicy.h
    cache/cachepolicy.cpp
    cache/cachestore.h
    cache/cachestore.cpp
    cache/payloadcache.h
    cache/payloadcache.cpp
    cache/migrations.h
    cache/migrations.cpp
    core/clock.h
    core/clock.cpp
    core/error.h
    core/error.cpp
    core/result.h
    domain/airquality.h
    domain/airquality.cpp
    domain/alert.h
    domain/alert.cpp
    domain/coordinate.h
    domain/coordinate.cpp
    domain/place.h
    domain/place.cpp
    domain/forecast.h
    domain/forecast.cpp
    domain/hourconvention.h
    domain/hourconvention.cpp
    domain/reading.h
    domain/scales.h
    domain/scales.cpp
    domain/timeaxis.h
    domain/timeaxis.cpp
    domain/units.h
    domain/units.cpp
    domain/weathercode.h
    domain/weathercode.cpp
    providers/ialertprovider.h
    providers/ialertprovider.cpp
    providers/iforecastprovider.h
    providers/iforecastprovider.cpp
    providers/registry.h
    providers/registry.cpp
    places/devicelocator.h
    places/devicelocator.cpp
    places/locationcontroller.h
    places/locationcontroller.cpp
    places/placesearchmodel.h
    places/placesearchmodel.cpp
    providers/airquality/openmeteoairqualityprovider.h
    providers/airquality/openmeteoairqualityprovider.cpp
    providers/eccc/ecccalertprovider.h
    providers/eccc/ecccalertprovider.cpp
    providers/nws/nwsalertprovider.h
    providers/nws/nwsalertprovider.cpp
    providers/geocoding/geocodingparser.h
    providers/geocoding/geocodingparser.cpp
    providers/geocoding/geonamesindex.h
    providers/geocoding/geonamesindex.cpp
    providers/geocoding/igeocoder.h
    providers/geocoding/igeocoder.cpp
    providers/geocoding/offlinereversegeocoder.h
    providers/geocoding/offlinereversegeocoder.cpp
    providers/geocoding/openmeteogeocoder.h
    providers/geocoding/openmeteogeocoder.cpp
    providers/fixture/fixtureprovider.h
    providers/fixture/fixtureprovider.cpp
    providers/openmeteo/openmeteoadapter.h
    providers/openmeteo/openmeteoadapter.cpp
    providers/openmeteo/openmeteoconsensus.h
    providers/openmeteo/openmeteoconsensus.cpp
    providers/openmeteo/openmeteoforecastprovider.h
    providers/openmeteo/openmeteoforecastprovider.cpp
    providers/openmeteo/openmeteovariables.h
    providers/openmeteo/openmeteovariables.cpp
    providers/metno/metnoforecastprovider.h
    providers/metno/metnoforecastprovider.cpp
    providers/metno/symbolcode.h
    providers/metno/symbolcode.cpp
    net/backoff.h
    net/backoff.cpp
    net/httpclient.h
    net/httpclient.cpp
    net/httprequest.h
    net/requestkey.h
    net/requestkey.cpp
    net/validatorstore.h
    net/validatorstore.cpp

    # How a forecast leaves this process. It is here rather than in daemon/
    # because both ends of the wire need it and one of them is not the daemon -
    # and because it is pure data with no I/O and no clock, which is what lets
    # a test build a snapshot out of a recorded fixture and compare the bytes.
    wire/snapshot.h
    wire/snapshot.cpp
)

set_target_properties(libclimat PROPERTIES
    OUTPUT_NAME climat
    AUTOMOC ON
)

# ---- who we say we are ------------------------------------------------------
#
# The User-Agent's version comes from PROJECT_VERSION, the same variable
# app/climatconfig.h.in reads, so the string a weather service sees and the
# string `climat --version` prints cannot disagree. See climatidentity.h.in - this
# is a compliance string, not a nicety.
configure_file(climatidentity.h.in climatidentity.h @ONLY)

# ---- the bundled place index ------------------------------------------------
#
# Four hundred kilobytes of GeoNames, compiled into the library, and the reason
# it is here rather than fetched is written out in
# providers/geocoding/geonamesindex.h: Nominatim answers 403 to the first
# reverse-geocoding request a properly identified client sends it, so the lookup
# has to be local. Being local also makes it deterministic, which is what golden
# images depend on, and offline, which is the app's headline property.
#
# The data is NOT ours. It is GeoNames, CC-BY 4.0, and REUSE.toml declares it
# with GeoNames as the copyright holder rather than with ours - the same
# distinction the bundled typeface gets in app/CMakeLists.txt, and for the same
# reason. tools/geonames/ is the packer, and its output is committed so that a
# build never needs a network.
#
# BASE strips the source path so the file lands at `:/climat/cities15000.cgx`
# rather than at `:/climat/providers/geocoding/data/cities15000.cgx`. A resource
# alias is what GeonamesIndex::bundledResourcePath() names, and a path that
# repeats the source layout is a path that changes when the source layout does.
qt_add_resources(libclimat "climat_geonames"
    PREFIX "/climat"
    BASE providers/geocoding/data
    FILES providers/geocoding/data/cities15000.cgx
)

# ---- the recorded fixtures --------------------------------------------------
#
# Three recorded afternoons, compiled in. `--fixture toronto` has to work from
# an installed binary on a machine with no source tree, because the whole point
# of it is that a reviewer, a CI runner and a golden-image test all see the same
# pixels - and "check out the repository first" is not a property those three
# have in common.
#
# The data is Open-Meteo's, under CC BY 4.0, and REUSE.toml declares it that way
# with Open-Meteo as the copyright holder. Same distinction as the bundled
# typeface and the GeoNames index: three things in this repository that are not
# ours.
#
# BASE strips the source path so a fixture lands at `:/climat/fixtures/<name>/`.
# fixtureprovider.cpp lists the catalogue by running QDir over that prefix, so
# the directory name IS the fixture name and there is no second list to keep in
# step - adding a fixture is four lines here and nothing anywhere else.
#
# 116 kB of JSON before compression, against the <15 MB budget in
# docs/03-tech-stack.md §3.4.
qt_add_resources(libclimat "climat_fixtures"
    PREFIX "/climat/fixtures"
    BASE providers/fixture/data
    FILES
        providers/fixture/data/toronto/fixture.json
        providers/fixture/data/toronto/forecast.json
        providers/fixture/data/toronto/airquality.json
        providers/fixture/data/toronto/alerts.json
        providers/fixture/data/seattle/fixture.json
        providers/fixture/data/seattle/forecast.json
        providers/fixture/data/seattle/airquality.json
        providers/fixture/data/seattle/alerts.json
        providers/fixture/data/berlin/fixture.json
        providers/fixture/data/berlin/forecast.json
        providers/fixture/data/berlin/airquality.json
        providers/fixture/data/kampala/fixture.json
        providers/fixture/data/kampala/forecast.json
)

# ---- optional: Qt Positioning -----------------------------------------------
#
# "My location" - docs/04-architecture.md §4.9 puts Qt Positioning against
# GeoClue2 on Linux, Windows Location on Windows and CoreLocation on macOS.
#
# The find_package is at the TOP LEVEL and not here, which is worth a note
# because everything else about this target is decided in this file. An
# imported target created by a find_package in a subdirectory is visible only
# in that subdirectory and below, and Qt Positioning brings a set of plugin
# targets with it that Qt's own executable finalizer resolves from *app/* and
# *gallery/* - sibling directories that would never see them. The build fails
# there, naming Qt6::QGeoPositionInfoSourceFactoryGeoclue2Plugin, which is a
# long way from the line that caused it.
#
# Qt6::Positioning links Qt6::Core and nothing else - checked, and it has to
# be: climat_forbid_gui() at the bottom of this file would refuse the configure
# otherwise, which is exactly the guard doing its job. The QML bindings that
# would have pulled in QtQml are a separate Qt6::PositioningQuick target that
# we do not link.
if(TARGET Qt6::Positioning)
    target_sources(libclimat PRIVATE
        places/qtpositioninglocator.h
        places/qtpositioninglocator.cpp
    )
    target_compile_definitions(libclimat PRIVATE CLIMAT_HAVE_POSITIONING)
    target_link_libraries(libclimat PRIVATE Qt6::Positioning)
endif()

# ---- optional: the Location portal, over Qt D-Bus ---------------------------
#
# The second backend behind DeviceLocator, and the only one that works from
# inside a Flatpak - see places/portallocator.h. Qt D-Bus links Qt Core and
# nothing else, so climat_forbid_gui() at the bottom of this file still holds,
# and it is a Linux-only feature by nature: there is no session bus anywhere
# else. The find_package is at the top level for the reason Positioning's is.
if(TARGET Qt6::DBus AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(libclimat PRIVATE
        places/portallocator.h
        places/portallocator.cpp
    )
    target_compile_definitions(libclimat PRIVATE CLIMAT_HAVE_PORTAL_LOCATION)
    target_link_libraries(libclimat PRIVATE Qt6::DBus)
endif()

# ---- include paths ----------------------------------------------------------
#
# The repository root, so that every include inside the engine reads
# `#include "libclimat/net/httpclient.h"` - the path the file actually has. The
# alternative, relative includes, means `../../core/clock.h` in a header whose
# meaning then depends on where the *including* file sits, and a header that
# moves breaks its consumers rather than itself.
#
# PUBLIC on both counts: app/ and tests/ include the engine's headers by the
# same spelling, and the test that asserts the User-Agent needs the generated
# identity header to state what it expects.
target_include_directories(libclimat PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_features(libclimat PUBLIC cxx_std_17)

# -Wswitch is what makes cachepolicy.cpp's total switch over DataKind a
# guarantee rather than a habit: adding a kind without adding its TTL row stops
# the build instead of silently caching it for zero seconds.
target_compile_options(libclimat PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wswitch>
)

# QT_NO_CAST_FROM_ASCII would be the stricter setting and is not on, because the
# SQL in cache/ is genuinely more readable as QStringLiteral than as anything
# else and the casts that remain are all QLatin1String on compile-time
# constants. What is on is the one that catches a real bug class: an implicit
# conversion from a QUrl to a QString drops the difference between an encoded
# and a decoded URL, which is how a query parameter with a `+` in it becomes a
# space on one code path and not another.
#
# PRIVATE. Neither macro changes a type's layout, so there is no ODR hazard in
# one translation unit having them and another not - which means they can be
# this library's house rules rather than something imposed on every consumer.
# app/ and the QML module get to make their own decision.
target_compile_definitions(libclimat PRIVATE
    QT_NO_CAST_FROM_BYTEARRAY
    QT_NO_URL_CAST_FROM_STRING
)

target_link_libraries(libclimat PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)

# The assertion. Everything above this line is a claim; this line is what makes
# it true. Try it: add Qt6::Gui to the list above and configure.
climat_forbid_gui(libclimat)
