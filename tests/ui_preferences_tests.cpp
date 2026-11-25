#include "ui_test_support.h"

#include <QMetaProperty>
#include <limits>
#include <future>

#include "ui/preferences.h"
#include "ui/theme_palette.h"
#include "ui/color_settings.h"
#include "ui/display_output.h"

namespace {
struct Choice {
    const char* property;
    const char* key;
    QVariant initial;
    QVariant changed;
};

const QList<Choice> choices{
    {"hue", "Appearance/hue", 150, 231.0},
    {"accentChroma", "Appearance/accentChroma", 0.075, 0.12},
    {"surfaceChroma", "Appearance/surfaceChroma", 0, 0.02},
    {"keepAutoContrast", "View/keepAutoContrast", false, true},
    {"showHiddenFolders", "View/showHiddenFolders", false, true},
    {"transparency", "View/transparency", QStringLiteral("dark"), QStringLiteral("light")},
    {"colorManaged", "Color/enabled", true, false},
    {"displayProfile", "Color/displayProfile", QString(), QStringLiteral("/test/display.icc")},
    {"sortKey", "Browser/sortKey", 0, 2},
    {"sortDescending", "Browser/sortDescending", false, true},
    {"askBeforeEditing", "FileOperations/askBeforeEditing", true, false},
    {"trimEdges", "FileOperations/trimEdges", false, true},
    {"trashAfterJxl", "FileOperations/trashAfterJxl", false, true},
    {"panelPinned", "InfoPanel/panelPinned", false, true},
    {"panelAnimated", "InfoPanel/panelAnimated", true, false},
    {"histogramLogScale", "InfoPanel/logScale", false, true},
    {"histogramFitRange", "InfoPanel/fitRange", false, true},
    {"thumbnailCacheMiB", "Thumbnails/cacheMiB", 512, 128},
    {"thumbnailSize", "Thumbnails/imageSize", QSize(160, 160), QSize(320, 320)},
    {"outputBits", "View/outputBits", 8, 10},
};
}

// Why: centralizing preferences must not rename existing keys, change defaults, or
// consume remembered state. Input: an isolated INI file; the truth is the previous
// keys/defaults and the same values read by a new object after the writer is destroyed.
TEST(Preferences, DefaultsAndPersistenceKeepExistingKeysAndRememberedState) {
    QTemporaryDir dir;
    const QString file = dir.filePath("settings.ini");
    {
        QSettings store(file, QSettings::IniFormat);
        store.setValue("Crop/ratioName", "16:9");
        store.setValue("Resize/keepRatio", false);
        store.setValue("FileOperations/recentFolders", QStringList{"/test/recent"});
    }
    {
        iv::Preferences preferences(file);
        for (const Choice& choice : choices) {
            SCOPED_TRACE(choice.property);
            EXPECT_EQ(preferences.property(choice.property), choice.initial);
            EXPECT_TRUE(preferences.setProperty(choice.property, choice.changed));
            EXPECT_EQ(preferences.property(choice.property), choice.changed);
        }
    }
    iv::Preferences reloaded(file);
    QSettings stored(file, QSettings::IniFormat);
    for (const Choice& choice : choices) {
        SCOPED_TRACE(choice.property);
        EXPECT_EQ(reloaded.property(choice.property), choice.changed);
        EXPECT_EQ(stored.value(choice.key), choice.changed);
    }
    EXPECT_EQ(stored.value("Crop/ratioName").toString(), "16:9");
    EXPECT_FALSE(stored.value("Resize/keepRatio").toBool());
    EXPECT_EQ(stored.value("FileOperations/recentFolders").toStringList(), QStringList{"/test/recent"});
}

// Why: installations already have these keys before any Preferences object exists.
// Inputs: non-default values written directly; the first reader must use them without
// migration or writing unrelated defaults back into the file.
TEST(Preferences, ExistingStoredChoicesAreReadWithoutMigration) {
    QTemporaryDir dir;
    const QString file = dir.filePath("settings.ini");
    {
        QSettings store(file, QSettings::IniFormat);
        for (const Choice& choice : choices) store.setValue(choice.key, choice.changed);
    }
    iv::Preferences preferences(file);
    for (const Choice& choice : choices) EXPECT_EQ(preferences.property(choice.property), choice.changed) << choice.property;
}

// Why: UI bindings need one notification per real change, and invalid input must not
// poison persisted settings. Bounds are part of the public preferences interface.
TEST(Preferences, InvalidValuesAndUnchangedWritesDoNotPublishOrPersist) {
    QTemporaryDir dir;
    const QString file = dir.filePath("settings.ini");
    iv::Preferences preferences(file);
    const QList<std::pair<const char*, QVariant>> invalid{
        {"hue", -1.0}, {"hue", 361.0}, {"hue", std::numeric_limits<double>::quiet_NaN()},
        {"accentChroma", -0.1}, {"surfaceChroma", std::numeric_limits<double>::infinity()},
        {"transparency", QStringLiteral("unknown")}, {"sortKey", -1}, {"sortKey", 4},
        {"thumbnailCacheMiB", 0}, {"thumbnailCacheMiB", 65537}, {"outputBits", 9},
        {"thumbnailSize", QSize(0, 120)}, {"thumbnailSize", QSize(400, -1)}, {"thumbnailSize", QSize(4096, 300)}
    };
    for (const auto& [name, value] : invalid) {
        const QMetaProperty property = preferences.metaObject()->property(preferences.metaObject()->indexOfProperty(name));
        QSignalSpy spy(&preferences, property.notifySignal());
        const QVariant before = preferences.property(name);
        preferences.setProperty(name, value);
        EXPECT_EQ(preferences.property(name), before) << name;
        EXPECT_TRUE(spy.isEmpty()) << name;
    }
    EXPECT_TRUE(QSettings(file, QSettings::IniFormat).allKeys().isEmpty());
    QSignalSpy hidden(&preferences, &iv::Preferences::showHiddenFoldersChanged);
    QSignalSpy hue(&preferences, &iv::Preferences::hueChanged);
    preferences.setShowHiddenFolders(true);
    preferences.setShowHiddenFolders(true);
    EXPECT_EQ(hidden.count(), 1);
    EXPECT_TRUE(hue.isEmpty());
}

// Why: malformed stored values must not reach rendering or cache arithmetic.
// Reading falls back to defaults without silently rewriting the user's file.
TEST(Preferences, InvalidStoredChoicesUseDefaultsWithoutRewritingThem) {
    QTemporaryDir dir;
    const QString file = dir.filePath("settings.ini");
    {
        QSettings stored(file, QSettings::IniFormat);
        stored.setValue("Appearance/hue", -10);
        stored.setValue("Thumbnails/cacheMiB", -1);
        stored.setValue("View/transparency", "unknown");
        stored.setValue("View/outputBits", 12);
    }
    iv::Preferences preferences(file);
    EXPECT_EQ(preferences.hue(), 150);
    EXPECT_EQ(preferences.thumbnailCacheMiB(), 512);
    EXPECT_EQ(preferences.transparency(), "dark");
    EXPECT_EQ(preferences.outputBits(), 8);
    EXPECT_EQ(QSettings(file, QSettings::IniFormat).value("Appearance/hue").toInt(), -10);
}

// Why: two QML engines/windows must see the same preference owner, rather than two
// independent Settings copies. The process-wide cache budget is safe for worker reads.
TEST(Preferences, QmlEnginesShareOneOwnerAndWorkersSeeTheCacheBudget) {
    QQmlEngine first;
    QQmlEngine second;
    auto* a = first.singletonInstance<iv::Preferences*>("ImageViewer", "Preferences");
    auto* b = second.singletonInstance<iv::Preferences*>("ImageViewer", "Preferences");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, iv::Preferences::instance());
    const int original = a->thumbnailCacheMiB();
    a->setThumbnailCacheMiB(64);
    EXPECT_EQ(b->thumbnailCacheMiB(), 64);
    EXPECT_EQ(iv::Preferences::thumbnailCacheBudgetBytes(), 64ull * 1024 * 1024);
    EXPECT_EQ(std::async(std::launch::async, [] { return iv::Preferences::thumbnailCacheBudgetBytes(); }).get(),
              64ull * 1024 * 1024);
    a->setThumbnailCacheMiB(original);
}

// Why: ThemePalette applies choices but does not own a second persisted copy.
// Input: isolated preferences and two consumers; changes through either interface
// update both consumers immediately and survive reconstruction of Preferences.
TEST(Preferences, ThemeConsumersFollowTheOwnerInBothDirections) {
    QTemporaryDir dir;
    const QString file = dir.filePath("settings.ini");
    {
        iv::Preferences preferences(file);
        iv::ThemePalette first(preferences);
        iv::ThemePalette second(preferences);
        const auto before = first.colours();
        preferences.setHue(250);
        EXPECT_EQ(first.hue(), 250);
        EXPECT_EQ(second.colours(), first.colours());
        EXPECT_NE(first.colours(), before);
        second.setSurfaceChroma(0.02);
        EXPECT_EQ(preferences.surfaceChroma(), 0.02);
        EXPECT_EQ(first.colours(), second.colours());
    }
    iv::Preferences reloaded(file);
    EXPECT_EQ(reloaded.hue(), 250);
    EXPECT_EQ(reloaded.surfaceChroma(), 0.02);
    // Restore the application's global palette after exercising an isolated consumer.
    iv::ThemePalette restore(*iv::Preferences::instance());
}

// Why: rendering has a thread-safe colour target, but Preferences must be its only
// persisted owner. Exercise both the generic preferences API and the validated profile
// picker, including failure without saving a bad profile.
TEST(Preferences, ColorBackendFollowsTheOwnerAndProfileValidationKeepsStoredChoice) {
    const ColorSettingsGuard guard;
    auto* preferences = iv::Preferences::instance();
    auto* color = iv::ColorSettings::instance();
    const bool enabled = preferences->colorManaged();
    preferences->setColorManaged(!enabled);
    EXPECT_EQ(iv::ColorSettings::target().enabled, !enabled);
    color->setEnabled(enabled);
    EXPECT_EQ(preferences->colorManaged(), enabled);
    QTemporaryDir dir;
    const QString profile = dir.filePath("display.icc");
    iv::test::writeFile(QFileInfo(profile).filesystemFilePath(), iv::adobeRgbProfile());
    EXPECT_TRUE(color->useDisplayProfile(QUrl::fromLocalFile(profile)).isEmpty());
    EXPECT_EQ(preferences->displayProfile(), profile);
    EXPECT_EQ(color->displayProfileName(), "Adobe RGB (1998)");
    EXPECT_FALSE(color->useDisplayProfile(QUrl::fromLocalFile(dir.filePath("missing.icc"))).isEmpty());
    EXPECT_EQ(preferences->displayProfile(), profile);
    preferences->setDisplayProfile({});
    EXPECT_FALSE(color->hasDisplayProfile());
    EXPECT_EQ(color->displayProfileName(), "sRGB");
}

// Why: a preferred sort is a default for a new browser, not a command to overwrite
// every existing view. Changing that preference leaves both live contexts independent.
TEST(Preferences, DefaultSortDoesNotOverwriteExistingBrowserContexts) {
    auto* preferences = iv::Preferences::instance();
    const int original = preferences->sortKey();
    MainFixture first(1);
    ASSERT_NE(first.window, nullptr);
    first.context->setSortKey(iv::BrowseContext::SortBySize);
    preferences->setSortKey(iv::BrowseContext::SortByDate);
    MainFixture second(1);
    ASSERT_NE(second.window, nullptr);
    EXPECT_EQ(first.context->sortKey(), iv::BrowseContext::SortBySize);
    EXPECT_EQ(second.context->sortKey(), iv::BrowseContext::SortByDate);
    preferences->setSortKey(iv::BrowseContext::SortByType);
    EXPECT_EQ(first.context->sortKey(), iv::BrowseContext::SortBySize);
    EXPECT_EQ(second.context->sortKey(), iv::BrowseContext::SortByDate);
    preferences->setSortKey(original);
}

// Why: output depth is a startup request, not proof of hardware support. The stored
// request must be available before windows are created, without changing actual output.
TEST(Preferences, RequestedOutputDepthIsSeparateFromTheActualOutput) {
    auto* preferences = iv::Preferences::instance();
    const int original = preferences->outputBits();
    preferences->setOutputBits(10);
    EXPECT_EQ(iv::Preferences::savedOutputBits(), 10);
    iv::DisplayOutput output;
    EXPECT_EQ(output.bits(), 8) << "no window has reported a ten-bit buffer";
    preferences->setOutputBits(original);
}
