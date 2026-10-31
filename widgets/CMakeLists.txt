# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The widget host: one process, every tile, and no engine underneath it.
#
# ---- what this target may not link ------------------------------------------
#
# The providers, the HTTP client and the cache. A widget draws what
# climat-daemon fetched; a desktop with six tiles on it must not be six things
# opening sockets and writing one SQLite file.
#
# That is enforced twice, and neither half is a comment:
#
#   * `climat-widget` does not link libclimat at all. It compiles two files out of
#     it - domain/weathercode.cpp and domain/scales.cpp, which are pure tables
#     over QString - so the WMO codes and the published bands are not copied,
#     and nothing else from the engine is reachable to be called.
#
#   * `widget_has_no_engine` reads the symbol table of the built binary. libclimat
#     is a STATIC archive, so `ldd` proves nothing here - an archive contributes
#     only what is referenced and a linked-but-unused provider leaves no trace in
#     DT_NEEDED. The symbol check is the one that would actually notice.
#
# The second exists because the first is easy to undo. `target_link_libraries(…
# libclimat)` is a plausible-looking line that would compile, link, run, and
# quietly put a second fetcher on the desktop.
#
# ---- why the app's QML is listed here rather than imported -------------------
#
# `import Climat` is not available: app/'s QML module is a static library whose
# types are registered by `climatqmlplugin`, and linking that would drag in
# `AppEngine` - and with it the providers this directory exists to keep out.
#
# So the shared files are listed below and compiled into a second module. One
# copy of each *file* in the repository, two copies of the bytecode in two
# binaries that were never going to share an address space anyway. A cloud that
# gains a highlight in the app gains it on the tiles in the same commit, which
# is the property that matters.

configure_file(widgetconfig.h.in widgetconfig.h @ONLY)

# The daemon's name, path and interface. Configured from the same template the
# daemon uses, out of the same CLIMAT_DAEMON_PATH the top-level CMakeLists
# computes, because a client and a server that disagree about an object path
# produce a service that registers and then answers nothing.
configure_file(${CMAKE_SOURCE_DIR}/daemon/daemonconfig.h.in daemonconfig.h @ONLY)

# ---- the presentation files shared with the app ------------------------------
#
# Deliberately short, and it is meant to stay short. Every file here is one a
# tile actually instantiates today; nothing is listed "because a widget might
# want it". A tile is 160 px square and the components that suit it are the
# small ones.
set(climat_widget_shared_qml
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/Theme.qml
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/WeatherGlyph.qml
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/SeriesArea.qml
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/SeverityGlyph.qml
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/SkyArc.qml
)

set(climat_widget_shared_js
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/theme.js
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/themelight.js
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/chartmath.js
)

# ---- this module's own files -------------------------------------------------
#
# Listed one by one, never file(GLOB), for the reason app/CMakeLists.txt spends
# a paragraph on: a globbed module goes stale the moment somebody adds a file
# without re-running CMake, and QML reports the resulting failure at the point
# of *use* rather than at the file that is missing.
set(climat_widget_qml
    qml/Climat/Widgets/WidgetWindow.qml
    qml/Climat/Widgets/WidgetTile.qml
    qml/Climat/Widgets/WidgetSurface.qml
    qml/Climat/Widgets/DialGauge.qml
    qml/Climat/Widgets/CurrentConditionsWidget.qml
    qml/Climat/Widgets/TemperatureSparklineWidget.qml
    qml/Climat/Widgets/HourlyStripWidget.qml
    qml/Climat/Widgets/PrecipitationWidget.qml
    qml/Climat/Widgets/WindRoseWidget.qml
    qml/Climat/Widgets/UvDialWidget.qml
    qml/Climat/Widgets/AqiDialWidget.qml
    qml/Climat/Widgets/SunArcWidget.qml
    qml/Climat/Widgets/DailyStripWidget.qml
    qml/Climat/Widgets/AlertsWidget.qml
    qml/Climat/Widgets/wire.js
)

# Theme.qml carries `pragma Singleton`, which is necessary and not sufficient:
# without this property the generated qmldir lists it as an ordinary component
# and every token read fails at run time with "Value is undefined", naming the
# file that read the token rather than the file that failed to become a
# singleton. There is no build-time complaint of any kind.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/app/qml/Climat/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

# Every file aliased to its bare name, which does two things at once. It puts
# the module where its URI says it is - `qrc:/qt/qml/Climat/Widgets/Theme.qml`
# rather than a path with the whole of app/ in it - and it is what makes
# `import "theme.js" as Tokens` inside Theme.qml resolve, because a JS import is
# resolved against the importing file's own URL and the alias is what puts the
# two at the same qrc directory.
foreach(climat_file IN LISTS climat_widget_shared_qml climat_widget_shared_js climat_widget_qml)
    get_filename_component(climat_file_name "${climat_file}" NAME)
    set_source_files_properties("${climat_file}"
        PROPERTIES QT_RESOURCE_ALIAS "${climat_file_name}")
endforeach()

# ---- the C++ the module registers --------------------------------------------
#
# Four of these are app/'s own files, compiled a second time rather than linked.
#
# `Settings`, `Units` and `TimeFormat` because a tile has to show the units and
# the clock the reader chose in the app, and they choose them by writing an INI
# that both processes read - a second implementation of either mapping is a
# second place for °F, or for a 24-hour clock, to be wrong.
# `SystemAppearance` because a tile follows the desktop's light/dark preference
# like everything else on it. `ScreenshotController` because `--grab` is what
# makes a tile reviewable in CI without a session bus. `QmlNetworkGuard`
# because a tile shows the place name too, and it keeps QML off the network.
#
# None of the four reaches libclimat. That is checked, not assumed:
# `widget_has_no_engine` in tests/CMakeLists.txt reads this binary's symbols.
set(climat_widget_sources
    daemonlink.h
    daemonlink.cpp
    layershell.h
    layershell.cpp
    widgetclock.h
    widgetclock.cpp
    widgetfeed.h
    widgetfeed.cpp
    widgetoptions.h
    widgetoptions.cpp
    wx.h
    wx.cpp

    ${CMAKE_SOURCE_DIR}/app/settings.h
    ${CMAKE_SOURCE_DIR}/app/settings.cpp
    ${CMAKE_SOURCE_DIR}/app/viewmodels/timeformat.h
    ${CMAKE_SOURCE_DIR}/app/viewmodels/timeformat.cpp
    ${CMAKE_SOURCE_DIR}/app/viewmodels/units.h
    ${CMAKE_SOURCE_DIR}/app/viewmodels/units.cpp
    ${CMAKE_SOURCE_DIR}/app/platform/systemappearance.h
    ${CMAKE_SOURCE_DIR}/app/platform/systemappearance.cpp
    ${CMAKE_SOURCE_DIR}/app/devtools/screenshotcontroller.h
    ${CMAKE_SOURCE_DIR}/app/devtools/screenshotcontroller.cpp
    ${CMAKE_SOURCE_DIR}/app/platform/qmlnetworkguard.h
    ${CMAKE_SOURCE_DIR}/app/platform/qmlnetworkguard.cpp

    # Three files out of libclimat rather than the whole archive. All three are
    # pure functions over QString and double with no I/O, no cache and no
    # provider - see the header of each. This is what keeps the WMO tables, the
    # published bands and the conversion factors single-sourced without linking
    # the engine.
    #
    # units.cpp is the newest of the three and arrived by the same route: the
    # factors used to live in app/viewmodels/units.cpp, which this target
    # already compiles, and moved when climat-cli - which links no QML - needed
    # them too. `Units` above is still the class QML binds to; what moved out
    # from under it is the arithmetic, not the preference.
    ${CMAKE_SOURCE_DIR}/libclimat/domain/weathercode.h
    ${CMAKE_SOURCE_DIR}/libclimat/domain/weathercode.cpp
    ${CMAKE_SOURCE_DIR}/libclimat/domain/scales.h
    ${CMAKE_SOURCE_DIR}/libclimat/domain/scales.cpp
    ${CMAKE_SOURCE_DIR}/libclimat/domain/units.h
    ${CMAKE_SOURCE_DIR}/libclimat/domain/units.cpp
)

qt_add_library(climatwidgetsqml STATIC)

qt_add_qml_module(climatwidgetsqml
    URI Climat.Widgets
    VERSION 1.0
    QML_FILES
        ${climat_widget_shared_qml}
        ${climat_widget_shared_js}
        ${climat_widget_qml}
    SOURCES
        ${climat_widget_sources}
)

target_include_directories(climatwidgetsqml PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_SOURCE_DIR}/app/viewmodels
    ${CMAKE_SOURCE_DIR}/app/platform
    ${CMAKE_SOURCE_DIR}/app/devtools
)

# ScreenshotController changes shape under this definition - a member the
# library was compiled with and a consumer was not is an ODR violation that
# links cleanly and corrupts memory at run time. PUBLIC, exactly as app/ has it.
if(CLIMAT_DEV_TOOLS)
    target_compile_definitions(climatwidgetsqml PUBLIC CLIMAT_DEV_TOOLS)
endif()

target_link_libraries(climatwidgetsqml PUBLIC Qt6::Quick Qt6::DBus)

# SystemAppearance's portal half, on the same terms app/ links it. Here it is
# not optional in practice - this whole directory is a D-Bus client - but the
# definition is what the shared source file branches on and it has to be spelled
# the same way in both modules.
target_compile_definitions(climatwidgetsqml PRIVATE CLIMAT_HAVE_DBUS)

# ---- pinning the tiles to the desktop, where the compositor allows it ---------
#
# `zwlr_layer_shell_v1` is what KWin, Sway, Hyprland, Wayfire, river and labwc
# implement and mutter does not. Where it exists, climat-widget asks for a
# desktop-layer surface itself and there is no shell extension, no applet and no
# plugin - see widgets/layershell.h for why that is the whole of the KDE story.
#
# Optional, on the same terms qtpositioning is optional in libclimat: a packager
# on a GNOME-only distribution may leave layer-shell-qt out, and then --pin
# reports that as its reason instead of failing to build.
#
# PUBLIC on the definition, not PRIVATE. widgets/layershell.h is included by
# widgetoptions.h, which the executable compiles against; a definition that
# changed the header's meaning between the library and its consumer is the ODR
# violation that links cleanly and misbehaves at run time - the same trap
# CLIMAT_DEV_TOOLS is PUBLIC for, twenty lines up.
find_package(LayerShellQt QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(WaylandClient QUIET IMPORTED_TARGET wayland-client)
endif()

if(LayerShellQt_FOUND AND WaylandClient_FOUND)
    target_compile_definitions(climatwidgetsqml PUBLIC CLIMAT_HAS_LAYER_SHELL)
    # PkgConfig::WaylandClient is for the one registry roundtrip in
    # layershell.cpp that asks the compositor what it implements. Qt will not
    # answer that question - its Wayland globals are private API.
    target_link_libraries(climatwidgetsqml PUBLIC LayerShellQt::Interface PkgConfig::WaylandClient)
    message(STATUS "climat: layer-shell-qt found - climat-widget can pin itself to the desktop")
else()
    message(STATUS
        "climat: layer-shell-qt or wayland-client not found, so climat-widget cannot "
        "pin itself to the desktop on KDE or a wlroots compositor. --pin says so at "
        "run time. GNOME is unaffected: its shell extension does the pinning.")
endif()

climat_guard_target(climatwidgetsqml)

# ---- the executable ----------------------------------------------------------

qt_add_executable(climat-widget main.cpp ${CMAKE_SOURCE_DIR}/app/appfont.cpp)

# Both targets. qt_add_qml_module generates `climatwidgetsqmlplugin` beside the
# backing library and the plugin is what registers the module with the engine;
# link only the library and the process comes up against an engine that has
# never heard of `Climat.Widgets`. But the plugin links its backing library
# privately, so link only the plugin and main.cpp cannot find <QQmlEngine>.
target_link_libraries(climat-widget PRIVATE climatwidgetsqml climatwidgetsqmlplugin)

# The same catalogue file the daemon embeds, embedded here too. `--list` works
# with no daemon running, and so does the first paint before the handshake
# lands; the daemon's answer wins whenever there is one, because it is the copy
# that will have been upgraded alongside the data.
qt_add_resources(climat-widget "climat_widget_catalogue"
    PREFIX "/climat"
    BASE ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${CMAKE_CURRENT_SOURCE_DIR}/catalogue.json
)

# The bundled faces, at the path app/appfont.cpp already looks for them at.
# Reviewing a tile in a typeface the app does not use is reviewing something
# else - and on a desktop the two are side by side.
qt_add_resources(climat-widget "climat_widget_fonts"
    PREFIX "/qt/qml/Climat"
    BASE ${CMAKE_SOURCE_DIR}/app
    FILES
        ${CMAKE_SOURCE_DIR}/app/fonts/Inter-Regular.ttf
        ${CMAKE_SOURCE_DIR}/app/fonts/Inter-Bold.ttf
)

climat_guard_target(climat-widget)

# ---- the check that keeps the engine out -------------------------------------
#
# Registered in tests/CMakeLists.txt beside `engine_has_no_gui`, not here.
# enable_testing() is called from the top-level directory *after* this one is
# entered, so an add_test() in this file silently registers nothing - which is
# the worst possible outcome for a guard: a green ctest run with the check
# missing from it. See cmake/ClimatNoEngineSymbolCheck.cmake for what it asks.

include(GNUInstallDirs)

install(TARGETS climat-widget
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
