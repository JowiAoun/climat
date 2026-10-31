// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A place name with an <img> in it, drawn by a Text in its default format, and
// whether that opens a connection. See app/platform/qmlnetworkguard.h.
//
// The unguarded engine goes first and has to see the request. Without it, a
// guarded engine that sees nothing could just mean Qt stopped loading images
// in text, and the second case would pass for the wrong reason.

#include "qmlnetworkguard.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTcpServer>
#include <QTest>

#include <memory>

namespace {

// How long the guarded case waits for a connection that should never come.
// The unguarded case connects in well under this on a loaded runner.
constexpr int quietMs = 1500;

struct DrawnName
{
    std::unique_ptr<QQuickWindow> window;
    std::unique_ptr<QObject> text;
};

// A Text in a window, holding a town name with an image that points at server.
DrawnName drawName(QQmlEngine &engine, const QTcpServer &server)
{
    const QString name = QStringLiteral("Springfield<img src=\"http://127.0.0.1:%1/p.png\">")
                             .arg(server.serverPort());

    QQmlComponent component(&engine);
    component.setData("import QtQuick\nText { }", QUrl());

    DrawnName drawn;
    drawn.window = std::make_unique<QQuickWindow>();
    drawn.text.reset(component.createWithInitialProperties({ { QStringLiteral("text"), name } }));
    if (auto *item = qobject_cast<QQuickItem *>(drawn.text.get()))
        item->setParentItem(drawn.window->contentItem());
    drawn.window->show();
    return drawn;
}

} // namespace

class TestQmlNetworkGuard : public QObject
{
    Q_OBJECT

private slots:
    void unguardedEngineFetchesTheImage();
    void guardedEngineOpensNoConnection();
};

void TestQmlNetworkGuard::unguardedEngineFetchesTheImage()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QQmlEngine engine;
    const DrawnName drawn = drawName(engine, server);
    QVERIFY(drawn.text);

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 10000);
}

void TestQmlNetworkGuard::guardedEngineOpensNoConnection()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QmlNetworkGuard guard;
    QQmlEngine engine;
    engine.setNetworkAccessManagerFactory(&guard);
    const DrawnName drawn = drawName(engine, server);
    QVERIFY(drawn.text);

    QTest::qWait(quietMs);
    QVERIFY(!server.hasPendingConnections());
}

QTEST_MAIN(TestQmlNetworkGuard)
#include "tst_qmlnetworkguard.moc"
