// SPDX-FileCopyrightText: 2026 Jowi Aoun
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Preferences that survive a restart, and the one path in this file that runs
// exactly once in the product's life.
//
// ============================================================================
// THE MIGRATION IS THE REASON THIS FILE EXISTS
//
// app/settings.h explains at length why `migrateConfigDirectory` was written
// before there was anything to migrate: "a migration written after the rename
// has already lost the data it was supposed to carry". It also says, in the
// same paragraph, why the function takes its table as an argument instead of
// reading supersededIdentities() directly - "so that it is testable without a
// rename having happened: a test hands it two identities it created itself and
// checks the file arrived".
//
// That test did not exist. So the one piece of code in the application whose
// entire purpose is to run correctly on the single day it is ever needed, with
// a user's saved preferences as the stake, had never been executed.
//
// It has four branches and three of them refuse to do anything:
//
//   an empty table          the state today, and the common case forever
//   a destination that      a user who has already run the new version. Their
//     already exists        current preferences win over an older copy, always
//   a source that does not  a rename in a build the user never ran
//   otherwise               copy the tree forward
//
// Getting the second one backwards is the expensive failure: it would overwrite
// live preferences with a stale copy on every launch.
//
// ============================================================================
// AND THE THINGS EVERY SETTING SHARES
//
// A default that is wrong is invisible on a developer's machine, where the key
// has been written at least once by hand. `QStandardPaths::setTestModeEnabled`
// gives every test below a config directory of its own, so "what does a fresh
// install read?" is a question this file can actually ask.

#include "app/settings.h"
#include "app/settingskeys.h"
#include "support/settingssandbox.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {

// Where QSettings would put an identity's file, without creating anything. The
// same computation settings.cpp does, repeated here rather than exported,
// because a test that called the private helper would be asserting that the
// helper agrees with itself.
QString configFileFor(const SettingsIdentity &identity)
{
    const QSettings probe(QSettings::IniFormat, QSettings::UserScope,
                          identity.organization, identity.application);
    return probe.fileName();
}

QString configDirectoryFor(const SettingsIdentity &identity)
{
    return QFileInfo(configFileFor(identity)).absolutePath();
}

SettingsIdentity currentIdentity()
{
    return { QCoreApplication::organizationName(), QCoreApplication::applicationName() };
}

bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    return file.write(contents) == contents.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class TestSettings : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void init();

    // ---- the migration ------------------------------------------------------
    void anEmptyTableOfSupersededIdentitiesCopiesNothing();
    void anOrganisationRenameCarriesThePreferencesForward();
    void anApplicationRenameCarriesThemForwardToo();
    void aFileThatAlreadyExistsIsNeverOverwritten();
    void aSharedDirectoryIsNotMistakenForHavingAlreadyMigrated();
    void anIdentityThatNeverWroteAnythingIsSkipped();
    void theFirstIdentityThatExistsWins();
    void anIdentityEqualToTheCurrentOneIsNotCopiedOntoItself();
    void everySubdirectoryComesForwardToo();
    void theShippedTableIsEmptyAndThatIsTheCorrectAnswer();

    // ---- defaults on a fresh install ---------------------------------------
    void aFreshInstallReadsTheDocumentedDefaults();
    void anAbsentWindowSizeIsReportedAsAbsentRatherThanAsZero();

    // ---- writing -------------------------------------------------------------
    void aValueSurvivesBeingWrittenAndReadBack_data();
    void aValueSurvivesBeingWrittenAndReadBack();
    void writingTheSameValueTwiceDoesNotEmitASecondTime();
    void geometryIsOneFactAndIsWrittenInOneGo();

    // ---- the acknowledged-alert list, which is opaque to this class ---------
    void theAcknowledgedListRoundTripsThroughAnIniFile();
    void aListEntryMayContainTheCharactersAnIniFileUsesItself();

    // ---- the file --------------------------------------------------------------
    void thePreferencesAreInAFileAnAnswerCanNameAndAUserCanRead();

    // ---- following another process --------------------------------------
    void aChangeAnotherProcessWroteIsNoticedOnReload();
    void aReloadThatFindsNothingNewIsSilent();
    void theWatcherReloadsWithoutBeingAsked();
    void theWatcherSurvivesTheFileBeingReplaced();
    void aFreshInstallTakesItsClockFromTheLocale();
};

void TestSettings::initTestCase()
{
    // Before any QSettings is constructed. The INI format is what main() sets;
    // the path is this file's own, on every platform.
    SettingsSandbox::install();
}

void TestSettings::init()
{
    Settings *settings = Settings::instance();

    // A known starting point for every test. Written through the setters rather
    // than by clearing the file, because the singleton's QSettings is already
    // open and would not see the file vanish.
    settings->setAppearance(QStringLiteral("system"));
    settings->setDynamicBackground(true);
    settings->setClockFormat(QStringLiteral("12h"));
    settings->setAcknowledgedAlerts({});
}

// ============================================================================
// The migration.
// ============================================================================

void TestSettings::anEmptyTableOfSupersededIdentitiesCopiesNothing()
{
    // The state today and the common case forever. It has to be cheap and it
    // has to be silent - this runs in main() on every single launch.
    QVERIFY(!Settings::migrateConfigDirectory({}));
}

void TestSettings::anOrganisationRenameCarriesThePreferencesForward()
{
    // The shape that always worked: the organisation moves, so the config
    // directory moves with it and the whole tree is copied.
    const SettingsIdentity old{ QStringLiteral("ClimatTestOldOrg"),
                                QCoreApplication::applicationName() };

    const QString legacyFile  = configFileFor(old);
    const QString currentFile = configFileFor(currentIdentity());

    QVERIFY2(configDirectoryFor(old) != configDirectoryFor(currentIdentity()),
             "this test needs two different directories to be testing anything");

    QDir(configDirectoryFor(old)).removeRecursively();
    QFile::remove(currentFile);

    QVERIFY(writeFile(legacyFile, QByteArrayLiteral("[General]\ntemperatureUnit=fahrenheit\n")));

    QVERIFY2(Settings::migrateConfigDirectory({ old }), "the rename carried nothing forward");

    // The file arrived, byte for byte, under the name this build will read.
    // Not "a file exists" - the contents are the user's preferences and a
    // truncated copy is worse than none.
    QCOMPARE(readFile(currentFile), QByteArrayLiteral("[General]\ntemperatureUnit=fahrenheit\n"));

    // The old directory is left alone. Copy forward, never move: a user who
    // downgrades has to find their preferences where they left them.
    QVERIFY(QFile::exists(legacyFile));

    QDir(configDirectoryFor(old)).removeRecursively();
    QFile::remove(currentFile);
}

void TestSettings::anApplicationRenameCarriesThemForwardToo()
{
    // The shape that did NOT work, and the likelier half of a rebrand - the
    // organisation is a domain and tends to outlive a product name.
    //
    // QSettings resolves to <config>/<organisation>/<application>.ini, so an
    // application rename leaves the directory exactly where it was and moves
    // only the FILENAME. A migration that compared directories saw `legacy ==
    // current`, skipped, and left the old file sitting unread beside the new
    // one - every preference back to its default, no error anywhere, which is
    // precisely the data-loss event app/settings.h says this helper exists to
    // prevent.
    const SettingsIdentity old{ QCoreApplication::organizationName(),
                                QStringLiteral("climat-before-the-rename") };

    QVERIFY2(configDirectoryFor(old) == configDirectoryFor(currentIdentity()),
             "this test needs one shared directory to be testing anything");

    const QString legacyFile  = configFileFor(old);
    const QString currentFile = configFileFor(currentIdentity());
    QVERIFY(legacyFile != currentFile);

    QFile::remove(legacyFile);
    QFile::remove(currentFile);

    QVERIFY(writeFile(legacyFile, QByteArrayLiteral("[General]\nclockFormat=24h\n")));

    QVERIFY2(Settings::migrateConfigDirectory({ old }),
             "an application rename carried nothing forward");

    QCOMPARE(readFile(currentFile), QByteArrayLiteral("[General]\nclockFormat=24h\n"));
    QVERIFY(QFile::exists(legacyFile));

    QFile::remove(legacyFile);
    QFile::remove(currentFile);
}

void TestSettings::aFileThatAlreadyExistsIsNeverOverwritten()
{
    // The expensive failure, and the branch worth having a test for even if the
    // others never got one. A user who has already run this version has current
    // preferences in the current file; copying an older identity over them
    // would revert their settings on every launch, silently, forever.
    const SettingsIdentity old{ QStringLiteral("ClimatTestStale"),
                                QCoreApplication::applicationName() };

    const QString legacyFile  = configFileFor(old);
    const QString currentFile = configFileFor(currentIdentity());

    QDir(configDirectoryFor(old)).removeRecursively();

    QVERIFY(writeFile(legacyFile, QByteArrayLiteral("[General]\ntemperatureUnit=stale\n")));
    QVERIFY(writeFile(currentFile, QByteArrayLiteral("[General]\ntemperatureUnit=current\n")));

    QVERIFY2(!Settings::migrateConfigDirectory({ old }),
             "an existing configuration file was migrated over");

    QCOMPARE(readFile(currentFile), QByteArrayLiteral("[General]\ntemperatureUnit=current\n"));

    QDir(configDirectoryFor(old)).removeRecursively();
}

void TestSettings::aSharedDirectoryIsNotMistakenForHavingAlreadyMigrated()
{
    // The directory is named after the ORGANISATION, so it can exist because a
    // sibling application under the same organisation has run - or because a
    // previous release of this one did, under its old name. Neither says
    // anything about whether THIS application has preferences yet.
    //
    // Guarding on the directory rather than the file made every such user look
    // like a completed migration, which is the same silent revert as above with
    // a different cause.
    const SettingsIdentity old{ QCoreApplication::organizationName(),
                                QStringLiteral("climat-sibling-test") };

    const QString legacyFile  = configFileFor(old);
    const QString currentFile = configFileFor(currentIdentity());

    QFile::remove(legacyFile);
    QFile::remove(currentFile);

    QVERIFY(writeFile(legacyFile, QByteArrayLiteral("[General]\nappearance=dark\n")));

    // The shared directory now exists and contains a file - just not ours.
    QVERIFY(QDir(configDirectoryFor(currentIdentity())).exists());
    QVERIFY(!QFile::exists(currentFile));

    QVERIFY(Settings::migrateConfigDirectory({ old }));
    QCOMPARE(readFile(currentFile), QByteArrayLiteral("[General]\nappearance=dark\n"));

    QFile::remove(legacyFile);
    QFile::remove(currentFile);
}

void TestSettings::anIdentityThatNeverWroteAnythingIsSkipped()
{
    const SettingsIdentity ghost{ QStringLiteral("ClimatTestGhost"),
                                  QStringLiteral("climat-never-existed") };
    const QString currentFile = configFileFor(currentIdentity());

    QDir(configDirectoryFor(ghost)).removeRecursively();
    QFile::remove(currentFile);

    QVERIFY(!Settings::migrateConfigDirectory({ ghost }));
    QVERIFY2(!QFile::exists(currentFile),
             "a file was created for a migration that found nothing");
}

void TestSettings::theFirstIdentityThatExistsWins()
{
    // "newest first", says the header on supersededIdentities(). Two renames
    // deep, both files still on disk, and the one that must come forward is the
    // most recent - the older one is preferences the user has already
    // superseded once.
    //
    // Two different ORGANISATIONS, so the two are genuinely two places: within
    // one organisation the earlier write would simply be a different filename
    // in the same directory, which tests less.
    const SettingsIdentity newer{ QStringLiteral("ClimatTestV2"),
                                  QCoreApplication::applicationName() };
    const SettingsIdentity older{ QStringLiteral("ClimatTestV1"),
                                  QCoreApplication::applicationName() };

    const QString currentFile = configFileFor(currentIdentity());

    QDir(configDirectoryFor(newer)).removeRecursively();
    QDir(configDirectoryFor(older)).removeRecursively();
    QFile::remove(currentFile);

    QVERIFY(writeFile(configFileFor(newer), QByteArrayLiteral("v2\n")));
    QVERIFY(writeFile(configFileFor(older), QByteArrayLiteral("v1\n")));

    QVERIFY(Settings::migrateConfigDirectory({ newer, older }));
    QCOMPARE(readFile(currentFile), QByteArrayLiteral("v2\n"));

    QDir(configDirectoryFor(newer)).removeRecursively();
    QDir(configDirectoryFor(older)).removeRecursively();
    QFile::remove(currentFile);
}

void TestSettings::anIdentityEqualToTheCurrentOneIsNotCopiedOntoItself()
{
    // A plausible editing mistake: the current identity left in the superseded
    // table after a rename is reverted. Copying a file onto itself truncates it
    // on some platforms and is refused on others; either way it is a user's
    // preferences.
    const SettingsIdentity self = currentIdentity();
    const QString currentFile   = configFileFor(self);

    QFile::remove(currentFile);
    QVERIFY(!Settings::migrateConfigDirectory({ self }));

    QVERIFY(writeFile(currentFile, QByteArrayLiteral("live\n")));
    QVERIFY(!Settings::migrateConfigDirectory({ self }));
    QCOMPARE(readFile(currentFile), QByteArrayLiteral("live\n"));

    QFile::remove(currentFile);
}

void TestSettings::everySubdirectoryComesForwardToo()
{
    // copyTree recurses, and it has to: the config location is a directory, not
    // a file, and anything Climat later puts beside climat.ini - a cached place
    // list, a per-widget layout - lives in it.
    const SettingsIdentity old{ QStringLiteral("ClimatTestNested"),
                                QCoreApplication::applicationName() };
    const QString legacyDir   = configDirectoryFor(old);
    const QString currentDir  = configDirectoryFor(currentIdentity());
    const QString currentFile = configFileFor(currentIdentity());

    QDir(legacyDir).removeRecursively();
    QFile::remove(currentFile);
    QDir(currentDir + QStringLiteral("/widgets")).removeRecursively();
    QFile::remove(currentDir + QStringLiteral("/.hidden"));

    QVERIFY(writeFile(configFileFor(old), QByteArrayLiteral("top\n")));
    QVERIFY(writeFile(legacyDir + QStringLiteral("/widgets/tile.json"),
                      QByteArrayLiteral("nested\n")));
    QVERIFY(writeFile(legacyDir + QStringLiteral("/.hidden"), QByteArrayLiteral("dotfile\n")));

    QVERIFY(Settings::migrateConfigDirectory({ old }));

    QCOMPARE(readFile(currentFile), QByteArrayLiteral("top\n"));
    QCOMPARE(readFile(currentDir + QStringLiteral("/widgets/tile.json")),
             QByteArrayLiteral("nested\n"));

    // Hidden files too - QDir::Hidden is in the entry filter, and on Unix a
    // dotfile is an ordinary way to store something.
    QCOMPARE(readFile(currentDir + QStringLiteral("/.hidden")), QByteArrayLiteral("dotfile\n"));

    QDir(legacyDir).removeRecursively();
    QFile::remove(currentFile);
    QDir(currentDir + QStringLiteral("/widgets")).removeRecursively();
    QFile::remove(currentDir + QStringLiteral("/.hidden"));
}

void TestSettings::theShippedTableIsEmptyAndThatIsTheCorrectAnswer()
{
    // Climat has written preferences under exactly one identity. This asserts
    // the table has not grown a speculative entry - an identity in this list
    // that never existed is a file probe on every launch, and one that is wrong
    // is a migration from somebody else's application.
    QVERIFY(Settings::supersededIdentities().isEmpty());
}

// ============================================================================
// Defaults. What a fresh install reads.
// ============================================================================

void TestSettings::aFreshInstallReadsTheDocumentedDefaults()
{
    // Through the object that reads them, on a store with nothing in it.
    //
    // This used to build a QSettings on a QTemporaryDir, assert it was empty,
    // and then never use it again - the three assertions below read the live
    // singleton, whose init() had just written every value they checked. So it
    // asserted what the test itself had set, and went on passing when the
    // clock's default stopped being a constant. An empty file and a reload is
    // what makes "what does a fresh install read?" a question this can ask.
    Settings *settings = Settings::instance();

    QVERIFY(writeFile(settings->filePath(), QByteArray()));
    settings->reloadFromDisk();

    QCOMPARE(settings->appearance(), QStringLiteral("system"));
    QCOMPARE(settings->dynamicBackground(), true);
    QCOMPARE(settings->alertNotifications(), false);

    // The clock is deliberately not here: it follows the reader's locale now,
    // and aFreshInstallTakesItsClockFromTheLocale drives both answers.
    QCOMPARE(settings->temperatureUnit(), QStringLiteral("celsius"));
    QCOMPARE(settings->windUnit(), QStringLiteral("kmh"));
    QCOMPARE(settings->pressureUnit(), QStringLiteral("hpa"));
    QCOMPARE(settings->visibilityUnit(), QStringLiteral("km"));
    QCOMPARE(settings->precipitationUnit(), QStringLiteral("mm"));
    QCOMPARE(settings->acknowledgedAlerts(), QStringList());
}

void TestSettings::anAbsentWindowSizeIsReportedAsAbsentRatherThanAsZero()
{
    // `hasWindowSize` exists so that Main.qml can tell "the user has never
    // resized the window" from "the user resized it to nothing". Without the
    // distinction, a first launch would open at 0×0 or silently ignore a
    // genuinely stored size.
    Settings *settings = Settings::instance();

    settings->saveWindowGeometry(120, 80, 1340, 900);
    QVERIFY(settings->hasWindowSize());
    QCOMPARE(settings->windowWidth(), 1340);
    QCOMPARE(settings->windowHeight(), 900);
    QCOMPARE(settings->windowX(), 120);
    QCOMPARE(settings->windowY(), 80);
}

// ============================================================================
// Writing.
// ============================================================================

void TestSettings::aValueSurvivesBeingWrittenAndReadBack_data()
{
    QTest::addColumn<QString>("property");
    QTest::addColumn<QString>("value");

    // The five unit properties, which "are per quantity, never bundled" -
    // §4.10, and "the single most repeated complaint under every weather app's
    // reviews". A mixture has to be storable, so each is written on its own.
    QTest::newRow("temperature") << QStringLiteral("temperatureUnit")   << QStringLiteral("fahrenheit");
    QTest::newRow("wind")        << QStringLiteral("windUnit")          << QStringLiteral("mph");
    QTest::newRow("pressure")    << QStringLiteral("pressureUnit")      << QStringLiteral("inhg");
    QTest::newRow("visibility")  << QStringLiteral("visibilityUnit")    << QStringLiteral("mi");
    QTest::newRow("precip")      << QStringLiteral("precipitationUnit") << QStringLiteral("in");
    QTest::newRow("appearance")  << QStringLiteral("appearance")        << QStringLiteral("dark");
    QTest::newRow("clock")       << QStringLiteral("clockFormat")       << QStringLiteral("24h");
}

void TestSettings::aValueSurvivesBeingWrittenAndReadBack()
{
    QFETCH(QString, property);
    QFETCH(QString, value);

    Settings *settings = Settings::instance();

    // Through the metaobject, so this covers the property a QML binding
    // actually writes rather than the C++ setter beside it - QML_SINGLETON
    // reaches these by name.
    QVERIFY2(settings->setProperty(property.toUtf8().constData(), value),
             qPrintable(property));
    QCOMPARE(settings->property(property.toUtf8().constData()).toString(), value);
}

void TestSettings::writingTheSameValueTwiceDoesNotEmitASecondTime()
{
    // "Writes only when the value actually changed, so a binding that reassigns
    // its own value does not dirty the file, and emits only then too."
    //
    // This is not tidiness. A QML control that writes back on every value change
    // and re-reads on every notify is a loop, and the only thing stopping it is
    // the second half of that sentence.
    Settings *settings = Settings::instance();
    settings->setAppearance(QStringLiteral("light"));

    QSignalSpy changes(settings, &Settings::appearanceChanged);
    QVERIFY(changes.isValid());

    settings->setAppearance(QStringLiteral("light"));
    QCOMPARE(changes.count(), 0);

    settings->setAppearance(QStringLiteral("dark"));
    QCOMPARE(changes.count(), 1);

    settings->setAppearance(QStringLiteral("dark"));
    QCOMPARE(changes.count(), 1);
}

void TestSettings::geometryIsOneFactAndIsWrittenInOneGo()
{
    // "Size and position in one write, because they are one fact and a window
    // remembered half-moved is worse than one not remembered at all." Four
    // writes would also be four notifies, and a listener acting on the first
    // would read three stale values.
    Settings *settings = Settings::instance();
    settings->saveWindowGeometry(0, 0, 800, 600);

    QSignalSpy geometry(settings, &Settings::windowGeometryChanged);
    QVERIFY(geometry.isValid());

    settings->saveWindowGeometry(10, 20, 1024, 768);

    QCOMPARE(settings->windowX(), 10);
    QCOMPARE(settings->windowY(), 20);
    QCOMPARE(settings->windowWidth(), 1024);
    QCOMPARE(settings->windowHeight(), 768);

    QVERIFY2(geometry.count() <= 1,
             qPrintable(QStringLiteral("one geometry change produced %1 notifications")
                            .arg(geometry.count())));
}

// ============================================================================
// The acknowledged-alert list. Opaque to Settings - app/viewmodels/alertsdata.cpp
// owns the format - which makes this the one property where the storage layer
// has to carry bytes it cannot interpret.
// ============================================================================

void TestSettings::theAcknowledgedListRoundTripsThroughAnIniFile()
{
    Settings *settings = Settings::instance();

    const QStringList stored = {
        QStringLiteral("nws:one\x1f") + QStringLiteral("3\x1f") + QStringLiteral("2026-08-07T06:00:00Z"),
        QStringLiteral("eccc:two\x1f") + QStringLiteral("2\x1f") + QStringLiteral("2026-08-08T12:00:00Z"),
    };

    settings->setAcknowledgedAlerts(stored);
    QCOMPARE(settings->acknowledgedAlerts(), stored);

    // Emptying it is a value, not an absence - a user who reveals their last
    // dismissed alert must not have the list read back as whatever was there
    // before.
    settings->setAcknowledgedAlerts({});
    QVERIFY(settings->acknowledgedAlerts().isEmpty());
}

void TestSettings::aListEntryMayContainTheCharactersAnIniFileUsesItself()
{
    // A QSettings INI writer escapes what it has to, and an NWS identity key is
    // full of exactly the characters that need it. If any of these came back
    // split, joined or truncated, a dismissal would silently stop working for
    // the alerts that update most often - and it would look like the
    // acknowledgement logic was wrong rather than the storage.
    Settings *settings = Settings::instance();

    const QStringList awkward = {
        QStringLiteral("urn:oid:2.49.0.1.840.0.abc, def"),   // a comma: INI list separator
        QStringLiteral("key=with=equals"),                    // the key/value separator
        QStringLiteral("key;with;semicolons"),                // a comment character
        QStringLiteral("  leading and trailing  "),           // whitespace INI likes to trim
        QStringLiteral("[bracketed]"),                        // a section header
        QStringLiteral("with\\backslashes"),
        QStringLiteral("with \"quotes\" in it"),
        QStringLiteral("unit\x1f" "separated\x1f" "fields"),  // split, or \x1f eats the f after it
    };

    settings->setAcknowledgedAlerts(awkward);
    QCOMPARE(settings->acknowledgedAlerts(), awkward);
}

// ============================================================================
// The file itself.
// ============================================================================

void TestSettings::thePreferencesAreInAFileAnAnswerCanNameAndAUserCanRead()
{
    // "'Which file?' is the first question of every support conversation and an
    // About box should be able to answer it." And it has to be a FILE - on
    // Windows without QSettings::setDefaultFormat(IniFormat) this would be a
    // registry path, which cannot be backed up by copying, cannot be read at
    // line 4, and is not writable by a portable unzip-and-run build.
    Settings *settings = Settings::instance();
    const QString path = settings->filePath();

    QVERIFY(!path.isEmpty());
    QVERIFY2(path.endsWith(QStringLiteral(".conf")) || path.endsWith(QStringLiteral(".ini")),
             qPrintable(path));

    // And it is under the test-mode config root, which is what proves the whole
    // file has been writing somewhere harmless.
    QVERIFY2(path.startsWith(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)),
             qPrintable(path));
}

// ============================================================================
// Following another process.
//
// The widget host reads the INI the app writes. These three are the difference
// between "reads it at start" and "follows it", and the discipline in all of
// them is that the other process is simulated with BYTES rather than with a
// second QSettings. Two QSettings on one path in one process share a parsed
// copy of the file and agree with each other without a disk read in between -
// a test written that way passes with reloadFromDisk() doing nothing at all.
// ============================================================================

void TestSettings::aChangeAnotherProcessWroteIsNoticedOnReload()
{
    Settings *settings = Settings::instance();

    // The baseline on disk: what init() set, flushed. A reload with a pending
    // write of our own would be testing the merge rather than the read. The
    // temperature is set explicitly because init() does not touch the units
    // and an earlier case in this file leaves it in Fahrenheit.
    settings->setClockFormat(QStringLiteral("12h"));
    settings->setTemperatureUnit(QStringLiteral("celsius"));
    settings->reloadFromDisk();
    QCOMPARE(settings->clockFormat(), QStringLiteral("12h"));
    QCOMPARE(settings->temperatureUnit(), QStringLiteral("celsius"));

    QSignalSpy clock(settings, &Settings::clockFormatChanged);
    QSignalSpy temperature(settings, &Settings::temperatureUnitChanged);

    // The reader flips the switch in the app. Written as the app would write
    // it and as nothing else in this process would: straight to the file.
    QVERIFY(writeFile(settings->filePath(),
                      QByteArrayLiteral("[time]\nformat=24h\n\n[units]\ntemperature=fahrenheit\n")));

    settings->reloadFromDisk();

    QCOMPARE(settings->clockFormat(), QStringLiteral("24h"));
    QCOMPARE(settings->temperatureUnit(), QStringLiteral("fahrenheit"));
    QCOMPARE(clock.count(), 1);
    QCOMPARE(temperature.count(), 1);
}

void TestSettings::aReloadThatFindsNothingNewIsSilent()
{
    // The other half of the contract. A binding on the clock format must not
    // be re-evaluated because the window was resized by another process, and
    // a reload that emits everything every time would do exactly that.
    Settings *settings = Settings::instance();
    settings->setClockFormat(QStringLiteral("12h"));
    settings->reloadFromDisk();

    QSignalSpy clock(settings, &Settings::clockFormatChanged);
    QSignalSpy units(settings, &Settings::windUnitChanged);

    settings->reloadFromDisk();
    settings->reloadFromDisk();

    QCOMPARE(clock.count(), 0);
    QCOMPARE(units.count(), 0);
}

void TestSettings::theWatcherReloadsWithoutBeingAsked()
{
    Settings *settings = Settings::instance();
    settings->setClockFormat(QStringLiteral("12h"));
    settings->reloadFromDisk();

    settings->watchForExternalChanges();

    QSignalSpy clock(settings, &Settings::clockFormatChanged);

    QVERIFY(writeFile(settings->filePath(), QByteArrayLiteral("[time]\nformat=24h\n")));

    // The settle interval plus inotify's own latency, with room. A watcher that
    // never fires is the failure this bounds, and three seconds of waiting for
    // it is the price of not asserting a timing.
    //
    // This case found a second bug in the first draft, and it is the reason
    // reloadFromDisk() compares against what it last announced rather than
    // against a read taken just before its sync. QSettings flushes itself on
    // the event loop after a setValue - init() made several - and that flush
    // re-read the changed file before the settle timer fired, so a "before"
    // read at reload time already said 24h and the change was never emitted.
    // The wait below spins the loop, which is what makes this case reach it.
    QVERIFY2(clock.wait(3000), "the file changed under the watcher and nothing was reloaded");
    QCOMPARE(settings->clockFormat(), QStringLiteral("24h"));
}

void TestSettings::theWatcherSurvivesTheFileBeingReplaced()
{
    // The case the directory watch and rearmWatch() exist for, and the one the
    // truncate-in-place test above does NOT reach. QSettings does not write
    // through the file it is given: it writes a temporary and renames over the
    // top, so the inode the watcher was told about is the one that has just
    // been replaced. QFileSystemWatcher drops a path it can no longer see, and
    // a watcher that did not re-arm would hear the first replacement and never
    // another one.
    //
    // So this replaces TWICE and asserts on the second, which is the assertion
    // that fails with the re-arm removed.
    Settings *settings = Settings::instance();
    settings->setClockFormat(QStringLiteral("12h"));
    settings->reloadFromDisk();

    settings->watchForExternalChanges();

    const QString path = settings->filePath();

    const auto replace = [&path](const QByteArray &contents) {
        const QString beside = path + QStringLiteral(".incoming");
        QFile::remove(beside);
        QFile file(beside);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        if (file.write(contents) != contents.size())
            return false;
        file.close();
        // Over the top, the way QSettings itself commits a write.
        QFile::remove(path);
        return QFile::rename(beside, path);
    };

    QSignalSpy first(settings, &Settings::clockFormatChanged);
    QVERIFY(replace(QByteArrayLiteral("[time]\nformat=24h\n")));
    QVERIFY2(first.wait(3000), "the first replacement was not noticed");
    QCOMPARE(settings->clockFormat(), QStringLiteral("24h"));

    QSignalSpy second(settings, &Settings::clockFormatChanged);
    QVERIFY(replace(QByteArrayLiteral("[time]\nformat=12h\n")));
    QVERIFY2(second.wait(3000),
             "the second replacement was not noticed - the watcher was told about an inode "
             "that no longer exists and nothing re-armed it");
    QCOMPARE(settings->clockFormat(), QStringLiteral("12h"));
}

void TestSettings::aFreshInstallTakesItsClockFromTheLocale()
{
    // The default itself, which nothing tested: the tautology in
    // aFreshInstallReadsTheDocumentedDefaults asserts values init() has just
    // written, which is why it went on passing when this default stopped being
    // a constant.
    //
    // Both directions, through QLocale rather than through the machine's own:
    // a test that read the runner's locale would assert whatever CI happened
    // to be set to.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QCOMPARE(climat::settingskeys::defaultClockFormat(), QStringLiteral("12h"));

    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
    QCOMPARE(climat::settingskeys::defaultClockFormat(), QStringLiteral("24h"));

    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    QCOMPARE(climat::settingskeys::defaultClockFormat(), QStringLiteral("24h"));

    // And through Settings, on a store with the key ABSENT, which is the wiring
    // rather than the constant. Without this the case passes against
    // `load(key::clockFormat, "12h")` - the exact regression it is named for -
    // because init() writes 12h before every test and the assertions above
    // never touch the object that reads it.
    Settings *settings = Settings::instance();

    QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
    QVERIFY(writeFile(settings->filePath(),
                      QByteArrayLiteral("[units]\ntemperature=celsius\n")));
    settings->reloadFromDisk();
    QCOMPARE(settings->clockFormat(), QStringLiteral("24h"));

    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QVERIFY(writeFile(settings->filePath(),
                      QByteArrayLiteral("[units]\ntemperature=celsius\n")));
    settings->reloadFromDisk();
    QCOMPARE(settings->clockFormat(), QStringLiteral("12h"));

    // And back to the one every capture and every other case in this file runs
    // under, so nothing after this inherits a French clock.
    QLocale::setDefault(QLocale::c());
}

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
