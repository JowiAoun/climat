// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qmlnetworkguard.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QUrl>

namespace {

class RefusingNetworkAccessManager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        const QUrl url = request.url();
        const QString scheme = url.scheme();
        if (url.isLocalFile() || scheme == QLatin1String("qrc") || scheme == QLatin1String("data"))
            return QNetworkAccessManager::createRequest(op, request, outgoingData);

        // An empty URL has no scheme, so Qt answers it with
        // ProtocolUnknownError and opens no socket.
        QNetworkRequest refused(request);
        refused.setUrl(QUrl());
        return QNetworkAccessManager::createRequest(op, refused, outgoingData);
    }
};

} // namespace

QNetworkAccessManager *QmlNetworkGuard::create(QObject *parent)
{
    return new RefusingNetworkAccessManager(parent);
}
