// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0

#include "openmeteoforecastprovider.h"

#include "libclimat/cache/payloadcache.h"
#include "libclimat/core/clock.h"
#include "libclimat/net/httpclient.h"
#include "libclimat/net/requestkey.h"
#include "libclimat/providers/openmeteo/openmeteoadapter.h"
#include "libclimat/providers/openmeteo/openmeteoconsensus.h"
#include "libclimat/providers/openmeteo/openmeteovariables.h"

#include <QFutureWatcher>
#include <QPromise>

namespace climat {
namespace {

// Sixteen is the documented maximum for `/v1/forecast`; one is the minimum
// that produces a series at all.
constexpr int kMaxForecastDays = 16;

// Yesterday, always. See the header: the hourly strip shows observed hours
// behind the "now" marker and there is nowhere else to get them.
constexpr int kPastDays = 1;

// Which capabilities this provider could conceivably have somewhere. Anything
// outside this set is known-absent everywhere and is never reported as
// undetermined - a UI must not hold space for an ensemble tab on the grounds
// that `/v1/forecast` might one day return one.
CapabilityFlags supportedEverywhere()
{
    return Capability::CurrentConditions | Capability::Hourly | Capability::Daily
         | Capability::Temperature | Capability::ApparentTemperature | Capability::DewPoint
         | Capability::Humidity | Capability::Precipitation | Capability::PrecipitationType
         | Capability::PrecipitationProbability | Capability::Wind | Capability::WindGust
         | Capability::Pressure | Capability::CloudCover | Capability::Visibility
         | Capability::UvIndex | Capability::WeatherCode | Capability::SunTimes;
}

} // namespace

OpenMeteoForecastProvider::OpenMeteoForecastProvider(HttpClient *http, Clock *clock,
                                                     QObject *parent)
    : QObject(parent)
    , m_http(http)
    , m_clock(clock)
    , m_baseUrl(QStringLiteral("https://api.open-meteo.com/v1/forecast"))
{
}

OpenMeteoForecastProvider::~OpenMeteoForecastProvider() = default;

void OpenMeteoForecastProvider::setCache(CacheStore *cache)
{
    m_cache = cache;
}

QString OpenMeteoForecastProvider::providerId()
{
    return QStringLiteral("open-meteo");
}

QString OpenMeteoForecastProvider::id() const
{
    return providerId();
}

QString OpenMeteoForecastProvider::displayName() const
{
    // Not translated here. iforecastprovider.h: "For humans. Translated by the
    // app, not here." - and this one is a proper noun anyway.
    return QStringLiteral("Open-Meteo");
}

Attribution OpenMeteoForecastProvider::attribution() const
{
    return openmeteo::attribution();
}

bool OpenMeteoForecastProvider::covers(Coordinate coord) const
{
    // Global. The only thing that is not covered is a coordinate that is not
    // one - a NaN out of a half-initialised map, a longitude of 400 - and
    // `covers` is the right place to catch it, because the alternative is a
    // request that goes out and comes back 400.
    return coord.isValid();
}

int OpenMeteoForecastProvider::clampDays(int requested)
{
    if (requested < 1)
        return 1;
    if (requested > kMaxForecastDays)
        return kMaxForecastDays;
    return requested;
}

void OpenMeteoForecastProvider::setBaseUrl(const QUrl &url)
{
    m_baseUrl = url;
}

QUrl OpenMeteoForecastProvider::baseUrl() const
{
    return m_baseUrl;
}

HttpRequest OpenMeteoForecastProvider::buildRequest(const ForecastRequest &request) const
{
    HttpRequest out;

    out.providerId = providerId();
    out.endpoint   = QStringLiteral("forecast");
    out.url        = m_baseUrl;
    out.kind       = DataKind::Forecast;

    // Handed over as a coordinate rather than baked into the parameters, so
    // that HttpClient rounds it - once, in one place, before it is hashed and
    // before it is sent. libclimat/net/httprequest.h is emphatic about why.
    out.coordinate         = request.coord;
    out.latitudeParameter  = QStringLiteral("latitude");
    out.longitudeParameter = QStringLiteral("longitude");

    out.parameters = {
        // The zone resolved from the coordinate. We do not trust the naive
        // timestamps it produces - libclimat/domain/timeaxis.h - but we do need
        // the IANA id and the offset it used, and this is the only parameter
        // that reports them.
        { QStringLiteral("timezone"), QStringLiteral("auto") },

        { QStringLiteral("forecast_days"), QString::number(clampDays(request.days)) },
        { QStringLiteral("past_days"), QString::number(kPastDays) },

        { QStringLiteral("current"), openmeteo::currentParameter() },
        { QStringLiteral("hourly"), openmeteo::hourlyParameter() },
        { QStringLiteral("daily"), openmeteo::dailyParameter() },
    };

    // No `temperature_unit`, `wind_speed_unit` or `precipitation_unit`. See
    // openmeteoadapter.h: unit-tagged responses make the cache unit-keyed, so
    // a °C-to-°F toggle would refetch every forecast the user has.

    if (!request.models.isEmpty()) {
        out.parameters.append({ QStringLiteral("models"),
                                request.models.join(QLatin1Char(',')) });
    }

    return out;
}

HttpRequest OpenMeteoForecastProvider::buildConsensusRequest(const ForecastRequest &request) const
{
    // The same coordinate, zone and window as buildRequest(), so that every
    // hour in one answer has its twin in the other. Only hours: a `current=`
    // block with two models comes back once, for one of them.
    HttpRequest out = buildRequest(request);
    out.parameters = {
        { QStringLiteral("timezone"), QStringLiteral("auto") },
        { QStringLiteral("forecast_days"), QString::number(clampDays(request.days)) },
        { QStringLiteral("past_days"), QString::number(kPastDays) },
        { QStringLiteral("hourly"), openmeteo::consensusHourlyParameter() },
        { QStringLiteral("models"), openmeteo::consensusModels().join(QLatin1Char(',')) },
    };
    return out;
}

Capabilities OpenMeteoForecastProvider::capabilitiesAt(Coordinate coord) const
{
    const auto learned = m_learned.constFind(coord.rounded().toKeyString());
    if (learned != m_learned.constEnd())
        return *learned;

    // Nothing fetched here yet, so nothing is known. Not "no" - a UI that
    // renders "no" hides the UV tab for the two seconds before the payload
    // lands and then pops it in, which looks like a bug and is one.
    return Capabilities({}, supportedEverywhere());
}

void OpenMeteoForecastProvider::rememberCapabilities(Coordinate coord, const Forecast &forecast)
{
    m_learned.insert(coord.rounded().toKeyString(), openmeteo::capabilitiesFor(forecast));
}

QFuture<Result<Forecast>> OpenMeteoForecastProvider::fetchForecast(const ForecastRequest &request)
{
    auto promise = std::make_shared<QPromise<Result<Forecast>>>();
    promise->start();
    QFuture<Result<Forecast>> future = promise->future();

    if (!covers(request.coord)) {
        Error error(ErrorKind::Unsupported, QStringLiteral("not a valid coordinate"));
        error.setProviderId(providerId());
        promise->addResult(Result<Forecast>(error));
        promise->finish();
        return future;
    }

    const Coordinate coord = request.coord;

    const HttpRequest http = buildRequest(request);
    const QString     key  = RequestKey::forRequest(http).toString();

    // ---- the cache, read before the socket is opened ------------------------
    //
    // docs/04-architecture.md §4.1, principle 1: "the UI renders from cache,
    // then reconciles with the network". A fresh entry is the whole answer and
    // no request is made - §4.5 puts the hourly forecast's TTL at 30 minutes,
    // and thirty minutes of identical bytes is thirty minutes of somebody
    // else's bandwidth.
    const payloadcache::Hit cached = payloadcache::lookUp(m_cache, key);

    if (cached.present && (cached.fresh || request.cachedOnly)) {
        Result<Forecast> adapted = openmeteo::adaptForecast(cached.payload, providerId());
        if (adapted) {
            // The moment the bytes were fetched, not the moment they were
            // read. That is what makes "updated 12 minutes ago" true rather
            // than "updated just now" every time the app is reopened.
            adapted.value().fetchedAt = cached.fetchedAt;
            rememberCapabilities(coord, adapted.value());
            deliver(promise, request, adapted, !request.cachedOnly);
            return future;
        }
        // A cached payload that no longer parses is a cached payload we should
        // not be holding. Fall through to the network rather than reporting a
        // parse error for something the user never asked us to read.
    }

    if (request.cachedOnly) {
        Error error(ErrorKind::NotFound,
                    QStringLiteral("nothing cached for %1").arg(coord.toKeyString()));
        error.setProviderId(providerId());
        promise->addResult(Result<Forecast>(error));
        promise->finish();
        return future;
    }

    QFuture<Result<HttpResponse>> transfer = m_http->send(http);

    // A watcher parented to this, deleted when it finishes. The alternative -
    // `.then()` - runs its continuation on whichever thread finished the
    // future, and this object's QHash of learned capabilities is not thread
    // safe. A watcher delivers on the thread that owns it, which is the thread
    // that owns us, which is the rule libclimat/net/httpclient.h already sets.
    auto *watcher = new QFutureWatcher<Result<HttpResponse>>(this);

    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, promise, request, coord, key, cached]() {
                watcher->deleteLater();

                const Result<HttpResponse> transferred = watcher->result();
                if (!transferred) {
                    // ---- the aeroplane case ---------------------------------
                    //
                    // The request failed and there is a stale payload on disk.
                    // §4.5 ticks stale-while-revalidate for every forecast row,
                    // and §4.1 says the app must never show an empty screen
                    // because an API is down. So the stale bytes are served,
                    // carrying the timestamp they were actually fetched at -
                    // which is what makes the UI say "updated 3 hours ago"
                    // rather than pretending this is current.
                    if (cached.present) {
                        Result<Forecast> stale =
                            openmeteo::adaptForecast(cached.payload, providerId());
                        if (stale) {
                            stale.value().fetchedAt = cached.fetchedAt;
                            rememberCapabilities(coord, stale.value());
                            deliver(promise, request, stale, false);
                            return;
                        }
                    }
                    promise->addResult(Result<Forecast>(transferred.error()));
                    promise->finish();
                    return;
                }

                const HttpResponse &response = transferred.value();

                // A 304 is a success with no bytes: the cache entry we already
                // hold is confirmed current. There is nothing to parse and it
                // is not this class's job to read the cache, so the caller is
                // told plainly rather than handed an empty Forecast that would
                // read as "the provider has nothing here".
                if (response.notModified) {
                    if (cached.present) {
                        payloadcache::touch(m_cache, key, DataKind::Forecast, response);
                        Result<Forecast> confirmed =
                            openmeteo::adaptForecast(cached.payload, providerId());
                        if (confirmed) {
                            // Confirmed current, so the age is now and not the
                            // age of the bytes: a 304 is the server saying the
                            // data has not changed, which makes it as fresh as
                            // a 200 carrying the same body.
                            confirmed.value().fetchedAt = response.fetchedAt;
                            rememberCapabilities(coord, confirmed.value());
                            deliver(promise, request, confirmed, true);
                            return;
                        }
                    }

                    Error error(ErrorKind::Cancelled,
                                QStringLiteral("not modified; the cached forecast stands"));
                    error.setProviderId(providerId());
                    error.setHttpStatus(response.status);
                    promise->addResult(Result<Forecast>(error));
                    promise->finish();
                    return;
                }

                Result<Forecast> adapted =
                    openmeteo::adaptForecast(response.body, providerId());

                if (adapted) {
                    // The one timestamp this class produces, and it comes from
                    // the injected clock. In fixture mode that makes "updated
                    // 25 minutes ago" say what it said on the afternoon the
                    // fixtures were recorded.
                    adapted.value().fetchedAt = m_clock->now();
                    rememberCapabilities(coord, adapted.value());
                    payloadcache::store(m_cache, key, providerId(),
                                        QStringLiteral("forecast"), DataKind::Forecast,
                                        coord, response);
                    deliver(promise, request, adapted, true);
                    return;
                }

                promise->addResult(adapted);
                promise->finish();
            });

    watcher->setFuture(transfer);
    return future;
}

// ---- the Canadian average ----------------------------------------------------
//
// openmeteo/openmeteoconsensus.h has the measurement this rests on. What is
// here is the plumbing, and its rule is that the average can only ever add:
// every way the second request fails ends in the forecast the first request
// produced, unaveraged, which is the forecast this app showed before.

Result<Forecast> OpenMeteoForecastProvider::averaged(Result<Forecast> result,
                                                     const QByteArray &payload) const
{
    const Result<QList<Forecast>> models = openmeteo::adaptConsensus(payload, providerId());
    if (models && result)
        openmeteo::applyConsensus(result.value(), models.value());
    return result;
}

void OpenMeteoForecastProvider::deliver(const Promise &promise, const ForecastRequest &request,
                                        Result<Forecast> result, bool networkAllowed)
{
    const auto finish = [promise](const Result<Forecast> &answer) {
        promise->addResult(answer);
        promise->finish();
    };

    // An explicit model list is a caller asking for those models and no
    // others, and an error has nothing to average into.
    if (!result || !request.models.isEmpty() || !openmeteo::consensusApplies(request.coord)) {
        finish(result);
        return;
    }

    const HttpRequest       http   = buildConsensusRequest(request);
    const QString           key    = RequestKey::forRequest(http).toString();
    const payloadcache::Hit cached = payloadcache::lookUp(m_cache, key);

    if (cached.present && (cached.fresh || !networkAllowed)) {
        finish(averaged(result, cached.payload));
        return;
    }
    if (!networkAllowed) {
        finish(result);
        return;
    }

    auto *watcher = new QFutureWatcher<Result<HttpResponse>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this,
            [this, watcher, finish, result, request, key, cached]() {
                watcher->deleteLater();

                const Result<HttpResponse> transferred = watcher->result();
                if (!transferred) {
                    finish(cached.present ? averaged(result, cached.payload) : result);
                    return;
                }

                const HttpResponse &response = transferred.value();
                if (response.notModified) {
                    if (cached.present) {
                        payloadcache::touch(m_cache, key, DataKind::Forecast, response);
                        finish(averaged(result, cached.payload));
                    } else {
                        finish(result);
                    }
                    return;
                }

                // Kept only if it parses, so a bad answer is not served from
                // the cache for the next thirty minutes as well.
                if (openmeteo::adaptConsensus(response.body, providerId())) {
                    payloadcache::store(m_cache, key, providerId(),
                                        QStringLiteral("forecast"), DataKind::Forecast,
                                        request.coord, response);
                }
                finish(averaged(result, response.body));
            });
    watcher->setFuture(m_http->send(http));
}

} // namespace climat
