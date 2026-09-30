# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The test suite. Qt Test, ctest, and nothing fetched from the internet at
# configure time or at run time.
#
# ---- why Qt Test and not Catch2, doctest or GoogleTest ----------------------
#
# D8 in docs/03-tech-stack.md rules out Conan and vcpkg so that a distribution
# packager can build Climat from what their distribution already ships. A test
# framework pulled in by FetchContent is exactly the dependency that decision
# excludes: it turns `cmake --build` into an operation that needs a network,
# which is the one thing a Debian or Fedora build environment does not have.
#
# Qt Test arrives with the Qt that is already required, and it happens to be the
# right shape anyway. Everything below is asynchronous - a network client, a
# signal, a future - and QSignalSpy and QTest::qWaitFor are what those are
# tested with.
#
# ---- the tests link libclimat and Qt Test, and that is the whole list --------
#
# Not Qt6::Gui, not Qt6::Quick. That is not tidiness: `climat_no_gui_test` below
# reads the linked binary's DT_NEEDED entries and fails if a GUI library is
# there, which is only a meaningful check because these executables link the
# engine and nothing else. Adding Qt6::Gui to a test target here would silently
# defeat the strongest guarantee in the build.

# ---- shared test support ----------------------------------------------------
#
# A loopback HTTP server and the proxy factory that makes reaching anything else
# impossible. Both are GPL-3.0-or-later like everything outside libclimat/ -
# tests are not part of the reusable engine.
add_library(climat_testsupport STATIC
    support/httpstub.h
    support/httpstub.cpp
    support/networkguard.h
    support/networkguard.cpp
    support/settingssandbox.h
)

target_include_directories(climat_testsupport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(climat_testsupport PUBLIC Qt6::Core Qt6::Network)
target_compile_features(climat_testsupport PUBLIC cxx_std_17)

climat_forbid_gui(climat_testsupport)

# ---- one function, because every test wants the same five lines --------------
#
# CLIMAT_SOURCE_DIR is the repository root, handed to the tests that read the
# tree - tst_sourcerules.cpp scans for wall-clock reads and for hostnames. A
# test that computed the path from its own binary location would be a test that
# breaks the day the build directory moves.
function(climat_add_test name)
    add_executable(${name} ${name}.cpp)

    target_link_libraries(${name} PRIVATE libclimat climat_testsupport Qt6::Test)
    target_compile_definitions(${name} PRIVATE
        CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    climat_forbid_gui(${name})

    add_test(NAME ${name} COMMAND ${name})

    set_tests_properties(${name} PROPERTIES
        # offscreen, in case a future test links Gui: a test that tries to open
        # a window on a headless runner hangs rather than fails, and a hung test
        # is a timeout nobody can read.
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"

        # Generous, because the failure this bounds is a deadlock rather than a
        # slow machine. Nothing here should take more than a second.
        TIMEOUT 60
    )
endfunction()

climat_add_test(tst_clock)
climat_add_test(tst_backoff)
climat_add_test(tst_requestkey)
climat_add_test(tst_httpclient)
climat_add_test(tst_cachestore)
climat_add_test(tst_cachepolicy)
climat_add_test(tst_networkisolation)
climat_add_test(tst_sourcerules)
climat_add_test(tst_alerts)
climat_add_test(tst_alertproviders)
climat_add_test(tst_weathercode)
climat_add_test(tst_moonphase)
climat_add_test(tst_scales)
climat_add_test(tst_timeaxis)
climat_add_test(tst_openmeteoadapter)
climat_add_test(tst_openmeteovariables)
climat_add_test(tst_airquality)
climat_add_test(tst_metno)
climat_add_test(tst_fixtureprovider)
climat_add_test(tst_providerregistry)
climat_add_test(tst_geocoding)
climat_add_test(tst_reversegeocode)
climat_add_test(tst_places)
climat_add_test(tst_wiresnapshot)

# ---- the tiles ---------------------------------------------------------------
#
# Registered by hand rather than through climat_add_test(), for two reasons that
# both come from the subject. It links `climatwidgetsqml` - the widget host's QML
# module - rather than libclimat, because `Wx` is what every number a tile prints
# goes through. And climat_forbid_gui() is deliberately not applied: that guard
# enforces libclimat's own promise, and this is a test of a thing that draws.
#
# Guarded on the target, because widgets/ is only entered on Linux with Qt DBus.
if(TARGET climatwidgetsqml)
    add_executable(tst_widgets tst_widgets.cpp)

    target_link_libraries(tst_widgets PRIVATE climatwidgetsqml Qt6::Test)
    target_compile_definitions(tst_widgets PRIVATE
        CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    add_test(NAME tst_widgets COMMAND tst_widgets)

    # TZ=UTC is load-bearing here rather than hygienic. Half of what this file
    # asserts is that an instant is read in the zone its own offset names and
    # NOT converted into the reader's - which is invisible on a machine whose
    # clock happens to agree with the fixture.
    set_tests_properties(tst_widgets PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
        TIMEOUT 60
    )
endif()

# ---- climat-cli, run as a script would run it ---------------------------------
#
# The test starts the real binary, so it links nothing of the CLI's and needs
# only to know where it is. climat_forbid_gui() applies to the test itself, and
# `cli_has_no_gui` below asks the same question of the binary the test runs.
add_executable(tst_cli tst_cli.cpp)

# It runs the binary rather than linking it, so nothing in the link line would
# make CMake build climat-cli first. Stated, or `--target tst_cli && ctest -R
# tst_cli` tests a path that may hold no executable.
add_dependencies(tst_cli climat-cli)

target_link_libraries(tst_cli PRIVATE climat_testsupport Qt6::Test)
target_compile_definitions(tst_cli PRIVATE
    CLIMAT_CLI_BINARY="$<TARGET_FILE:climat-cli>"
)

climat_forbid_gui(tst_cli)

add_test(NAME tst_cli COMMAND tst_cli)

set_tests_properties(tst_cli PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
    TIMEOUT 120
)

# ---- the location portal, against a portal of our own -----------------------
#
# Needs a bus that is not the developer's, because the test OWNS the portal's
# well-known name. dbus-run-session starts one that lives exactly as long as
# the test. Where it is not installed the test is not registered, and the
# configure says so - a test that cannot run has not passed, and a runner
# without dbus-daemon has said nothing about this code.
#
# Linked against Qt D-Bus directly as well as through libclimat, because the
# fake portal in it is itself a D-Bus service.
if(TARGET Qt6::DBus AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(CLIMAT_DBUS_RUN_SESSION dbus-run-session)

    add_executable(tst_portallocator tst_portallocator.cpp)

    target_link_libraries(tst_portallocator PRIVATE
        libclimat climat_testsupport Qt6::Test Qt6::DBus)
    target_compile_definitions(tst_portallocator PRIVATE
        CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    climat_forbid_gui(tst_portallocator)

    # Through scripts/dbus-test.sh rather than through dbus-run-session
    # directly: a private bus still activates the host's real portal out of
    # /usr/share/dbus-1/services, and the wrapper is where that and two other
    # environment traps are handled and written down.
    if(CLIMAT_DBUS_RUN_SESSION)
        add_test(NAME tst_portallocator
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/dbus-test.sh $<TARGET_FILE:tst_portallocator>
        )
        set_tests_properties(tst_portallocator PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
            TIMEOUT 120
        )
    else()
        message(STATUS
            "climat: dbus-run-session was not found, so tst_portallocator is built and "
            "NOT registered. Install dbus to run it.")
    endif()
endif()

# ---- the notifications, against a desktop of our own -------------------------
#
# Same arrangement and the same gate as tst_portallocator: the test owns
# org.freedesktop.portal.Desktop and org.freedesktop.Notifications on a bus
# dbus-run-session started for it. It links climatqml because Notifier is
# compiled into the app's module, so climat_forbid_gui() is deliberately not
# applied - the thing under test lives in a binary that draws.
if(TARGET Qt6::DBus AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(tst_notifier tst_notifier.cpp)

    target_link_libraries(tst_notifier PRIVATE climatqml climat_testsupport Qt6::Test Qt6::DBus)
    target_compile_definitions(tst_notifier PRIVATE
        CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    if(CLIMAT_DBUS_RUN_SESSION)
        add_test(NAME tst_notifier
            COMMAND ${CMAKE_SOURCE_DIR}/scripts/dbus-test.sh $<TARGET_FILE:tst_notifier>
        )
        set_tests_properties(tst_notifier PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
            TIMEOUT 120
        )
    else()
        message(STATUS
            "climat: dbus-run-session was not found, so tst_notifier is built and "
            "NOT registered. Install dbus to run it.")
    endif()
endif()

# ---- the daemon's service, without the bus -----------------------------------
#
# Registered by hand rather than through climat_add_test() because it links the
# daemon's core library rather than libclimat directly. climat_forbid_gui() is
# applied all the same: the daemon's whole promise is that it draws nothing, and
# a test binary that linked Qt Quick through it would be the first place that
# promise broke.
#
# Guarded on the target, because daemon/ is only entered on Linux with Qt DBus.
if(TARGET climat_daemon_core)
    add_executable(tst_snapshotservice tst_snapshotservice.cpp)

    target_link_libraries(tst_snapshotservice PRIVATE
        climat_daemon_core climat_testsupport Qt6::Test)
    target_compile_definitions(tst_snapshotservice PRIVATE
        CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    climat_forbid_gui(tst_snapshotservice)

    add_test(NAME tst_snapshotservice COMMAND tst_snapshotservice)

    set_tests_properties(tst_snapshotservice PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
        TIMEOUT 60
    )
endif()

# ---- the one test that links app/ -------------------------------------------
#
# Registered by hand rather than through climat_add_test(), because it is the one
# test whose subject is a VIEW MODEL and not the engine. `Detail` is
# app/viewmodels/conditionsdata.h: GPL-3.0-or-later, not MPL-2.0, read by QML and
# therefore allowed to depend on it. climat_forbid_gui() is deliberately not
# applied - it would refuse the link, and refusing it here would be enforcing
# libclimat's promise against something that never made it.
#
# The promise itself is untouched. `engine_has_no_gui` below asks the question of
# tst_httpclient, which links the engine and nothing else, and that is still the
# binary the guarantee is read off.
#
# What it is for: the neutral shape those maps are born with has to stay in step
# with what the build functions produce, and nothing else would notice when it
# does not. The file's own header has the argument.
add_executable(tst_conditionsdata tst_conditionsdata.cpp)

target_link_libraries(tst_conditionsdata PRIVATE climatqml climat_testsupport Qt6::Test)
target_compile_definitions(tst_conditionsdata PRIVATE
    CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME tst_conditionsdata COMMAND tst_conditionsdata)

set_tests_properties(tst_conditionsdata PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
    TIMEOUT 60
)

# ---- and the window under the chart -----------------------------------------
#
# `Data`'s hourly window, which stopped being one window the moment the day
# strip was wired to it. Registered here for tst_conditionsdata's reason: the
# subject is a view model in app/, and climat_forbid_gui() would refuse the link.
#
# TZ is pinned for the same reason the golden run pins it. The window is
# selected by comparing a local date against a local date, and the fixture's
# zone is Toronto's - a runner in UTC and a runner in Tokyo disagreeing about
# which hours are "Friday" is exactly the failure this asserts is impossible.
add_executable(tst_forecastdata tst_forecastdata.cpp)

target_link_libraries(tst_forecastdata PRIVATE climatqml climat_testsupport Qt6::Test)
target_compile_definitions(tst_forecastdata PRIVATE
    CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME tst_forecastdata COMMAND tst_forecastdata)

set_tests_properties(tst_forecastdata PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
    TIMEOUT 60
)

# ---- the other two that link app/ -------------------------------------------
#
# `TimeFormat`'s five spellings and `Units`' two presets. Here rather than
# through climat_add_test() for tst_conditionsdata's reason - their subject is
# app/ and climat_forbid_gui() would refuse the link.
#
# LC_ALL matters more to the first of these than anywhere else in this file. Half
# of what it asserts is that a clock reading is decided by the *preference* and
# not by the locale, which was true of five of the six formatters in this app and
# not of the sixth; pinning the locale is what makes a regression to
# QLocale::ShortFormat fail here rather than pass on a developer's machine and
# fail in CI.
foreach(climat_app_test tst_timeformat tst_units tst_settings)
    add_executable(${climat_app_test} ${climat_app_test}.cpp)

    target_link_libraries(${climat_app_test} PRIVATE climatqml climat_testsupport Qt6::Test)
    target_compile_definitions(${climat_app_test} PRIVATE
        CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    )

    add_test(NAME ${climat_app_test} COMMAND ${climat_app_test})

    set_tests_properties(${climat_app_test} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
        TIMEOUT 60
    )
endforeach()

# ---- the alert view model ---------------------------------------------------
#
# Here rather than through climat_add_test() for tst_conditionsdata's reason: the
# subject is a view model in app/ and climat_forbid_gui() would refuse the link.
#
# TZ is load-bearing rather than hygienic, as it is for tst_widgets. Half of
# what this file asserts is which words a banner puts under a headline, and one
# of them is a weekday - "Until Fri 11:00 PM" for a hazard ending on a different
# day from the one the reader is in. Which day that is depends on the zone the
# instant is read in, so a runner in Tokyo would disagree with a runner in
# Toronto about a string neither of them chose.
add_executable(tst_alertsdata tst_alertsdata.cpp)

target_link_libraries(tst_alertsdata PRIVATE climatqml climat_testsupport Qt6::Test)
target_compile_definitions(tst_alertsdata PRIVATE
    CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME tst_alertsdata COMMAND tst_alertsdata)

set_tests_properties(tst_alertsdata PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_FORCE_STDERR_LOGGING=1;LC_ALL=C.UTF-8;TZ=UTC"
    TIMEOUT 60
)

# ---- the golden images' canary ----------------------------------------------
#
# Links climatqml for AppFont and therefore links Qt6::Gui, which is why it is
# registered here rather than through climat_add_test() - that function applies
# climat_forbid_gui() and would refuse the link. Measuring text without a GUI is
# not a thing that can be arranged.
#
# FONTCONFIG_FILE is the whole reason this is meaningful: it measures the font
# environment the golden images are *taken* in, not the one the developer's
# desktop happens to have. Without it the numbers are about the host and the
# test would fail on every machine but one.
add_executable(tst_environment tst_environment.cpp)

target_link_libraries(tst_environment PRIVATE climatqml Qt6::Test)
target_compile_definitions(tst_environment PRIVATE
    CLIMAT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME tst_environment COMMAND tst_environment)

# Labelled `golden`, so that `ctest -LE golden` drops the canary along with the
# images it guards. That is not tidiness - it was a real failure. Built against
# Debian 13's Qt 6.8.2 the suite is 21/22, and the one failure is this test
# correctly reporting that FreeType there rounds line heights differently:
#
#   11px height: recorded 13.296875, measured 13.312500
#
# One sixty-fourth of a pixel, at five of the eight sizes, with every advance
# identical. Every golden image would differ and not one of those differences
# would be a defect - which is exactly what this test exists to say. But the
# packager's build deliberately runs a *different* Qt, so asking it whether the
# machine matches the one the images were recorded on is asking a question with
# a known answer and calling it a failure.
set_tests_properties(tst_environment PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=;QT_FORCE_STDERR_LOGGING=1;QT_SCALE_FACTOR=1;QT_ENABLE_HIGHDPI_SCALING=0;QT_SCREEN_SCALE_FACTORS=;QT_FONT_DPI=96;LC_ALL=C.UTF-8;LANG=C.UTF-8;TZ=UTC;FONTCONFIG_FILE=${CMAKE_CURRENT_SOURCE_DIR}/golden/fontconfig.conf"
    LABELS "golden"
    TIMEOUT 60
)

# ---- the QML suite ----------------------------------------------------------
#
# One executable running every tests/qml/tst_*.qml, and the first test coverage
# this repository has ever had of the part of it that users look at.
#
# It links both QML modules and both plugins. `Climat` for the components and the
# Theme singleton; `Climat.Gallery` for gallery.js - the catalogue of everything
# in the tree, which tst_specimen walks - and contrast.js, which is where WCAG's
# arithmetic lives. Linking the gallery module is why gallery/ builds a static
# library with a thin executable on top rather than baking its module into the
# binary: a module inside an executable is a module only that executable can
# import.
#
# climat_forbid_gui() is deliberately absent, as it is for tst_conditionsdata.
# This one is *all* GUI.
add_subdirectory(qml)

# ---- the binary-level no-GUI check ------------------------------------------
#
# cmake/ClimatEngineGuard.cmake refuses at configure time if libclimat's target
# graph reaches Qt6::Gui. This reads the actual dynamic dependencies of a linked
# executable, which is the same question asked of the artefact rather than of
# the model - see the script for why both exist.
#
# It is pointed at tst_httpclient because that binary links the widest part of
# the engine: the network client, the cache store and the clock, plus Qt Test.
# If any of them dragged a GUI library in, it is in this executable's DT_NEEDED
# list.
# ---- the golden images ------------------------------------------------------
#
# Registered only when there is something to photograph with. `--grab` lives
# behind CLIMAT_DEV_TOOLS and half the cases are gallery pages, so without both
# this would be a test that fails for want of a flag rather than for want of a
# pixel.
#
# DEPENDS tst_environment so that ctest runs the canary first. When the machine's
# text rasterisation has moved, every image differs and none of those
# differences is a defect - having the one legible failure appear above thirty
# image diffs is the entire reason the canary exists.
#
# LABELS so that `ctest -LE slow` skips it: a minute of rendering is the right
# price in CI and the wrong one for a developer re-running a unit test.
if(CLIMAT_DEV_TOOLS AND CLIMAT_BUILD_GALLERY AND UNIX AND NOT APPLE)
    add_test(NAME golden_images
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/golden.sh check
    )

    set_tests_properties(golden_images PROPERTIES
        ENVIRONMENT "CLIMAT_BUILD_DIR=${CMAKE_BINARY_DIR}"
        DEPENDS tst_environment
        LABELS "slow;golden"
        TIMEOUT 600
    )
endif()

# ---- the binary-level licence check -----------------------------------------
#
# R2 and R3 from docs/08-risks.md, asked of the artefact. cmake/
# ClimatLicenceGuard.cmake asks the same two questions of the build's own model
# at configure time and cannot see a module that arrives without being named -
# through a QML import, or through some future component's dependency graph.
#
# Registered as a ctest rather than only as a CI job because it is a property of
# what was just built: a developer who adds `find_package(Qt6 COMPONENTS
# Charts)` should learn about it from `ctest`, not from a red pull request.
if(UNIX AND NOT APPLE)
    add_test(NAME licences_are_compatible
        COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-licences.sh
            $<TARGET_FILE:climat>
            $<$<BOOL:${CLIMAT_BUILD_GALLERY}>:$<TARGET_FILE:climat-gallery>>
            # And the two that ship beside it on Linux. The widget host is the
            # one that matters here: it is a second Qt Quick binary, built from
            # a second QML module, and a banned module added to that module
            # would be invisible to a check that only ever looked at `climat`.
            $<$<TARGET_EXISTS:climat-widget>:$<TARGET_FILE:climat-widget>>
            $<$<TARGET_EXISTS:climat-daemon>:$<TARGET_FILE:climat-daemon>>
    )
endif()

find_program(CLIMAT_OBJDUMP NAMES objdump llvm-objdump)

# Registered only when it can actually run. A test that reports "passed"
# because the tool it needed was missing is worse than one that is absent: the
# first is a guarantee nobody is checking, and the second is visible in the
# ctest output as a test that is not there.
if(CLIMAT_OBJDUMP AND UNIX AND NOT APPLE)
    add_test(NAME engine_has_no_gui
        COMMAND ${CMAKE_COMMAND}
            -DCLIMAT_BINARY=$<TARGET_FILE:tst_httpclient>
            -DCLIMAT_OBJDUMP=${CLIMAT_OBJDUMP}
            -P ${CMAKE_SOURCE_DIR}/cmake/ClimatNoGuiBinaryCheck.cmake
    )
else()
    message(STATUS
        "climat: no objdump found, so the binary-level no-GUI test is not registered. "
        "The configure-time guard in cmake/ClimatEngineGuard.cmake still applies.")
endif()

# ---- the same question, asked of the command-line client ---------------------
#
# climat-cli exists to be started by a status bar every minute, and a GUI
# library in its DT_NEEDED list would be a windowing stack initialised sixty
# times an hour to print one temperature. The configure-time guard forbids it
# on the target graph; this reads the binary.
# UNIX AND NOT APPLE, like licences_are_compatible below and for the same
# reason: ClimatNoGuiBinaryCheck reads `objdump -p` for NEEDED lines, which is an
# ELF spelling. macOS ships an objdump that prints LC_LOAD_DYLIB for a Mach-O,
# so find_program succeeds, no NEEDED line is ever matched, and the script
# correctly FATAL_ERRORs on a question it could not ask. A check that cannot run
# is not a check that failed.
if(CLIMAT_OBJDUMP AND UNIX AND NOT APPLE)
    add_test(NAME cli_has_no_gui
        COMMAND ${CMAKE_COMMAND}
            -DCLIMAT_BINARY=$<TARGET_FILE:climat-cli>
            -DCLIMAT_OBJDUMP=${CLIMAT_OBJDUMP}
            -P ${CMAKE_SOURCE_DIR}/cmake/ClimatNoGuiBinaryCheck.cmake
    )
endif()

# ---- and the opposite question, asked of the widget host ---------------------
#
# `engine_has_no_gui` above asks whether the engine reached a windowing toolkit.
# This asks whether a *drawing* process reached the engine's fetching half, which
# is the same architectural rule from the other end: one process fetches and
# every other one reads from it over the bus.
#
# It is a symbol check and not an `ldd`, and cmake/ClimatNoEngineSymbolCheck.cmake
# opens with why: libclimat is a static archive, so there is no shared object to
# look for and DT_NEEDED records nothing about which members were pulled in. What
# a static link leaves behind is a symbol.
#
# Registered here rather than in widgets/CMakeLists.txt because enable_testing()
# runs from the top-level directory after that one is entered, so an add_test()
# there registers nothing at all - a guard that is silently absent from a green
# ctest run.
if(TARGET climat-widget)
    find_program(CLIMAT_NM NAMES nm llvm-nm)
    if(CLIMAT_NM)
        add_test(NAME widget_has_no_engine
            COMMAND ${CMAKE_COMMAND}
                -DCLIMAT_BINARY=$<TARGET_FILE:climat-widget>
                -DCLIMAT_NM=${CLIMAT_NM}
                -P ${CMAKE_SOURCE_DIR}/cmake/ClimatNoEngineSymbolCheck.cmake
        )
    else()
        message(STATUS
            "climat: no nm found, so the widget host's link line is unchecked in this build.")
    endif()
endif()
