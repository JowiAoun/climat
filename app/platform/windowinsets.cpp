// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windowinsets.h"

#include <QtGlobal>

WindowInsets::WindowInsets(QObject *parent)
    : QObject(parent)
{
}

void WindowInsets::watch(QWindow *window)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    if (window == nullptr)
        return;
    connect(window, &QWindow::safeAreaMarginsChanged, this,
            [this](QMargins margins) { apply(margins); });
    apply(window->safeAreaMargins());
#else
    Q_UNUSED(window)
#endif
}

void WindowInsets::apply(const QMargins &margins)
{
    if (margins == m_margins)
        return;
    m_margins = margins;
    Q_EMIT changed();
}
