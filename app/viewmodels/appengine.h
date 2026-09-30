// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Where libclimat is assembled, and where the offline-first loop lives.
//
// ============================================================================
// THE LOOP, WHICH IS THE WHOLE FILE
//
// docs/04-architecture.md §4.1 principle 1: "the UI renders from cache, then
// reconciles with the network. The app must never show an empty screen because
// an API is down - the documented failure mode of the current best Linux
// weather app."
//
// Three steps, in this order, every time a place is selected:
//
//   1. ask for a CACHED answer only. No socket is opened. If there is one, it
//      is published immediately - inside the same call stack, before the first
//      frame - carrying the timestamp it was originally fetched at, so the UI
//      says "updated 40 minutes ago" and means it.
//
//   2. ask again, normally. The provider revalidates, or serves from a fresh
//      cache entry without asking, or falls through to MET Norway.
//
//   3. publish whatever came back. A failure at this step does NOT clear what
//      step 1 published: the screen keeps the stale forecast and gains a line
//      saying the refresh failed. That is the sentence in §4.1 made into
//      control flow - there is no path here that replaces data with a spinner.
//
// Step 1 is why ForecastRequest has a `cachedOnly` flag. Without it the choice
// is between a synchronous cache read the view model does not have access to,
// and a second cache in app/ holding a second copy of the same forecast in some
// other shape - and two caches is how the two disagree.
//
// ============================================================================
// WHAT MAKES A RUN DETERMINISTIC
//
// One decision, taken in configure(): which Clock, and which providers.
//
//     live      SystemClock + Open-Meteo → MET Norway + Open-Meteo air quality
//     fixture   FrozenClock at the recording's instant + FixtureForecastProvider
//
// Nothing downstream of that line branches on which one happened. The TTL
// table, the backoff, the "now" marker, the past veil, the sky phase and the
// "updated N minutes ago" line all read the clock they were handed and behave
// identically - which is the argument libclimat/core/clock.h makes at length and
// this class is the place it pays off.
//
// Fixture is the DEFAULT under `--grab`, under `--film`, in climat-gallery and
// in CI. Not because those are tests but because those are the four situations
// where a picture is going to be compared against another picture.
//
// ============================================================================
// ONE ENGINE, THREE SINGLETONS
//
// QML sees `Engine`, `Data` and `Detail`. They are three views of one snapshot
// and not three sources: this class owns the Forecast and the AirQuality, and
// the other two are formatters over them that this class pushes into. A
// component reading `Data.temperature[i]` and one reading
// `Detail.temperature.value` are reading the same number through two shapes,
// which is the property the prototype's mockdata.js and detaildata.js had to
// maintain by hand - and got wrong twice, which is what the comments at the top
// of both files are about.

#pragma once

#include "libclimat/domain/airquality.h"
#include "libclimat/domain/forecast.h"
#include "libclimat/domain/place.h"
#include "libclimat/providers/fixture/fixtureprovider.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <memory>

class AlertsData;
class Notifier;
class ConditionsData;
class ForecastData;

namespace climat {
class CacheStore;
class Clock;
class DeviceLocator;
class EcccAlertProvider;
class FixtureAirQualityProvider;
class FixtureAlertProvider;
class FixtureForecastProvider;
class NwsAlertProvider;
class HttpClient;
class LocationController;
class MetNoForecastProvider;
class OfflineReverseGeocoder;
class OpenMeteoAirQualityProvider;
class OpenMeteoForecastProvider;
class OpenMeteoGeocoder;
class PlaceSearchModel;
class ProviderRegistry;
} // namespace climat

class AppEngine : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Engine)
    QML_SINGLETON

    // ---- where -------------------------------------------------------------
    Q_PROPERTY(QObject *places READ placesModel CONSTANT)
    Q_PROPERTY(QObject *search READ searchModel CONSTANT)
    Q_PROPERTY(QString placeLabel READ placeLabel NOTIFY placeChanged)
    Q_PROPERTY(QString placeRegion READ placeRegion NOTIFY placeChanged)
    Q_PROPERTY(bool placeIsHome READ placeIsHome NOTIFY placeChanged)
    Q_PROPERTY(bool hasPlace READ hasPlace NOTIFY placeChanged)

    // ---- how it is going ---------------------------------------------------
    Q_PROPERTY(bool hasData READ hasData NOTIFY forecastChanged)
    Q_PROPERTY(bool loading READ isLoading NOTIFY loadingChanged)

    // ---- what time it is there ---------------------------------------------
    //
    // "10:31 PM" - the wall clock at the *place*, not at this machine. MSN's
    // overview card carries exactly this and nothing else beside it; fetched
    // for Seattle and Toronto in the same second it reads 7:31 PM and 10:31 PM,
    // so it is the location's clock rather than the reader's.
    //
    // It used to be `Detail.observedAt`, the observation's own timestamp, which
    // is a defensible thing to show and is not what anybody reads it as. A time
    // on a weather screen is taken for the time, and this one sat at whatever
    // quarter-hour the provider last stamped and never moved again - a clock
    // that has stopped, which is worse than no clock at all.
    //
    // Empty until a place with a zone is known, and the line it belongs to drops
    // empty fields, so nothing shows rather than the wrong city's evening.
    Q_PROPERTY(QString localTime READ localTime NOTIFY freshnessChanged)

    // "Updated 12 minutes ago". The §4.5 affordance, and the reason
    // `Forecast::fetchedAt` is a field rather than a detail.
    Q_PROPERTY(QString updatedLabel READ updatedLabel NOTIFY freshnessChanged)

    // True once the data on screen is older than its TTL. Not an error state:
    // a stale forecast is the right thing to be showing, it just has to say so.
    Q_PROPERTY(bool stale READ isStale NOTIFY freshnessChanged)

    // Empty on the common path. A sentence when the last refresh failed, shown
    // *beside* the data rather than instead of it.
    Q_PROPERTY(QString problem READ problem NOTIFY forecastChanged)

    // ---- who answered ------------------------------------------------------
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY forecastChanged)
    Q_PROPERTY(bool fromFallback READ isFromFallback NOTIFY forecastChanged)

    // Whether a desktop notification can be posted at all: this build has Qt
    // D-Bus and there is a session bus. CONSTANT because neither answer
    // changes while the process runs. The preferences row is hidden when it is
    // false - a switch that cannot do anything teaches the reader that the
    // preferences lie.
    Q_PROPERTY(bool notificationsAvailable READ notificationsAvailable CONSTANT)

    // ---- what this build is doing ------------------------------------------
    Q_PROPERTY(bool fixtureMode READ isFixtureMode CONSTANT)
    Q_PROPERTY(QString fixtureName READ fixtureName CONSTANT)

    // Every registered provider's credit, generated. docs/08-risks.md R12: the
    // About screen is built from the registry so it cannot go stale.
    Q_PROPERTY(QVariantList sources READ sources CONSTANT)

    // The project's page, from CMakeLists.txt's HOMEPAGE_URL, so the About
    // group's links are the same URL every other file carries.
    Q_PROPERTY(QString homepage READ homepage CONSTANT)

    // ---- what may be drawn here --------------------------------------------
    //
    // Three-valued underneath (see iforecastprovider.h) and collapsed to two
    // here on purpose: a card asks "do I draw", and "not yet" and "no" are both
    // "not now". What the third value buys is that `pollenKnownAbsent` below is
    // false while the answer is unknown, so nothing pops in.
    Q_PROPERTY(bool hasPollen READ hasPollen NOTIFY forecastChanged)
    Q_PROPERTY(bool pollenUndetermined READ isPollenUndetermined NOTIFY forecastChanged)
    Q_PROPERTY(bool hasAirQuality READ hasAirQuality NOTIFY forecastChanged)

public:
    ~AppEngine() override;

    static AppEngine *instance();
    static AppEngine *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // Drops everything that needs a live QCoreApplication to be released
    // cleanly - the cache's SQLite connection above all. Registered as a
    // qAddPostRoutine from instance(), so it runs inside ~QCoreApplication
    // rather than during static destruction, which is far too late. Idempotent
    // and safe to call again from ~AppEngine.
    void releaseQtResources();

    // Builds the clock, the cache, the client, the providers and the models,
    // then publishes the first snapshot from cache. Call once from main()
    // before the QML engine loads anything, because Main.qml's first frame
    // reads Data and Detail.
    //
    // `fixtureName` empty means live. An unknown name is a hard error rather
    // than a fallback to live: a CI job that asked for a fixture and silently
    // got the internet is a CI job whose golden images drift.
    void configure(const QString &fixtureName);

    // Resolves a query through the geocoder and selects the first result. For
    // `--place`, which exists so that a headless capture can be taken of
    // somewhere other than the saved place. Blocks the calling thread on the
    // geocoder's future, which is acceptable in exactly one situation - before
    // the window exists - and nowhere else.
    void selectByQuery(const QString &query, int timeoutMs = 15000);

    [[nodiscard]] QObject *placesModel() const;
    [[nodiscard]] QObject *searchModel() const;

    [[nodiscard]] QString placeLabel() const;
    [[nodiscard]] QString placeRegion() const;
    [[nodiscard]] bool    placeIsHome() const;
    [[nodiscard]] bool    hasPlace() const;

    [[nodiscard]] bool    hasData() const;
    [[nodiscard]] bool    isLoading() const { return m_inFlight > 0; }
    [[nodiscard]] QString localTime() const;
    [[nodiscard]] QString updatedLabel() const;
    [[nodiscard]] bool    isStale() const;
    [[nodiscard]] QString problem() const { return m_problem; }
    [[nodiscard]] QString sourceName() const { return m_sourceName; }
    [[nodiscard]] bool    isFromFallback() const { return m_fromFallback; }

    [[nodiscard]] bool isFixtureMode() const { return m_fixture.isValid(); }
    [[nodiscard]] QString fixtureName() const { return m_fixture.name; }
    [[nodiscard]] QString homepage() const;

    [[nodiscard]] QVariantList sources() const;

    [[nodiscard]] bool hasPollen() const;
    [[nodiscard]] bool isPollenUndetermined() const;
    [[nodiscard]] bool hasAirQuality() const;

    // ---- what the UI calls -------------------------------------------------

    Q_INVOKABLE void refresh();

    // Row in the saved-places model.
    Q_INVOKABLE void selectPlace(int row);

    // Row in the *search* model. Saves it if it is new, selects it either way,
    // and clears the query - which is what "tapping a search result" means.
    Q_INVOKABLE void chooseSearchResult(int row);

    Q_INVOKABLE void removePlace(int row);
    Q_INVOKABLE void toggleHome(int row);

    // Asks the platform where we are, then names the coordinate with the
    // bundled offline index. Both halves can fail and both say so through
    // `locationFailed`.
    Q_INVOKABLE void useMyLocation();

    [[nodiscard]] Q_INVOKABLE bool locationAvailable() const;

    [[nodiscard]] bool notificationsAvailable() const;

    // For the C++ side of the app: the current snapshot, canonical units.
    [[nodiscard]] const climat::Forecast   &forecast() const { return m_forecast; }
    [[nodiscard]] const climat::AirQuality &airQuality() const { return m_airQuality; }
    [[nodiscard]] climat::Place             place() const;
    [[nodiscard]] climat::Clock            *clock() const { return m_clock.get(); }

    [[nodiscard]] ForecastData   *forecastData() const { return m_forecastData; }
    [[nodiscard]] ConditionsData *conditionsData() const { return m_conditionsData; }

Q_SIGNALS:
    void placeChanged();
    void forecastChanged();
    void loadingChanged();

    // Everything whose answer changes because time passed rather than because
    // data arrived: the clock at the place, how long ago the forecast was
    // fetched, and whether that has aged past its TTL.
    //
    // A signal of its own because the alternative is re-emitting
    // forecastChanged() once a minute, and forecastChanged() means "there is new
    // weather" - every view model rebuilds its whole snapshot on it. Wiring the
    // minute timer to it would rebuild twelve detail cards to move one colon.
    void freshnessChanged();

    // "Location services are unavailable", "Permission denied". A sentence for
    // a human; the picker shows it inline rather than in a dialog.
    void locationFailed(const QString &reason);

private:
    AppEngine();

    void buildLiveProviders();
    void buildFixtureProviders();
    void registerProviders();

    // The three steps of the loop. `load` runs step 1 and then step 2.
    void load();
    void fetch(bool cachedOnly);

    // Re-arms `m_minute` on the next minute boundary. Single-shot and re-armed
    // rather than periodic, because a 60 s repeating timer started at :17 fires
    // at :17 for ever and the displayed minute changes 17 s late.
    void armMinuteTimer();

    void applyForecast(const climat::Forecast &forecast, const QString &servedBy, bool fromFallback);
    void applyAirQuality(const climat::AirQuality &airQuality);
    void publish();

    void setInFlight(int delta);

    std::unique_ptr<climat::Clock>       m_clock;

    // Never started under --fixture, where the clock is frozen and a tick would
    // publish the same reading for ever.
    //
    // Which is also what keeps a capture still, though not directly: --grab and
    // --film default to a fixture, so the timer is not running in any capture
    // this repository takes. A capture that has explicitly asked for live data
    // - --place, or --fixture off - does tick, and appoptions.cpp has already
    // said in so many words that such a capture is not reproducible.
    QTimer                              m_minute;
    std::unique_ptr<climat::CacheStore>  m_cache;
    std::unique_ptr<climat::HttpClient>  m_http;
    std::unique_ptr<climat::ProviderRegistry> m_registry;

    // Live. Null in fixture mode, and vice versa: the two sets are never both
    // registered, because a chain containing a fixture and a network provider
    // would fall through from one to the other and produce a screen that is
    // half recorded and half not.
    climat::OpenMeteoForecastProvider   *m_openMeteo   = nullptr;
    climat::MetNoForecastProvider       *m_metNo       = nullptr;
    climat::OpenMeteoAirQualityProvider *m_openMeteoAq = nullptr;

    climat::EcccAlertProvider *m_eccc = nullptr;
    climat::NwsAlertProvider  *m_nws  = nullptr;

    climat::FixtureForecastProvider   *m_fixtureForecast = nullptr;
    climat::FixtureAirQualityProvider *m_fixtureAq       = nullptr;
    climat::FixtureAlertProvider      *m_fixtureAlerts   = nullptr;

    climat::OpenMeteoGeocoder      *m_geocoder = nullptr;
    climat::OfflineReverseGeocoder *m_reverse  = nullptr;
    climat::LocationController     *m_places   = nullptr;
    climat::PlaceSearchModel       *m_search   = nullptr;
    climat::DeviceLocator          *m_locator  = nullptr;

    climat::Fixture m_fixture;

    climat::Forecast   m_forecast;
    climat::AirQuality m_airQuality;

    QString m_sourceName;
    QString m_problem;
    bool    m_fromFallback = false;
    int     m_inFlight     = 0;

    // Bumped on every place change. An answer carrying an older number is an
    // answer about somewhere the user has left, and is dropped - the same rule
    // PlaceSearchModel applies to out-of-order geocoder replies, for the same
    // reason.
    quint64 m_generation = 0;

    ForecastData   *m_forecastData   = nullptr;
    ConditionsData *m_conditionsData = nullptr;

    // The fourth singleton QML sees. Not a formatter over the forecast the way
    // the other two are - it is a view of a different product, on a different
    // schedule, which is why it owns its own poll timer rather than riding this
    // class's refresh.
    AlertsData *m_alerts = nullptr;

    // One per process, handed to AlertsData, which decides what is worth
    // interrupting somebody for. Parented to this; nothing else posts.
    Notifier *m_notifier = nullptr;
};
