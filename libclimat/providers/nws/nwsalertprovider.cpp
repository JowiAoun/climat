// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0

#include "libclimat/providers/nws/nwsalertprovider.h"

#include "libclimat/cache/payloadcache.h"
#include "libclimat/core/clock.h"
#include "libclimat/net/httpclient.h"
#include "libclimat/providers/registry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace climat {

namespace {

const char kProviderId[] = "nws";

// "2026-08-05T14:15:00-07:00". Qt::ISODate handles the offset; the result is
// kept as sent rather than converted, because a QDateTime carrying its own
// offset compares correctly against any other and converting would throw away
// the only clue about which office issued it.
QDateTime instant(const QJsonValue &value)
{
    if (!value.isString())
        return {};
    return QDateTime::fromString(value.toString(), Qt::ISODate);
}

// CAP 1.2, sent verbatim by this service, so these are transcriptions rather
// than mappings. The default is Unknown in each case - a value nobody here has
// seen is not a value to guess at.
AlertSeverity severityFrom(const QString &value)
{
    if (value == QLatin1String("Extreme"))  return AlertSeverity::Extreme;
    if (value == QLatin1String("Severe"))   return AlertSeverity::Severe;
    if (value == QLatin1String("Moderate")) return AlertSeverity::Moderate;
    if (value == QLatin1String("Minor"))    return AlertSeverity::Minor;
    return AlertSeverity::Unknown;
}

AlertUrgency urgencyFrom(const QString &value)
{
    if (value == QLatin1String("Immediate")) return AlertUrgency::Immediate;
    if (value == QLatin1String("Expected"))  return AlertUrgency::Expected;
    if (value == QLatin1String("Future"))    return AlertUrgency::Future;
    if (value == QLatin1String("Past"))      return AlertUrgency::Past;
    return AlertUrgency::Unknown;
}

AlertCertainty certaintyFrom(const QString &value)
{
    if (value == QLatin1String("Observed")) return AlertCertainty::Observed;
    if (value == QLatin1String("Likely"))   return AlertCertainty::Likely;
    if (value == QLatin1String("Possible")) return AlertCertainty::Possible;
    if (value == QLatin1String("Unlikely")) return AlertCertainty::Unlikely;
    return AlertCertainty::Unknown;
}

AlertMessageType messageTypeFrom(const QString &value)
{
    if (value == QLatin1String("Update")) return AlertMessageType::Update;
    if (value == QLatin1String("Cancel")) return AlertMessageType::Cancel;
    if (value == QLatin1String("Ack"))    return AlertMessageType::Ack;
    if (value == QLatin1String("Error"))  return AlertMessageType::Error;
    return AlertMessageType::Alert;
}

} // namespace

// ---- construction ---------------------------------------------------------------

NwsAlertProvider::NwsAlertProvider(HttpClient *http, Clock *clock, QObject *parent)
    : QObject(parent)
    , m_http(http)
    , m_clock(clock)
    , m_baseUrl(QUrl(QStringLiteral("https://api.weather.gov/alerts/active")))
{
}

NwsAlertProvider::~NwsAlertProvider() = default;

void NwsAlertProvider::setBaseUrl(const QUrl &url)
{
    m_baseUrl = url;
}

void NwsAlertProvider::setCache(CacheStore *cache)
{
    m_cache = cache;
}

// ---- identity and credit ----------------------------------------------------------

QString NwsAlertProvider::id() const
{
    return QString::fromLatin1(kProviderId);
}

QString NwsAlertProvider::displayName() const
{
    return QStringLiteral("National Weather Service");
}

Attribution NwsAlertProvider::attribution() const
{
    Attribution credit;
    credit.name       = QStringLiteral("NOAA / National Weather Service");
    credit.creditLine = QStringLiteral("Alerts from the NOAA National Weather Service");
    credit.homepage   = QUrl(QStringLiteral("https://www.weather.gov/"));

    // A work of the United States government: public domain by 17 U.S.C. §105,
    // which is a licence status rather than a licence. Named as what it is
    // rather than squeezed into an SPDX identifier that would imply a grant
    // nobody made.
    credit.licenceName = QStringLiteral("U.S. Public Domain (17 U.S.C. §105)");
    credit.licenceUrl  = QUrl(QStringLiteral("https://www.weather.gov/disclaimer"));
    credit.note        = QStringLiteral(
        "api.weather.gov requires an identifying User-Agent and answers 403 without one. "
        "Alerts cover the United States and its territories only.");
    return credit;
}

bool NwsAlertProvider::covers(Coordinate coord) const
{
    return coord.isValid() && regionContains(Region::UnitedStates, coord);
}

Capabilities NwsAlertProvider::capabilitiesAt(Coordinate coord) const
{
    if (!covers(coord))
        return {};
    return Capabilities(Capability::Alerts);
}

// ---- fetching ----------------------------------------------------------------------

QFuture<Result<AlertSet>> NwsAlertProvider::fetchAlerts(const AlertRequest &request)
{
    HttpRequest http;
    http.providerId = id();
    http.endpoint   = QStringLiteral("alerts");
    http.url        = m_baseUrl;
    http.kind       = DataKind::Alerts;
    http.coordinate = request.coord;

    // point=<lat>,<lon> - latitude first, one parameter. The opposite order to
    // ECCC's bbox, which is why the spelling is a CoordinateForm rather than
    // something each provider assembles.
    http.coordinateForm      = CoordinateForm::LatitudeCommaLongitude;
    http.coordinateParameter = QStringLiteral("point");

    // Unlike GeoMet, this service sends an ETag - verified - so the conditional
    // request HttpClient adds is real here and most polls come back 304.
    const QString key = RequestKey::forRequest(http).toString();

    const payloadcache::Hit cached = payloadcache::lookUp(m_cache, key);
    const Coordinate        coord  = request.coord;

    if (cached.present && cached.fresh) {
        Result<AlertSet> adapted = parse(cached.payload, cached.fetchedAt);
        if (adapted.hasValue()) {
            adapted.value().providerId  = id();
            adapted.value().coordinate  = coord;
            adapted.value().confirmedAt = cached.fetchedAt;
            return QtFuture::makeReadyValueFuture(adapted);
        }
    }

    if (request.cachedOnly) {
        if (cached.present) {
            Result<AlertSet> stale = parse(cached.payload, cached.fetchedAt);
            if (stale.hasValue()) {
                stale.value().providerId  = id();
                stale.value().coordinate  = coord;
                stale.value().confirmedAt = cached.fetchedAt;
                return QtFuture::makeReadyValueFuture(stale);
            }
        }
        Error error(ErrorKind::NotFound,
                    QStringLiteral("no cached alerts for %1").arg(coord.toKeyString()));
        error.setProviderId(id());
        return QtFuture::makeReadyValueFuture(Result<AlertSet>(error));
    }

    QFuture<Result<HttpResponse>> transfer = m_http->send(http);

    return transfer.then(this,
                         [this, key, cached, coord](const Result<HttpResponse> &result)
                             -> Result<AlertSet> {
        if (!result.hasValue()) {
            const Error transportError = result.error();

            // "Parameter \"point\" is invalid: out of bounds" - the service
            // declining the question rather than failing to answer it. The
            // header has the argument for why this becomes Unsupported and what
            // that costs.
            if (transportError.kind() == ErrorKind::HttpStatus
                && transportError.httpStatus() == 400) {
                Error unsupported(
                    ErrorKind::Unsupported,
                    QStringLiteral("api.weather.gov does not cover %1 (%2)")
                        .arg(coord.toKeyString(), transportError.message()));
                unsupported.setProviderId(id());
                unsupported.setHttpStatus(400);
                return unsupported;
            }

            if (cached.present) {
                Result<AlertSet> stale = parse(cached.payload, cached.fetchedAt);
                if (stale.hasValue()) {
                    stale.value().providerId  = id();
                    stale.value().coordinate  = coord;
                    stale.value().confirmedAt = cached.fetchedAt;
                    return stale;
                }
            }
            return transportError;
        }

        const HttpResponse &response = result.value();

        if (response.notModified) {
            payloadcache::touch(m_cache, key, DataKind::Alerts, response);
            if (cached.present) {
                Result<AlertSet> current = parse(cached.payload, cached.fetchedAt);
                if (current.hasValue()) {
                    current.value().providerId = id();
                    current.value().coordinate = coord;
                    // Confirmed NOW, fetched now: a 304 is the service saying
                    // the set it sent before is still the set. That is a
                    // successful confirmation, and treating it as one is the
                    // whole reason to send the conditional request.
                    current.value().fetchedAt   = response.fetchedAt;
                    current.value().confirmedAt = response.fetchedAt;
                    return current;
                }
            }
            Error error(ErrorKind::Parse,
                        QStringLiteral("304 for a payload this process never held"));
            error.setProviderId(id());
            error.setHttpStatus(response.status);
            return error;
        }

        Result<AlertSet> parsed = parse(response.body, response.fetchedAt);
        if (!parsed.hasValue()) {
            Error error = parsed.error();
            error.setProviderId(id());
            return error;
        }

        parsed.value().providerId  = id();
        parsed.value().coordinate  = coord;
        parsed.value().confirmedAt = response.fetchedAt;
        payloadcache::store(m_cache, key, id(), QStringLiteral("alerts"), DataKind::Alerts,
                            coord, response);
        return parsed;
    });
}

// ---- parsing ------------------------------------------------------------------------

Result<AlertSet> NwsAlertProvider::parse(const QByteArray &body, const QDateTime &fetchedAt)
{
    QJsonParseError     parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (document.isNull() || !document.isObject()) {
        return Error(ErrorKind::Parse,
                     QStringLiteral("alert payload is not a JSON object: %1")
                         .arg(parseError.errorString()));
    }

    const QJsonObject root = document.object();

    // The problem+json shape. Reached when a body is parsed outside the transport
    // - a fixture, or the tools/provider-probe path - rather than on the live
    // error path, which HttpClient has already turned into an Error.
    if (root.contains(QStringLiteral("detail")) && !root.contains(QStringLiteral("features"))) {
        const int status = root.value(QStringLiteral("status")).toInt();
        const QString detail = root.value(QStringLiteral("detail")).toString();

        // Same judgement as the fetch path, so a fixture and a live response
        // produce the same kind. Anything else here is genuinely a bad request.
        Error error(status == 400 ? ErrorKind::Unsupported : ErrorKind::Parse,
                    QStringLiteral("api.weather.gov: %1").arg(detail));
        error.setHttpStatus(status);
        return error;
    }

    const QJsonValue features = root.value(QStringLiteral("features"));
    if (!features.isArray()) {
        return Error(ErrorKind::Parse,
                     QStringLiteral("alert payload carries no feature collection"));
    }

    AlertSet set;
    set.fetchedAt = fetchedAt;

    const QJsonArray array = features.toArray();
    for (const QJsonValue &entry : array) {
        const QJsonObject feature    = entry.toObject();
        const QJsonObject properties = feature.value(QStringLiteral("properties")).toObject();
        if (properties.isEmpty())
            continue;

        Alert alert;
        alert.providerId = QString::fromLatin1(kProviderId);
        alert.id         = properties.value(QStringLiteral("id")).toString();

        alert.event           = properties.value(QStringLiteral("event")).toString();
        alert.headline        = properties.value(QStringLiteral("headline")).toString();
        alert.description     = properties.value(QStringLiteral("description")).toString();
        alert.instruction     = properties.value(QStringLiteral("instruction")).toString();
        alert.areaDescription = properties.value(QStringLiteral("areaDesc")).toString();
        alert.senderName      = properties.value(QStringLiteral("senderName")).toString();

        alert.severity  = severityFrom(properties.value(QStringLiteral("severity")).toString());
        alert.urgency   = urgencyFrom(properties.value(QStringLiteral("urgency")).toString());
        alert.certainty = certaintyFrom(properties.value(QStringLiteral("certainty")).toString());
        alert.messageType =
            messageTypeFrom(properties.value(QStringLiteral("messageType")).toString());

        // The issuer's own grading, spelled their way. For NWS that is the CAP
        // word itself, which makes issuerLabel look redundant here - it is not,
        // it is the field the sheet renders, and it has to be populated by every
        // provider or the sheet renders a blank for one of them.
        alert.issuerLabel = properties.value(QStringLiteral("severity")).toString();

        alert.sent      = instant(properties.value(QStringLiteral("sent")));
        alert.effective = instant(properties.value(QStringLiteral("effective")));
        alert.onset     = instant(properties.value(QStringLiteral("onset")));
        alert.expires   = instant(properties.value(QStringLiteral("expires")));

        // Null for every Air Quality Alert in the recorded set. Alert::hazardEnd()
        // is where that is handled; nothing is substituted here.
        alert.ends = instant(properties.value(QStringLiteral("ends")));

        // A web page or nothing. Nothing opens this link today, and the day
        // something does, it should not be handed a file: or intent: URL that
        // came off a feed.
        const QUrl web(properties.value(QStringLiteral("web")).toString());
        if (web.scheme() == QLatin1String("https") || web.scheme() == QLatin1String("http"))
            alert.web = web;

        // Identity: own id, plus every id this message supersedes. The header
        // has the two fixtures that decided this shape.
        const QString prefix = QString::fromLatin1(kProviderId) + QLatin1Char(':');
        if (!alert.id.isEmpty())
            alert.identityKeys.append(prefix + alert.id);

        const QJsonArray references = properties.value(QStringLiteral("references")).toArray();
        for (const QJsonValue &reference : references) {
            const QString identifier =
                reference.toObject().value(QStringLiteral("identifier")).toString();
            if (!identifier.isEmpty())
                alert.identityKeys.append(prefix + identifier);
        }

        if (alert.isValid())
            set.alerts.append(alert);
    }

    return set;
}

} // namespace climat
