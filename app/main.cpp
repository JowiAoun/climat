// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The entry point.
//
// Five things happen here and their order is the only interesting thing about
// the file: identity, then storage, then the typeface, then the command line,
// then the engine. Each one is a precondition of the next, and each of the four
// ways to get it wrong is silent.

#include "appengine.h"
#include "appfont.h"
#include "apptranslator.h"
#include "appoptions.h"
#include "climatconfig.h"
#include "qmlnetworkguard.h"
#include "settings.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // ---- 1. identity -------------------------------------------------------
    // QStandardPaths derives the cache and config directories from the
    // organisation and application names, so these two decide where the
    // forecast cache and the preferences file will live long before there is
    // either. The app ID is the reverse-DNS name the rest of the desktop stack
    // keys off - the desktop entry's basename, the icon name, the D-Bus name -
    // and setDesktopFileName is what lets a Wayland compositor match this
    // window to that entry. Without it the window gets a generic icon and no
    // app-menu association, which is the sort of thing nobody notices until
    // packaging.
    QGuiApplication::setOrganizationName(QStringLiteral("Climat"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("github.io"));
    QGuiApplication::setApplicationName(QStringLiteral(CLIMAT_APP_NAME));
    QGuiApplication::setApplicationVersion(QStringLiteral(CLIMAT_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral(CLIMAT_APP_ID));

    // ---- 2. storage --------------------------------------------------------
    // Before anything constructs a QSettings, because the format is decided at
    // construction and never revisited: a QSettings built before this line
    // reads the Windows registry while every later one reads an INI file, and
    // nothing complains - the preferences simply do not stick. Also the moment
    // a superseded config directory gets copied forward. See settings.h.
    Settings::prepareStorage();

    // ---- 3. the typeface ---------------------------------------------------
    // Before the engine, because a Text item resolves its family from the
    // application font at construction and nothing re-reads it afterwards: set
    // this later and the first screenful comes up in the host's font.
    //
    // After the QGuiApplication, because the font database needs the platform
    // integration up. There is no third position that works.
    //
    // The return value is deliberately dropped here - QML reads the family back
    // off the application font as `Theme.type.family`, which is one source of
    // truth rather than two spellings of "Inter". See app/appfont.h.
    AppFont::install();

    // Before the QML engine loads: a translator installed after a component is
    // built does not reach the strings already in it. No-op today - there are
    // no language catalogues yet, which app/apptranslator.h explains.
    climat::AppTranslator::install(&app);

    // ---- 4. the command line -----------------------------------------------
    // Before the engine loads anything, because Main.qml reads AppOptions at
    // construction: --viewport and --size choose the window's width, and the
    // window's width chooses which shell is built.
    //
    // Does not return for --help, --version, an unknown flag or a malformed
    // value.
    AppOptions::parseCommandLine(app);

    // ---- 5. the weather ----------------------------------------------------
    // Before the QML engine, and that is the whole reason this line is here
    // rather than in a Component.onCompleted. `Data` and `Detail` are read
    // while Main.qml's first objects are being constructed, so the snapshot has
    // to exist by then - and it can, because docs/04-architecture.md §4.1's
    // first step is a cache read that opens no socket and returns inside this
    // call. A window that came up empty and filled in a frame later would be
    // the "renders from cache" promise kept in spirit and broken in the one
    // frame anybody photographs.
    //
    // AppOptions::fixture() decides where the data comes from: a recording at a
    // frozen clock, or the live network. Nothing after this line knows which.
    AppEngine::instance()->configure(AppOptions::instance()->fixture());

    if (!AppOptions::instance()->place().isEmpty())
        AppEngine::instance()->selectByQuery(AppOptions::instance()->place());

    // ---- 6. the QML engine -------------------------------------------------
    // The guard is declared first so it outlives the engine. See its header.
    QmlNetworkGuard networkGuard;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&networkGuard);

    // A QML file that fails to construct its root object leaves the engine
    // holding nothing and the event loop with no window to show - the app comes
    // up as an invisible process that has to be killed. Exit instead, non-zero,
    // so a headless capture in CI fails as a failure rather than as a timeout.
    //
    // Queued, because this signal is emitted from inside load() and calling
    // exit() before exec() has started does nothing at all.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // By URI, not by file path or qrc URL. loadFromModule asks the engine's
    // import machinery for `Climat.Main`, which means the same line works
    // whether the module is compiled into this binary (it is) or found on
    // QML_IMPORT_PATH (which is how qmllint and qmlls see it).
    engine.loadFromModule("Climat", "Main");

    return app.exec();
}
