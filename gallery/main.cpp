// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The gallery's entry point. Four things happen, and the interesting one is the
// thing that does not.
//
// app/main.cpp calls Settings::prepareStorage() before anything else, because
// the weather app remembers its window size and its units. This calls it too,
// and the rule it used to state - a developer tool has no business writing to
// the config directory the product reads - is kept by the FILE rather than by
// the directory: `climat-gallery.ini` sits beside `climat.ini` and nothing in
// this process can reach the second one. Open the gallery at 1500x950 and the
// app still opens where the reader left it.
//
// It used to set no organisation at all, which meant it read no preferences
// file whatsoever and every preference it showed was a default. That was
// invisible until the clock format's default started following the reader's
// locale: the seven gallery cards carrying a time went 24-hour under the
// capture locale while every app image stayed 12-hour, so the component
// browser was drawing something the product does not. A gallery that renders
// the app's components in the app's typeface should read the app's
// preferences too - and scripts/golden.sh pins them for a capture by writing
// exactly this file.
//
// AppFont::install() is the opposite case - the one piece of app/main.cpp this
// file must copy. The specimens on the stage are the app's own components out
// of the app's own module, and a review of them in a different typeface is a
// review of something the product does not ship.

#include "appengine.h"
#include "appfont.h"
#include "galleryoptions.h"
#include "qmlnetworkguard.h"
#include "settings.h"

#include "climatconfig.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // The app's organisation and a name of its own, which together are what
    // QSettings keys a file off: `<config>/Climat/climat-gallery.ini`. Sharing
    // the organisation is what puts it in the directory a capture redirects and
    // pins; not sharing the name is what keeps it out of the app's own file.
    QGuiApplication::setOrganizationName(QStringLiteral("Climat"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("github.io"));
    QGuiApplication::setApplicationName(QStringLiteral("climat-gallery"));
    QGuiApplication::setApplicationVersion(QStringLiteral(CLIMAT_VERSION));

    // INI on every platform, before anything constructs a QSettings - the same
    // first line app/main.cpp runs, and for the same reason its header gives.
    // Without it this process would read `.conf` through NativeFormat while
    // every other Climat binary reads `.ini`, which is a preference that appears
    // to be ignored.
    Settings::prepareStorage();

    // Before the engine, for the reason app/main.cpp gives at the same line: a
    // Text item resolves its family from the application font when it is
    // created, and never again.
    AppFont::install();

    // Before the engine, because Main.qml reads these at construction: the
    // window sizes itself from --size, and the stage frames itself from
    // --viewport.
    //
    // Does not return for --help, --version, an unknown flag or a malformed
    // value.
    GalleryOptions::parseCommandLine(app);

    // ---- always a fixture, never the network -------------------------------
    //
    // The gallery reviews components, and a component reviewed against
    // whatever the weather happens to be doing is a component reviewed against
    // a different specimen every time. It is also the tool most likely to be
    // opened on a train.
    //
    // So there is no live mode here and no flag for one: the recorded Toronto
    // afternoon at its frozen clock, always, which is the same data
    // `climat --fixture toronto` shows - so a card that looks right on the stage
    // looks the same in the product.
    AppEngine::instance()->configure(climat::fixtures::defaultName());

    // The guard is declared first so it outlives the engine. See its header.
    QmlNetworkGuard networkGuard;
    QQmlApplicationEngine engine;
    engine.setNetworkAccessManagerFactory(&networkGuard);

    // A QML file that fails to construct its root object leaves the engine
    // holding nothing and the event loop with no window to show - the process
    // comes up invisible and has to be killed. Exit instead, non-zero, so a
    // headless capture in CI fails as a failure rather than as a timeout.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // By URI. `Climat.Gallery` is this executable's own module; the components it
    // stages come from `Climat`, which it links and which Main.qml imports.
    engine.loadFromModule("Climat.Gallery", "Main");

    return app.exec();
}
