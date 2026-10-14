// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// How much of the window the system draws over: the status bar, the gesture
// strip, a camera cutout.
//
// Android 15 and later draw every app that targets them edge to edge, under
// the status bar and the navigation bar, and there is no opting out from
// Android 16. So a phone's top row of controls sat under the clock until the
// shell padded by these numbers.
//
// Qt reports them from 6.9, as QWindow::safeAreaMargins(). Before that, and on
// every desktop, they are zero, so a desktop window and every golden image is
// laid out exactly as it was. The SafeArea attached property says the same
// thing to QML, but only from 6.9, and this project's floor is 6.8: an
// attached type the build cannot find is a QML file that does not load.
#pragma once

#include <QMargins>
#include <QObject>
#include <QQmlEngine>
#include <QWindow>

class WindowInsets : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(qreal top READ top NOTIFY changed)
    Q_PROPERTY(qreal bottom READ bottom NOTIFY changed)
    Q_PROPERTY(qreal left READ left NOTIFY changed)
    Q_PROPERTY(qreal right READ right NOTIFY changed)

public:
    explicit WindowInsets(QObject *parent = nullptr);

    qreal top() const { return m_margins.top(); }
    qreal bottom() const { return m_margins.bottom(); }
    qreal left() const { return m_margins.left(); }
    qreal right() const { return m_margins.right(); }

    // Main.qml hands over its window once it exists. The singleton cannot go
    // looking for it: it can be created before the window is.
    Q_INVOKABLE void watch(QWindow *window);

Q_SIGNALS:
    void changed();

private:
    void apply(const QMargins &margins);

    QMargins m_margins;
};
