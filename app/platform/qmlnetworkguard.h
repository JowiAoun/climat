// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keeps QML off the network.
//
// Every request the app makes goes through libclimat's HttpClient, to the
// services SECURITY.md names. QML never needs the network itself. But a Text
// in its default format reads `<img src="https://...">` out of its string and
// fetches it, and some of those strings are place names from the geocoder,
// which come from GeoNames, which anyone can edit. One edited town name would
// make every search for it send the reader's address to a stranger's server.
//
// So the engine gets a network access manager that refuses everything that is
// not a local file. Set it on every engine, before anything loads:
//
//     QmlNetworkGuard guard;
//     engine.setNetworkAccessManagerFactory(&guard);
//
// The guard has to outlive the engine.
#pragma once

#include <QQmlNetworkAccessManagerFactory>

class QmlNetworkGuard : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager *create(QObject *parent) override;
};
