// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The two alert providers, against nine recorded responses and a loopback
// server. No network - tests/support/networkguard.h makes that a property of
// the process.
//
// Every literal in this file was read off a payload the live service sent on
// 2026-08-05; tests/fixtures/alerts/record.sh is the command that captured
// them and tests/fixtures/alerts/README.md says what each one is for. The
// fixtures matter more here than anywhere else in the suite, because an alert
// parser written against hand-made JSON agrees with whatever the author
// believed - and three of the beliefs a reasonable author would hold about
// these two services are wrong:
//
//     that severity is always stated              6 of 9 say "Unknown"
//     that an alert always has an end             3 of 9 have ends: null
//     that a point outside coverage returns []    api.weather.gov returns 400
//
// The last one is why the routing test is in this file rather than in a
// registry test: it is a fact about the service, and it is only true because
// somebody asked.

#include "libclimat/core/clock.h"
#include "libclimat/domain/alert.h"
#include "libclimat/net/httpclient.h"
#include "libclimat/providers/eccc/ecccalertprovider.h"
#include "libclimat/providers/nws/nwsalertprovider.h"
#include "libclimat/providers/registry.h"
#include "support/httpstub.h"
#include "support/networkguard.h"

#include <QFile>
#include <QTest>
#include <QUrl>

using namespace climat;

namespace {

QByteArray fixture(const QString &relative)
{
    QFile file(QStringLiteral(CLIMAT_SOURCE_DIR) + QStringLiteral("/tests/fixtures/alerts/")
               + relative);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

// Annapolis County, Nova Scotia - inside the heat warning's polygon.
const Coordinate kAnnapolis{ 44.6487, -65.2007 };

// Seattle. Four NWS alerts at this point on the recorded afternoon.
const Coordinate kSeattle{ 47.6062, -122.3321 };

// Toronto: inside both loose boxes, and inside no alert polygon that day.
const Coordinate kToronto{ 43.6532, -79.3832 };

// The instant everything here is judged against: 2026-08-05T21:30Z, which is
// the same instant libclimat/providers/fixture/data/seattle/fixture.json freezes
// its clock to, so a test and a screenshot agree about which alerts are live.
const QDateTime kNow{ QDate(2026, 8, 5), QTime(21, 30), QTimeZone::UTC };

} // namespace

class TestAlertProviders : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    // ---- ECCC ----
    void ecccReadsTheHeatWarningItWasSent();
    void ecccMapsTheRiskColourAndNotTheAlertType();
    void ecccCarriesTheIssuersOwnWordsBesideTheCapGrade();
    void ecccKeysIdentityOnTheCodeAndTheFeatureNotTheMessageId();
    void ecccExpiresBeforeItEndsToo();
    void ecccReadsFrenchFromTheSamePayload();
    void ecccAnEmptyCollectionIsASuccessAndNotAFailure();
    void ecccSendsADegenerateBoundingBoxAndNotACqlFilter();

    // ---- NWS ----
    void nwsReadsAllFourSeattleAlerts();
    void nwsKeepsUnknownSeverityUnknown();
    void nwsCarriesANullEndsAsInvalidRatherThanSubstituting();
    void nwsIdentityIsTheOwnIdPlusEveryReference();
    void nwsTwoAirQualityAlertsStayTwo();
    void nwsKeepsTheLinkOnlyWhenItIsAWebPage();
    void nwsSendsLatitudeFirstInOneParameter();
    void nwsOutOfBoundsIsUnsupportedAndNotAFailure();

    // ---- routing and the fan-out ----
    void coverageFollowsTheRegionBoxes();
    void theRegistryAsksBothProvidersAndMergesThem();
    void oneProviderFailingLeavesTheSetIncompleteRatherThanEmpty();
    void aDeclinedProviderDoesNotMakeTheSetIncomplete();
    void everyProviderDecliningIsUnsupported();
};

void TestAlertProviders::initTestCase()
{
    NetworkGuard::install();

    // The fixtures are the test. A missing one has to fail here rather than as
    // twenty confusing comparisons against empty payloads.
    QVERIFY(!fixture(QStringLiteral("eccc/annapolis-heat.json")).isEmpty());
    QVERIFY(!fixture(QStringLiteral("nws/seattle-four.json")).isEmpty());
    QVERIFY(!fixture(QStringLiteral("nws/out-of-bounds.json")).isEmpty());
}

// ---- ECCC -------------------------------------------------------------------------------

void TestAlertProviders::ecccReadsTheHeatWarningItWasSent()
{
    const Result<AlertSet> parsed = EcccAlertProvider::parse(
        fixture(QStringLiteral("eccc/annapolis-heat.json")), kNow, QStringLiteral("en"));

    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().alerts.size(), 1);

    const Alert &alert = parsed.value().alerts.constFirst();
    QCOMPARE(alert.event, QStringLiteral("heat warning"));
    QCOMPARE(alert.areaDescription, QStringLiteral("Annapolis County"));
    QCOMPARE(alert.senderName, QStringLiteral("Environment and Climate Change Canada"));
    QVERIFY(alert.description.contains(QStringLiteral("mainland Nova Scotia")));

    // ECCC publishes no headline and no separate instruction. Empty rather than
    // manufactured - a view that finds an empty headline shows `event`.
    QVERIFY(alert.headline.isEmpty());
    QVERIFY(alert.instruction.isEmpty());

    // And no link, because the payload carries none and both provincial pages
    // that could be constructed answer 404. Verified, not assumed.
    QVERIFY(alert.web.isEmpty());

    QVERIFY(alert.isDisplayableAt(kNow));
}

void TestAlertProviders::ecccMapsTheRiskColourAndNotTheAlertType()
{
    // Two recorded alerts, both alert_type "warning", differing only in colour.
    // If severity came from the type they would grade the same; the whole point
    // of reading the colour is that they do not.
    const Result<AlertSet> yellow = EcccAlertProvider::parse(
        fixture(QStringLiteral("eccc/annapolis-heat.json")), kNow, QStringLiteral("en"));
    const Result<AlertSet> orange = EcccAlertProvider::parse(
        fixture(QStringLiteral("eccc/fraser-valley-air-quality.json")), kNow,
        QStringLiteral("en"));

    QVERIFY(yellow.hasValue());
    QVERIFY(orange.hasValue());

    QCOMPARE(yellow.value().alerts.constFirst().severity, AlertSeverity::Moderate);
    QCOMPARE(orange.value().alerts.constFirst().severity, AlertSeverity::Severe);

    // Both are warnings, so urgency is the same on both - which is the evidence
    // that the two axes are actually being read from two different fields.
    QCOMPARE(yellow.value().alerts.constFirst().urgency, AlertUrgency::Expected);
    QCOMPARE(orange.value().alerts.constFirst().urgency, AlertUrgency::Expected);

    // confidence_en is "High" in both recordings.
    QCOMPARE(yellow.value().alerts.constFirst().certainty, AlertCertainty::Likely);

    QVERIFY(orange.value().alerts.constFirst().outranks(yellow.value().alerts.constFirst()));
}

void TestAlertProviders::ecccCarriesTheIssuersOwnWordsBesideTheCapGrade()
{
    const Result<AlertSet> parsed = EcccAlertProvider::parse(
        fixture(QStringLiteral("eccc/annapolis-heat.json")), kNow, QStringLiteral("en"));

    QVERIFY(parsed.hasValue());

    // "yellow warning" is what weather.gc.ca shows. "Moderate" is what this app
    // sorts with. A reader who only ever sees the second has been shown a
    // translation of their own warning into a vocabulary they have never met.
    QCOMPARE(parsed.value().alerts.constFirst().issuerLabel, QStringLiteral("yellow warning"));
}

void TestAlertProviders::ecccKeysIdentityOnTheCodeAndTheFeatureNotTheMessageId()
{
    const Result<AlertSet> parsed = EcccAlertProvider::parse(
        fixture(QStringLiteral("eccc/annapolis-heat.json")), kNow, QStringLiteral("en"));

    QVERIFY(parsed.hasValue());
    const Alert &alert = parsed.value().alerts.constFirst();

    QCOMPARE(alert.identityKeys, QStringList{ QStringLiteral("eccc:EHW:fea1-786") });

    // The message id embeds an issue timestamp, so it changes every time the
    // warning is continued. Keying on it would un-dismiss the alert several
    // times a day.
    QCOMPARE(alert.id, QStringLiteral("1730038981338508350202608040506_fea1-786"));
    QVERIFY(!alert.identityKeys.contains(alert.id));
}

void TestAlertProviders::ecccExpiresBeforeItEndsToo()
{
    // The expires-before-ends shape is not an American peculiarity. This
    // Canadian payload has expiration_datetime 2026-08-06T10:44:57Z and
    // event_end_datetime 2026-08-08T22:00Z - the message goes stale two days
    // before the heat does.
    const Result<AlertSet> parsed = EcccAlertProvider::parse(
        fixture(QStringLiteral("eccc/annapolis-heat.json")), kNow, QStringLiteral("en"));

    QVERIFY(parsed.hasValue());
    const Alert &alert = parsed.value().alerts.constFirst();

    QVERIFY(alert.expires.isValid());
    QVERIFY(alert.ends.isValid());
    QVERIFY(alert.expires < alert.ends);
    QCOMPARE(alert.hazardEnd(), alert.ends);

    // 2026-08-07: a day past the message's refresh deadline, a day before the
    // heat ends.
    const QDateTime between{ QDate(2026, 8, 7), QTime(12, 0), QTimeZone::UTC };
    QVERIFY(alert.isPastRefreshDeadline(between));
    QVERIFY(alert.isDisplayableAt(between));
}

void TestAlertProviders::ecccReadsFrenchFromTheSamePayload()
{
    // Bilingual is field selection, not document selection: the same bytes
    // answer both. Which is why the language is not in the cache key and
    // switching it costs no request.
    const QByteArray bytes = fixture(QStringLiteral("eccc/annapolis-heat.json"));

    const Result<AlertSet> english = EcccAlertProvider::parse(bytes, kNow, QStringLiteral("en"));
    const Result<AlertSet> french  = EcccAlertProvider::parse(bytes, kNow, QStringLiteral("fr-CA"));

    QVERIFY(english.hasValue());
    QVERIFY(french.hasValue());

    QCOMPARE(french.value().alerts.constFirst().event,
             QStringLiteral("avertissement de chaleur"));
    QCOMPARE(french.value().alerts.constFirst().areaDescription,
             QString::fromUtf8("comté d'Annapolis"));
    QCOMPARE(french.value().alerts.constFirst().senderName,
             QStringLiteral("Environnement et Changement climatique Canada"));

    // The grade is identical, because it is read from risk_colour_en whichever
    // language was asked for - "jaune" is shown, never matched.
    QCOMPARE(french.value().alerts.constFirst().severity,
             english.value().alerts.constFirst().severity);
    QVERIFY(french.value().alerts.constFirst().issuerLabel.contains(QStringLiteral("jaune")));
}

void TestAlertProviders::ecccAnEmptyCollectionIsASuccessAndNotAFailure()
{
    const Result<AlertSet> parsed = EcccAlertProvider::parse(
        fixture(QStringLiteral("eccc/toronto-clear.json")), kNow, QStringLiteral("en"));

    QVERIFY(parsed.hasValue());
    QVERIFY(parsed.value().alerts.isEmpty());

    // 850 bytes of HTTP 200. Everything above this line in the stack has to keep
    // the difference between this and a failure, because a fall-through chain
    // treats it as an answer - which is why alerts fan out instead.
}

void TestAlertProviders::ecccSendsADegenerateBoundingBoxAndNotACqlFilter()
{
    HttpStub stub;
    QVERIFY(stub.listen());
    stub.enqueue(StubResponse::ok(fixture(QStringLiteral("eccc/annapolis-heat.json"))));

    SystemClock clock;
    HttpClient  client(&clock);

    EcccAlertProvider provider(&client, &clock);
    provider.setBaseUrl(QUrl(stub.baseUrl() + QStringLiteral("/items")));

    AlertRequest request;
    request.coord = kAnnapolis;

    QFuture<Result<AlertSet>> future = provider.fetchAlerts(request);
    QVERIFY(QTest::qWaitFor([&future] { return future.isFinished(); }, 5000));
    QVERIFY(future.result().hasValue());

    QCOMPARE(stub.requestCount(), 1);
    const QByteArray target = stub.requests().constFirst().target;

    // Longitude first, the same point twice - and rounded to four decimals by
    // composeUrl(), which is the property that keeps a map drag from becoming a
    // hundred requests.
    QVERIFY2(target.contains("bbox=-65.2007,44.6487,-65.2007,44.6487"), target.constData());

    // Explicitly NOT the CQL2 spatial filter. The prefixed form of it answers
    // HTTP 500 today and the bare form answered 500 in the research this was
    // planned from; Part 1's bbox does not move.
    QVERIFY(!target.contains("filter="));
    QVERIFY(!target.contains("INTERSECTS"));
}

// ---- NWS ----------------------------------------------------------------------------------

void TestAlertProviders::nwsReadsAllFourSeattleAlerts()
{
    const Result<AlertSet> parsed =
        NwsAlertProvider::parse(fixture(QStringLiteral("nws/seattle-four.json")), kNow);

    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().alerts.size(), 4);
    QCOMPARE(parsed.value().displayableAt(kNow).size(), 4);

    // The Heat Advisory leads: Moderate beats three Unknowns.
    const QList<Alert> ranked = parsed.value().displayableAt(kNow);
    QCOMPARE(ranked.constFirst().event, QStringLiteral("Heat Advisory"));
    QCOMPARE(ranked.constFirst().severity, AlertSeverity::Moderate);
    QCOMPARE(ranked.constFirst().senderName, QStringLiteral("NWS Seattle WA"));
    QVERIFY(ranked.constFirst().headline.startsWith(QStringLiteral("Heat Advisory issued")));
    QVERIFY(!ranked.constFirst().instruction.isEmpty());
}

void TestAlertProviders::nwsKeepsUnknownSeverityUnknown()
{
    const Result<AlertSet> parsed =
        NwsAlertProvider::parse(fixture(QStringLiteral("nws/seattle-four.json")), kNow);

    QVERIFY(parsed.hasValue());

    int unknown = 0;
    for (const Alert &alert : parsed.value().alerts) {
        if (alert.event == QStringLiteral("Air Quality Alert")) {
            ++unknown;
            // Not Minor. The issuer declined to grade, and inventing Minor is
            // inventing a grade - see alert.h.
            QCOMPARE(alert.severity, AlertSeverity::Unknown);
            QCOMPARE(alert.urgency, AlertUrgency::Unknown);
            QCOMPARE(alert.certainty, AlertCertainty::Unknown);

            // Ungraded and still shown.
            QVERIFY(alert.isDisplayableAt(kNow));
        }
    }
    QCOMPARE(unknown, 3);
}

void TestAlertProviders::nwsCarriesANullEndsAsInvalidRatherThanSubstituting()
{
    const Result<AlertSet> parsed =
        NwsAlertProvider::parse(fixture(QStringLiteral("nws/seattle-four.json")), kNow);

    QVERIFY(parsed.hasValue());

    for (const Alert &alert : parsed.value().alerts) {
        if (alert.event != QStringLiteral("Air Quality Alert"))
            continue;

        // The parser does not substitute. hazardEnd() does, and keeping those
        // two apart is what lets a view ask "did the issuer say when this ends"
        // separately from "when does it come off the screen".
        QVERIFY(!alert.ends.isValid());
        QVERIFY(alert.expires.isValid());
        QCOMPARE(alert.hazardEnd(), alert.expires);
    }

    // The Heat Advisory does have one, eighteen hours after its expires.
    const Result<AlertSet> siskiyou = NwsAlertProvider::parse(
        fixture(QStringLiteral("nws/siskiyou-heat-advisory.json")), kNow);
    QVERIFY(siskiyou.hasValue());
    QCOMPARE(siskiyou.value().alerts.size(), 1);

    const Alert &advisory = siskiyou.value().alerts.constFirst();
    QVERIFY(advisory.expires < advisory.ends);
    QCOMPARE(advisory.ends.toUTC(), QDateTime(QDate(2026, 8, 7), QTime(6, 0), QTimeZone::UTC));
    QCOMPARE(advisory.expires.toUTC(), QDateTime(QDate(2026, 8, 6), QTime(12, 0), QTimeZone::UTC));
}

void TestAlertProviders::nwsIdentityIsTheOwnIdPlusEveryReference()
{
    const Result<AlertSet> parsed =
        NwsAlertProvider::parse(fixture(QStringLiteral("nws/seattle-four.json")), kNow);

    QVERIFY(parsed.hasValue());

    for (const Alert &alert : parsed.value().alerts) {
        QVERIFY(!alert.identityKeys.isEmpty());
        QVERIFY(alert.identityKeys.constFirst().startsWith(QStringLiteral("nws:")));

        if (alert.event == QStringLiteral("Heat Advisory")) {
            // messageType Update, referencing the message it supersedes. Two
            // keys: its own and the one it replaces, which is what lets a
            // dismissal survive the update.
            QCOMPARE(alert.messageType, AlertMessageType::Update);
            QCOMPARE(alert.identityKeys.size(), 2);
            QVERIFY(alert.identityKeys.at(1).contains(
                QStringLiteral("23b6389b838e66f5af975c8d3d82b438d0a41d31")));
        }
    }
}

void TestAlertProviders::nwsTwoAirQualityAlertsStayTwo()
{
    const Result<AlertSet> parsed =
        NwsAlertProvider::parse(fixture(QStringLiteral("nws/seattle-four.json")), kNow);

    QVERIFY(parsed.hasValue());

    // Two of the three air quality alerts share event, sender and geocode list
    // exactly. Assert the collision the obvious identity scheme would have made,
    // so that anyone who "simplifies" identityKeys back to (event, area) finds
    // out here rather than from a user who missed a warning.
    QList<Alert> airQuality;
    for (const Alert &alert : parsed.value().alerts) {
        if (alert.event == QStringLiteral("Air Quality Alert"))
            airQuality.append(alert);
    }
    QCOMPARE(airQuality.size(), 3);

    for (int i = 0; i < airQuality.size(); ++i) {
        for (int j = i + 1; j < airQuality.size(); ++j) {
            QVERIFY2(!airQuality.at(i).isSameHazard(airQuality.at(j)),
                     "two distinct air quality alerts were merged into one");
        }
    }
}

void TestAlertProviders::nwsKeepsTheLinkOnlyWhenItIsAWebPage()
{
    const QByteArray body = fixture(QStringLiteral("nws/seattle-four.json"));

    const Result<AlertSet> asSent = NwsAlertProvider::parse(body, kNow);
    QVERIFY(asSent.hasValue());
    QCOMPARE(asSent.value().alerts.constFirst().web, QUrl(QStringLiteral("http://www.weather.gov")));

    // The same feed with its link swapped for one that is not a web page.
    QByteArray tampered = body;
    tampered.replace("\"web\": \"http://www.weather.gov\"", "\"web\": \"file:///etc/passwd\"");
    QVERIFY(tampered != body);

    const Result<AlertSet> parsed = NwsAlertProvider::parse(tampered, kNow);
    QVERIFY(parsed.hasValue());
    QCOMPARE(parsed.value().alerts.size(), 4);
    for (const Alert &alert : parsed.value().alerts)
        QVERIFY(alert.web.isEmpty());
}

void TestAlertProviders::nwsSendsLatitudeFirstInOneParameter()
{
    HttpStub stub;
    QVERIFY(stub.listen());
    stub.enqueue(StubResponse::ok(fixture(QStringLiteral("nws/seattle-four.json"))));

    SystemClock clock;
    HttpClient  client(&clock);

    NwsAlertProvider provider(&client, &clock);
    provider.setBaseUrl(QUrl(stub.baseUrl() + QStringLiteral("/alerts/active")));

    AlertRequest request;
    request.coord = kSeattle;

    QFuture<Result<AlertSet>> future = provider.fetchAlerts(request);
    QVERIFY(QTest::qWaitFor([&future] { return future.isFinished(); }, 5000));
    QVERIFY(future.result().hasValue());
    QCOMPARE(future.result().value().alerts.size(), 4);

    // point=<lat>,<lon>. The opposite order to ECCC's bbox, which is the whole
    // reason the spelling is a CoordinateForm rather than a string each provider
    // assembles for itself.
    const QByteArray target = stub.requests().constFirst().target;
    QVERIFY2(target.contains("point=47.6062,-122.3321"), target.constData());
}

void TestAlertProviders::nwsOutOfBoundsIsUnsupportedAndNotAFailure()
{
    // The recorded 400 body, parsed directly: a fixture and a live response have
    // to produce the same kind, or the fan-out behaves differently under test
    // than it does in the field.
    const Result<AlertSet> parsed =
        NwsAlertProvider::parse(fixture(QStringLiteral("nws/out-of-bounds.json")), kNow);

    QVERIFY(!parsed.hasValue());
    QCOMPARE(parsed.errorKind(), ErrorKind::Unsupported);
    QVERIFY(parsed.error().message().contains(QStringLiteral("out of bounds")));

    // And through the transport, where HttpClient turns the status into an
    // Error before the provider ever sees the body.
    HttpStub stub;
    QVERIFY(stub.listen());
    StubResponse refusal = StubResponse::withStatus(400);
    refusal.body = fixture(QStringLiteral("nws/out-of-bounds.json"));
    stub.enqueue(refusal);

    SystemClock clock;
    HttpClient  client(&clock);

    NwsAlertProvider provider(&client, &clock);
    provider.setBaseUrl(QUrl(stub.baseUrl() + QStringLiteral("/alerts/active")));

    AlertRequest request;
    request.coord = kAnnapolis;

    QFuture<Result<AlertSet>> future = provider.fetchAlerts(request);
    QVERIFY(QTest::qWaitFor([&future] { return future.isFinished(); }, 5000));

    QVERIFY(!future.result().hasValue());
    QCOMPARE(future.result().errorKind(), ErrorKind::Unsupported);

    // The service's own sentence survives into the message, because HttpClient
    // now appends a 4xx body. Without it this reads "Bad Request" and says
    // nothing about which of the two possible causes happened.
    QVERIFY2(future.result().error().message().contains(QStringLiteral("out of bounds")),
             qPrintable(future.result().error().message()));
}

// ---- routing and the fan-out ------------------------------------------------------------

void TestAlertProviders::coverageFollowsTheRegionBoxes()
{
    SystemClock clock;
    HttpClient  client(&clock);

    EcccAlertProvider eccc(&client, &clock);
    NwsAlertProvider  nws(&client, &clock);

    QVERIFY(eccc.covers(kAnnapolis));
    QVERIFY(nws.covers(kSeattle));

    // An invalid coordinate is covered by nobody, which is what stops a
    // default-constructed Place putting two providers on the wire.
    QVERIFY(!eccc.covers(Coordinate{}));
    QVERIFY(!nws.covers(Coordinate{}));

    // Toronto is inside BOTH boxes, because no rectangle follows the border.
    // That is not a defect to fix - it is why alerts fan out.
    QVERIFY(eccc.covers(kToronto));
    QVERIFY(nws.covers(kToronto));

    // Berlin is in neither.
    const Coordinate berlin{ 52.52, 13.405 };
    QVERIFY(!eccc.covers(berlin));
    QVERIFY(!nws.covers(berlin));
}

void TestAlertProviders::theRegistryAsksBothProvidersAndMergesThem()
{
    HttpStub canadian;
    HttpStub american;
    QVERIFY(canadian.listen());
    QVERIFY(american.listen());

    canadian.enqueue(StubResponse::ok(fixture(QStringLiteral("eccc/annapolis-heat.json"))));
    american.enqueue(StubResponse::ok(fixture(QStringLiteral("nws/seattle-four.json"))));

    SystemClock clock;
    HttpClient  client(&clock);

    EcccAlertProvider eccc(&client, &clock);
    NwsAlertProvider  nws(&client, &clock);
    eccc.setBaseUrl(QUrl(canadian.baseUrl() + QStringLiteral("/items")));
    nws.setBaseUrl(QUrl(american.baseUrl() + QStringLiteral("/alerts/active")));

    ProviderRegistry registry;
    QVERIFY(registry.addAlertProvider(&eccc, 0).hasValue());
    QVERIFY(registry.addAlertProvider(&nws, 0).hasValue());

    // Toronto: covered by both.
    QCOMPARE(registry.alertChain(kToronto).size(), 2);

    AlertRequest request;
    request.coord = kToronto;

    QFuture<Result<AlertAnswer>> future = registry.fetchAlerts(request);
    QVERIFY(QTest::qWaitFor([&future] { return future.isFinished(); }, 5000));
    QVERIFY(future.result().hasValue());

    const AlertAnswer &answer = future.result().value();

    // One Canadian plus four American. A fall-through chain would have stopped
    // at whichever answered first and reported one of the two numbers.
    QCOMPARE(answer.value.alerts.size(), 5);
    QVERIFY(answer.value.complete);
    QVERIFY(answer.value.providerId.contains(QStringLiteral("eccc")));
    QVERIFY(answer.value.providerId.contains(QStringLiteral("nws")));

    // Never "the fallback took over": both were asked and both answered.
    QVERIFY(!answer.fromFallback);
    QVERIFY(answer.failures.isEmpty());
}

void TestAlertProviders::oneProviderFailingLeavesTheSetIncompleteRatherThanEmpty()
{
    HttpStub canadian;
    HttpStub american;
    QVERIFY(canadian.listen());
    QVERIFY(american.listen());

    // GeoMet is down. NWS is fine.
    canadian.enqueue(StubResponse::withStatus(500));
    american.enqueue(StubResponse::ok(fixture(QStringLiteral("nws/seattle-four.json"))));

    SystemClock clock;
    HttpClient  client(&clock);

    // No retries worth waiting for: the backoff schedule is replaced so the 500
    // gives up promptly rather than making this a slow test.
    BackoffPolicy immediate;
    immediate.maxRetries = 0;
    client.setBackoffPolicy(immediate, 1);

    EcccAlertProvider eccc(&client, &clock);
    NwsAlertProvider  nws(&client, &clock);
    eccc.setBaseUrl(QUrl(canadian.baseUrl() + QStringLiteral("/items")));
    nws.setBaseUrl(QUrl(american.baseUrl() + QStringLiteral("/alerts/active")));

    ProviderRegistry registry;
    QVERIFY(registry.addAlertProvider(&eccc, 0).hasValue());
    QVERIFY(registry.addAlertProvider(&nws, 0).hasValue());

    AlertRequest request;
    request.coord = kToronto;

    QFuture<Result<AlertAnswer>> future = registry.fetchAlerts(request);
    QVERIFY(QTest::qWaitFor([&future] { return future.isFinished(); }, 15000));

    // The half we could get, and an explicit statement that it is a half. The
    // failure this whole design is against is rendering it as "no warnings".
    QVERIFY(future.result().hasValue());
    QCOMPARE(future.result().value().value.alerts.size(), 4);
    QVERIFY(!future.result().value().value.complete);
    QCOMPARE(future.result().value().failures.size(), 1);
    QCOMPARE(future.result().value().failures.constFirst().providerId, QStringLiteral("eccc"));
}

void TestAlertProviders::aDeclinedProviderDoesNotMakeTheSetIncomplete()
{
    HttpStub canadian;
    HttpStub american;
    QVERIFY(canadian.listen());
    QVERIFY(american.listen());

    canadian.enqueue(StubResponse::ok(fixture(QStringLiteral("eccc/annapolis-heat.json"))));

    // The real answer for a Canadian point: 400, out of bounds.
    StubResponse refusal = StubResponse::withStatus(400);
    refusal.body = fixture(QStringLiteral("nws/out-of-bounds.json"));
    american.enqueue(refusal);

    SystemClock clock;
    HttpClient  client(&clock);

    EcccAlertProvider eccc(&client, &clock);
    NwsAlertProvider  nws(&client, &clock);
    eccc.setBaseUrl(QUrl(canadian.baseUrl() + QStringLiteral("/items")));
    nws.setBaseUrl(QUrl(american.baseUrl() + QStringLiteral("/alerts/active")));

    ProviderRegistry registry;
    QVERIFY(registry.addAlertProvider(&eccc, 0).hasValue());
    QVERIFY(registry.addAlertProvider(&nws, 0).hasValue());

    AlertRequest request;
    request.coord = kToronto;

    QFuture<Result<AlertAnswer>> future = registry.fetchAlerts(request);
    QVERIFY(QTest::qWaitFor([&future] { return future.isFinished(); }, 5000));

    QVERIFY(future.result().hasValue());
    QCOMPARE(future.result().value().value.alerts.size(), 1);

    // Asked, declined, and NOT counted as missing. Anything else puts "alerts
    // unavailable" under every Canadian border city permanently.
    QVERIFY(future.result().value().value.complete);
    QVERIFY(future.result().value().failures.isEmpty());
    QCOMPARE(future.result().value().value.providerId, QStringLiteral("eccc"));
}

void TestAlertProviders::everyProviderDecliningIsUnsupported()
{
    HttpStub american;
    QVERIFY(american.listen());

    StubResponse refusal = StubResponse::withStatus(400);
    refusal.body = fixture(QStringLiteral("nws/out-of-bounds.json"));
    american.enqueue(refusal);

    SystemClock clock;
    HttpClient  client(&clock);

    NwsAlertProvider nws(&client, &clock);
    nws.setBaseUrl(QUrl(american.baseUrl() + QStringLiteral("/alerts/active")));

    ProviderRegistry registry;
    QVERIFY(registry.addAlertProvider(&nws, 0).hasValue());

    AlertRequest request;
    request.coord = kSeattle;

    QFuture<Result<AlertAnswer>> future = registry.fetchAlerts(request);
    QVERIFY(QTest::qWaitFor([&future] { return future.isFinished(); }, 5000));

    // Nobody had anything to say about this place. §4.4: the UI hides the
    // feature rather than showing a broken one - and "no alerts here" is not
    // the same claim.
    QVERIFY(!future.result().hasValue());
    QCOMPARE(future.result().errorKind(), ErrorKind::Unsupported);
}

QTEST_MAIN(TestAlertProviders)
#include "tst_alertproviders.moc"
