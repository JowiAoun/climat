// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The alert view model: what is on the screen, what a dismissal means, and how
// often we ask.
//
// tests/tst_alerts.cpp covers the domain type - which of four timestamps ends a
// hazard, which of two alerts outranks the other. This file covers the layer
// above it, and the three things that layer decides on its own. None of them is
// visible in a screenshot and all three fail silently.
//
// ============================================================================
// 1. EVERYTHING IS FILTERED AGAINST THE CLOCK, EVERY MINUTE
//
// alertsdata.h calls this "the safety property of the whole feature". A set
// that arrived from a cache written yesterday is filtered against today, so
// nothing here can put an ended warning on the screen because the network went
// away. The test for it is the one where the payload never changes and the
// clock does - if rebuild() only ran on `apply()`, a tornado warning would sit
// on the screen until the next successful poll.
//
// ============================================================================
// 2. DISMISSAL IS ACKNOWLEDGEMENT
//
// Three rules, and each of them is a way for a banner to be annoying or
// dangerous:
//
//   keyed by hazard      NWS re-sends an alert in full on every update under a
//                        new id. Keyed by message id, a dismissal would come
//                        undone several times a day.
//
//   a raise un-dismisses A Moderate acknowledged and then upgraded to Severe
//                        has to come back at full height. This is the rule that
//                        makes dismissal safe, and it is one `>` - flip it and
//                        an upgrade to Extreme stays collapsed.
//
//   entries are pruned   A stored acknowledgement for a hazard that ended in
//                        March is a trap the next time the same county is
//                        warned about the same thing.
//
// ============================================================================
// 3. THE POLL SCHEDULE
//
// "Hidden means stopped" is the line the bandwidth arithmetic rests on:
// api.weather.gc.ca sends no validator at all, so every Canadian poll is a full
// ~10 kB and a day of uninterrupted foreground polling would be nearer 5 MB
// than the planned 264 kB. Nothing bounds that except the window being closed.

#include "app/settings.h"
#include "app/viewmodels/alertsdata.h"
#include "libclimat/core/clock.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QScopeGuard>
#include <QtTest>

using namespace climat;
using namespace std::chrono_literals;

namespace {

// The recorded Heat Advisory from tests/fixtures/alerts/nws/, to the minute.
// Same instants tst_alerts.cpp uses, so a failure in both files points at the
// domain type and a failure in one points here.
//
//     onset      2026-08-05T21:15Z
//     expires    2026-08-06T12:00Z      <- the issuer's refresh deadline
//     ends       2026-08-07T06:00Z      <- the hazard's end
QDateTime at(int day, int hour, int minute = 0)
{
    return QDateTime(QDate(2026, 8, day), QTime(hour, minute), QTimeZone::UTC);
}

Alert heatAdvisory()
{
    Alert alert;
    alert.id              = QStringLiteral("urn:oid:siskiyou.001.1");
    alert.providerId      = QStringLiteral("nws");
    alert.event           = QStringLiteral("Heat Advisory");
    alert.headline        = QStringLiteral("Heat Advisory issued August 5");
    alert.areaDescription = QStringLiteral("Central Siskiyou County");
    alert.senderName      = QStringLiteral("NWS Medford OR");
    alert.issuerLabel     = QStringLiteral("Moderate");
    alert.severity        = AlertSeverity::Moderate;
    alert.urgency         = AlertUrgency::Expected;
    alert.certainty       = AlertCertainty::Likely;
    alert.effective       = at(5, 21, 15);
    alert.onset           = at(5, 21, 15);
    alert.expires         = at(6, 12, 0);
    alert.ends            = at(7, 6, 0);
    alert.identityKeys    = { QStringLiteral("nws:siskiyou:heat-advisory") };
    return alert;
}

// A second hazard, gradeable, so ranking and "+N more" have something to sort.
Alert graded(AlertSeverity severity, const QString &key,
             const QString &event = QStringLiteral("Test Event"))
{
    Alert alert;
    alert.id           = key;
    alert.providerId   = QStringLiteral("nws");
    alert.event        = event;
    alert.severity     = severity;
    alert.urgency      = AlertUrgency::Expected;
    alert.certainty    = AlertCertainty::Likely;
    alert.effective    = at(5, 0);
    alert.onset        = at(5, 0);
    alert.expires      = at(9, 0);
    alert.ends         = at(9, 0);
    alert.identityKeys = { key };
    return alert;
}

AlertSet setOf(const QList<Alert> &alerts, const QDateTime &fetchedAt)
{
    AlertSet set;
    set.alerts      = alerts;
    set.fetchedAt   = fetchedAt;
    set.confirmedAt = fetchedAt;
    set.providerId  = QStringLiteral("nws");
    set.complete    = true;
    return set;
}

} // namespace

class TestAlertsData : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanup();

    // ---- what is on the screen ---------------------------------------------
    void theWorstAlertLeadsAndTheRestAreCountedBehindIt();
    void oneAlertIsNotPlusZeroMore();
    void nothingIsOnTheScreenBeforeAnythingHasBeenApplied();
    void clearingKeepsTheDistinctionBetweenNoAlertsAndNoCoverage();

    // ---- the clock, which is the safety property ---------------------------
    void anEndedHazardLeavesTheScreenWithoutANewPayload();
    void aPendingHazardIsShownWithTheTimeItBegins();
    void anActiveHazardIsShownWithTheTimeItEnds();
    void theEndIsReadInThePlacesOwnZone();
    void aModelWithNoClockShowsNothingRatherThanGuessing();

    // ---- confidence ---------------------------------------------------------
    void beingPastTheDeadlineIsNotEnoughToBeUnconfirmed();
    void aFailedPollBeforeTheDeadlineIsNotEnoughEither();
    void bothTogetherAreWhatSaysLastConfirmed();
    void anIncompleteSetSaysSoRatherThanClaimingSilence();

    // ---- acknowledgement -----------------------------------------------------
    void acknowledgingCollapsesTheTopAlertAndNothingElse();
    void anUpdateAtTheSameGradeStaysCollapsed();
    void aRaiseInSeverityBringsTheBannerBack();
    void aDropInSeverityStaysCollapsed();
    void revealingDropsTheEntryRatherThanDowngradingIt();
    void anAcknowledgementIsStoredAgainstEveryKeyTheHazardAnswersTo();
    void acknowledgingWithNothingOnTheScreenDoesNothing();

    // ---- persistence ---------------------------------------------------------
    void anAcknowledgementSurvivesARestart();
    void anAcknowledgementForAnEndedHazardIsPrunedOnLoad();
    void aStoredLineWithTheWrongShapeIsSkippedRatherThanCrashing();
    void theStoredKeyMayContainAnythingAUrnCan();

    // ---- the schedule ---------------------------------------------------------
    void theScheduleIsTheOneInTheArchitectureDocument_data();
    void theScheduleIsTheOneInTheArchitectureDocument();
    void aHiddenWindowKeepsPollingWhenItWasAskedToInterrupt();
    void showingTheWindowAsksForWhatWasMissed();
    void aWindowMerelyLosingFocusDoesNotFetch();
    void aPlaceWithNoCoverageIsNotAskedOnShowing();

    // ---- what is worth interrupting somebody for -------------------------
    void nothingIsAnnouncedWhileTheBannerIsOnScreen();
    void aHiddenWindowAnnouncesAHazardTheReaderHasNotSeen();
    void anUpdateAtTheSameGradeIsNotAnnouncedTwice();
    void anNwsUpdateChainIsOneHazardAllTheWayDown();
    void aRaiseInSeverityIsAnnouncedAgain();
    void nothingIsAnnouncedWithThePreferenceOff();
    void showingTheWindowTakesTheNotificationDown();
    void aWarningTheReaderWasLookingAtIsNotAnnouncedOnMinimising();
    void aHazardThatEndsTakesItsNotificationWithIt();
    void aHiddenWindowStopsPollingAltogether();
    void aPlaceWithNoCoverageIsNotPolled();

private:
    // The view model is a process-wide singleton with a private constructor, so
    // every test shares one. `init()`/`cleanup()` below put it back to a known
    // state rather than constructing a new one - see init() for what "known"
    // has to include.
    AlertsData  *m_alerts = nullptr;
    FrozenClock  m_clock;
};

void TestAlertsData::initTestCase()
{
    // Before anything constructs a QSettings. Settings is a singleton too and
    // AlertsData writes acknowledgements through it; without this the suite
    // would edit the developer's own configuration file.
    QStandardPaths::setTestModeEnabled(true);

    m_alerts = AlertsData::instance();
    m_alerts->setClock(&m_clock);
}

void TestAlertsData::init()
{
    // Mid-hazard: after onset (21:15 on the 5th), before the refresh deadline
    // (12:00 on the 6th), well before the end (06:00 on the 7th).
    m_clock.setNow(at(6, 0));

    // Order matters. The stored acknowledgements have to be emptied BEFORE
    // setSettings() reloads them, or a test starts holding the previous test's
    // dismissals - which passes or fails depending on which order QTest happens
    // to run the slots in, and that is the worst kind of test to debug.
    Settings::instance()->setAcknowledgedAlerts({});

    // Off, and before setSettings: it gates the announcements, and a case that
    // inherited it from the one before would announce or stay silent depending
    // on the order QTest happened to run the slots in.
    Settings::instance()->setAlertNotifications(false);

    m_alerts->setSettings(Settings::instance());
    m_alerts->setRefreshFailed(false);
    m_alerts->setWindowState(true, true);

    // With the preference off this also empties what has been announced, so no
    // case starts holding the previous one's notifications.
    m_alerts->clear(false);
}

void TestAlertsData::cleanup()
{
    Settings::instance()->setAcknowledgedAlerts({});
    Settings::instance()->setAlertNotifications(false);
    m_alerts->setWindowState(true, true);
    m_alerts->clear(false);
}

// ============================================================================
// What is on the screen.
// ============================================================================

void TestAlertsData::theWorstAlertLeadsAndTheRestAreCountedBehindIt()
{
    m_alerts->apply(setOf({ graded(AlertSeverity::Minor, QStringLiteral("a")),
                            graded(AlertSeverity::Extreme, QStringLiteral("b")),
                            graded(AlertSeverity::Moderate, QStringLiteral("c")) },
                          at(6, 0)));

    QCOMPARE(m_alerts->count(), 3);

    // The banner shows one alert, and the one it must not omit is the worst.
    QCOMPARE(m_alerts->top().value(QStringLiteral("severityKey")).toString(),
             QStringLiteral("extreme"));

    // "+2 more" - everything the banner is not showing.
    QCOMPARE(m_alerts->moreCount(), 2);

    // Both spellings of the severity travel with it. `severityKey` indexes
    // Theme.severity; `issuerLabel` is what the reader saw on the issuer's own
    // site. A banner with only the first cannot say "yellow warning".
    QVERIFY(m_alerts->top().contains(QStringLiteral("severityName")));
    QVERIFY(m_alerts->top().contains(QStringLiteral("issuerLabel")));
}

void TestAlertsData::oneAlertIsNotPlusZeroMore()
{
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QCOMPARE(m_alerts->count(), 1);
    QCOMPARE(m_alerts->moreCount(), 0);
    QCOMPARE(m_alerts->top().value(QStringLiteral("event")).toString(),
             QStringLiteral("Heat Advisory"));
}

void TestAlertsData::nothingIsOnTheScreenBeforeAnythingHasBeenApplied()
{
    QCOMPARE(m_alerts->count(), 0);
    QCOMPARE(m_alerts->moreCount(), 0);
    QVERIFY(m_alerts->top().isEmpty());

    // `top` being empty is not what a banner binds to - alertsdata.h says a
    // banner binds `visible: Alerts.count > 0` - but an empty map rather than a
    // map of empty strings is what makes that possible.
    QVERIFY(!m_alerts->isAcknowledged());
    QVERIFY(!m_alerts->isUnconfirmed());
}

void TestAlertsData::clearingKeepsTheDistinctionBetweenNoAlertsAndNoCoverage()
{
    // Two different facts that look identical on screen unless the model keeps
    // them apart: "this place has alert coverage and there is nothing in force"
    // and "nobody issues alerts for this place". §4.4 hides the feature
    // outright for the second rather than showing an empty one.
    m_alerts->clear(true);
    QVERIFY(m_alerts->isAvailable());
    QCOMPARE(m_alerts->count(), 0);

    m_alerts->clear(false);
    QVERIFY(!m_alerts->isAvailable());
    QCOMPARE(m_alerts->count(), 0);

    // And a set arriving makes it available whatever it said before.
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    QVERIFY(m_alerts->isAvailable());
}

// ============================================================================
// The clock. THE test in this file.
// ============================================================================

void TestAlertsData::anEndedHazardLeavesTheScreenWithoutANewPayload()
{
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    QCOMPARE(m_alerts->count(), 1);

    // The hazard ends at 06:00 on the 7th. Move the clock past it and change
    // nothing else - no new set, no successful poll, no network at all. This is
    // the situation where the app has been open all night and the service has
    // been unreachable since midnight.
    m_clock.setNow(at(7, 6, 1));

    // The one-minute tick is what would do this in the running app. Calling the
    // rebuild through the public surface rather than waiting sixty seconds:
    // re-applying the SAME set is what an unchanged cached payload looks like.
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QCOMPARE(m_alerts->count(), 0);
    QVERIFY(m_alerts->top().isEmpty());

    // And it is still an available place - the coverage did not go away, the
    // hazard did.
    QVERIFY(m_alerts->isAvailable());
}

void TestAlertsData::aPendingHazardIsShownWithTheTimeItBegins()
{
    // Effective at breakfast for an afternoon that has not arrived. docs/06
    // §6.6 was corrected for exactly this: hiding it would take a Heat Advisory
    // off the screen before the heat starts.
    Alert pending  = heatAdvisory();
    pending.effective = at(6, 8);
    pending.onset     = at(6, 14);
    pending.expires   = at(7, 2);
    pending.ends      = at(7, 2);

    m_clock.setNow(at(6, 9));
    m_alerts->apply(setOf({ pending }, at(6, 9)));

    QCOMPARE(m_alerts->count(), 1);
    QCOMPARE(m_alerts->top().value(QStringLiteral("phase")).toString(),
             QStringLiteral("pending"));

    // "Begins …" rather than "Until …". The sentence is built here rather than
    // in QML because this is the one place that has both the timestamps and the
    // phase; a view that recomputed the phase to pick a format string would be
    // a second copy of the rule the whole feature turns on.
    const QString when = m_alerts->top().value(QStringLiteral("when")).toString();
    QVERIFY2(when.startsWith(QStringLiteral("Begins")), qPrintable(when));
}

void TestAlertsData::anActiveHazardIsShownWithTheTimeItEnds()
{
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QCOMPARE(m_alerts->top().value(QStringLiteral("phase")).toString(),
             QStringLiteral("active"));

    const QString when = m_alerts->top().value(QStringLiteral("when")).toString();
    QVERIFY2(when.startsWith(QStringLiteral("Until")), qPrintable(when));

    // The hazard ends on a different day from `now`, so the stamp carries a
    // weekday. A bare "11:00 PM" for something ending tomorrow night is the
    // half of this string that actually misleads.
    QVERIFY2(when.contains(QStringLiteral("Fri")), qPrintable(when));
}

// The same advisory, read where it was issued. Its own text says "Until 11 PM
// PDT Thursday", and a banner that followed the machine's zone said Friday
// morning to anyone east of the Atlantic, and to every capture, which runs in
// UTC. AppEngine hands the model the place's zone; this is that hand-off.
void TestAlertsData::theEndIsReadInThePlacesOwnZone()
{
    const auto restore = qScopeGuard([this] {
        m_alerts->setTimeZone(QTimeZone::systemTimeZone());
    });
    m_alerts->setTimeZone(QTimeZone("America/Los_Angeles"));
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    const QString when = m_alerts->top().value(QStringLiteral("when")).toString();
    QVERIFY2(when.startsWith(QStringLiteral("Until Thu ")), qPrintable(when));
    QVERIFY2(when.endsWith(QStringLiteral("11:00 PM")) || when.endsWith(QStringLiteral("23:00")),
             qPrintable(when));
}

void TestAlertsData::aModelWithNoClockShowsNothingRatherThanGuessing()
{
    // "AND THERE IS NO FALLBACK, deliberately" - alertsdata.h. A fallback to
    // QDateTime::currentDateTimeUtc() would make a run that forgot to set the
    // clock keep working, look right, and quietly judge a recorded alert
    // against today. An invalid instant instead, which phaseAt() answers Ended
    // for, so nothing is displayed.
    m_alerts->setClock(nullptr);

    QTest::ignoreMessage(QtWarningMsg, "climat: the alert model has no clock; no alert can be displayed");
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QCOMPARE(m_alerts->count(), 0);

    m_alerts->setClock(&m_clock);
}

// ============================================================================
// Confidence. Two conditions, and the bug this replaced could satisfy neither.
// ============================================================================

void TestAlertsData::beingPastTheDeadlineIsNotEnoughToBeUnconfirmed()
{
    // Past `expires` alone is routine: the issuer is simply due to speak again,
    // and they usually have. Reporting "last confirmed" for every alert that
    // has outlived its own refresh deadline would put the words on the screen
    // permanently, which is the same as not having them.
    m_clock.setNow(at(6, 13));   // expires was 12:00
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 13)));

    QVERIFY(m_alerts->top().value(QStringLiteral("pastDeadline")).toBool());
    QVERIFY(!m_alerts->isUnconfirmed());
}

void TestAlertsData::aFailedPollBeforeTheDeadlineIsNotEnoughEither()
{
    m_clock.setNow(at(6, 0));    // well before expires
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    m_alerts->setRefreshFailed(true);

    QVERIFY(!m_alerts->top().value(QStringLiteral("pastDeadline")).toBool());
    QVERIFY(!m_alerts->isUnconfirmed());
}

void TestAlertsData::bothTogetherAreWhatSaysLastConfirmed()
{
    m_clock.setNow(at(6, 13));
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 13)));
    m_alerts->setRefreshFailed(true);

    QVERIFY(m_alerts->isUnconfirmed());

    // And the label the sheet prints beside it. `confirmedAt` is the last time
    // a poll SUCCEEDED, which is not when this set was built - a set served
    // from cache after a failed refresh keeps the older instant.
    QVERIFY2(m_alerts->confirmedLabel().startsWith(QStringLiteral("Last confirmed")),
             qPrintable(m_alerts->confirmedLabel()));

    // Clearing the failure clears the state without a new payload, because the
    // next successful poll is what resolves this.
    m_alerts->setRefreshFailed(false);
    QVERIFY(!m_alerts->isUnconfirmed());
}

void TestAlertsData::anIncompleteSetSaysSoRatherThanClaimingSilence()
{
    // "A UI that renders a partial set as complete is telling the user there
    // are no tornado warnings when what it means is that it could not reach the
    // service that would know."
    AlertSet partial = setOf({ heatAdvisory() }, at(6, 0));
    partial.complete = false;

    m_alerts->apply(partial);
    QVERIFY(!m_alerts->isComplete());

    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    QVERIFY(m_alerts->isComplete());
}

// ============================================================================
// Acknowledgement.
// ============================================================================

void TestAlertsData::acknowledgingCollapsesTheTopAlertAndNothingElse()
{
    m_alerts->apply(setOf({ graded(AlertSeverity::Extreme, QStringLiteral("worst")),
                            graded(AlertSeverity::Minor, QStringLiteral("least")) },
                          at(6, 0)));

    QVERIFY(!m_alerts->isAcknowledged());
    m_alerts->acknowledge();
    QVERIFY(m_alerts->isAcknowledged());

    // Collapsed, NOT removed. It is still the top alert, it is still counted,
    // and it is still in the sheet - "dismissal is acknowledgement, it is not
    // deletion".
    QCOMPARE(m_alerts->count(), 2);
    QCOMPARE(m_alerts->top().value(QStringLiteral("id")).toString(), QStringLiteral("worst"));
    QVERIFY(m_alerts->top().value(QStringLiteral("acknowledged")).toBool());

    // And the other one is untouched, which is what makes the banner come back
    // when the acknowledged hazard ends.
    QVERIFY(!m_alerts->list().at(1).toMap().value(QStringLiteral("acknowledged")).toBool());
}

void TestAlertsData::anUpdateAtTheSameGradeStaysCollapsed()
{
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    m_alerts->acknowledge();
    QVERIFY(m_alerts->isAcknowledged());

    // NWS re-sends an alert in full to move its expiry: a new message id, the
    // same hazard, the same grade. 24 of the 25 alerts in force in California
    // on the recording afternoon were updates. Keyed by message id this would
    // re-raise the banner several times a day.
    Alert update    = heatAdvisory();
    update.id       = QStringLiteral("urn:oid:siskiyou.001.2");
    update.expires  = at(6, 18);
    update.messageType = AlertMessageType::Update;

    m_alerts->apply(setOf({ update }, at(6, 12)));
    QVERIFY2(m_alerts->isAcknowledged(), "an update at the same grade re-raised the banner");
}

void TestAlertsData::aRaiseInSeverityBringsTheBannerBack()
{
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    m_alerts->acknowledge();
    QVERIFY(m_alerts->isAcknowledged());

    // The same hazard, upgraded. This is the rule that makes dismissal safe to
    // offer at all, and it is one `>`: a Heat Advisory that becomes an Excessive
    // Heat Warning has to be seen.
    Alert raised     = heatAdvisory();
    raised.id        = QStringLiteral("urn:oid:siskiyou.001.3");
    raised.severity  = AlertSeverity::Severe;

    m_alerts->apply(setOf({ raised }, at(6, 12)));
    QVERIFY2(!m_alerts->isAcknowledged(), "a raise in severity stayed collapsed");
}

void TestAlertsData::aDropInSeverityStaysCollapsed()
{
    Alert severe    = heatAdvisory();
    severe.severity = AlertSeverity::Severe;

    m_alerts->apply(setOf({ severe }, at(6, 0)));
    m_alerts->acknowledge();

    // Downgraded. The reader has already seen and dismissed something worse, so
    // re-raising for the milder version would be teaching them to dismiss
    // without reading - which is the behaviour the whole design is avoiding.
    Alert eased     = heatAdvisory();
    eased.id        = QStringLiteral("urn:oid:siskiyou.001.4");
    eased.severity  = AlertSeverity::Minor;

    m_alerts->apply(setOf({ eased }, at(6, 12)));
    QVERIFY(m_alerts->isAcknowledged());
}

void TestAlertsData::revealingDropsTheEntryRatherThanDowngradingIt()
{
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    m_alerts->acknowledge();
    QVERIFY(m_alerts->isAcknowledged());

    m_alerts->reveal();
    QVERIFY(!m_alerts->isAcknowledged());

    // "Dropped rather than downgraded. 'Show me this again' is not a statement
    // about a severity, and leaving a lower-graded entry behind would make the
    // banner collapse again by itself the next time the issuer said anything."
    //
    // So: an update at the SAME grade, which would match a leftover entry.
    Alert update = heatAdvisory();
    update.id    = QStringLiteral("urn:oid:siskiyou.001.5");
    m_alerts->apply(setOf({ update }, at(6, 12)));

    QVERIFY2(!m_alerts->isAcknowledged(), "reveal() left an entry behind");

    // And it is gone from storage too, not merely from memory.
    QVERIFY(Settings::instance()->acknowledgedAlerts().isEmpty());
}

void TestAlertsData::anAcknowledgementIsStoredAgainstEveryKeyTheHazardAnswersTo()
{
    // "Stored against EVERY key the alert answers to, which is what lets the
    // next message in an NWS update chain match on the reference it carries."
    Alert chained          = heatAdvisory();
    chained.identityKeys   = { QStringLiteral("nws:msg:001"),
                               QStringLiteral("nws:siskiyou:heat-advisory") };

    m_alerts->apply(setOf({ chained }, at(6, 0)));
    m_alerts->acknowledge();

    QCOMPARE(Settings::instance()->acknowledgedAlerts().size(), 2);

    // The successor carries only the second key - it references the hazard, not
    // the message we saw. It still matches.
    Alert successor        = heatAdvisory();
    successor.id           = QStringLiteral("urn:oid:siskiyou.001.6");
    successor.identityKeys = { QStringLiteral("nws:siskiyou:heat-advisory") };

    m_alerts->apply(setOf({ successor }, at(6, 12)));
    QVERIFY(m_alerts->isAcknowledged());
}

void TestAlertsData::acknowledgingWithNothingOnTheScreenDoesNothing()
{
    // Reachable: the tick can empty the list between the banner being drawn and
    // the button being pressed, which is a hazard ending in the half-second a
    // finger is travelling.
    m_alerts->acknowledge();
    m_alerts->reveal();

    QCOMPARE(m_alerts->count(), 0);
    QVERIFY(Settings::instance()->acknowledgedAlerts().isEmpty());
}

// ============================================================================
// Persistence. "A dismissal that did not survive a restart would re-raise a
// multi-day heat warning every time the app was opened, which is precisely the
// behaviour that teaches people to ignore banners."
// ============================================================================

void TestAlertsData::anAcknowledgementSurvivesARestart()
{
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    m_alerts->acknowledge();

    const QStringList stored = Settings::instance()->acknowledgedAlerts();
    QCOMPARE(stored.size(), 1);

    // Three fields - key, severity, expiry - separated by a unit separator
    // rather than by a comma or a pipe, "because the key it has to survive is
    // an NWS URN - colons, dots and digits - and picking a printable delimiter
    // is picking one that will eventually appear in the data".
    QCOMPARE(stored.constFirst().count(QChar(0x1f)), 2);

    // The restart: reload from storage, then re-apply the same hazard.
    m_alerts->clear(false);
    m_alerts->setSettings(Settings::instance());
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 1)));

    QVERIFY2(m_alerts->isAcknowledged(), "the dismissal did not survive a reload");
}

void TestAlertsData::anAcknowledgementForAnEndedHazardIsPrunedOnLoad()
{
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));
    m_alerts->acknowledge();
    QCOMPARE(Settings::instance()->acknowledgedAlerts().size(), 1);

    // Past the hazard's end (06:00 on the 7th). "An entry whose hazard is over
    // is a trap the next time the same county is warned about the same thing" -
    // without the prune, next August's Heat Advisory for Siskiyou County would
    // arrive already dismissed.
    m_clock.setNow(at(8, 0));
    m_alerts->setSettings(Settings::instance());

    Alert nextYear     = heatAdvisory();
    nextYear.id        = QStringLiteral("urn:oid:siskiyou.002.1");
    nextYear.effective = at(8, 0);
    nextYear.onset     = at(8, 0);
    nextYear.expires   = at(9, 0);
    nextYear.ends      = at(9, 0);

    m_alerts->apply(setOf({ nextYear }, at(8, 0)));

    QCOMPARE(m_alerts->count(), 1);
    QVERIFY2(!m_alerts->isAcknowledged(), "a stale acknowledgement dismissed a new hazard");
}

void TestAlertsData::aStoredLineWithTheWrongShapeIsSkippedRatherThanCrashing()
{
    // A settings file edited by hand, truncated by a full disk, or written by a
    // version of Climat with a different format. Every one of these has to be
    // dropped quietly: refusing to start because a dismissal is unreadable
    // would be the worst possible trade.
    const QChar sep(0x1f);
    Settings::instance()->setAcknowledgedAlerts({
        QStringLiteral("no separators at all"),
        QStringLiteral("two") + sep + QStringLiteral("fields"),
        QStringLiteral("a") + sep + QStringLiteral("b") + sep + QStringLiteral("c")
            + sep + QStringLiteral("d"),
        QString(),
        // Right shape, unparseable contents: severity is not a number and the
        // instant is not ISO 8601. Both fall back rather than throw.
        QStringLiteral("key") + sep + QStringLiteral("nonsense") + sep
            + QStringLiteral("also nonsense"),
    });

    m_alerts->setSettings(Settings::instance());
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QCOMPARE(m_alerts->count(), 1);
    QVERIFY(!m_alerts->isAcknowledged());
}

void TestAlertsData::theStoredKeyMayContainAnythingAUrnCan()
{
    // The reason the separator is U+001F. An NWS identity key is a URN with
    // colons, dots and digits in it, and any printable delimiter would
    // eventually split one in half - at which point the stored line has four
    // fields, is skipped as malformed, and the dismissal silently stops working
    // for exactly the alerts that update most often.
    Alert urn        = heatAdvisory();
    urn.identityKeys = { QStringLiteral("nws:urn:oid:2.49.0.1.840.0.a,b|c;d.001.1") };

    m_alerts->apply(setOf({ urn }, at(6, 0)));
    m_alerts->acknowledge();

    const QStringList stored = Settings::instance()->acknowledgedAlerts();
    QCOMPARE(stored.size(), 1);
    QCOMPARE(stored.constFirst().count(QChar(0x1f)), 2);

    m_alerts->setSettings(Settings::instance());
    m_alerts->apply(setOf({ urn }, at(6, 1)));
    QVERIFY(m_alerts->isAcknowledged());
}

// ============================================================================
// The schedule.
// ============================================================================

void TestAlertsData::theScheduleIsTheOneInTheArchitectureDocument_data()
{
    QTest::addColumn<bool>("visible");
    QTest::addColumn<bool>("focused");
    QTest::addColumn<int>("intervalMs");

    // alertsdata.h, transcribed:
    //     visible and focused      3 min
    //     visible, not focused    10 min
    //     hidden                  stopped
    QTest::newRow("visible and focused")   << true  << true  << 3 * 60 * 1000;
    QTest::newRow("visible, not focused")  << true  << false << 10 * 60 * 1000;
    QTest::newRow("hidden and focused")    << false << true  << 0;
    QTest::newRow("hidden and unfocused")  << false << false << 0;
}

void TestAlertsData::theScheduleIsTheOneInTheArchitectureDocument()
{
    QFETCH(bool, visible);
    QFETCH(bool, focused);
    QFETCH(int, intervalMs);

    m_alerts->setWindowState(visible, focused);

    // The metered case is deliberately not a row: QNetworkInformation has no
    // backend in a headless run, so metered() answers false here whatever the
    // machine is doing. Asserting 15 minutes would be asserting the absence of
    // a backend rather than the schedule.
    QCOMPARE(m_alerts->pollIntervalMs(), intervalMs);
}

void TestAlertsData::aHiddenWindowStopsPollingAltogether()
{
    // Not "polls more slowly". Stopped - this is the line the bandwidth
    // arithmetic in alertsdata.h rests on, and it is the schedule's most
    // important entry and its least obvious one.
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    m_alerts->setWindowState(true, true);
    QVERIFY(m_alerts->pollIntervalMs() > 0);

    m_alerts->setWindowState(false, false);
    QCOMPARE(m_alerts->pollIntervalMs(), 0);

    // And it comes back when the window does, without a new payload.
    m_alerts->setWindowState(true, true);
    QCOMPARE(m_alerts->pollIntervalMs(), 3 * 60 * 1000);
}

void TestAlertsData::aPlaceWithNoCoverageIsNotPolled()
{
    // pollIntervalMs() answers what the WINDOW state implies, and reschedule()
    // additionally refuses to start a timer for a place with no provider. The
    // two are separate on purpose: the first is a preference, the second is
    // "there is nobody to ask".
    m_alerts->clear(false);
    m_alerts->setWindowState(true, true);

    QSignalSpy refreshes(m_alerts, &AlertsData::refreshRequested);
    QVERIFY(refreshes.isValid());

    // Three minutes of wall clock is not something a test may wait for. What is
    // observable without waiting is that nothing was requested synchronously,
    // and that the model reports an unavailable place.
    QVERIFY(!m_alerts->isAvailable());
    QCOMPARE(refreshes.count(), 0);
}

void TestAlertsData::aHiddenWindowKeepsPollingWhenItWasAskedToInterrupt()
{
    // §4.5's own exception, and the thing that makes a notification worth
    // having: a warning that only ever arrives while the reader is already
    // looking at the banner is not a warning. Fifteen minutes - the slowest
    // rate that keeps the feature true.
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    m_alerts->setWindowState(false, false);
    QCOMPARE(m_alerts->pollIntervalMs(), 0);

    Settings::instance()->setAlertNotifications(true);
    QCOMPARE(m_alerts->pollIntervalMs(), 15 * 60 * 1000);

    // And a visible window is unaffected: the preference buys a floor under a
    // hidden one, not a different schedule for everybody.
    m_alerts->setWindowState(true, true);
    QCOMPARE(m_alerts->pollIntervalMs(), 3 * 60 * 1000);
}

void TestAlertsData::showingTheWindowAsksForWhatWasMissed()
{
    // The other half of "hidden means stopped". Stopping bounds the bandwidth
    // and also means the set on screen is as old as the moment the window was
    // hidden, so bringing it back has to ask - otherwise a warning issued
    // while the app sat in the dock is missing from the banner for a further
    // three minutes, which is the one moment the banner is for.
    //
    // The minute tick cannot stand in for it: it re-filters what is held
    // against the clock, so it retires a hazard that has ended and can never
    // introduce one that has begun.
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QSignalSpy asked(m_alerts, &AlertsData::refreshRequested);

    m_alerts->setWindowState(false, false);
    QCOMPARE(asked.count(), 0);

    m_alerts->setWindowState(true, true);
    QCOMPARE(asked.count(), 1);
}

void TestAlertsData::aWindowMerelyLosingFocusDoesNotFetch()
{
    // Focus changes several times a minute on a desktop. Only the hidden ->
    // visible transition is a catch-up; everything else is a rate change.
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QSignalSpy asked(m_alerts, &AlertsData::refreshRequested);

    m_alerts->setWindowState(true, false);
    m_alerts->setWindowState(true, true);
    m_alerts->setWindowState(true, false);

    QCOMPARE(asked.count(), 0);
}

void TestAlertsData::aPlaceWithNoCoverageIsNotAskedOnShowing()
{
    // There is nobody to ask. `available` is false before any set has been
    // applied and after clear(false), and a fetch then would be a request on
    // behalf of a feature the UI is hiding.
    m_alerts->clear(/*available=*/false);

    QSignalSpy asked(m_alerts, &AlertsData::refreshRequested);

    m_alerts->setWindowState(false, false);
    m_alerts->setWindowState(true, true);

    QCOMPARE(asked.count(), 0);
}

// ============================================================================
// What is worth interrupting somebody for.
//
// The POLICY, asserted through `announced` - which is why that signal exists.
// Whether a notification reaches a desktop is tst_notifier's question and needs
// a session bus; whether one should be posted at all is this file's, and needs
// nothing.
// ============================================================================

void TestAlertsData::nothingIsAnnouncedWhileTheBannerIsOnScreen()
{
    Settings::instance()->setAlertNotifications(true);
    QSignalSpy announced(m_alerts, &AlertsData::announced);

    m_alerts->setWindowState(true, true);
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    // The banner is right there. A desktop notification for something two
    // centimetres away is noise.
    QCOMPARE(announced.count(), 0);
}

void TestAlertsData::aHiddenWindowAnnouncesAHazardTheReaderHasNotSeen()
{
    Settings::instance()->setAlertNotifications(true);
    QSignalSpy announced(m_alerts, &AlertsData::announced);

    m_alerts->setWindowState(false, false);
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QCOMPARE(announced.count(), 1);
    QCOMPARE(announced.constFirst().at(0).toString(),
             QStringLiteral("nws:siskiyou:heat-advisory"));
    QCOMPARE(announced.constFirst().at(1).toString(), QStringLiteral("moderate"));
}

void TestAlertsData::anUpdateAtTheSameGradeIsNotAnnouncedTwice()
{
    Settings::instance()->setAlertNotifications(true);
    m_alerts->setWindowState(false, false);
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QSignalSpy announced(m_alerts, &AlertsData::announced);

    // NWS re-sends an alert in full under a new id on every update - 24 of the
    // 25 alerts in force in California on the recording afternoon were
    // updates. Identity is the hazard, so this is not news.
    Alert again = heatAdvisory();
    again.id = QStringLiteral("urn:oid:siskiyou.001.2");
    m_alerts->apply(setOf({ again }, at(6, 0, 30)));

    QCOMPARE(announced.count(), 0);
}

void TestAlertsData::anNwsUpdateChainIsOneHazardAllTheWayDown()
{
    // The shape NWS actually sends, which the helper above cannot express: an
    // update carries its OWN id first and the one id it replaces second, so
    // identityKeys is a two-element chain one hop long. Every recorded NWS
    // fixture in this repository has references of length 1, and
    // tst_alertproviders pins identityKeys.size() == 2.
    //
    // Recognising that chain takes two things and the first version of this
    // fix did only one. Looking the incoming keys up in what has been
    // announced is necessary; RECORDING every one of them is what makes the
    // next hop match. Keyed on one, k1 -> [k2,k1] -> [k3,k2] recognises the
    // second message and not the third, and a long-running warning
    // re-interrupts the reader on every other update.
    const auto message = [](const QString &own, const QString &replaces) {
        Alert alert        = heatAdvisory();
        alert.id           = own;
        alert.identityKeys = replaces.isEmpty() ? QStringList{ own }
                                                : QStringList{ own, replaces };
        return alert;
    };

    Settings::instance()->setAlertNotifications(true);
    m_alerts->setWindowState(false, false);

    QSignalSpy announced(m_alerts, &AlertsData::announced);

    m_alerts->apply(setOf({ message(QStringLiteral("k1"), QString()) }, at(6, 0)));
    QCOMPARE(announced.count(), 1);

    // Four updates, none of them news. Without every key recorded, the third
    // and the fifth announce again.
    m_alerts->apply(setOf({ message(QStringLiteral("k2"), QStringLiteral("k1")) }, at(6, 0, 15)));
    m_alerts->apply(setOf({ message(QStringLiteral("k3"), QStringLiteral("k2")) }, at(6, 0, 30)));
    m_alerts->apply(setOf({ message(QStringLiteral("k4"), QStringLiteral("k3")) }, at(6, 0, 45)));
    m_alerts->apply(setOf({ message(QStringLiteral("k5"), QStringLiteral("k4")) }, at(6, 0, 50)));

    QCOMPARE(announced.count(), 1);

    // And the notification stays addressed to the key it was posted under, so
    // the update that finally raises the grade replaces it rather than opening
    // a second one beside it.
    Alert worse        = message(QStringLiteral("k6"), QStringLiteral("k5"));
    worse.severity     = AlertSeverity::Extreme;
    m_alerts->apply(setOf({ worse }, at(6, 0, 55)));

    QCOMPARE(announced.count(), 2);
    QCOMPARE(announced.at(1).at(0).toString(), announced.at(0).at(0).toString());
    QCOMPARE(announced.at(1).at(1).toString(), QStringLiteral("extreme"));
}

void TestAlertsData::aRaiseInSeverityIsAnnouncedAgain()
{
    Settings::instance()->setAlertNotifications(true);
    m_alerts->setWindowState(false, false);
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QSignalSpy announced(m_alerts, &AlertsData::announced);

    // The same hazard, graded worse. That is news, and it replaces rather than
    // stacks - the same rule the banner's dismissal follows.
    Alert worse   = heatAdvisory();
    worse.id       = QStringLiteral("urn:oid:siskiyou.001.3");
    worse.severity = AlertSeverity::Extreme;
    m_alerts->apply(setOf({ worse }, at(6, 0, 30)));

    QCOMPARE(announced.count(), 1);
    QCOMPARE(announced.constFirst().at(1).toString(), QStringLiteral("extreme"));
}

void TestAlertsData::nothingIsAnnouncedWithThePreferenceOff()
{
    // The default, and the case that was wrong first: rebuild() runs every
    // minute whether or not anything was fetched, so a hazard reaching its
    // onset under a hidden window announced itself with the switch off and
    // with nothing having polled for it at all.
    Settings::instance()->setAlertNotifications(false);
    QSignalSpy announced(m_alerts, &AlertsData::announced);

    m_alerts->setWindowState(false, false);
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QCOMPARE(announced.count(), 0);
}

void TestAlertsData::showingTheWindowTakesTheNotificationDown()
{
    // The banner says everything the notification does and says it better, so
    // opening the window takes the notification down - but it must NOT forget
    // that the reader has been told, or hiding the window again would announce
    // the same hazard a second time.
    Settings::instance()->setAlertNotifications(true);
    m_alerts->setWindowState(false, false);
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QSignalSpy withdrawn(m_alerts, &AlertsData::withdrawn);
    QSignalSpy announced(m_alerts, &AlertsData::announced);

    m_alerts->setWindowState(true, true);
    QCOMPARE(withdrawn.count(), 1);
    QCOMPARE(withdrawn.constFirst().at(0).toString(),
             QStringLiteral("nws:siskiyou:heat-advisory"));

    // Hidden again: still nothing new to say.
    m_alerts->setWindowState(false, false);
    QCOMPARE(announced.count(), 0);
}

void TestAlertsData::aWarningTheReaderWasLookingAtIsNotAnnouncedOnMinimising()
{
    // Minimising must not fire a notification for every warning the reader was
    // just reading. Announcing is gated on the window being hidden, so a
    // hazard that stood only while it was visible would otherwise be "new" the
    // instant it was hidden - which is what running the announcement pass on
    // the visibility change made possible.
    //
    // The answer is that a hazard on the banner is RECORDED as told without
    // being posted. This is the case that says so.
    Settings::instance()->setAlertNotifications(true);
    m_alerts->setWindowState(true, true);
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QSignalSpy announced(m_alerts, &AlertsData::announced);

    m_alerts->setWindowState(false, false);
    QCOMPARE(announced.count(), 0);
}

void TestAlertsData::aHazardThatEndsTakesItsNotificationWithIt()
{
    // A reader should not have to dismiss a warning about weather that is
    // over. The set is filtered against the clock every minute, so the hazard
    // leaves the screen on its own; the notification has to follow it.
    Settings::instance()->setAlertNotifications(true);
    m_alerts->setWindowState(false, false);
    m_alerts->apply(setOf({ heatAdvisory() }, at(6, 0)));

    QSignalSpy withdrawn(m_alerts, &AlertsData::withdrawn);

    // Past `ends` - 06:00 on the 7th.
    m_clock.setNow(at(7, 6, 1));
    m_alerts->apply(setOf({ heatAdvisory() }, at(7, 6, 1)));

    QCOMPARE(m_alerts->count(), 0);
    QCOMPARE(withdrawn.count(), 1);
}

QTEST_MAIN(TestAlertsData)
#include "tst_alertsdata.moc"
