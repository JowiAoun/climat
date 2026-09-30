// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Gives a test process a preferences file of its own, on every platform.
//
// QStandardPaths::setTestModeEnabled() moves GenericConfigLocation, and on
// Linux that is where an INI QSettings keeps its file. On macOS the file is
// under ~/.config and on Windows under %APPDATA%, and test mode moves neither,
// so a test that only turned test mode on wrote into the real configuration
// directory there. QSettings::setPath() moves it on all three.
//
// The INI format is set here too, as main() sets it. Without it Windows uses
// the registry, and a registry QSettings with no organisation name has no key
// to open: every preference a test wrote read back as its default.
//
// Call it from initTestCase(), before anything constructs a QSettings. One
// that already exists keeps the file it was given.
#pragma once

#include <QSettings>
#include <QStandardPaths>

struct SettingsSandbox
{
    static void install()
    {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    }
};
