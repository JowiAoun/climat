// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0

#include "libclimat/providers/eccc/ecccalertprovider.h"

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

const char kProviderId[] = "eccc";

// ---- reading the payload -----------------------------------------------------

// "2026-08-05T18:44:57.573Z". Qt::ISODate accepts the fractional seconds and the
// Z, and produces a QDateTime in UTC. An unparseable or absent value comes back
// invalid, which every consumer in libclimat/domain/alert.h already handles -
// there is no timestamp here whose absence is a parse failure.
QDateTime instant(const QJsonValue &value)
{
    if (!value.isString())
        return {};
    return QDateTime::fromString(value.toString(), Qt::ISODate);
}

QString text(const QJsonObject &object, const QString &stem, const QString &language)
{
    // `stem` is the field without its language suffix: "alert_name" becomes
    // alert_name_fr or alert_name_en. Falling back to English rather than to an
    // empty string, because a French user reading an English warning is a worse
    // outcome than nothing only if you have never been rained on.
    const QString suffixed = stem + QLatin1Char('_') + language;
    const QString localised = object.value(suffixed).toString();
    if (!localised.isEmpty())
        return localised;
    return object.value(stem + QStringLiteral("_en")).toString();
}

// THE severity mapping. From the risk colour, never from alert_type - the
// header argues why at length.
//
// Matched on the English colour whatever language was asked for: `risk_colour_fr`
// says "jaune", and a mapping that had to know both spellings would be a mapping
// with two places to forget a colour. The French word is still shown to the
// reader, through issuerLabel.
AlertSeverity severityFromColour(const QString &colourEnglish)
{
    const QString colour = colourEnglish.trimmed().toLower();
    if (colour == QLatin1String("red"))
        return AlertSeverity::Extreme;
    if (colour == QLatin1String("orange"))
        return AlertSeverity::Severe;
    if (colour == QLatin1String("yellow"))
        return AlertSeverity::Moderate;
    if (colour == QLatin1String("green") || colour == QLatin1String("grey")
        || colour == QLatin1String("gray"))
        return AlertSeverity::Minor;

    // A colour nobody here has seen. Unknown rather than a guess: it still
    // displays, it just does not claim a grade - see alert.h on why Unknown
    // sorts below Minor and is not the same as "probably fine".
    return AlertSeverity::Unknown;
}

// From alert_type. A watch is about time and confidence, not magnitude.
AlertUrgency urgencyFromType(const QString &typeEnglish)
{
    const QString type = typeEnglish.trimmed().toLower();
    if (type == QLatin1String("warning"))
        return AlertUrgency::Expected;
    if (type == QLatin1String("watch"))
        return AlertUrgency::Future;
    if (type == QLatin1String("statement") || type == QLatin1String("advisory"))
        return AlertUrgency::Future;
    return AlertUrgency::Unknown;
}

AlertCertainty certaintyFromConfidence(const QString &confidenceEnglish)
{
    const QString confidence = confidenceEnglish.trimmed().toLower();
    if (confidence == QLatin1String("high"))
        return AlertCertainty::Likely;
    if (confidence == QLatin1String("medium") || confidence == QLatin1String("moderate"))
        return AlertCertainty::Possible;
    if (confidence == QLatin1String("low"))
        return AlertCertainty::Unlikely;
    return AlertCertainty::Unknown;
}

AlertMessageType messageTypeFromStatus(const QString &statusEnglish)
{
    const QString status = statusEnglish.trimmed().toLower();
    if (status == QLatin1String("issued"))
        return AlertMessageType::Alert;
    if (status == QLatin1String("continued") || status == QLatin1String("updated")
        || status == QLatin1String("extended"))
        return AlertMessageType::Update;
    if (status == QLatin1String("ended") || status == QLatin1String("cancelled")
        || status == QLatin1String("canceled"))
        return AlertMessageType::Cancel;
    return AlertMessageType::Alert;
}

} // namespace

// ---- construction ---------------------------------------------------------------

EcccAlertProvider::EcccAlertProvider(HttpClient *http, Clock *clock, QObject *parent)
    : QObject(parent)
    , m_http(http)
    , m_clock(clock)
    , m_baseUrl(QUrl(QStringLiteral(
          "https://api.weather.gc.ca/collections/weather-alerts/items")))
{
}

EcccAlertProvider::~EcccAlertProvider() = default;

void EcccAlertProvider::setBaseUrl(const QUrl &url)
{
    m_baseUrl = url;
}

void EcccAlertProvider::setCache(CacheStore *cache)
{
    m_cache = cache;
}

// ---- identity and credit ----------------------------------------------------------

QString EcccAlertProvider::id() const
{
    return QString::fromLatin1(kProviderId);
}

QString EcccAlertProvider::displayName() const
{
    return QStringLiteral("Environment and Climate Change Canada");
}

Attribution EcccAlertProvider::attribution() const
{
    // docs/02-data-sources.md §2.10. GeoMet is covered by ECCC's Data Server
    // End-use Licence, which asks for the originator by name and, where
    // possible, a link to the licence. The alerts are shown as issued and come
    // from one originator, so the line is the licence's own example rather
    // than its fallback for mixed sources. It is transcribed, not paraphrased.
    Attribution credit;
    credit.name        = QStringLiteral("Environment and Climate Change Canada");
    credit.creditLine  = QStringLiteral("Data Source: Environment and Climate Change Canada");
    credit.homepage    = QUrl(QStringLiteral("https://weather.gc.ca/"));
    credit.licenceName = QStringLiteral("ECCC Data Server End-use Licence");
    credit.licenceUrl =
        QUrl(QStringLiteral("https://eccc-msc.github.io/open-data/licence/readme_en/"));
    credit.note = QStringLiteral(
        "Public weather alerts via the GeoMet-Weather OGC API, shown as issued: the licence "
        "allows no change to an alert's content or intent. Alerts are issued for Canadian "
        "territory only; the authoritative presentation is weather.gc.ca.");
    return credit;
}

bool EcccAlertProvider::covers(Coordinate coord) const
{
    // The shared box from registry.h, which is loose and says so. A point in
    // Detroit is inside it, and that is deliberate: the honest answer for a
    // border city is "ask both", which is exactly what the alert fan-out in
    // ialertprovider.h does with it.
    return coord.isValid() && regionContains(Region::Canada, coord);
}

Capabilities EcccAlertProvider::capabilitiesAt(Coordinate coord) const
{
    if (!covers(coord))
        return {};

    // Never undetermined. Whether Canada issues alerts here is not a fact that
    // has to be learned from a payload the way pollen coverage is - the service
    // covers its own territory, and an empty answer means no alerts rather than
    // no product.
    return Capabilities(Capability::Alerts);
}

// ---- fetching ----------------------------------------------------------------------

QFuture<Result<AlertSet>> EcccAlertProvider::fetchAlerts(const AlertRequest &request)
{
    HttpRequest http;
    http.providerId = id();
    http.endpoint   = QStringLiteral("alerts");
    http.url        = m_baseUrl;
    http.kind       = DataKind::Alerts;
    http.coordinate = request.coord;

    // A zero-area bounding box, not the CQL2 spatial filter. The header records
    // the measurement that decided it and why Part 1 beats Part 3 here.
    http.coordinateForm      = CoordinateForm::DegenerateBoundingBox;
    http.coordinateParameter = QStringLiteral("bbox");

    http.parameters = { { QStringLiteral("f"), QStringLiteral("json") } };

    // The language is NOT in the request. One payload carries both languages -
    // `alert_text_en` beside `alert_text_fr` - so the bytes are the same
    // whichever was asked for, and keeping the language out of the key means
    // switching it costs nothing and shares a cache entry. It is applied at
    // parse time instead.
    const QString key = RequestKey::forRequest(http).toString();

    const payloadcache::Hit cached = payloadcache::lookUp(m_cache, key);
    const QString language = request.language;
    const Coordinate coord = request.coord;

    // §4.5 gives alerts staleWhileRevalidate = false, which is why a stale
    // entry is not served here the way a stale forecast is. It is still served
    // on the failure path below, and that is not a contradiction: an alert we
    // cannot refresh is one the user should keep seeing with a "last confirmed"
    // line, and an alert whose hazard has ended is filtered out by
    // Alert::phaseAt() against the wall clock no matter how it got here. A
    // stale cache cannot resurrect an ended warning.
    if (cached.present && cached.fresh) {
        Result<AlertSet> adapted = parse(cached.payload, cached.fetchedAt, language);
        if (adapted.hasValue()) {
            adapted.value().providerId  = id();
            adapted.value().coordinate  = coord;
            adapted.value().confirmedAt = cached.fetchedAt;
            return QtFuture::makeReadyValueFuture(adapted);
        }
    }

    if (request.cachedOnly) {
        if (cached.present) {
            Result<AlertSet> stale = parse(cached.payload, cached.fetchedAt, language);
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
                         [this, key, cached, coord, language](const Result<HttpResponse> &result)
                             -> Result<AlertSet> {
        if (!result.hasValue()) {
            // The refresh failed and we have something. Serve it, stamped with
            // when it was last CONFIRMED rather than with now - that stamp is
            // what the banner's "last confirmed 14:05" reads, and it is the
            // difference between silently keeping an alert and saying so.
            if (cached.present) {
                Result<AlertSet> stale = parse(cached.payload, cached.fetchedAt, language);
                if (stale.hasValue()) {
                    stale.value().providerId  = id();
                    stale.value().coordinate  = coord;
                    stale.value().confirmedAt = cached.fetchedAt;
                    return stale;
                }
            }
            return result.error();
        }

        const HttpResponse &response = result.value();

        // GeoMet sends no validator, so this branch is unreachable against the
        // real service today and is kept because HttpClient may send a
        // conditional request the day it starts sending one. Re-parsing the
        // cached bytes rather than memoising a parsed value is affordable here
        // in a way it is not for a 400-hour forecast: an alert payload is a
        // handful of features.
        if (response.notModified) {
            payloadcache::touch(m_cache, key, DataKind::Alerts, response);
            if (cached.present) {
                Result<AlertSet> current = parse(cached.payload, cached.fetchedAt, language);
                if (current.hasValue()) {
                    current.value().providerId  = id();
                    current.value().coordinate  = coord;
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

        Result<AlertSet> parsed = parse(response.body, response.fetchedAt, language);
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

Result<AlertSet> EcccAlertProvider::parse(const QByteArray &body, const QDateTime &fetchedAt,
                                          const QString &language)
{
    QJsonParseError     parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (document.isNull() || !document.isObject()) {
        return Error(ErrorKind::Parse,
                     QStringLiteral("alert payload is not a JSON object: %1")
                         .arg(parseError.errorString()));
    }

    const QJsonObject root = document.object();

    // GeoMet reports a query problem in-band with an OGC exception object and a
    // 4xx/5xx status. Worth reading rather than reporting "no features": the
    // description names what it disliked, and that is a bug in this file rather
    // than an outage. This is the shape the CQL2 experiment in the header came
    // back as.
    if (root.contains(QStringLiteral("code")) && !root.contains(QStringLiteral("features"))) {
        return Error(ErrorKind::Parse,
                     QStringLiteral("GeoMet refused the query: %1 - %2")
                         .arg(root.value(QStringLiteral("code")).toString(),
                              root.value(QStringLiteral("description")).toString()));
    }

    const QJsonValue features = root.value(QStringLiteral("features"));
    if (!features.isArray()) {
        return Error(ErrorKind::Parse,
                     QStringLiteral("alert payload carries no feature collection"));
    }

    // A short language code - "fr" from "fr-CA" - because the field suffixes are
    // two letters.
    const QString suffix = language.left(2).toLower();

    AlertSet set;
    set.fetchedAt = fetchedAt;

    const QJsonArray array = features.toArray();
    for (const QJsonValue &entry : array) {
        const QJsonObject feature    = entry.toObject();
        const QJsonObject properties = feature.value(QStringLiteral("properties")).toObject();
        if (properties.isEmpty())
            continue;

        // The clause that used to be server-side. See the header: it moved here
        // when the CQL2 filter did, and it is one comparison over a handful of
        // features. An ended alert is dropped at the source rather than left to
        // the phase filter, because ECCC keeps serving it for a while after it
        // stops and there is nothing to be gained by carrying it further.
        const QString statusEnglish = properties.value(QStringLiteral("status_en")).toString();
        if (statusEnglish.trimmed().compare(QLatin1String("ended"), Qt::CaseInsensitive) == 0)
            continue;

        Alert alert;
        alert.providerId = QString::fromLatin1(kProviderId);
        alert.id         = feature.value(QStringLiteral("id")).toString();

        alert.event           = text(properties, QStringLiteral("alert_name"), suffix);
        alert.description     = text(properties, QStringLiteral("alert_text"), suffix);
        alert.areaDescription = text(properties, QStringLiteral("feature_name"), suffix);

        // ECCC publishes no headline and no separate instruction paragraph -
        // the remarks are inside alert_text. Left empty rather than manufactured
        // from the other fields; alert.h says an empty headline means the view
        // shows `event`, which is the right thing to show.
        alert.senderName = suffix == QLatin1String("fr")
            ? QStringLiteral("Environnement et Changement climatique Canada")
            : QStringLiteral("Environment and Climate Change Canada");

        const QString colourEnglish = properties.value(QStringLiteral("risk_colour_en")).toString();
        const QString typeEnglish   = properties.value(QStringLiteral("alert_type")).toString();

        alert.severity  = severityFromColour(colourEnglish);
        alert.urgency   = urgencyFromType(typeEnglish);
        alert.certainty = certaintyFromConfidence(
            properties.value(QStringLiteral("confidence_en")).toString());
        alert.messageType = messageTypeFromStatus(statusEnglish);

        // "yellow warning" / "jaune avertissement". The reader's own vocabulary,
        // in the reader's own language, beside the CAP grade the app sorts with.
        const QString colourLocal = text(properties, QStringLiteral("risk_colour"), suffix);
        if (!colourLocal.isEmpty() && !typeEnglish.isEmpty())
            alert.issuerLabel = colourLocal + QLatin1Char(' ') + typeEnglish;
        else
            alert.issuerLabel = colourLocal.isEmpty() ? typeEnglish : colourLocal;

        alert.sent      = instant(properties.value(QStringLiteral("publication_datetime")));
        alert.effective = instant(properties.value(QStringLiteral("validity_datetime")));

        // No onset. ECCC states when the message takes effect and when the event
        // ends, and does not state when the weather starts - so `onset` stays
        // invalid rather than being aliased to `effective`, which would make
        // every Canadian alert permanently "Active" by definition and hide the
        // Pending phase behind an assumption.
        alert.expires = instant(properties.value(QStringLiteral("expiration_datetime")));
        alert.ends    = instant(properties.value(QStringLiteral("event_end_datetime")));

        // `web` is deliberately empty. The payload carries no per-alert URL, and
        // the two obvious provincial pages to construct one from -
        // weather.gc.ca/warnings/index_e.html?prov=ns and its /en/ variant -
        // both answer 404, verified. A dead link is worse than no link.

        // Identity: (code, feature), not the message id, which embeds an issue
        // timestamp and therefore changes every time the alert is continued.
        const QString code    = properties.value(QStringLiteral("alert_code")).toString();
        const QString feature_ = properties.value(QStringLiteral("feature_id")).toString();
        if (!code.isEmpty() && !feature_.isEmpty()) {
            alert.identityKeys = { QString::fromLatin1(kProviderId) + QLatin1Char(':') + code
                                   + QLatin1Char(':') + feature_ };
        } else if (!alert.id.isEmpty()) {
            // Nothing better. The message id at least makes the alert identical
            // to itself, which is what alert.h promises identityKeys is never
            // empty for.
            alert.identityKeys = { QString::fromLatin1(kProviderId) + QLatin1Char(':')
                                   + alert.id };
        }

        if (alert.isValid())
            set.alerts.append(alert);
    }

    return set;
}

} // namespace climat
