// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The alerts, in the shape a banner reads, plus the two things that are
// genuinely this layer's job: when to poll, and what a dismissal means.
//
// ============================================================================
// EVERY QUESTION IS ASKED AGAINST THE CLOCK, NOT AGAINST THE FETCH
//
// `AlertSet::displayableAt(now)` is re-run on a ticking timer rather than only
// when a payload arrives, and that is the safety property of the whole feature.
// It means an alert cannot outlive its hazard because the network went away: a
// set served from a cache written yesterday is filtered against today, so
// everything in it that has ended is gone before anything is drawn. There is no
// path here where "we could not refresh" turns into "a warning is still on the
// screen an hour after it ended".
//
// It is also why docs/04-architecture.md §4.5's alerts row is the only one with
// staleWhileRevalidate false and why that is not contradicted by the providers
// serving a stale set on a failed refresh. Those are different questions. The
// cache decides whether to hand over bytes; this decides what is on the screen,
// and it decides it every minute.
//
// ============================================================================
// DISMISSAL IS ACKNOWLEDGEMENT. IT IS NOT DELETION.
//
// Dismissing collapses the banner to a one-line strip. It does not remove the
// alert, it cannot remove it from the sheet, and it lapses the moment the
// issuer says something worse.
//
//   the key      Alert::identityKeys - the hazard, not the message. NWS re-sends
//                an alert in full on every update under a new id, and 24 of the
//                25 alerts in force in California on the recording afternoon
//                were updates. Keyed by message id, a dismissal would come
//                undone several times a day.
//
//   the lapse    the severity that was acknowledged is stored with the key. An
//                update carrying a HIGHER severity does not match, so the banner
//                comes back at full height. An update at the same or a lower
//                grade stays collapsed, which is the common case and the one a
//                user would be annoyed to lose.
//
//   the expiry   each entry carries the hazard's end, and entries past it are
//                pruned on load. Without that the list grows forever, and a
//                stored acknowledgement for a hazard that ended in March is a
//                small trap waiting for the same county to be warned again.
//
// Persisted through Settings, as opaque strings whose format is owned here. A
// dismissal that did not survive a restart would re-raise a multi-day heat
// warning every time the app was opened, which is precisely the behaviour that
// teaches people to ignore banners.
//
// ============================================================================
// WHAT IS WORTH INTERRUPTING SOMEBODY FOR
//
// A notification goes out for a hazard the reader has not been told about, and
// only while the window is hidden - the banner is already on the screen
// otherwise, and a desktop notification for something two centimetres away is
// noise. `announced` carries every one, so the policy is testable without a
// desktop; app/platform/notifier.h carries it to one.
//
// Re-announcement follows exactly the rule dismissal follows, and for the same
// reason: NWS re-sends an alert in full under a new id on every update, so
// identity is the hazard rather than the message, and a grade that has not
// risen is not news. Higher severity re-announces and replaces; the same or
// lower does nothing. A hazard that stops being displayable is withdrawn.
//
// ============================================================================
// THE POLL SCHEDULE, AND WHAT IT ACTUALLY COSTS
//
// docs/04-architecture.md §4.5: three minutes in the foreground. Then:
//
//     visible and focused      3 min
//     visible, not focused    10 min
//     hidden                  stopped - unless notifications are on
//     hidden, notifying       15 min
//     metered                 15 min
//
// The fourth line is §4.5's own exception - "no background polling while the
// window is hidden *unless the user enabled alert notifications*" - and it is
// what makes the notification worth having. A warning that only arrives while
// the reader is already looking at the banner is not a warning, and stopping
// the poll for a window somebody asked to be interrupted from would be
// answering a question they did not ask. Off by default, so the schedule above
// is what almost every run does.
//
// The plan estimated ~264 KB/day. That assumed both services revalidate, and
// only one does: api.weather.gov sends an ETag and most of its polls come back
// 304, while api.weather.gc.ca sends no validator at all - no ETag, no
// Last-Modified, no Cache-Control - so every Canadian poll is a full ~10 kB.
// Verified, and recorded in tests/fixtures/alerts/README.md.
//
// A day of uninterrupted foreground polling in Canada would therefore be nearer
// 5 MB than 264 kB. Nothing is in the foreground for a day; what actually bounds
// this is the "hidden means stopped" line above, which is why that line is the
// schedule's most important entry and not its most obvious one.

#pragma once

#include "libclimat/domain/alert.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <QTimeZone>
#include <QVariantList>
#include <QVariantMap>

namespace climat {
class Clock;
}

class Notifier;
class Settings;

class AlertsData : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Alerts)
    QML_SINGLETON

    // ---- what is on the screen ---------------------------------------------

    // Displayable alerts, ranked worst first. Everything the sheet lists.
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)

    // The one the banner shows. Empty when there is nothing to show - a banner
    // binds `visible: Alerts.count > 0` rather than testing this.
    Q_PROPERTY(QVariantMap top READ top NOTIFY changed)

    // "+2 more". Zero when the banner is showing everything there is.
    Q_PROPERTY(int moreCount READ moreCount NOTIFY changed)

    // ---- how much to believe it --------------------------------------------

    // False when at least one provider covering this place did not answer. The
    // sheet says so; saying nothing would be claiming a complete picture we do
    // not have.
    Q_PROPERTY(bool complete READ isComplete NOTIFY changed)

    // Past the issuer's own refresh deadline AND the last poll failed. This is
    // the "last confirmed 14:05" state - never silently keep an alert, never
    // silently drop it.
    Q_PROPERTY(bool unconfirmed READ isUnconfirmed NOTIFY changed)
    Q_PROPERTY(QString confirmedLabel READ confirmedLabel NOTIFY changed)

    // Whether this place has alert coverage at all. False hides the feature
    // outright rather than showing an empty one - §4.4.
    Q_PROPERTY(bool available READ isAvailable NOTIFY changed)

    // Comma-joined ids of the services that answered, for the sheet's footer.
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY changed)

    // ---- acknowledgement ----------------------------------------------------

    // True when the top alert has been dismissed at this severity or higher.
    // The banner collapses to a strip; it does not disappear.
    Q_PROPERTY(bool acknowledged READ isAcknowledged NOTIFY changed)

public:
    ~AlertsData() override;

    static AlertsData *instance();
    static AlertsData *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    // `clock` is the app's, so a fixture run judges alerts against the instant
    // the fixture was recorded. Without it a recorded banner would empty itself
    // the day the recording aged out, and every golden image of it with it.
    void setClock(climat::Clock *clock);
    void setSettings(Settings *settings);

    // The zone of the place the alerts are for. Every "Until 11:00 PM" is read
    // in it, as every other time in the app is. The machine's own zone until
    // AppEngine says otherwise.
    void setTimeZone(const QTimeZone &zone);

    // A new set from the engine. `complete` and `confirmedAt` travel inside it.
    void apply(const climat::AlertSet &set);

    // Whether the most recent refresh attempt failed. Pushed in rather than
    // inferred: a provider serving a stale set on a failed refresh parses the
    // cached bytes, so `fetchedAt` and `confirmedAt` come out of the same cache
    // row and comparing them can never detect anything. Only the engine knows
    // that a request came back an Error.
    void setRefreshFailed(bool failed);

    // The place has no alert provider, or every one of them declined. Clears
    // the set and reports `available` false.
    void clear(bool available);

    [[nodiscard]] QVariantList list() const { return m_list; }
    [[nodiscard]] int          count() const { return int(m_list.size()); }
    [[nodiscard]] QVariantMap  top() const;
    [[nodiscard]] int          moreCount() const;

    [[nodiscard]] bool    isComplete() const { return m_set.complete; }
    [[nodiscard]] bool    isUnconfirmed() const;
    [[nodiscard]] QString confirmedLabel() const;
    [[nodiscard]] bool    isAvailable() const { return m_available; }
    [[nodiscard]] QString sourceName() const { return m_set.providerId; }
    [[nodiscard]] bool    isAcknowledged() const;

    // ---- what the UI calls --------------------------------------------------

    // Collapse the banner for the top alert's hazard, at its current severity.
    Q_INVOKABLE void acknowledge();

    // Expand it again. The user asking to see it is always allowed.
    Q_INVOKABLE void reveal();

    // Where an announcement goes. Not owned: the app hands in one Notifier for
    // the process, and a test hands in nothing and watches `announced`.
    void setNotifier(Notifier *notifier);

    // How the QML says what state the window is in, which is what decides the
    // poll interval. Pushed rather than read, because a view model that reached
    // for a QWindow would be a view model that cannot be tested without one.
    Q_INVOKABLE void setWindowState(bool visible, bool focused);

    // Milliseconds until the next poll, or 0 when polling is stopped. For the
    // diagnostics panel, and for the test that asserts the schedule without
    // waiting fifteen minutes.
    [[nodiscard]] Q_INVOKABLE int pollIntervalMs() const;

Q_SIGNALS:
    void changed();

    // One per hazard actually announced, with the grade it was announced at.
    // The policy above, made observable - tst_alertsdata asserts against this
    // and needs no session bus to do it.
    void announced(const QString &key, const QString &severityKey);

    // The other half: a hazard that ended, a reader who opened the window, or
    // the preference being switched off.
    void withdrawn(const QString &key);

    // Time to ask again. AppEngine connects this; this class does not know what
    // a provider is.
    void refreshRequested();

private:
    AlertsData();

    // Re-runs displayableAt() against the clock and republishes. Called on a
    // new set, on acknowledgement, and every minute from m_tick.
    void rebuild();

    void reschedule();

    // What `alertNotificationsChanged` runs. A named member rather than the
    // lambda this used to be, because the connection is a Qt::UniqueConnection
    // and Qt only honours that for a pointer to a member function - with a
    // functor it asserts, which aborted every Debug build the moment
    // setSettings() ran. See the connect in setSettings().
    void applyNotificationPreference();

    // Posts for what is newly worth posting for, withdraws what has ended.
    // Called from rebuild(), which is the one place that knows what is
    // displayable at this minute.
    void announce(const QList<climat::Alert> &shown);

    // Takes one down, or all of them. Both emit `withdrawn`, so the half of
    // the policy that removes a notification is as observable as the half that
    // posts one - without either of them needing a desktop.
    void withdraw(const QString &key);
    void takeDownEverythingPosted();

    [[nodiscard]] QVariantMap toVariant(const climat::Alert &alert) const;
    [[nodiscard]] QDateTime   now() const;

    // ---- the acknowledgement store -----------------------------------------

    struct Acknowledgement {
        QString              key;
        climat::AlertSeverity severity = climat::AlertSeverity::Unknown;
        QDateTime            until;
    };

    void loadAcknowledgements();
    void saveAcknowledgements();

    [[nodiscard]] bool isAcknowledged(const climat::Alert &alert) const;

    climat::Clock *m_clock    = nullptr;
    Settings     *m_settings = nullptr;
    QTimeZone     m_zone     = QTimeZone::systemTimeZone();

    climat::AlertSet m_set;
    QVariantList    m_list;
    bool            m_available     = false;
    bool            m_refreshFailed = false;

    QList<Acknowledgement> m_acknowledged;

    // Every minute, whatever the poll interval is. This is the timer that makes
    // an alert leave the screen when its hazard ends rather than when the next
    // payload happens to arrive - a Heat Advisory that ends at 23:00 must be
    // gone at 23:00, not at 23:09.
    QTimer m_tick;

    QTimer m_poll;
    bool   m_visible = true;
    bool   m_focused = true;

    Notifier *m_notifier = nullptr;

    // What one hazard's announcement remembers. Held against EVERY key the
    // hazard answers to, so an update that leads with a new message id still
    // finds it - NWS re-sends under a new id and carries only the id it
    // replaces, so the keys form a chain one hop long and a record kept
    // against a single key goes stale after two.
    struct Announcement {
        // The key the notification was actually posted under. Every later
        // update addresses that one, so the desktop replaces the popup on
        // screen rather than opening a second beside it. Re-derived from the
        // incoming keys it would drift down the chain, which is a fresh
        // notification for a hazard the reader has already been told about.
        QString canonical;

        // And the grade they were told at, which only ever rises.
        climat::AlertSeverity severity = climat::AlertSeverity::Unknown;
    };

    // Kept in memory only, because a notification that survived a restart
    // would be a notification for something the reader has already been shown.
    QHash<QString, Announcement> m_announced;

    // And which of those are on the screen right now, which is not the same
    // list. Showing the window takes the notifications down - the banner is
    // the better copy of the same news - but it must NOT forget that the
    // reader has been told, or hiding the window again would announce
    // everything a second time. m_announced is the memory; this is the state.
    QSet<QString> m_posted;
};
