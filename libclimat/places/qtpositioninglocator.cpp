// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0

#include "qtpositioninglocator.h"

#include <QCoreApplication>
#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QtGlobal>

#if defined(Q_OS_ANDROID) && QT_CONFIG(permissions)
#include <QPermissions>
#endif

namespace climat {

QtPositioningLocator::QtPositioningLocator(QObject *parent)
    : DeviceLocator(parent)
    , m_source(QGeoPositionInfoSource::createDefaultSource(this))
{
    if (m_source == nullptr)
        return;

    connect(m_source, &QGeoPositionInfoSource::positionUpdated,
            this, &QtPositioningLocator::onPositionUpdated);

    // errorOccurred rather than the deprecated `error` signal, and the handler
    // takes an int so that this header does not have to name
    // QGeoPositionInfoSource::Error in a signature - the enum's spelling has
    // moved once already across Qt 6 minors.
    connect(m_source, &QGeoPositionInfoSource::errorOccurred, this,
            [this](QGeoPositionInfoSource::Error error) { onErrorOccurred(int(error)); });
}

QtPositioningLocator::~QtPositioningLocator() = default;

bool QtPositioningLocator::isAvailable() const
{
    return m_source != nullptr;
}

QString QtPositioningLocator::sourceName() const
{
    return m_source != nullptr ? m_source->sourceName() : QString();
}

void QtPositioningLocator::requestPosition()
{
    if (m_source == nullptr) {
        reportFailure(Failure::Unavailable,
                      QStringLiteral("Qt Positioning found no source on this machine. On Linux "
                                     "that usually means GeoClue2 is not running."));
        return;
    }

    // A second press while the first request is outstanding is the user being
    // impatient, not a second question. Answering it with a second D-Bus round
    // trip would make the wait longer.
    if (isRequestInFlight())
        return;

#if defined(Q_OS_ANDROID) && QT_CONFIG(permissions)
    // Android asks the reader first, and Qt Positioning leaves the asking to
    // the app: without it every request ends in an access error. Approximate
    // and while in use, which is the coarse permission the manifest declares.
    QLocationPermission permission;
    permission.setAccuracy(QLocationPermission::Approximate);
    permission.setAvailability(QLocationPermission::WhenInUse);
    if (qApp->checkPermission(permission) != Qt::PermissionStatus::Granted) {
        setRequestInFlight(true);
        qApp->requestPermission(permission, this, [this](const QPermission &answer) {
            setRequestInFlight(false);
            if (answer.status() == Qt::PermissionStatus::Granted)
                requestPosition();
            else
                reportFailure(Failure::PermissionDenied,
                              QStringLiteral("location access was not granted"));
        });
        return;
    }
#endif

    setRequestInFlight(true);
    m_source->requestUpdate(timeout());
}

void QtPositioningLocator::cancel()
{
    if (m_source != nullptr)
        m_source->stopUpdates();
    setRequestInFlight(false);
}

void QtPositioningLocator::onPositionUpdated(const QGeoPositionInfo &info)
{
    if (!info.isValid()) {
        reportFailure(Failure::Error, QStringLiteral("the position source reported an invalid fix"));
        return;
    }

    const QGeoCoordinate coordinate = info.coordinate();

    // Horizontal accuracy is an optional attribute and a source is entitled to
    // omit it. -1 rather than 0 for "unknown", because zero metres of error is
    // a claim no positioning system makes and a caller reading it as one would
    // be reading a lie.
    const double accuracy = info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy)
        ? info.attribute(QGeoPositionInfo::HorizontalAccuracy)
        : -1.0;

    reportPosition(Coordinate{ coordinate.latitude(), coordinate.longitude() }, accuracy);
}

void QtPositioningLocator::onErrorOccurred(int error)
{
    switch (QGeoPositionInfoSource::Error(error)) {
    case QGeoPositionInfoSource::AccessError:
        // The one that is not a malfunction. GeoClue2 returns it when the user
        // or the agent refuses, and the right response is to stop asking and
        // let the user search for a place by name.
        reportFailure(Failure::PermissionDenied,
                      QStringLiteral("the system refused access to the device's location"));
        return;

    case QGeoPositionInfoSource::UpdateTimeoutError:
        reportFailure(Failure::Timeout,
                      QStringLiteral("no position arrived within %1 ms").arg(timeout()));
        return;

    case QGeoPositionInfoSource::ClosedError:
        reportFailure(Failure::Error,
                      QStringLiteral("the positioning backend closed the connection"));
        return;

    case QGeoPositionInfoSource::NoError:
        return;

    case QGeoPositionInfoSource::UnknownSourceError:
        break;
    }

    reportFailure(Failure::Error, QStringLiteral("the positioning backend failed (error %1)")
                                      .arg(error));
}

} // namespace climat
