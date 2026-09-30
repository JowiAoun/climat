// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later

#include "snapshotservice.h"

#include "libclimat/cache/cachestore.h"
#include "libclimat/core/clock.h"
#include "libclimat/net/httpclient.h"
#include "libclimat/places/locationcontroller.h"
#include "libclimat/providers/airquality/openmeteoairqualityprovider.h"
#include "libclimat/providers/eccc/ecccalertprovider.h"
#include "libclimat/providers/metno/metnoforecastprovider.h"
#include "libclimat/providers/nws/nwsalertprovider.h"
#include "libclimat/providers/openmeteo/openmeteoforecastprovider.h"
#include "libclimat/providers/registry.h"

#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QSet>
#include <QTimer>

#include <algorithm>

using namespace climat;

namespace {

// One timer for every watched place. It is deliberately not the TTL: the
// providers consult libclimat's own cache policy and only open a socket when
// what they hold has actually gone stale, so this is how often we *ask*, not
// how often we fetch. Putting a second TTL here would be a second policy that
// could disagree with the first.
constexpr int kPollIntervalMs = 5 * 60 * 1000;

// What the daemon keeps for a place that nobody has narrowed. A widget asks
// for what it needs through the mask; these are the ceilings behind it.

// How long the places table has to hold still before it is re-read. Long
// enough that the several writes behind one edit in the app are seen as one,
// short enough that somebody who just changed their home place watches the
// tiles follow rather than wondering whether they will.
constexpr int kPlacesSettleMs = 750;

QString tokenFor(quint64 n)
{
    return QStringLiteral("s%1").arg(n);
}

} // namespace

SnapshotService::SnapshotService(QObject *parent)
    : QObject(parent)
{
}

SnapshotService::~SnapshotService() = default;

void SnapshotService::configure(const QString &fixtureName)
{
    m_fixtureName = fixtureName;

    // The clock first, and everything else is handed it - the same single
    // branch AppEngine::configure takes, for the same reason.
    if (isFixtureMode()) {
        m_fixture = fixtures::load(fixtureName);
        m_clock   = std::make_unique<FrozenClock>(m_fixture.recordedAt);
    } else {
        m_clock = std::make_unique<SystemClock>();
    }

    m_cache              = std::make_unique<CacheStore>(m_clock.get());
    const Status opened  = m_cache->open(CacheStore::defaultDatabasePath());
    if (!opened) {
        // Same posture as the app: a cache that will not open is a daemon that
        // asks the network more often, not a daemon that cannot serve.
        qWarning("climat-daemon: the cache could not be opened (%s); "
                 "widgets will not survive going offline",
                 qPrintable(opened.error().toString()));
    }

    m_http = std::make_unique<HttpClient>(m_clock.get());
    m_http->setValidatorStore(m_cache.get());

    m_registry = std::make_unique<ProviderRegistry>();

    if (isFixtureMode())
        buildFixtureProviders();
    else
        buildLiveProviders();

    registerProviders();

    m_places = new LocationController(m_cache.get(), this);
    m_places->load();
    m_placesFingerprint = placesFingerprint();
    watchPlaces();

    m_poll = new QTimer(this);
    m_poll->setInterval(kPollIntervalMs);
    connect(m_poll, &QTimer::timeout, this, &SnapshotService::onPollTimeout);
    m_poll->start();
}

void SnapshotService::buildLiveProviders()
{
    m_openMeteo = new OpenMeteoForecastProvider(m_http.get(), m_clock.get(), this);
    m_openMeteo->setCache(m_cache.get());

    m_metNo = new MetNoForecastProvider(m_http.get(), m_clock.get(), this);
    m_metNo->setCache(m_cache.get());

    m_openMeteoAq = new OpenMeteoAirQualityProvider(m_http.get(), m_clock.get(), this);
    m_openMeteoAq->setCache(m_cache.get());

    m_eccc = new EcccAlertProvider(m_http.get(), m_clock.get(), this);
    m_eccc->setCache(m_cache.get());

    m_nws = new NwsAlertProvider(m_http.get(), m_clock.get(), this);
    m_nws->setCache(m_cache.get());
}

void SnapshotService::buildFixtureProviders()
{
    m_fixtureForecast = new FixtureForecastProvider(m_fixture, this);
    m_fixtureAir      = new FixtureAirQualityProvider(m_fixture, this);
    m_fixtureAlerts   = new FixtureAlertProvider(m_fixture, this);
}

void SnapshotService::registerProviders()
{
    const auto complain = [](const Status &status, const char *what) {
        if (!status) {
            qFatal("climat-daemon: %s could not be registered: %s", what,
                   qPrintable(status.error().toString()));
        }
    };

    if (isFixtureMode()) {
        complain(m_registry->addForecastProvider(m_fixtureForecast, 100), "the fixture provider");
        complain(m_registry->addAirQualityProvider(m_fixtureAir, 100), "the fixture air quality");
        complain(m_registry->addAlertProvider(m_fixtureAlerts, 0), "the fixture alerts");
        return;
    }

    complain(m_registry->addForecastProvider(m_openMeteo, 100), "Open-Meteo");
    complain(m_registry->addForecastProvider(m_metNo, 200), "MET Norway");
    complain(m_registry->addAirQualityProvider(m_openMeteoAq, 100), "Open-Meteo air quality");
    complain(m_registry->addAlertProvider(m_eccc, 0), "ECCC alerts");
    complain(m_registry->addAlertProvider(m_nws, 0), "NWS alerts");
}

// ---- places -----------------------------------------------------------------

Place SnapshotService::resolve(const QString &placeId) const
{
    if (isFixtureMode())
        return m_fixture.place;

    // "home" and "" both mean the home place, because that is what a widget
    // wants to say and it has to keep working when the user renames a city.
    if (placeId.isEmpty() || placeId == QLatin1String("home")) {
        const QList<Place> all = m_places->places();
        for (const Place &p : all) {
            if (p.isHome)
                return p;
        }
        return m_places->currentPlace();
    }

    bool         ok = false;
    const qint64 id = placeId.toLongLong(&ok);
    if (ok) {
        const QList<Place> all = m_places->places();
        for (const Place &p : all) {
            if (p.id == id)
                return p;
        }
    }
    return {};
}

QString SnapshotService::canonical(const QString &placeId) const
{
    const Place place = resolve(placeId);
    if (isFixtureMode())
        return QStringLiteral("fixture");
    return place.id != 0 ? QString::number(place.id) : QString();
}

// ---- keeping up with the app ------------------------------------------------

void SnapshotService::watchPlaces()
{
    // Nothing to watch: a fixture's place comes out of a recorded file and no
    // edit in the app can reach it.
    if (isFixtureMode())
        return;

    const QString database = CacheStore::defaultDatabasePath();
    if (database.isEmpty())
        return;

    // Settled rather than immediate, and the interval is chosen for SQLite
    // rather than for the user. One place added by the app is several writes -
    // the row, the home flag on the row that used to hold it, the WAL, a
    // checkpoint - and re-reading the table between two of them would answer a
    // question about a half-finished edit.
    m_placesSettle = new QTimer(this);
    m_placesSettle->setSingleShot(true);
    m_placesSettle->setInterval(kPlacesSettleMs);
    connect(m_placesSettle, &QTimer::timeout, this, &SnapshotService::reloadPlaces);

    m_placesWatch = new QFileSystemWatcher(this);

    // The directory as well as the files, because in WAL mode the interesting
    // write lands in cache.sqlite-wal, and that file is created, checkpointed
    // and deleted underneath a watcher that is only ever told about paths that
    // exist. Watching the directory catches it reappearing; re-arming below
    // catches everything else.
    const QFileInfo info(database);
    m_placesWatch->addPath(info.absolutePath());
    m_placesWatch->addPath(database);
    m_placesWatch->addPath(database + QLatin1String("-wal"));

    const auto touched = [this, database](const QString &) {
        // Re-arm first. A path that was replaced rather than modified is
        // dropped by the watcher, and the second edit of the evening would
        // otherwise be the one nobody hears.
        const QStringList held = m_placesWatch->files();
        for (const QString &path : { database, database + QLatin1String("-wal") }) {
            if (!held.contains(path) && QFile::exists(path))
                m_placesWatch->addPath(path);
        }
        m_placesSettle->start();
    };

    connect(m_placesWatch, &QFileSystemWatcher::fileChanged, this, touched);
    connect(m_placesWatch, &QFileSystemWatcher::directoryChanged, this, touched);
}

void SnapshotService::reloadPlaces()
{
    if (isFixtureMode())
        return;

    m_places->load();

    const QString now = placesFingerprint();
    if (now == m_placesFingerprint)
        return; // Our own cache write, almost every time.

    m_placesFingerprint = now;

    // Every subscription re-resolved against the list that just arrived. A
    // reader asked for "home" and is entitled to whichever place is home now,
    // not the one that was home when it happened to start its widgets.
    QSet<QString> moved;
    for (auto it = m_subscriptions.begin(); it != m_subscriptions.end(); ++it) {
        const QString resolved = canonical(it->requested);

        // An empty resolution is a place that has been deleted with a
        // subscription still on it. The subscription is left pointing where it
        // was: the tile goes on showing its last reading and ages it, which is
        // the same answer this process gives for every other kind of loss.
        if (resolved.isEmpty() || resolved == it->placeId)
            continue;

        it->placeId = resolved;
        moved.insert(resolved);
    }

    for (const QString &placeId : std::as_const(moved)) {
        const Watched &watched = ensureWatched(placeId);

        // Published only when there is something to publish. ensureWatched has
        // already kicked a fetch for a place nobody was watching, and sending
        // an empty snapshot in the meantime would replace a tile's old-but-real
        // city with a new one made of dashes.
        if (!watched.forecast.isEmpty())
            publish(placeId);
    }

    Q_EMIT placesChanged();
}

QString SnapshotService::placesFingerprint() const
{
    if (m_places == nullptr)
        return {};

    QStringList parts;
    const QList<Place> all = m_places->places();
    parts.reserve(all.size() + 1);

    for (const Place &place : all) {
        parts.append(QStringLiteral("%1/%2/%3/%4/%5")
                         .arg(place.id)
                         .arg(place.isHome ? 1 : 0)
                         .arg(place.coordinate.latitude, 0, 'f', 5)
                         .arg(place.coordinate.longitude, 0, 'f', 5)
                         .arg(place.timezone));
    }

    // The current place is a setting rather than a column, and it is the second
    // thing resolve() consults. A reader whose home place has been deleted
    // follows it, so a change to it has to count as a change.
    parts.append(QStringLiteral("current/%1").arg(m_places->currentPlace().id));

    return parts.join(QLatin1Char('|'));
}

QStringList SnapshotService::placeIds() const
{
    if (isFixtureMode())
        return {QStringLiteral("fixture")};

    QStringList ids;
    const QList<Place> all = m_places->places();
    ids.reserve(all.size());
    for (const Place &p : all)
        ids.append(QString::number(p.id));
    return ids;
}

// ---- the bus ----------------------------------------------------------------

SnapshotService::Watched &SnapshotService::ensureWatched(const QString &placeId)
{
    auto it = m_watched.find(placeId);
    if (it != m_watched.end())
        return *it;

    Watched fresh;
    fresh.place = resolve(placeId);
    it          = m_watched.insert(placeId, fresh);

    // A place nobody has asked about before has nothing in memory. The cache
    // first, synchronously, so that a GetSnapshot in this same turn - which is
    // when a starting widget host makes it - gets a stale reading rather than
    // a gap; then a fetch, which brings the reading up to date. The same rule
    // the app's first frame follows, one process further out.
    warmFromCache(*it);
    fetch(placeId);
    return *it;
}

void SnapshotService::warmFromCache(Watched &watched)
{
    if (!watched.place.coordinate.isValid())
        return;

    ForecastRequest request;
    request.coord      = watched.place.coordinate;
    request.days       = forecastDays;
    request.timeZone   = QTimeZone(watched.place.timezone.toUtf8());
    request.cachedOnly = true;

    // Asked of the providers directly rather than through the registry's walk.
    // The walk chains its attempts through QFuture::then with a context
    // object, and a continuation with a context runs on the event loop - even
    // when the future it hangs off finished before it was attached. A cached
    // answer from a provider, on the other hand, is a promise finished before
    // fetchForecast() returns, and isFinished() is what tells the two apart. A
    // provider that read its cache asynchronously would simply not be here in
    // time, which is the right outcome for a warm-up.
    const QList<IForecastProvider *> forecasts = m_registry->forecastChain(request.coord);
    for (IForecastProvider *provider : forecasts) {
        QFuture<Result<Forecast>> answer = provider->fetchForecast(request);
        if (!answer.isFinished())
            continue;
        const Result<Forecast> result = answer.result();
        if (!result || result.value().isEmpty())
            continue;
        watched.forecast  = result.value();
        watched.servedBy  = provider->id();
        watched.fromCache = true;
        break;
    }

    const QList<IAirQualityProvider *> airs = m_registry->airQualityChain(request.coord);
    for (IAirQualityProvider *provider : airs) {
        QFuture<Result<AirQuality>> answer = provider->fetchAirQuality(request);
        if (!answer.isFinished())
            continue;
        const Result<AirQuality> result = answer.result();
        if (!result)
            continue;
        watched.air = result.value();
        break;
    }

    // Alerts fan out rather than fall back, so every covering provider's cached
    // set is taken and merged, keyed the way the registry keys them. A set is
    // only ever as complete as the providers that answered: one that has
    // nothing cached is a provider that did not answer, and the merged set says
    // so, which is what keeps a tile from claiming "no warnings" on the
    // strength of the one service it happened to have bytes from.
    AlertRequest alertRequest;
    alertRequest.coord      = request.coord;
    alertRequest.cachedOnly = true;

    AlertSet    merged;
    QStringList servedBy;
    bool        anyAnswered = false;
    bool        allAnswered = true;

    const QList<IAlertProvider *> alerts = m_registry->alertChain(alertRequest.coord);
    for (IAlertProvider *provider : alerts) {
        QFuture<Result<AlertSet>> answer = provider->fetchAlerts(alertRequest);
        if (!answer.isFinished()) {
            allAnswered = false;
            continue;
        }

        // One copy, checked and then read. QFuture::result() returns the
        // Result by value, so calling it once for the check and again for the
        // set would copy every alert twice. The forecast and air-quality loops
        // above take theirs the same way.
        const Result<AlertSet> result = answer.result();
        if (!result) {
            allAnswered = false;
            continue;
        }
        const AlertSet &part = result.value();
        for (const Alert &alert : part.alerts) {
            const bool known = std::any_of(merged.alerts.cbegin(), merged.alerts.cend(),
                                           [&alert](const Alert &seen) {
                                               return seen.isSameHazard(alert);
                                           });
            if (!known)
                merged.alerts.append(alert);
        }
        servedBy.append(provider->id());
        anyAnswered = true;
        if (part.fetchedAt.isValid()
            && (!merged.fetchedAt.isValid() || part.fetchedAt > merged.fetchedAt))
            merged.fetchedAt = part.fetchedAt;
        if (part.confirmedAt.isValid()
            && (!merged.confirmedAt.isValid() || part.confirmedAt < merged.confirmedAt))
            merged.confirmedAt = part.confirmedAt;
    }

    if (anyAnswered) {
        merged.coordinate = alertRequest.coord;
        merged.providerId = servedBy.join(QStringLiteral(", "));
        merged.complete   = allAnswered;
        watched.alerts    = merged;
    }
}

QByteArray SnapshotService::snapshot(const QString    &placeId,
                                     const QStringList &fields,
                                     int                hours,
                                     int                days)
{
    const QString key = canonical(placeId);
    if (key.isEmpty())
        return {};

    const Watched &watched = ensureWatched(key);

    wire::SnapshotSource source;
    source.placeId    = key;
    source.place      = watched.place;
    source.forecast   = watched.forecast;
    source.airQuality = watched.air;
    source.alerts     = watched.alerts;
    source.now        = m_clock->now();
    source.hours      = hours;
    source.days       = days;
    source.servedBy   = watched.servedBy;
    source.fromCache  = watched.fromCache;

    return wire::encode(wire::buildSnapshot(source, wire::FieldMask::fromFields(fields)));
}

QString SnapshotService::subscribe(const QString    &placeId,
                                   const QStringList &fields,
                                   int                hours,
                                   int                days)
{
    const QString key = canonical(placeId);
    if (key.isEmpty())
        return {};

    Subscription sub;
    sub.placeId   = key;
    sub.requested = placeId;
    sub.mask      = wire::FieldMask::fromFields(fields);
    sub.hours     = hours;
    sub.days      = days;

    const QString token = tokenFor(m_nextToken++);
    m_subscriptions.insert(token, sub);

    ensureWatched(key);

    // ---- there is deliberately no first publish here ------------------------
    //
    // There used to be: a QTimer::singleShot(0) that emitted one snapshot on the
    // next return to the event loop, so that "a subscriber which connects to the
    // signal after this call still gets its first snapshot". That does not work,
    // and the way it fails is invisible from this side.
    //
    // The subscriber is blocked in this method call. It cannot add its match
    // rule until the reply reaches it, and adding one is itself a round trip to
    // the bus daemon - while singleShot(0) fires here as soon as the reply is
    // *written*. So the race is not close: the first snapshot is normally
    // emitted before anybody is listening for it, and a widget then shows its
    // waiting skeleton until the next poll five minutes later.
    //
    // Measured, not reasoned about. Every tile in climat-widget came up blank
    // against a live daemon while `gdbus monitor` showed the signals going out.
    //
    // The subscriber calls GetSnapshot once after its match rule is in place,
    // which is deterministic and is one call it is already making a connection
    // for. See DaemonLink::resubscribe.

    return token;
}

bool SnapshotService::unsubscribe(const QString &token)
{
    return m_subscriptions.remove(token) > 0;
}

void SnapshotService::requestRefresh(const QString &placeId)
{
    const QString key = canonical(placeId);
    if (key.isEmpty())
        return;
    ensureWatched(key);
    fetch(key);
}

QByteArray SnapshotService::catalogue() const
{
    // Compiled in as a resource rather than read from an install path, so that
    // ListWidgets answers the same thing whether the daemon was started from a
    // build directory, a .deb or inside a Flatpak. It is the same bytes the
    // test in tests/tst_wiresnapshot.cpp checks.
    QFile file(QStringLiteral(":/climat/catalogue.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// ---- fetching ---------------------------------------------------------------

void SnapshotService::onPollTimeout()
{
    // The guarantee behind the file watcher. If a notification was never
    // delivered - a filesystem that does not report, a database on a network
    // home, a container that isolates inotify - the list is still re-read here,
    // and the worst case becomes "the widgets follow within five minutes"
    // rather than "the widgets never follow".
    reloadPlaces();

    // Only what somebody is actually looking at. A place with no subscription
    // and no recent GetSnapshot is not refreshed, which is what keeps a daemon
    // with ten saved cities from making ten requests for the nine nobody has
    // on their desktop.
    QSet<QString> wanted;
    for (const Subscription &sub : std::as_const(m_subscriptions))
        wanted.insert(sub.placeId);

    for (const QString &placeId : std::as_const(wanted))
        fetch(placeId);
}

void SnapshotService::fetch(const QString &placeId)
{
    auto it = m_watched.find(placeId);
    if (it == m_watched.end())
        return;
    if (it->inFlight)
        return;
    if (!it->place.coordinate.isValid())
        return;

    it->inFlight = true;

    ForecastRequest request;
    request.coord    = it->place.coordinate;
    request.days     = forecastDays;
    request.timeZone = QTimeZone(it->place.timezone.toUtf8());

    auto *forecast = new QFutureWatcher<Result<ForecastAnswer>>(this);
    connect(forecast, &QFutureWatcherBase::finished, this, [this, forecast, placeId]() {
        forecast->deleteLater();
        auto watched = m_watched.find(placeId);
        if (watched == m_watched.end())
            return;
        watched->inFlight = false;

        const Result<ForecastAnswer> result = forecast->result();
        if (result) {
            watched->forecast  = result.value().value;
            watched->servedBy  = result.value().servedBy;
            watched->fromCache = false;
            publish(placeId);
        } else if (!watched->forecast.isEmpty()) {
            // A failed refresh does not clear what we already have. The widget
            // keeps its reading and its timestamp, and the timestamp is what
            // tells the truth about the failure.
            watched->fromCache = true;
            publish(placeId);
        }
    });
    forecast->setFuture(m_registry->fetchForecast(request));

    auto *air = new QFutureWatcher<Result<AirQualityAnswer>>(this);
    connect(air, &QFutureWatcherBase::finished, this, [this, air, placeId]() {
        air->deleteLater();
        auto watched = m_watched.find(placeId);
        if (watched == m_watched.end())
            return;
        const Result<AirQualityAnswer> result = air->result();
        if (result) {
            watched->air = result.value().value;
            publish(placeId);
        }
    });
    air->setFuture(m_registry->fetchAirQuality(request));

    AlertRequest alertRequest;
    alertRequest.coord = it->place.coordinate;

    auto *alerts = new QFutureWatcher<Result<AlertAnswer>>(this);
    connect(alerts, &QFutureWatcherBase::finished, this, [this, alerts, placeId]() {
        alerts->deleteLater();
        auto watched = m_watched.find(placeId);
        if (watched == m_watched.end())
            return;
        const Result<AlertAnswer> result = alerts->result();
        if (result) {
            watched->alerts = result.value().value;
            publish(placeId);
        }
    });
    alerts->setFuture(m_registry->fetchAlerts(alertRequest));
}

void SnapshotService::publish(const QString &placeId)
{
    for (auto it = m_subscriptions.cbegin(); it != m_subscriptions.cend(); ++it) {
        if (it->placeId != placeId)
            continue;
        const QByteArray json = snapshot(placeId, it->mask.fields(), it->hours, it->days);
        if (!json.isEmpty())
            Q_EMIT snapshotChanged(it.key(), QString::fromUtf8(json));
    }
}
