// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Saved places: the round trip through SQLite, and the list model over it.
//
// The assertion that matters most in this file is the dullest one -
// `theAppOpensOnTheHomePlaceFromCache`. The app has a 400 ms cold-start budget
// and the first frame is drawn from the cached forecast, so the place name in
// the location bar has to be known before any network call completes. That is
// a synchronous SELECT over a table with a handful of rows, and the test is
// what stops it quietly becoming a signal somebody has to wait for.
//
// The rest is about the two invariants the schema enforces and the model must
// not fight: exactly one place is home (a partial unique index over is_home),
// and the same GeoNames id cannot be saved twice (a unique index over
// geonames_id).

#include "libclimat/cache/cachestore.h"
#include "libclimat/core/clock.h"
#include "libclimat/places/devicelocator.h"
#include "libclimat/places/locationcontroller.h"
#include "libclimat/providers/geocoding/offlinereversegeocoder.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace climat;

namespace {

Place place(const QString &name, qint64 geonamesId, double latitude, double longitude,
            const QString &admin1 = {}, const QString &countryCode = {})
{
    Place made;
    made.name = name;
    made.geonamesId = geonamesId;
    made.admin1 = admin1;
    made.countryCode = countryCode;
    made.coordinate = Coordinate{ latitude, longitude };
    return made;
}

Place toronto()
{
    Place made = place(QStringLiteral("Toronto"), 6167865, 43.70643, -79.39864,
                       QStringLiteral("Ontario"), QStringLiteral("CA"));
    made.country = QStringLiteral("Canada");
    made.timezone = QStringLiteral("America/Toronto");
    return made;
}

Place berlin()
{
    return place(QStringLiteral("Berlin"), 2950159, 52.5200066, 13.4049540,
                 QStringLiteral("Berlin"), QStringLiteral("DE"));
}

Place tromso()
{
    return place(QString::fromUtf8("Tromsø"), 3133895, 69.6492, 18.9553, QString(),
                 QStringLiteral("NO"));
}

} // namespace

class TestPlaces : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aPlaceRoundTripsThroughTheDatabase();
    void theFirstPlaceAddedBecomesHome();
    void addingTheSameGeonamesIdTwiceSelectsItInsteadOfDuplicatingIt();
    void addingTheSamePinTwiceIsAlsoOnePlace();

    void theAppOpensOnTheHomePlaceFromCache();
    void theAppReopensOnThePlaceItWasLastShowing();
    void aRememberedPlaceThatIsGoneFallsBackToHome();

    void settingHomeMovesItAndOnlyOnePlaceIsEverHome();
    void removingHomeHandsItOn();
    void removingTheCurrentPlaceShowsTheNextOne();

    void reorderingRewritesSortOrderAndSurvivesAReopen();

    void aDetectedPlaceAndASearchedPlaceCollapseOntoOneRow();

    void thereIsAlwaysALocatorAndItNeverThrows();
};

void TestPlaces::aPlaceRoundTripsThroughTheDatabase()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    Place saved = toronto();
    saved.isHome = true;
    saved.elevationMetres = 161.0;
    QVERIFY2(store.savePlace(saved).hasValue(), "savePlace");
    QVERIFY(saved.id != 0);

    const Result<QList<Place>> read = store.places();
    QVERIFY2(read.hasValue(), qPrintable(read.error().toString()));
    QCOMPARE(read.value().size(), 1);

    const Place &back = read.value().first();
    QCOMPARE(back.id, saved.id);
    QCOMPARE(back.geonamesId, Q_INT64_C(6167865));
    QCOMPARE(back.name, QStringLiteral("Toronto"));
    QCOMPARE(back.admin1, QStringLiteral("Ontario"));
    QCOMPARE(back.country, QStringLiteral("Canada"));
    QCOMPARE(back.countryCode, QStringLiteral("CA"));
    QCOMPARE(back.timezone, QStringLiteral("America/Toronto"));
    QVERIFY(back.isHome);

    // Full precision, and this is the point of storing the coordinate rather
    // than deriving it: requests round to four decimals, but what the user
    // chose is a different fact from what we ask for.
    QCOMPARE(back.coordinate.latitude, 43.70643);
    QCOMPARE(*back.elevationMetres, 161.0);
}

void TestPlaces::theFirstPlaceAddedBecomesHome()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());
    QCOMPARE(places.count(), 0);
    QCOMPARE(places.currentIndex(), -1);
    QVERIFY(places.currentLabel().isEmpty());

    QSignalSpy homeChanged(&places, &LocationController::homeChanged);

    QCOMPARE(places.addPlace(toronto()), 0);

    // An app whose only saved place is not its home place has a home nothing
    // points at.
    QVERIFY(places.placeAt(0).isHome);
    QCOMPARE(places.homeIndex(), 0);
    QCOMPARE(homeChanged.count(), 1);
    QCOMPARE(places.currentIndex(), 0);
    QCOMPARE(places.currentLabel(), QStringLiteral("Toronto, Ontario"));
    QVERIFY(places.currentIsHome());

    // The second is not home, because the user chose the first one by being
    // the first thing they did.
    QCOMPARE(places.addPlace(berlin()), 1);
    QVERIFY(!places.placeAt(1).isHome);
    QCOMPARE(places.homeIndex(), 0);
}

void TestPlaces::addingTheSameGeonamesIdTwiceSelectsItInsteadOfDuplicatingIt()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());

    QCOMPARE(places.addPlace(toronto()), 0);
    QCOMPARE(places.addPlace(berlin()), 1);
    QCOMPARE(places.currentIndex(), 1);

    // The same city, arriving with a different spelling and a coordinate from
    // a different snapshot of the dataset. It is still 6167865.
    Place drifted = toronto();
    drifted.name = QStringLiteral("Toronto City");
    drifted.coordinate = Coordinate{ 43.7001, -79.4163 };

    QCOMPARE(places.addPlace(drifted), 0);
    QCOMPARE(places.count(), 2);

    // And adding it selected it, because that is what a user pressing "add"
    // on a place they already have meant.
    QCOMPARE(places.currentIndex(), 0);

    // The stored row is untouched: reconciling a description is
    // OpenMeteoGeocoder::resolve()'s job, not a side effect of adding.
    QCOMPARE(places.placeAt(0).name, QStringLiteral("Toronto"));
}

void TestPlaces::addingTheSamePinTwiceIsAlsoOnePlace()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());

    // No GeoNames identity - a dropped map pin. Two pins eleven metres apart
    // round to the same coordinate, produce the same cache key and the same
    // URL, and are the same place by every operational definition the engine
    // has.
    Place pin = place(QStringLiteral("Somewhere"), 0, 45.123456, -75.123456);
    QCOMPARE(places.addPlace(pin), 0);

    Place almost = place(QStringLiteral("Somewhere else"), 0, 45.1234561, -75.1234559);
    QCOMPARE(places.addPlace(almost), 0);
    QCOMPARE(places.count(), 1);

    // A hundred metres away is a different pin.
    Place elsewhere = place(QStringLiteral("Actually elsewhere"), 0, 45.1244, -75.1244);
    QCOMPARE(places.addPlace(elsewhere), 1);
    QCOMPARE(places.count(), 2);
}

void TestPlaces::theAppOpensOnTheHomePlaceFromCache()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cache.sqlite"));

    FrozenClock clock;

    // A previous run: three places, home is the second.
    {
        CacheStore         store(&clock);
        QVERIFY(store.open(path).hasValue());
        LocationController places(&store);
        QVERIFY(places.load().hasValue());
        places.addPlace(toronto());
        places.addPlace(berlin());
        places.addPlace(tromso());
        QVERIFY(places.setHome(1));

        // Forget which one was being looked at, so that the fallback under
        // test is home rather than the remembered current.
        QVERIFY(store.setSetting(QStringLiteral("places.current"), QString()).hasValue());
    }

    // The cold start. One open, one load, and the place name is available on
    // the line after - no signal, no event loop turn, nothing to wait for.
    {
        CacheStore store(&clock);
        QVERIFY(store.open(path).hasValue());

        LocationController places(&store);
        const Status       status = places.load();
        QVERIFY2(status.hasValue(), qPrintable(status.error().toString()));

        QCOMPARE(places.count(), 3);
        QCOMPARE(places.currentIndex(), 1);
        // "Berlin" and not "Berlin, Berlin" - the city-state's admin1 repeats
        // its name, and Place::label() drops the repetition.
        QCOMPARE(places.currentLabel(), QStringLiteral("Berlin"));
        QVERIFY(places.currentIsHome());
    }
}

void TestPlaces::theAppReopensOnThePlaceItWasLastShowing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("cache.sqlite"));

    FrozenClock clock;

    {
        CacheStore         store(&clock);
        QVERIFY(store.open(path).hasValue());
        LocationController places(&store);
        QVERIFY(places.load().hasValue());
        places.addPlace(toronto());   // home, being first
        places.addPlace(berlin());
        places.addPlace(tromso());
        QVERIFY(places.setCurrentIndex(2));
    }

    {
        CacheStore         store(&clock);
        QVERIFY(store.open(path).hasValue());
        LocationController places(&store);
        QVERIFY(places.load().hasValue());

        // Where the user left off beats home. Home is the fallback, not the
        // policy - an app that jumped back to home every launch would lose the
        // place somebody has been watching all week.
        QCOMPARE(places.currentIndex(), 2);
        QCOMPARE(places.currentLabel(), QString::fromUtf8("Tromsø"));
        QVERIFY(!places.currentIsHome());
        QCOMPARE(places.homeIndex(), 0);
    }
}

void TestPlaces::aRememberedPlaceThatIsGoneFallsBackToHome()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());
    places.addPlace(toronto());
    places.addPlace(berlin());

    // A row id that never existed - a database restored from a backup, or a
    // place deleted by another process.
    QVERIFY(store.setSetting(QStringLiteral("places.current"), QStringLiteral("9999")).hasValue());

    LocationController reopened(&store);
    QVERIFY(reopened.load().hasValue());
    QCOMPARE(reopened.currentIndex(), reopened.homeIndex());
    QCOMPARE(reopened.currentIndex(), 0);
}

void TestPlaces::settingHomeMovesItAndOnlyOnePlaceIsEverHome()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());
    places.addPlace(toronto());
    places.addPlace(berlin());
    places.addPlace(tromso());

    QSignalSpy homeChanged(&places, &LocationController::homeChanged);

    QVERIFY(places.setHome(2));
    QCOMPARE(places.homeIndex(), 2);
    QCOMPARE(homeChanged.count(), 1);

    // Exactly one, checked against the database rather than against the model
    // - the partial unique index over is_home is what actually guarantees it,
    // and a model that got the write order wrong would have failed the write
    // rather than produced two homes.
    int homes = 0;
    QStringList stored;
    for (const Place &saved : store.places().value()) {
        homes += saved.isHome ? 1 : 0;
        stored << QStringLiteral("%1 #%2%3").arg(saved.name).arg(saved.id)
                      .arg(saved.isHome ? QStringLiteral(" home") : QString());
    }
    QStringList shown;
    for (int row = 0; row < places.count(); ++row)
        shown << QStringLiteral("%1 #%2").arg(places.placeAt(row).name).arg(places.placeAt(row).id);
    QVERIFY2(homes == 1, qPrintable(QStringLiteral("stored: %1; shown: %2")
                                        .arg(stored.join(QStringLiteral(", ")),
                                             shown.join(QStringLiteral(", ")))));

    // The home marker is a toggle with one legal direction: tapping it on a
    // place that is already home is a no-op rather than an app with no home.
    QVERIFY(places.toggleHome(2));
    QCOMPARE(places.homeIndex(), 2);
    QCOMPARE(homeChanged.count(), 1);

    QVERIFY(!places.setHome(7));
    QVERIFY(!places.setHome(-1));
}

void TestPlaces::removingHomeHandsItOn()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());
    places.addPlace(toronto());
    places.addPlace(berlin());

    QCOMPARE(places.homeIndex(), 0);
    QVERIFY(places.removeAt(0));

    // There is a home while there are places. Anything else leaves the next
    // cold start with nothing to fall back to.
    QCOMPARE(places.count(), 1);
    QCOMPARE(places.homeIndex(), 0);
    QCOMPARE(places.placeAt(0).name, QStringLiteral("Berlin"));

    // And it survives a reopen, which is what proves the flag was written and
    // not merely set in memory.
    LocationController reopened(&store);
    QVERIFY(reopened.load().hasValue());
    QCOMPARE(reopened.homeIndex(), 0);

    QVERIFY(reopened.removeAt(0));
    QCOMPARE(reopened.count(), 0);
    QCOMPARE(reopened.currentIndex(), -1);
    QCOMPARE(reopened.homeIndex(), -1);
}

void TestPlaces::removingTheCurrentPlaceShowsTheNextOne()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());
    places.addPlace(toronto());
    places.addPlace(berlin());
    places.addPlace(tromso());

    QVERIFY(places.setCurrentIndex(1));
    QSignalSpy currentChanged(&places, &LocationController::currentChanged);

    QVERIFY(places.removeAt(1));

    // The row after the removed one takes its index, so staying put shows the
    // next place rather than following the deleted one into nothing. The index
    // is unchanged and the place under it is not, which is exactly the case a
    // "did the index change?" guard would have swallowed.
    QCOMPARE(places.currentIndex(), 1);
    QCOMPARE(places.currentLabel(), QString::fromUtf8("Tromsø"));
    QVERIFY(currentChanged.count() >= 1);

    // Removing the last row moves the selection back rather than off the end.
    QVERIFY(places.setCurrentIndex(1));
    QVERIFY(places.removeAt(1));
    QCOMPARE(places.count(), 1);
    QCOMPARE(places.currentIndex(), 0);
}

void TestPlaces::reorderingRewritesSortOrderAndSurvivesAReopen()
{
    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());
    places.addPlace(toronto());
    places.addPlace(berlin());
    places.addPlace(tromso());
    QVERIFY(places.setCurrentIndex(0));

    // Drag the last one to the front.
    QVERIFY(places.move(2, 0));
    QCOMPARE(places.placeAt(0).name, QString::fromUtf8("Tromsø"));
    QCOMPARE(places.placeAt(1).name, QStringLiteral("Toronto"));
    QCOMPARE(places.placeAt(2).name, QStringLiteral("Berlin"));

    // The selection follows the place, not the index: the user was looking at
    // Toronto before the drag and is looking at Toronto after it.
    QCOMPARE(places.currentIndex(), 1);
    QCOMPARE(places.currentLabel(), QStringLiteral("Toronto, Ontario"));

    // Home followed too - it is a property of the row and not of the position.
    QCOMPARE(places.homeIndex(), 1);

    LocationController reopened(&store);
    QVERIFY(reopened.load().hasValue());
    QCOMPARE(reopened.placeAt(0).name, QString::fromUtf8("Tromsø"));
    QCOMPARE(reopened.placeAt(2).name, QStringLiteral("Berlin"));

    QVERIFY(!places.move(0, 0));
    QVERIFY(!places.move(0, 9));
}

void TestPlaces::aDetectedPlaceAndASearchedPlaceCollapseOntoOneRow()
{
    // The end-to-end version of the claim the offline index exists to make.
    // A place added by searching and the same place added by reverse-geocoding
    // a GPS fix are one row, because both are GeoNames and both carry 6167865.
    OfflineReverseGeocoder geocoder;
    QVERIFY(geocoder.load().hasValue());

    const Result<ReverseMatch> detected = geocoder.reverse(Coordinate{ 43.65, -79.38 });
    QVERIFY2(detected.hasValue(), qPrintable(detected.error().toString()));

    FrozenClock clock;
    CacheStore  store(&clock);
    QVERIFY(store.open(QStringLiteral(":memory:")).hasValue());

    LocationController places(&store);
    QVERIFY(places.load().hasValue());

    QCOMPARE(places.addPlace(toronto()), 0);
    QCOMPARE(places.addPlace(detected.value().place), 0);
    QCOMPARE(places.count(), 1);

    QCOMPARE(places.indexOfGeonamesId(6167865), 0);
    QCOMPARE(places.indexOfGeonamesId(2950159), -1);
    QCOMPARE(places.indexOfGeonamesId(0), -1);
}

void TestPlaces::thereIsAlwaysALocatorAndItNeverThrows()
{
    // DeviceLocator::create() returns something whatever the build is. When Qt
    // Positioning was not compiled in, or when it was and found no source, the
    // answer is the same one a user who denied permission gets - one branch,
    // exercised either way.
    //
    // Nothing here asserts that a fix arrives. A test that needed GeoClue2,
    // a session bus and somebody to approve a portal prompt would be a test
    // that fails on every CI runner for reasons that have nothing to do with
    // this code.
    DeviceLocator *locator = DeviceLocator::create(this);
    QVERIFY(locator != nullptr);
    QVERIFY(!locator->isRequestInFlight());

    QSignalSpy failed(locator, &DeviceLocator::failed);
    QSignalSpy located(locator, &DeviceLocator::located);

    if (!locator->isAvailable()) {
        locator->requestPosition();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(failed.first().first().value<DeviceLocator::Failure>(),
                 DeviceLocator::Failure::Unavailable);
        QCOMPARE(located.count(), 0);
    }

    // Cancelling something that was never asked for is quiet: a cancelled
    // request has no outcome, and a caller that cancelled does not need to be
    // told.
    locator->cancel();
    QVERIFY(!locator->isRequestInFlight());

    delete locator;
}

QTEST_MAIN(TestPlaces)
#include "tst_places.moc"
