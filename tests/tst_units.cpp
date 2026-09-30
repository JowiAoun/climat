// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The two unit presets, and the one state they must not round away.
//
// docs/04-architecture.md §4.10 and app/settings.h are emphatic that units in
// this app are per quantity and that there is no metric/imperial switch: people
// want °C with mph, or inHg with mm. The preferences screen offers a °C and a °F
// preset anyway, because changing five rows to read Fahrenheit - and knowing
// that inHg goes with miles - is not a settings screen anybody finishes.
//
// The preset is a shortcut that writes the five, not a sixth preference, and
// this file is where that distinction is held to:
//
//   * `system()` must answer "custom" for a mixture. A screen that filled the
//     metric radio for °C-with-mph would be telling the reader their units are
//     something they are not - and the next thing they do is tap it, which
//     silently rewrites the other four.
//
//   * the metric preset must be exactly what Settings falls back to with nothing
//     stored, or a reader who has never opened the screen sees "custom" selected
//     on a fresh install. Two tables, one answer.
//
//   * `applySystem` must refuse a name it does not know. Five preferences
//     written from a typo is a reader reset to Celsius with nothing to undo it.
//
// ---- why it links climatqml ---------------------------------------------------
//
// Same reason tst_conditionsdata does: its subject is app/, not libclimat.
// `climat_forbid_gui()` is deliberately not applied, which the function in
// tests/CMakeLists.txt cannot express - so this is registered by hand there.
#include "settings.h"
#include "units.h"
#include "support/settingssandbox.h"

#include <QtTest>

class TestUnits : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    void thePresetsAreTheDefaultsAndEachOther();
    void aMixtureIsCustomRatherThanTheNearestPreset();
    void anUnknownPresetChangesNothing();
};

void TestUnits::initTestCase()
{
    // Before anything constructs a Settings, which Units does on first use.
    SettingsSandbox::install();
}

void TestUnits::init()
{
    Units::instance()->applySystem(QStringLiteral("metric"));
}

// The metric preset has to be exactly what Settings falls back to with nothing
// stored, or a reader who has never opened this screen sees "custom" selected on
// a fresh install. Two tables, one answer, and this is what holds them together.
void TestUnits::thePresetsAreTheDefaultsAndEachOther()
{
    Units *units = Units::instance();

    QCOMPARE(units->system(), QStringLiteral("metric"));
    QCOMPARE(units->temperatureUnit(), QStringLiteral("celsius"));
    QCOMPARE(units->windUnit(), QStringLiteral("kmh"));
    QCOMPARE(units->pressureUnit(), QStringLiteral("hpa"));
    QCOMPARE(units->visibilityUnit(), QStringLiteral("km"));
    QCOMPARE(units->precipitationUnit(), QStringLiteral("mm"));

    units->applySystem(QStringLiteral("imperial"));

    QCOMPARE(units->system(), QStringLiteral("imperial"));
    QCOMPARE(units->temperatureUnit(), QStringLiteral("fahrenheit"));
    QCOMPARE(units->windUnit(), QStringLiteral("mph"));
    QCOMPARE(units->pressureUnit(), QStringLiteral("inhg"));
    QCOMPARE(units->visibilityUnit(), QStringLiteral("mi"));
    QCOMPARE(units->precipitationUnit(), QStringLiteral("in"));

    // And every spelling either preset writes is one the screen can offer and
    // the converter understands. A preset naming a unit that is not in
    // `choicesFor` would be a value no row could ever cycle back to.
    const auto offers = [units](Units::Quantity quantity, const QString &id) {
        const QVariantList choices = units->choicesFor(quantity);
        for (const QVariant &choice : choices)
            if (choice.toMap().value(QStringLiteral("id")).toString() == id)
                return true;
        return false;
    };

    for (const QString &system : { QStringLiteral("metric"), QStringLiteral("imperial") }) {
        units->applySystem(system);
        QVERIFY2(offers(Units::Quantity::Temperature, units->temperatureUnit()),
                 qPrintable(system));
        QVERIFY2(offers(Units::Quantity::Wind, units->windUnit()), qPrintable(system));
        QVERIFY2(offers(Units::Quantity::Pressure, units->pressureUnit()), qPrintable(system));
        QVERIFY2(offers(Units::Quantity::Visibility, units->visibilityUnit()),
                 qPrintable(system));
        QVERIFY2(offers(Units::Quantity::Precipitation, units->precipitationUnit()),
                 qPrintable(system));
    }

    // The screen's own list, which is what the two radios are built from. Two
    // entries, each with the id `applySystem` takes and a blurb naming the units
    // it writes.
    const QVariantList choices = units->systemChoices();
    QCOMPARE(choices.size(), 2);
    QCOMPARE(choices.at(0).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("metric"));
    QCOMPARE(choices.at(1).toMap().value(QStringLiteral("id")).toString(),
             QStringLiteral("imperial"));
    for (const QVariant &choice : choices) {
        QVERIFY(!choice.toMap().value(QStringLiteral("label")).toString().isEmpty());
        QVERIFY(!choice.toMap().value(QStringLiteral("blurb")).toString().isEmpty());
    }
}

// °C with mph, which docs/04-architecture.md §4.10 says is the single most
// requested combination and which no metric/imperial switch can express. The
// preferences screen draws it by filling neither radio; this is the state it
// draws that from.
void TestUnits::aMixtureIsCustomRatherThanTheNearestPreset()
{
    Units *units = Units::instance();

    Settings::instance()->setWindUnit(QStringLiteral("mph"));
    QCOMPARE(units->system(), QStringLiteral("custom"));

    // And the reverse: an imperial bundle with millimetres of rain, which is the
    // "precipitation in inches" switch turned back off. Four of five matching is
    // not a preset.
    units->applySystem(QStringLiteral("imperial"));
    Settings::instance()->setPrecipitationUnit(QStringLiteral("mm"));
    QCOMPARE(units->system(), QStringLiteral("custom"));

    // Restoring the fifth restores the preset, so "custom" is a reading of the
    // five and not a sticky flag of its own.
    Settings::instance()->setPrecipitationUnit(QStringLiteral("in"));
    QCOMPARE(units->system(), QStringLiteral("imperial"));
}

void TestUnits::anUnknownPresetChangesNothing()
{
    Units *units = Units::instance();
    units->applySystem(QStringLiteral("imperial"));

    QTest::ignoreMessage(QtWarningMsg, "units: emperial is not a unit system");
    units->applySystem(QStringLiteral("emperial"));

    QCOMPARE(units->system(), QStringLiteral("imperial"));
    QCOMPARE(units->temperatureUnit(), QStringLiteral("fahrenheit"));
}

QTEST_MAIN(TestUnits)
#include "tst_units.moc"
