// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: MPL-2.0

#include "libclimat/providers/fixture/fixtureprovider.h"

#include "libclimat/providers/airquality/openmeteoairqualityprovider.h"
#include "libclimat/providers/eccc/ecccalertprovider.h"
#include "libclimat/providers/nws/nwsalertprovider.h"
#include "libclimat/providers/openmeteo/openmeteoadapter.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1String>
#include <QPromise>

namespace climat {
namespace {

constexpr char kForecastId[]   = "fixture";
constexpr char kAirQualityId[] = "fixture-air-quality";
constexpr char kAlertId[]      = "fixture-alerts";
constexpr char kRoot[]         = ":/climat/fixtures";

QByteArray readResource(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// The credit belongs to whoever produced the numbers, which is Open-Meteo - the
// recording changed nothing about them. What the note adds is the one fact a
// credit line cannot carry: that this is a photograph and of what moment.
Attribution recordedCredit(const Attribution &original, const Fixture &fixture)
{
    Attribution credit = original;
    credit.note = QStringLiteral("Recorded response, replayed offline. Captured %1 for %2. "
                                 "The app's clock is frozen at that instant, so every time on "
                                 "screen is that afternoon's. ")
                      .arg(fixture.recordedAt.toString(Qt::ISODate), fixture.place.label())
                  + original.note;
    return credit;
}

Place placeFromJson(const QJsonObject &object)
{
    Place place;
    place.geonamesId  = qint64(object.value(QStringLiteral("geonamesId")).toDouble());
    place.name        = object.value(QStringLiteral("name")).toString();
    place.admin1      = object.value(QStringLiteral("admin1")).toString();
    place.country     = object.value(QStringLiteral("country")).toString();
    place.countryCode = object.value(QStringLiteral("countryCode")).toString();
    place.timezone    = object.value(QStringLiteral("timezone")).toString();
    place.coordinate  = Coordinate{ object.value(QStringLiteral("latitude")).toDouble(),
                                    object.value(QStringLiteral("longitude")).toDouble() };
    place.isHome      = true;
    return place;
}

} // namespace

// ---- the catalogue -------------------------------------------------------------

namespace fixtures {

QStringList names()
{
    // QDir over a resource prefix, sorted. A hard-coded list here would be a
    // fourth place to keep in step with the CMake FILES list, the directory on
    // disk and REUSE.toml - and the one that fails as a runtime "unknown
    // fixture" rather than as a build error.
    QDir dir(QString::fromLatin1(kRoot));
    return dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

bool exists(const QString &name)
{
    return !name.isEmpty() && names().contains(name);
}

QString defaultName()
{
    return QStringLiteral("toronto");
}

Fixture load(const QString &name)
{
    if (!exists(name))
        return {};

    const QString base = QString::fromLatin1(kRoot) + QLatin1Char('/') + name;

    const QJsonObject manifest =
        QJsonDocument::fromJson(readResource(base + QStringLiteral("/fixture.json"))).object();
    if (manifest.isEmpty())
        return {};

    Fixture fixture;
    fixture.name        = name;
    fixture.description = manifest.value(QStringLiteral("description")).toString();
    fixture.place       = placeFromJson(manifest.value(QStringLiteral("place")).toObject());

    // UTC, insisted on rather than assumed: a manifest that said
    // "2026-07-30T12:28" would be parsed as local time and the whole fixture
    // would drift by the developer's own offset, which is precisely the class
    // of bug the frozen clock exists to remove.
    fixture.recordedAt = QDateTime::fromString(
        manifest.value(QStringLiteral("recordedAt")).toString(), Qt::ISODate);
    fixture.recordedAt.setTimeZone(QTimeZone::UTC);

    fixture.forecast   = readResource(base + QStringLiteral("/forecast.json"));
    fixture.airQuality = readResource(base + QStringLiteral("/airquality.json"));

    fixture.alerts = readResource(base + QStringLiteral("/alerts.json"));
    fixture.alertProviderId = manifest.value(QStringLiteral("alertProvider")).toString();

    // Bytes without a service named to parse them is a manifest mistake, and a
    // silent one: the alert provider would cover the place and answer nothing,
    // which looks exactly like a quiet afternoon. Dropping the bytes makes the
    // fixture behave as though it carries no alerts, which is at least a state
    // the rest of the code has a name for.
    if (fixture.alertProviderId.isEmpty())
        fixture.alerts.clear();

    return fixture;
}

} // namespace fixtures

// ---- forecast --------------------------------------------------------------------

FixtureForecastProvider::FixtureForecastProvider(Fixture fixture, QObject *parent)
    : QObject(parent)
    , m_fixture(std::move(fixture))
{
    // Learned once, at construction, from the payload this provider will
    // always answer with. There is nothing here that a later fetch could
    // teach - which is why this provider never reports anything undetermined.
    const Result<Forecast> parsed = openmeteo::adaptForecast(m_fixture.forecast, id());
    if (parsed.hasValue())
        m_capabilities = openmeteo::capabilitiesFor(parsed.value());
}

FixtureForecastProvider::~FixtureForecastProvider() = default;

QString FixtureForecastProvider::providerId()
{
    return QString::fromLatin1(kForecastId);
}

QString FixtureForecastProvider::id() const
{
    return providerId();
}

QString FixtureForecastProvider::displayName() const
{
    // Names the source, then says it is a recording. The order is deliberate:
    // the reader's question is "where did this come from", and "Open-Meteo" is
    // the answer to it. That it arrived from a file is the qualifier.
    return QStringLiteral("Open-Meteo (recorded ") + m_fixture.name + QLatin1Char(')');
}

Attribution FixtureForecastProvider::attribution() const
{
    return recordedCredit(openmeteo::attribution(), m_fixture);
}

bool FixtureForecastProvider::covers(Coordinate coord) const
{
    // Everywhere, and this is not laziness. A fixture answers with the place it
    // recorded whatever it is asked about, because the alternative - refusing
    // any coordinate but its own - means `--fixture toronto` on a machine whose
    // saved place is Berlin comes up with an empty chain and a blank screen,
    // which is the one outcome the whole offline-first design exists to
    // prevent. The recorded place is what the app selects on start; a request
    // for anywhere else is a reviewer poking at the picker, and answering with
    // Toronto's weather labelled Toronto is honest, if unhelpful.
    return coord.isValid();
}

Capabilities FixtureForecastProvider::capabilitiesAt(Coordinate coord) const
{
    if (!coord.isValid())
        return {};
    return m_capabilities;
}

QFuture<Result<Forecast>> FixtureForecastProvider::fetchForecast(const ForecastRequest &request)
{
    Q_UNUSED(request)

    QPromise<Result<Forecast>> promise;
    promise.start();

    Result<Forecast> adapted = openmeteo::adaptForecast(m_fixture.forecast, id());
    if (adapted.hasValue()) {
        // The recording's own moment, not the clock's. They are the same number
        // when the clock is the FrozenClock this fixture is meant to be paired
        // with - and when they are not, the honest answer is still the moment
        // the bytes were captured, because that is the age of the data.
        adapted.value().fetchedAt = m_fixture.recordedAt;
    }

    promise.addResult(adapted);
    promise.finish();
    return promise.future();
}

// ---- air quality -----------------------------------------------------------------

FixtureAirQualityProvider::FixtureAirQualityProvider(Fixture fixture, QObject *parent)
    : QObject(parent)
    , m_fixture(std::move(fixture))
{
    const CapabilityFlags always = Capability::CurrentConditions | Capability::Hourly
        | Capability::AirQualityIndex | Capability::Pollutants | Capability::UvIndex;

    const Result<AirQuality> parsed =
        OpenMeteoAirQualityProvider::parse(m_fixture.airQuality, m_fixture.recordedAt);
    if (!parsed.hasValue()) {
        // No air-quality payload in this fixture. Known-absent, not
        // undetermined: there is no later fetch that could change the answer,
        // and a card that waits forever for a payload that is never coming is
        // worse than one that is simply not there.
        m_capabilities = {};
        return;
    }

    CapabilityFlags available = always;
    if (parsed.value().hasPollen)
        available |= Capability::Pollen;
    if (parsed.value().hasAmmonia)
        available |= Capability::Ammonia;
    m_capabilities = Capabilities(available);
}

FixtureAirQualityProvider::~FixtureAirQualityProvider() = default;

QString FixtureAirQualityProvider::providerId()
{
    return QString::fromLatin1(kAirQualityId);
}

QString FixtureAirQualityProvider::id() const
{
    return providerId();
}

QString FixtureAirQualityProvider::displayName() const
{
    return QStringLiteral("Open-Meteo Air Quality (recorded ") + m_fixture.name + QLatin1Char(')');
}

Attribution FixtureAirQualityProvider::attribution() const
{
    Attribution original;
    original.name        = QStringLiteral("Open-Meteo");
    original.creditLine  = QStringLiteral("Weather data by Open-Meteo.com");
    original.homepage    = QUrl(QStringLiteral("https://open-meteo.com/"));
    original.licenceName = QStringLiteral("CC BY 4.0");
    original.licenceUrl  = QUrl(QStringLiteral("https://creativecommons.org/licenses/by/4.0/"));
    original.upstream    = { QStringLiteral("Copernicus Atmosphere Monitoring Service (CAMS)"),
                             QStringLiteral("ECMWF") };
    return recordedCredit(original, m_fixture);
}

bool FixtureAirQualityProvider::covers(Coordinate coord) const
{
    return coord.isValid() && !m_fixture.airQuality.isEmpty();
}

Capabilities FixtureAirQualityProvider::capabilitiesAt(Coordinate coord) const
{
    if (!coord.isValid())
        return {};
    return m_capabilities;
}

QFuture<Result<AirQuality>>
FixtureAirQualityProvider::fetchAirQuality(const ForecastRequest &request)
{
    Q_UNUSED(request)

    QPromise<Result<AirQuality>> promise;
    promise.start();

    if (m_fixture.airQuality.isEmpty()) {
        Error error(ErrorKind::Unsupported,
                    QStringLiteral("fixture \"%1\" carries no air-quality recording")
                        .arg(m_fixture.name));
        error.setProviderId(id());
        promise.addResult(Result<AirQuality>(error));
        promise.finish();
        return promise.future();
    }

    promise.addResult(
        OpenMeteoAirQualityProvider::parse(m_fixture.airQuality, m_fixture.recordedAt));
    promise.finish();
    return promise.future();
}

// ---- alerts ------------------------------------------------------------------------

FixtureAlertProvider::FixtureAlertProvider(Fixture fixture, QObject *parent)
    : QObject(parent)
    , m_fixture(std::move(fixture))
{
}

FixtureAlertProvider::~FixtureAlertProvider() = default;

QString FixtureAlertProvider::providerId()
{
    return QString::fromLatin1(kAlertId);
}

QString FixtureAlertProvider::id() const
{
    return providerId();
}

QString FixtureAlertProvider::displayName() const
{
    // Names the service that issued the alerts, then says it is a recording.
    // Same order and same reason as the forecast fixture's.
    const QString issuer = m_fixture.alertProviderId == QLatin1String("nws")
        ? QStringLiteral("National Weather Service")
        : QStringLiteral("Environment and Climate Change Canada");
    return issuer + QStringLiteral(" (recorded ") + m_fixture.name + QLatin1Char(')');
}

Attribution FixtureAlertProvider::attribution() const
{
    Attribution original;
    if (m_fixture.alertProviderId == QLatin1String("nws")) {
        original.name        = QStringLiteral("NOAA / National Weather Service");
        original.creditLine  = QStringLiteral("Alerts from the NOAA National Weather Service");
        original.homepage    = QUrl(QStringLiteral("https://www.weather.gov/"));
        original.licenceName = QStringLiteral("U.S. Public Domain (17 U.S.C. §105)");
        original.licenceUrl  = QUrl(QStringLiteral("https://www.weather.gov/disclaimer"));
    } else {
        original.name        = QStringLiteral("Environment and Climate Change Canada");
        original.creditLine  = QStringLiteral("Data Source: Environment and Climate Change Canada");
        original.homepage    = QUrl(QStringLiteral("https://weather.gc.ca/"));
        original.licenceName = QStringLiteral("ECCC Data Server End-use Licence");
        original.licenceUrl  = QUrl(
            QStringLiteral("https://eccc-msc.github.io/open-data/licence/readme_en/"));
    }
    return recordedCredit(original, m_fixture);
}

bool FixtureAlertProvider::covers(Coordinate coord) const
{
    return coord.isValid() && !m_fixture.alerts.isEmpty();
}

Capabilities FixtureAlertProvider::capabilitiesAt(Coordinate coord) const
{
    if (!covers(coord))
        return {};
    return Capabilities(Capability::Alerts);
}

QFuture<Result<AlertSet>> FixtureAlertProvider::fetchAlerts(const AlertRequest &request)
{
    QPromise<Result<AlertSet>> promise;
    promise.start();

    if (m_fixture.alerts.isEmpty()) {
        Error error(ErrorKind::Unsupported,
                    QStringLiteral("fixture \"%1\" carries no alert recording")
                        .arg(m_fixture.name));
        error.setProviderId(id());
        promise.addResult(Result<AlertSet>(error));
        promise.finish();
        return promise.future();
    }

    // Through the real parser, both of them. A fixture that handed back
    // pre-built Alerts would stop testing the severity mapping and the identity
    // keys, which are the two things in this feature that are invisible on
    // screen when they are wrong.
    Result<AlertSet> parsed = m_fixture.alertProviderId == QLatin1String("nws")
        ? NwsAlertProvider::parse(m_fixture.alerts, m_fixture.recordedAt)
        : EcccAlertProvider::parse(m_fixture.alerts, m_fixture.recordedAt, request.language);

    if (parsed.hasValue()) {
        parsed.value().providerId = id();
        parsed.value().coordinate = request.coord;

        // Confirmed at the moment of the recording, which is also `now` under
        // the FrozenClock. So a fixture run never shows "last confirmed" - it is
        // not out of date, it is a photograph of a moment.
        parsed.value().confirmedAt = m_fixture.recordedAt;
    }

    promise.addResult(parsed);
    promise.finish();
    return promise.future();
}

} // namespace climat
