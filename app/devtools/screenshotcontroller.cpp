// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later

#include "devtools/screenshotcontroller.h"

#include <QCoreApplication>
#include <QImage>
#include <QPauseAnimation>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>

#include <utility>

namespace {

// Every delay in this file, with the reason it is what it is. They were tuned
// against the real scenes and every one of them is the difference between a
// golden image and a lottery ticket.
namespace when {

// Long enough for the details grid's staggered reveal to finish: the last card
// waits a stagger-per-card into the wave and then takes a full reveal of its
// own. Grab before that lands and every golden image catches a different card
// mid-sweep.
constexpr int grab = 1600;

// After first paint, before anything a person would call "settled". Both the
// walk and the scroll need a laid-out scene rather than a settled one -
// contentHeight is still 0 during componentComplete, and a contentY assigned
// against it is clamped straight back to zero.
constexpr int placement = 400;

// A poke without a film is a poke whose result gets grabbed at rest, so it has
// to land early enough for the motion it causes to finish before `grab` above.
constexpr int poke = 500;

// Filming starts later than everything else on purpose: frame 00 has to be a
// genuinely quiet "before", so the mount motion of whatever is on screen must
// be over before the first shutter.
constexpr int film = 900;

} // namespace when

// PNG stores colour with alpha kept apart, and a grab arrives premultiplied, so
// something has to divide by alpha before the file is written. Left to QImage,
// that runs on a SIMD path built on an approximate reciprocal, and AMD and Intel
// approximate it differently. The desktop tiles are the one translucent capture,
// and CI drew them a level apart on 510 pixels depending on which runner it got.
// qUnpremultiply() is integer arithmetic and gives the same answer on any CPU.
// An opaque pixel comes through untouched, so every other image is unchanged.
QImage straightAlpha(const QImage &grabbed)
{
    if (!grabbed.hasAlphaChannel())
        return grabbed;

    QImage image = grabbed.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (!image.reinterpretAsFormat(QImage::Format_ARGB32))
        return grabbed;
    for (int y = 0; y < image.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            line[x] = qUnpremultiply(line[x]);
    }
    return image;
}

} // namespace

ScreenshotController::ScreenshotController(QObject *parent)
    : QObject(parent)
{
}

void ScreenshotController::setWindow(QQuickWindow *window)
{
    if (m_window == window)
        return;
    m_window = window;
    Q_EMIT windowChanged();
}

void ScreenshotController::setShell(QQuickItem *shell)
{
    if (m_shell == shell)
        return;
    m_shell = shell;
    Q_EMIT shellChanged();
}

void ScreenshotController::setGallery(QQuickItem *gallery)
{
    if (m_gallery == gallery)
        return;
    m_gallery = gallery;
    Q_EMIT galleryChanged();
}

void ScreenshotController::setMobile(bool mobile)
{
    if (m_mobile == mobile)
        return;
    m_mobile = mobile;
    Q_EMIT mobileChanged();
}

void ScreenshotController::after(int milliseconds, void (ScreenshotController::*slot)())
{
    // DeleteWhenStopped, so each of these cleans itself up after it fires. The
    // connection to `slot` is made first and therefore runs first: a queued
    // deletion cannot outrun a direct signal emission.
    QPauseAnimation *pause = new QPauseAnimation(milliseconds, this);
    connect(pause, &QAbstractAnimation::finished, this, slot);
    pause->start(QAbstractAnimation::DeleteWhenStopped);
}

bool ScreenshotController::offer(QQuickItem *item, const char *property, const QVariant &value)
{
    if (item == nullptr)
        return false;
    // indexOfProperty rather than property().isValid(): a QML property that
    // currently holds undefined reads back as an invalid QVariant, and "the
    // property is empty" is not "the property does not exist".
    if (item->metaObject()->indexOfProperty(property) < 0)
        return false;
    return item->setProperty(property, value);
}

void ScreenshotController::start()
{
#ifdef CLIMAT_DEV_TOOLS
    // Order matters here in exactly one way and it is worth naming: the state
    // flags go on before the capture timers start, so a --grab of a --metric is
    // a grab of the metric and not of the moment before it.
    applyOpeningState();

    // Scheduled on the count alone and not on "is there a gallery yet". There
    // may not be: `gallery` is bound to a Loader's item, and whether that
    // binding has run by the time a Window's Component.onCompleted calls start()
    // is not something to depend on. onWalk asks again when it fires, which is
    // 400 ms later and certain.
    if (m_walk > 0)
        after(when::placement, &ScreenshotController::onWalk);

    if (m_scroll >= 0)
        after(when::placement, &ScreenshotController::onScroll);

    if (!m_film.isEmpty())
        after(when::film, &ScreenshotController::startFilming);
    else if (!m_pokes.isEmpty())
        // --poke without --film: apply once the scene has settled, so a plain
        // --grab captures a poked *resting* state rather than a transition.
        after(when::poke, &ScreenshotController::applyPokes);
#endif

    if (m_grab.isEmpty())
        return;

    // The precipitation field is the one thing on this page that moves without
    // being asked, so a grab of it would otherwise catch a different frame
    // every run and no two golden images would agree. Frozen, it still draws
    // rain - precip.js seeds every drop from its hour, so the frozen frame is a
    // deterministic one rather than an empty one.
    //
    // Offered rather than assigned, which is MobileShell.push()'s rule and is
    // here for the same reason: in the gallery there is no shell at all.
    offer(m_shell, "animated", false);

    after(when::grab, &ScreenshotController::onGrab);
}

void ScreenshotController::grabTo(const QString &file, bool quitWhenSaved)
{
    if (m_window == nullptr) {
        qWarning("grab: no window to photograph");
        QCoreApplication::quit();
        return;
    }

    // contentItem, not the window: grabToImage() renders an item tree into an
    // FBO and never sees QQuickWindow::color, so grabbing the window instead
    // would put whatever the clear colour happens to be behind the page.
    const QSharedPointer<QQuickItemGrabResult> result = m_window->contentItem()->grabToImage();
    if (result.isNull()) {
        qWarning("grab: grabToImage refused");
        QCoreApplication::quit();
        return;
    }

    // The result is kept alive by this lambda and by nothing else - it is a
    // shared pointer whose only other owner was the local above. Capture it by
    // value or the render thread finishes into a deleted object.
    connect(result.data(), &QQuickItemGrabResult::ready, this,
            [this, result, file, quitWhenSaved] {
                if (!straightAlpha(result->image()).save(file))
                    qWarning("grab: could not write %s", qPrintable(file));
                else if (quitWhenSaved)
                    qInfo("grab: wrote %s", qPrintable(file));

#ifdef CLIMAT_DEV_TOOLS
                if (!quitWhenSaved) {
                    // Filming: the run ends when the last frame is on disk, not
                    // when the last shutter fires. Saves are asynchronous and
                    // quitting on the shutter loses the tail of the reel.
                    ++m_filmSaved;
                    if (m_filmSaved >= m_frames)
                        QCoreApplication::quit();
                    return;
                }
#endif
                QCoreApplication::quit();
            });
}

void ScreenshotController::onGrab()
{
    grabTo(m_grab, true);
}

#ifdef CLIMAT_DEV_TOOLS

void ScreenshotController::applyOpeningState()
{
    // --tab is not here: it is the shell's own opening state rather than
    // something done to a running shell, so Main.qml hands it to MobileShell at
    // construction. Assigning it from out here would rebuild the page that has
    // just finished laying itself out.
    if (!m_metric.isEmpty())
        offer(m_shell, "metricId", m_metric);
    if (m_day >= 0)
        offer(m_shell, "dayIndex", m_day);
    if (m_list)
        offer(m_shell, "listView", true);
}

void ScreenshotController::onWalk()
{
    if (m_gallery == nullptr) {
        qWarning("--walk: only meaningful in the component gallery");
        return;
    }
    for (int i = 0; i < m_walk; ++i)
        QMetaObject::invokeMethod(m_gallery, "step", Q_ARG(QVariant, QVariant(1)));
}

void ScreenshotController::onScroll()
{
    if (m_shell == nullptr)
        return;
    const qreal limit = m_shell->property("maxContentY").toReal();
    offer(m_shell, "contentY", qMin(m_scroll, limit));
}

void ScreenshotController::applyPokes()
{
    for (const QString &poke : std::as_const(m_pokes)) {
        const qsizetype split = poke.indexOf(QLatin1Char('='));
        const QString target  = poke.left(split);
        const QString value   = poke.mid(split + 1);
        const bool on         = value == QLatin1String("true") || value == QLatin1String("1");

        // Every target below except `remount` lives on the shell, and in
        // climat-gallery there is no shell. Warning beats throwing: a poke that
        // cannot land should say so, not abort the rest of the list.
        if (target != QLatin1String("remount") && target != QLatin1String("hits")
            && m_shell == nullptr) {
            qWarning("--poke %s: no shell is running", qPrintable(target));
            continue;
        }

        if (target == QLatin1String("metric")) {
            offer(m_shell, "metricId", value);
        } else if (target == QLatin1String("day")) {
            offer(m_shell, "dayIndex", value.toInt());
        } else if (target == QLatin1String("list")) {
            offer(m_shell, "listView", on);
        } else if (target == QLatin1String("feels")) {
            offer(m_shell, "feelsLike", on);
        } else if (target == QLatin1String("picker")) {
            // The place picker is a sheet the shell owns, and it is the one
            // piece of UI in this app that is otherwise reachable only by
            // clicking. Without this poke it could be reviewed by a person and
            // by nothing else - which for a screen with a search field, a saved
            // list and a failure state is the wrong side of the line.
            offer(m_shell, "pickerOpen", on);
        } else if (target == QLatin1String("prefs")) {
            // The preferences sheet, which only the desktop shell has: the phone
            // shows the same two groups inline on its Me tab, and `--poke tab=me`
            // is how that is photographed. Saying so beats offering a property
            // the mobile shell does not have and reporting nothing - `offer`
            // is silent by design, and silence here would read as a sheet that
            // failed to open.
            if (!m_mobile)
                offer(m_shell, "prefsOpen", on);
            else
                qWarning("--poke prefs: the mobile shell has no sheet; use --tab me");
        } else if (target == QLatin1String("scroll")) {
            offer(m_shell, "contentY", value.toDouble());
        } else if (target == QLatin1String("tab")) {
            // Only the mobile shell has tabs. On the desktop this is the one
            // poke that is genuinely meaningless rather than merely ignored, so
            // it says so.
            if (m_mobile)
                offer(m_shell, "tab", value);
            else
                qWarning("--poke tab: only the mobile shell has tabs");
        } else if (target == QLatin1String("flick")) {
            // Negative velocity carries the content upward, i.e. scrolls down.
            bool ok = false;
            const double velocity = value.toDouble(&ok);
            const double amount   = (!ok || velocity == 0) ? 1400.0 : velocity;
            QMetaObject::invokeMethod(m_shell, "flickBy",
                                      Q_ARG(QVariant, QVariant(-qAbs(amount))));
        } else if (target == QLatin1String("hits")) {
            // The touch-target overlay. A poke rather than a flag on the
            // gallery's parser, because it is the same kind of thing `remount`
            // is: a review state to put a capture into, not a mode the binary
            // runs in.
            if (m_gallery != nullptr)
                offer(m_gallery, "showHits", on);
            else
                qWarning("--poke hits: only meaningful in the component gallery");
        } else if (target == QLatin1String("remount")) {
            // Rebuilding the specimen replays whatever the component does on
            // mount, which for a detail card is the only animation it has - the
            // data behind these cards never changes while the app runs.
            if (m_gallery != nullptr)
                QMetaObject::invokeMethod(m_gallery, "remount");
            else
                qWarning("--poke remount: only meaningful in the component gallery");
        } else {
            qWarning("--poke: unknown target %s", qPrintable(target));
        }
    }
}

void ScreenshotController::startFilming()
{
    // Looping rather than restarted, which is how a QML Timer with repeat:true
    // behaves: the loop boundary is the tick, so the interval cannot drift by
    // however long the previous tick's work took.
    m_filmTicker = new QPauseAnimation(m_every, this);
    m_filmTicker->setLoopCount(-1);
    connect(m_filmTicker, &QAbstractAnimation::currentLoopChanged, this,
            &ScreenshotController::onFilmTick);
    m_filmTicker->start();
}

void ScreenshotController::onFilmTick()
{
    // On the second tick, so frame 00 is a settled "before" that has definitely
    // rendered and frame 01 is the first moment of change.
    if (m_filmShot == 1)
        applyPokes();

    if (m_filmShot >= m_frames) {
        // Stop the ticker but not the process: the saves are still in flight,
        // and the last one to land is what quits.
        m_filmTicker->stop();
        return;
    }

    const int index = m_filmShot++;
    const QString name = QStringLiteral("%1-%2.png")
                             .arg(m_film, QString::number(index).rightJustified(2, u'0'));
    grabTo(name, false);
}

#endif // CLIMAT_DEV_TOOLS
