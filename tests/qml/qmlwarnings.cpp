// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
#include "qmlwarnings.h"

#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>

#include <cstdio>

namespace {

// File-static rather than members, because a Qt message handler is a plain
// function pointer with no user data: there is nowhere to hang an instance
// pointer, and the handler has to work before any QML object exists.
QMutex      g_mutex;
QStringList g_messages;
QtMessageHandler g_previous = nullptr;

// Set while a message is being handed down the chain. See handler().
thread_local bool t_forwarding = false;

// Every warning the QML engine raises arrives with a category. Filtering on it
// rather than on the text keeps this from being a grep that goes stale: the
// interesting ones are `qml` (a binding failed) and `js` (something threw).
//
// Deliberately not filtered by severity beyond Warning. A QML `console.warn`
// from our own code - Theme.qml's startup key check, PageBackdrop's phase
// check, Specimen's error report - is exactly as much a failure as one the
// engine raises, and those are the ones written on purpose to be noticed.
bool isInteresting(QtMsgType type)
{
    return type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;
}

void handler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    // The message came back around. When QTestLib stops logging it puts back
    // the handler it replaced at the start, which is this one, and its own
    // handler, still next in our chain, passes whatever it gets to that one.
    // A warning printed after the last test then went back and forth between
    // the two with no end: GCC turns both calls into jumps and the loop eats
    // memory, and MSVC keeps them and runs out of stack. The Windows run
    // crashed like that after every test had passed.
    if (t_forwarding) {
        std::fprintf(stderr, "%s\n", qPrintable(qFormatLogMessage(type, context, message)));
        return;
    }

    if (isInteresting(type)) {
        QMutexLocker locker(&g_mutex);
        const QString where = context.file != nullptr
            ? QStringLiteral("%1:%2: ").arg(QString::fromUtf8(context.file)).arg(context.line)
            : QString();
        g_messages.append(where + message);
    }

    // Passed through, always. A test that swallowed the output would make a
    // failing run harder to read than a passing one, which is backwards.
    if (g_previous != nullptr) {
        t_forwarding = true;
        g_previous(type, context, message);
        t_forwarding = false;
    }
}

} // namespace

// Installing once in main() is not enough, and finding that out cost a test
// that could not fail: QTestLib installs its own handler when the run starts -
// it is how QTest::ignoreMessage works - which quietly replaced ours before a
// single test function ran. The suite was green with `Theme.ink.typo` wired
// into a live component, while the app printed four warnings a launch about
// exactly that line.
//
// So it is re-asserted on every clear(), which every test calls immediately
// before the work it is measuring. `qInstallMessageHandler` hands back whoever
// was there; if that is us, nothing has changed and the chain is already right.
// If it is somebody else, they become the next link, so QTestLib keeps seeing
// every message and its own machinery goes on working.
void QmlWarnings::install()
{
    QtMessageHandler previous = qInstallMessageHandler(handler);
    if (previous != handler)
        g_previous = previous;
}

QmlWarnings::QmlWarnings(QObject *parent)
    : QObject(parent)
{
}

QmlWarnings::~QmlWarnings() = default;

int QmlWarnings::count() const
{
    QMutexLocker locker(&g_mutex);
    return int(g_messages.size());
}

QStringList QmlWarnings::messages() const
{
    QMutexLocker locker(&g_mutex);
    return g_messages;
}

void QmlWarnings::clear()
{
    // Re-asserted here rather than only in main(); see install().
    install();

    {
        QMutexLocker locker(&g_mutex);
        g_messages.clear();
    }
    Q_EMIT changed();
}

QString QmlWarnings::summary() const
{
    QMutexLocker locker(&g_mutex);
    if (g_messages.isEmpty())
        return QStringLiteral("no warnings");

    constexpr qsizetype shown = 8;
    QStringList head = g_messages.mid(0, shown);
    QString out = QStringLiteral("%1 warning(s); first %2:\n  ")
                      .arg(g_messages.size())
                      .arg(head.size())
                  + head.join(QStringLiteral("\n  "));
    if (g_messages.size() > shown)
        out += QStringLiteral("\n  … and %1 more").arg(g_messages.size() - shown);
    return out;
}
