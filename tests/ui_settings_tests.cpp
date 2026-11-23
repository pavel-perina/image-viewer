#include "ui_test_support.h"

#include <limits>
#include <atomic>
#include <QScopeGuard>
#include <QQmlProperty>
#include "ui/appearance_draft.h"
#include "ui/preferences.h"
#include "ui/theme_palette.h"
#include "ui/themed_icon.h"
#include "ui/settings_draft.h"
#include "ui/thumbnail_store.h"
#include "core/thumbnail_cache.h"

namespace {
struct AppearanceFixture : testing::Test {
    QTemporaryDir dir;
    iv::Preferences preferences{dir.filePath("preferences.ini")};
    iv::ThemePalette theme{preferences};
    iv::AppearanceDraft draft{preferences, theme};
    ~AppearanceFixture() override {
        draft.cancel();
        // Isolated themes also set Qt's application palette; restore the real test owner.
        QGuiApplication::setPalette(iv::ThemePalette::instance()->palette());
    }
};
}

// Live preview must affect the rendered palette, without changing saved preferences.
// Cancel and destruction both return to the saved colours, even without a window.
TEST_F(AppearanceFixture, PreviewDoesNotPersistAndCancelOrDestructionRestoresSavedColours) {
    const auto original = theme.colours();
    ASSERT_TRUE(draft.begin());
    draft.setHue(230);
    draft.setAccentChroma(0.12);
    draft.setSurfaceChroma(0.02);
    EXPECT_NE(theme.colours(), original);
    EXPECT_TRUE(draft.dirty());
    EXPECT_EQ(preferences.hue(), 150);
    EXPECT_TRUE(QSettings(dir.filePath("preferences.ini"), QSettings::IniFormat).allKeys().isEmpty());
    draft.cancel();
    EXPECT_EQ(theme.colours(), original);
    EXPECT_FALSE(draft.dirty());
    {
        iv::AppearanceDraft temporary(preferences, theme);
        ASSERT_TRUE(temporary.begin());
        temporary.setHue(40);
        EXPECT_NE(theme.colours(), original);
    }
    EXPECT_EQ(theme.colours(), original);
}

// Apply establishes the checkpoint Cancel returns to. Reset changes the draft only;
// unrelated preferences survive both operations. Reload from the same isolated INI.
TEST_F(AppearanceFixture, ApplyPersistsOnlyAppearanceAndResetCanBeCancelled) {
    preferences.setShowHiddenFolders(true);
    ASSERT_TRUE(draft.begin());
    draft.setHue(230);
    draft.setSurfaceChroma(0.02);
    draft.apply();
    EXPECT_FALSE(draft.dirty());
    iv::Preferences reloaded(dir.filePath("preferences.ini"));
    EXPECT_EQ(reloaded.hue(), 230);
    EXPECT_EQ(reloaded.surfaceChroma(), 0.02);
    const auto applied = theme.colours();
    draft.resetDefaults();
    EXPECT_EQ(draft.hue(), 150);
    EXPECT_EQ(draft.surfaceChroma(), 0);
    EXPECT_TRUE(draft.dirty());
    EXPECT_EQ(preferences.hue(), 230);
    draft.cancel();
    EXPECT_EQ(theme.colours(), applied);
    EXPECT_TRUE(preferences.showHiddenFolders());
    ASSERT_TRUE(draft.begin());
    draft.resetDefaults();
    draft.apply();
    EXPECT_EQ(preferences.hue(), 150);
    EXPECT_EQ(preferences.surfaceChroma(), 0);
}

// Numeric validation is shared with Preferences. Inactive/invalid edits cannot affect
// either the palette or storage; restored settings may exceed the UI's suggested range.
TEST_F(AppearanceFixture, InvalidAndInactiveEditsAreIgnored) {
    draft.setHue(240);
    EXPECT_EQ(draft.hue(), 150);
    ASSERT_TRUE(draft.begin());
    draft.setHue(std::numeric_limits<double>::quiet_NaN());
    draft.setHue(361);
    draft.setAccentChroma(-0.001);
    draft.setSurfaceChroma(std::numeric_limits<double>::infinity());
    EXPECT_FALSE(draft.dirty());
    draft.setSurfaceChroma(0.2);
    EXPECT_EQ(draft.surfaceChroma(), 0.2);
}

// Multiple windows share one palette. A second editor raises the first rather than
// replacing its preview; after it closes, the second can start a fresh session.
TEST_F(AppearanceFixture, ASecondEditorActivatesTheExistingSessionWithoutLosingEdits) {
    iv::AppearanceDraft second(preferences, theme);
    QSignalSpy activation(&draft, &iv::AppearanceDraft::activationRequested);
    ASSERT_TRUE(draft.begin());
    draft.setHue(230);
    EXPECT_FALSE(second.begin());
    EXPECT_EQ(activation.size(), 1);
    EXPECT_EQ(theme.hue(), 230);
    draft.cancel();
    ASSERT_TRUE(second.begin());
    EXPECT_EQ(second.hue(), 150);
}

// External menu/window changes merge into untouched fields. Cancel must use the latest
// saved values rather than overwriting another window's edits with the opening snapshot.
TEST_F(AppearanceFixture, ExternalChangesMergeUntouchedFieldsAndSurviveCancel) {
    ASSERT_TRUE(draft.begin());
    draft.setHue(230);
    preferences.setHue(40);
    preferences.setSurfaceChroma(0.015);
    EXPECT_EQ(draft.hue(), 230);
    EXPECT_EQ(draft.surfaceChroma(), 0.015);
    draft.cancel();
    EXPECT_EQ(theme.hue(), 40);
    EXPECT_EQ(theme.surfaceChroma(), 0.015);
}

namespace {
struct SettingsDraftFixture : testing::Test {
    QTemporaryDir dir;
    iv::Preferences preferences{dir.filePath("settings.ini")};
    iv::ThemePalette theme{preferences};
    iv::SettingsDraft draft{preferences, theme};
    ~SettingsDraftFixture() override {
        draft.cancel();
        QGuiApplication::setPalette(iv::ThemePalette::instance()->palette());
    }
};
}

// The shared draft is the cancellation boundary, even when edits belong to different
// pages. Reload the isolated INI after Apply; Reset must affect only its own section.
TEST_F(SettingsDraftFixture, PagesShareOneCheckpointAndResetOnlyTheirOwnSection) {
    preferences.setShowHiddenFolders(true);
    ASSERT_TRUE(draft.begin());
    draft.appearance()->setHue(230);
    draft.setThumbnailSize(QSize(320, 240));
    draft.setThumbnailCacheMiB(256);
    draft.setTransparency("light");
    draft.setColorManaged(false);
    draft.setOutputBits(10);
    EXPECT_TRUE(draft.dirty());
    EXPECT_EQ(preferences.thumbnailSize(), QSize(164, 164));
    EXPECT_TRUE(preferences.colorManaged());
    draft.resetViewing();
    EXPECT_EQ(draft.transparency(), "dark");
    EXPECT_EQ(draft.thumbnailSize(), QSize(320, 240));
    EXPECT_EQ(draft.appearance()->hue(), 230);
    draft.setTransparency("plain");
    draft.apply();
    EXPECT_FALSE(draft.dirty());
    iv::Preferences reloaded(dir.filePath("settings.ini"));
    EXPECT_EQ(reloaded.thumbnailSize(), QSize(320, 240));
    EXPECT_EQ(reloaded.thumbnailCacheMiB(), 256);
    EXPECT_EQ(reloaded.transparency(), "plain");
    EXPECT_EQ(reloaded.hue(), 230);
    EXPECT_TRUE(reloaded.showHiddenFolders());
    draft.resetThumbnails();
    draft.appearance()->setHue(40);
    draft.cancel();
    EXPECT_EQ(theme.hue(), 230);
    EXPECT_EQ(preferences.thumbnailSize(), QSize(320, 240));
    ASSERT_TRUE(draft.begin());
    EXPECT_FALSE(draft.dirty());
    EXPECT_EQ(draft.thumbnailCacheMiB(), 256);
}

// Invalid or inactive edits must not poison the draft. External changes merge into
// untouched fields, while explicit edits retain the person's choice until Apply/Cancel.
TEST_F(SettingsDraftFixture, ValidationAndExternalChangesKeepTheDraftConsistent) {
    draft.setThumbnailSize(QSize(400, 300));
    EXPECT_EQ(draft.thumbnailSize(), QSize(164, 164));
    ASSERT_TRUE(draft.begin());
    draft.setThumbnailSize(QSize(0, 120));
    draft.setThumbnailCacheMiB(0);
    draft.setOutputBits(9);
    draft.setTransparency("unknown");
    EXPECT_FALSE(draft.dirty());
    draft.setTransparency("light");
    preferences.setThumbnailSize(QSize(240, 160));
    preferences.setTransparency("plain");
    EXPECT_EQ(draft.thumbnailSize(), QSize(240, 160));
    EXPECT_EQ(draft.transparency(), "light");
    draft.cancel();
    EXPECT_EQ(draft.transparency(), "plain");
    EXPECT_EQ(preferences.thumbnailSize(), QSize(240, 160));
}

// Profile validation runs before changing either the draft or persistence. A file can
// disappear after selection: failed Apply must save no section, including Appearance.
TEST_F(SettingsDraftFixture, ProfileSelectionAndApplyValidateWithoutPartialWrites) {
    const QString file = dir.filePath("display.icc");
    iv::test::writeFile(QFileInfo(file).filesystemFilePath(), iv::adobeRgbProfile());
    ASSERT_TRUE(draft.begin());
    draft.chooseProfile(QUrl::fromLocalFile(file));
    ASSERT_TRUE(waitUntil([&] { return !draft.busy(); }));
    EXPECT_EQ(draft.displayProfile(), file);
    EXPECT_TRUE(preferences.displayProfile().isEmpty());
    draft.chooseProfile(QUrl::fromLocalFile(dir.filePath("missing.icc")));
    ASSERT_TRUE(waitUntil([&] { return !draft.busy(); }));
    EXPECT_EQ(draft.displayProfile(), file);
    EXPECT_FALSE(draft.problem().isEmpty());
    draft.appearance()->setHue(230);
    draft.setThumbnailSize(QSize(400, 300));
    ASSERT_TRUE(QFile::remove(file));
    QSignalSpy applied(&draft, &iv::SettingsDraft::applied);
    draft.apply(true);
    ASSERT_TRUE(waitUntil([&] { return !draft.busy(); }));
    EXPECT_TRUE(applied.isEmpty());
    EXPECT_EQ(preferences.hue(), 150);
    EXPECT_EQ(preferences.thumbnailSize(), QSize(164, 164));
    iv::test::writeFile(QFileInfo(file).filesystemFilePath(), iv::adobeRgbProfile());
    draft.apply(true);
    ASSERT_TRUE(waitUntil([&] { return !draft.busy(); }));
    ASSERT_EQ(applied.size(), 1);
    EXPECT_TRUE(applied.first().first().toBool());
    EXPECT_EQ(preferences.displayProfile(), file);
    EXPECT_EQ(preferences.hue(), 230);
    EXPECT_EQ(preferences.thumbnailSize(), QSize(400, 300));
}

// Closing during validation must neither publish a late profile result nor retain a
// palette override. Destruction waits for worker access before releasing model memory.
TEST_F(SettingsDraftFixture, DestructionDuringProfileValidationDiscardsTheSession) {
    const QString file = dir.filePath("display.icc");
    iv::test::writeFile(QFileInfo(file).filesystemFilePath(), iv::adobeRgbProfile());
    {
        iv::SettingsDraft temporary(preferences, theme);
        ASSERT_TRUE(temporary.begin());
        temporary.appearance()->setHue(230);
        temporary.chooseProfile(QUrl::fromLocalFile(file));
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(theme.hue(), 150);
    EXPECT_TRUE(preferences.displayProfile().isEmpty());
}

// Re-selecting the same path is an explicit reload: a changed/deleted file must still
// be validated on Apply, even though the saved path string itself has not changed.
TEST_F(SettingsDraftFixture, SelectingTheSameProfilePathStillValidatesAndReloadsIt) {
    const QString file = dir.filePath("display.icc");
    iv::test::writeFile(QFileInfo(file).filesystemFilePath(), iv::adobeRgbProfile());
    preferences.setDisplayProfile(file);
    ASSERT_TRUE(draft.begin());
    EXPECT_FALSE(draft.dirty());
    draft.chooseProfile(QUrl::fromLocalFile(file));
    ASSERT_TRUE(waitUntil([&] { return !draft.busy(); }));
    EXPECT_TRUE(draft.dirty());
    ASSERT_TRUE(QFile::remove(file));
    QSignalSpy applied(&draft, &iv::SettingsDraft::applied);
    draft.apply();
    ASSERT_TRUE(waitUntil([&] { return !draft.busy(); }));
    EXPECT_TRUE(applied.isEmpty());
    EXPECT_FALSE(draft.problem().isEmpty());
    iv::test::writeFile(QFileInfo(file).filesystemFilePath(), iv::adobeRgbProfile());
    draft.apply();
    ASSERT_TRUE(waitUntil([&] { return !draft.busy(); }));
    EXPECT_EQ(applied.size(), 1);
    EXPECT_FALSE(draft.dirty());
}

namespace {
struct SettingsWindowFixture : testing::Test {
    iv::Preferences& preferences = *iv::Preferences::instance();
    iv::PaletteChoices saved{preferences.hue(), preferences.accentChroma(), preferences.surfaceChroma()};
    QVariantMap savedOthers{
        {"transparency", preferences.transparency()}, {"colorManaged", preferences.colorManaged()},
        {"displayProfile", preferences.displayProfile()}, {"outputBits", preferences.outputBits()},
        {"thumbnailSize", preferences.thumbnailSize()}, {"thumbnailCacheMiB", preferences.thumbnailCacheMiB()}
    };
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    iv::AppearanceDraft* draft = nullptr;
    SettingsWindowFixture() {
        QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings) ADD_FAILURE() << warning.toString().toStdString();
        });
        QQmlComponent component(&engine);
        component.loadFromModule("ImageViewer", "SettingsDialog");
        root.reset(component.create());
        EXPECT_NE(root, nullptr) << component.errorString().toStdString();
        if (!root) return;
        window = qobject_cast<QQuickWindow*>(root.get());
        draft = root->findChild<iv::AppearanceDraft*>("appearanceDraft");
    }
    ~SettingsWindowFixture() override {
        root.reset();
        preferences.setHue(saved.hue);
        preferences.setAccentChroma(saved.accentChroma);
        preferences.setSurfaceChroma(saved.surfaceChroma);
        for (auto it = savedOthers.cbegin(); it != savedOthers.cend(); ++it) preferences.setProperty(it.key().toUtf8(), it.value());
    }
    void start() {
        ASSERT_TRUE(window);
        ASSERT_TRUE(draft);
        ASSERT_TRUE(QMetaObject::invokeMethod(root.get(), "start"));
        ASSERT_TRUE(waitUntil([&] { return window->isExposed(); }));
    }
    QQuickItem* item(const char* name) { return root->findChild<QQuickItem*>(QString::fromLatin1(name)); }
    void click(const char* name) {
        auto* button = item(name);
        ASSERT_NE(button, nullptr) << name;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint());
        QCoreApplication::processEvents();
    }
    void section(int index) {
        auto* nav = item("settingsSections");
        ASSERT_TRUE(nav);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, nav->mapToScene(QPointF(70, index * 28 + 14)).toPoint());
        QCoreApplication::processEvents();
        EXPECT_EQ(nav->property("currentIndex").toInt(), index);
    }
    void capture(const char* variable) {
        if (const QString file = qEnvironmentVariable(variable); !file.isEmpty()) {
            const QImage image = grabIfShown(*window);
            ASSERT_FALSE(image.isNull());
            EXPECT_TRUE(image.save(file));
        }
    }
};
}

// The real QML owns rollback on all close paths and keeps Apply disabled until dirty.
// Exercise actual buttons, a resized viewport, Escape, and reopening the same instance.
TEST_F(SettingsWindowFixture, ApplyCancelCloseAndEscapeUseTheLastAppliedCheckpoint) {
    start();
    ASSERT_TRUE(window);
    auto* apply = item("settingsApplyButton");
    ASSERT_NE(apply, nullptr);
    EXPECT_FALSE(apply->isEnabled());
    if (const QString file = qEnvironmentVariable("IV_SETTINGS_PNG"); !file.isEmpty()) {
        const QImage image = grabIfShown(*window);
        ASSERT_FALSE(image.isNull());
        EXPECT_TRUE(image.save(file));
    }
    draft->setHue(saved.hue == 230 ? 40 : 230);
    EXPECT_TRUE(apply->isEnabled());
    const double chosen = draft->hue();
    click("settingsApplyButton");
    EXPECT_EQ(preferences.hue(), chosen);
    EXPECT_FALSE(apply->isEnabled());
    draft->setHue(90);
    click("settingsCancelButton");
    EXPECT_FALSE(window->isVisible());
    EXPECT_EQ(iv::ThemePalette::instance()->hue(), chosen);
    start();
    draft->setHue(100);
    window->close();
    EXPECT_EQ(iv::ThemePalette::instance()->hue(), chosen);
    start();
    window->resize(680, 400);
    draft->setHue(120);
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_FALSE(window->isVisible());
    EXPECT_EQ(iv::ThemePalette::instance()->hue(), chosen);
}

// Typed numeric values must commit on Apply and OK without requiring Enter.
// Reset must also resynchronize spin boxes after manual editing (no lost QML binding).
TEST_F(SettingsWindowFixture, TypedValuesCommitOnApplyAndOkAndResetResynchronizesEditors) {
    start();
    ASSERT_TRUE(window);
    auto* spin = item("surfaceInput");
    ASSERT_NE(spin, nullptr);
    auto* input = spin->property("contentItem").value<QQuickItem*>();
    ASSERT_NE(input, nullptr);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                      input->mapToScene(QPointF(input->width() / 2, input->height() / 2)).toPoint());
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    typeText(window, QStringLiteral("0.025"));
    EXPECT_EQ(input->property("text").toString(), QStringLiteral("0.025"));
    EXPECT_TRUE(item("settingsApplyButton")->isEnabled()) << "typing should enable Apply before losing focus";
    click("settingsApplyButton");
    EXPECT_TRUE(window->isVisible());
    EXPECT_DOUBLE_EQ(preferences.surfaceChroma(), 0.025);
    click("hueInput");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    typeText(window, QStringLiteral("245"));
    click("settingsOkButton");
    EXPECT_FALSE(window->isVisible());
    EXPECT_EQ(preferences.hue(), 245);
    start();
    click("resetAppearanceButton");
    EXPECT_EQ(spin->property("realValue").toDouble(), 0);
    EXPECT_EQ(item("hueInput")->property("value").toInt(), 150);
}

// Theme is one runtime owner across QML engines so live preview cannot leave an older
// window on the saved colours. Both engines must see the same singleton and palette.
TEST_F(SettingsWindowFixture, EveryEngineSharesTheLivePreviewPalette) {
    start();
    ASSERT_TRUE(window);
    QQmlEngine second;
    auto* firstTheme = engine.singletonInstance<iv::ThemePalette*>("ImageViewer", "ThemePalette");
    auto* secondTheme = second.singletonInstance<iv::ThemePalette*>("ImageViewer", "ThemePalette");
    ASSERT_EQ(firstTheme, secondTheme);
    draft->setHue(saved.hue == 230 ? 40 : 230);
    EXPECT_EQ(secondTheme->hue(), draft->hue());
    EXPECT_EQ(preferences.hue(), saved.hue);
}

// Switching pages must retain edits without applying them. Exercise native control
// activation, typed cache limits, per-section reset, and one shared Apply checkpoint.
TEST_F(SettingsWindowFixture, ViewingAndThumbnailsRetainEditsAcrossPagesAndApplyTogether) {
    start();
    ASSERT_TRUE(window);
    // Another dialog test deliberately remembers a short window. This workflow tests
    // the normal page layout, where its cache-limit editor is visible before clicking.
    window->resize(760, 560);
    ASSERT_FALSE(grabIfShown(*window).isNull());
    auto* model = root->findChild<iv::SettingsDraft*>("settingsDraft");
    ASSERT_TRUE(model);
    draft->setHue(saved.hue == 230 ? 40 : 230);
    section(2);
    if (!qEnvironmentVariableIsEmpty("IV_SETTINGS_THUMBNAILS_PNG")) {
        auto* store = engine.singletonInstance<iv::ThumbnailStore*>("ImageViewer", "ThumbnailStore");
        ASSERT_TRUE(waitUntil([&] { return !store->busy(); }));
        capture("IV_SETTINGS_THUMBNAILS_PNG");
    }
    ASSERT_TRUE(item("thumbnailSizeChoice"));
    ASSERT_TRUE(QMetaObject::invokeMethod(item("thumbnailSizeChoice"), "activated", Q_ARG(int, 4)));
    click("thumbnailCacheLimit");
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    typeText(window, QStringLiteral("256"));
    section(1);
    capture("IV_SETTINGS_VIEWING_PNG");
    EXPECT_EQ(model->thumbnailCacheMiB(), 256);
    EXPECT_EQ(model->thumbnailSize(), QSize(320, 240));
    ASSERT_TRUE(QMetaObject::invokeMethod(item("transparencyChoice"), "activated", Q_ARG(int, 1)));
    ASSERT_TRUE(QMetaObject::invokeMethod(item("outputBitsChoice"), "activated", Q_ARG(int, 1)));
    EXPECT_EQ(model->transparency(), "light");
    EXPECT_EQ(model->outputBits(), 10);
    EXPECT_EQ(preferences.thumbnailSize(), savedOthers.value("thumbnailSize").toSize());
    EXPECT_EQ(preferences.outputBits(), savedOthers.value("outputBits").toInt());
    click("resetViewingButton");
    EXPECT_EQ(model->outputBits(), 8);
    EXPECT_EQ(model->transparency(), "dark");
    EXPECT_EQ(model->thumbnailSize(), QSize(320, 240));
    section(0);
    click("settingsApplyButton");
    EXPECT_EQ(preferences.thumbnailSize(), QSize(320, 240));
    EXPECT_EQ(preferences.thumbnailCacheMiB(), 256);
    EXPECT_EQ(preferences.hue(), draft->hue());
    EXPECT_FALSE(model->dirty());
    section(2);
    click("resetThumbnailsButton");
    click("settingsCancelButton");
    EXPECT_EQ(preferences.thumbnailSize(), QSize(320, 240));
    EXPECT_EQ(preferences.thumbnailCacheMiB(), 256);
}

// Cancelling the profile chooser must not select an empty custom profile or write
// preferences. A chosen valid profile is staged, then installed only after Apply.
TEST_F(SettingsWindowFixture, ProfileChooserIsOwnedBySettingsAndCancelPreservesTheChoice) {
    start();
    ASSERT_TRUE(window);
    section(1);
    auto* model = root->findChild<iv::SettingsDraft*>("settingsDraft");
    ASSERT_TRUE(model);
    model->setColorManaged(true);
    model->useSrgb();
    click("fileProfileChoice");
    auto* chooser = root->findChild<QQuickWindow*>("settingsProfileChooser");
    ASSERT_TRUE(chooser);
    ASSERT_TRUE(waitUntil([&] { return chooser->isExposed(); }));
    EXPECT_EQ(chooser->transientParent(), window);
    chooser->close();
    EXPECT_TRUE(model->displayProfile().isEmpty());
    EXPECT_TRUE(item("srgbProfileChoice")->property("checked").toBool());
    EXPECT_FALSE(item("fileProfileChoice")->property("checked").toBool());
    QTemporaryDir dir;
    const QString path = dir.filePath("display.icc");
    iv::test::writeFile(QFileInfo(path).filesystemFilePath(), iv::adobeRgbProfile());
    model->chooseProfile(QUrl::fromLocalFile(path));
    ASSERT_TRUE(waitUntil([&] { return !model->busy(); }));
    EXPECT_NE(preferences.displayProfile(), path);
    click("settingsApplyButton");
    ASSERT_TRUE(waitUntil([&] { return !model->busy(); }));
    EXPECT_EQ(preferences.displayProfile(), path);
    EXPECT_TRUE(iv::ColorSettings::instance()->hasDisplayProfile());
}

// Regressions included a left-hand dot instead of a proportional vertical thumb.
// Check actual thumb geometry and content movement, not only the outer bar's size.
// Settings keeps its bar visible throughout overflow, and hides it when content fits.
TEST_F(SettingsWindowFixture, OverflowShowsAPersistentWorkingScrollbarOnEveryPage) {
    start();
    ASSERT_TRUE(window);
    window->resize(680, 400);
    ASSERT_FALSE(grabIfShown(*window).isNull());
    for (int index = 0; index < 3; ++index) {
        SCOPED_TRACE(testing::Message() << "section=" << index);
        section(index);
        QQuickItem* bar = nullptr;
        const bool ready = waitUntil([&] {
            for (auto* candidate : root->findChildren<QQuickItem*>("settingsPageScrollBar")) {
                if (candidate->isVisible() && candidate->opacity() > 0.95 && candidate->property("size").toDouble() < 1) {
                    bar = candidate;
                    return true;
                }
            }
            return false;
        });
        if (!ready) {
            capture("IV_SETTINGS_SCROLLBAR_PNG");
        }
        ASSERT_TRUE(ready);
        ASSERT_GT(bar->width(), 0);
        ASSERT_GT(bar->height(), 0);
        EXPECT_FALSE(bar->property("autoHide").toBool());
        auto* area = bar->parentItem();
        ASSERT_TRUE(area);
        EXPECT_EQ(bar->property("orientation").toInt(), Qt::Vertical);
        EXPECT_NEAR(bar->x() + bar->width(), area->width(), 1);
        EXPECT_NEAR(bar->height(), area->height(), 1);
        const qreal contentHeight = area->property("contentHeight").toDouble();
        ASSERT_GT(contentHeight, area->height());
        EXPECT_NEAR(bar->property("size").toDouble(), area->height() / contentHeight, 0.01);
        auto* thumb = bar->property("contentItem").value<QQuickItem*>();
        ASSERT_TRUE(thumb);
        EXPECT_NEAR(thumb->height(), bar->property("availableHeight").toDouble() * bar->property("visualSize").toDouble(), 1);
        EXPECT_GT(thumb->height(), thumb->width() * 3) << "the handle must be a vertical thumb, not a dot";
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                          bar->mapToScene(QPointF(bar->width() / 2, bar->height() - 4)).toPoint());
        ASSERT_TRUE(waitUntil([&] { return bar->property("position").toDouble() > 0; }));
        EXPECT_GT(area->property("contentY").toDouble(), 0);
        // The page moves over a few frames, and wrapped text can still change its height; bar and
        // content agree once both have settled.
        const auto contentAt = [&] { return area->property("contentY").toDouble() / area->property("contentHeight").toDouble(); };
        const auto agree = [&] { return std::abs(bar->property("position").toDouble() - contentAt()) < 0.01; };
        EXPECT_TRUE(waitUntil(agree)) << "bar at " << bar->property("position").toDouble() << ", content at " << contentAt();
        EXPECT_TRUE(bar->property("revealed").toBool());
        const auto* ok = item("settingsOkButton");
        ASSERT_TRUE(ok);
        const qreal bottom = ok->mapToScene(QPointF(0, ok->height())).y();
        EXPECT_LE(bottom, window->height());
    }
    capture("IV_SETTINGS_SCROLLBAR_PNG");
    window->resize(760, 800);
    ASSERT_FALSE(grabIfShown(*window).isNull());
    ASSERT_TRUE(waitUntil([&] {
        for (auto* bar : root->findChildren<QQuickItem*>("settingsPageScrollBar")) {
            if (bar->isVisible()) return false;
        }
        return true;
    }));
}

// Adjust Colours uses the same viewport and proportional bar as Settings, retaining
// its hover/fade policy and bringing keyboard-focused fields into view.
TEST(Settings, AdjustColoursUsesTheSharedScrollAreaWithHoverVisibility) {
    MainFixture main(1);
    ASSERT_TRUE(main.window);
    main.window->resize(900, 400);
    QTest::keyClick(main.window, Qt::Key_A);
    auto* panel = qobject_cast<QQuickItem*>(main.find("adjustPanel"));
    ASSERT_TRUE(panel);
    ASSERT_TRUE(waitUntil([&] { return panel->isVisible(); }));
    auto* area = panel->findChild<QQuickItem*>("toolPanelScroller");
    ASSERT_TRUE(area);
    ASSERT_TRUE(waitUntil([&] { return area->property("contentHeight").toDouble() > area->height(); }));
    auto* bar = area->property("verticalScrollBar").value<QQuickItem*>();
    ASSERT_TRUE(bar);
    EXPECT_TRUE(bar->property("autoHide").toBool());
    EXPECT_EQ(bar->property("orientation").toInt(), Qt::Vertical);
    EXPECT_NEAR(bar->x() + bar->width(), area->width(), 1);
    EXPECT_NEAR(bar->property("size").toDouble(), area->height() / area->property("contentHeight").toDouble(), 0.01);
    auto* thumb = bar->property("contentItem").value<QQuickItem*>();
    ASSERT_TRUE(thumb);
    EXPECT_NEAR(thumb->height(), bar->property("availableHeight").toDouble() * bar->property("visualSize").toDouble(), 1);
    EXPECT_TRUE(main.warnings.isEmpty()) << main.warnings.join('\n').toStdString();
}

// The dimensions change every live browser grid, without changing its current image,
// selected files or keyboard focus. The existing square default remains 180 x 200.
TEST(Settings, ThumbnailSizeReflowsTheBrowserWithoutChangingItsSelectionOrFocus) {
    auto* preferences = iv::Preferences::instance();
    const QSize saved = preferences->thumbnailSize();
    const auto restore = qScopeGuard([preferences, saved] { preferences->setThumbnailSize(saved); });
    preferences->setThumbnailSize(QSize(164, 164));
    MainFixture main;
    ASSERT_TRUE(main.window);
    auto* grid = qobject_cast<QQuickItem*>(main.find("thumbnailGrid"));
    ASSERT_TRUE(grid);
    const QString current = main.context->currentPath();
    const QStringList selected = main.context->selectedPaths();
    EXPECT_EQ(grid->property("cellWidth").toInt(), 180);
    EXPECT_EQ(grid->property("cellHeight").toInt(), 200);
    grid->forceActiveFocus();
    preferences->setThumbnailSize(QSize(320, 240));
    ASSERT_TRUE(waitUntil([&] { return grid->property("cellWidth").toInt() == 336; }));
    EXPECT_EQ(grid->property("cellHeight").toInt(), 276);
    EXPECT_EQ(main.context->currentPath(), current);
    EXPECT_EQ(main.context->selectedPaths(), selected);
    EXPECT_TRUE(grid->hasActiveFocus());
    EXPECT_TRUE(main.warnings.isEmpty()) << main.warnings.join('\n').toStdString();
}

// A logical image box needs enough source pixels for the screen's device ratio, while
// still fitting the logical cell. Also run this test with QT_SCALE_FACTOR=2.
TEST(Settings, ThumbnailRequestsMatchPhysicalPixelsAndFitTheLogicalCell) {
    class ObservedProvider : public iv::ThumbnailProvider {
    public:
        std::atomic<int> width{0}, height{0};
        QQuickImageResponse* requestImageResponse(const QString& id, const QSize& size) override {
            width.store(size.width());
            height.store(size.height());
            return iv::ThumbnailProvider::requestImageResponse(id, size);
        }
    };
    QTemporaryDir dir;
    const QString path = dir.filePath("portrait.jpg");
    writeJpeg(path, {.width = 500, .height = 1000});
    QQmlEngine engine;
    auto* provider = new ObservedProvider;
    engine.addImageProvider("thumbnails", provider);
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) ADD_FAILURE() << warning.toString().toStdString();
    });
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import ImageViewer
        Window {
            width: 200; height: 220; visible: true
            required property string thumbnailUrl
            property alias cell: cell
            ThumbnailCell { id: cell; width: 180; height: 200; thumbnail: thumbnailUrl }
        }
    )", QUrl());
    std::unique_ptr<QObject> root(component.createWithInitialProperties({{"thumbnailUrl", iv::ThumbnailProvider::urlForPath(path)}}));
    ASSERT_TRUE(root) << component.errorString().toStdString();
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    ASSERT_TRUE(window);
    auto* cell = root->property("cell").value<QQuickItem*>();
    ASSERT_TRUE(cell);
    auto* image = cell->property("thumbnailImage").value<QQuickItem*>();
    ASSERT_TRUE(image);
    ASSERT_TRUE(waitUntil([&] { return image->property("status").toInt() == 1; }));
    const qreal ratio = window->devicePixelRatio();
    SCOPED_TRACE(testing::Message() << "DPR=" << ratio << " request=" << provider->width.load() << "x" << provider->height.load()
                 << " implicit=" << image->implicitWidth() << "x" << image->implicitHeight());
    EXPECT_GE(provider->width.load(), std::ceil(164 * ratio));
    EXPECT_GE(provider->height.load(), std::ceil(164 * ratio));
    EXPECT_LE(image->width(), 164);
    EXPECT_LE(image->height(), 164);
}

// Usage measurement must not trigger confirmation. Clearing belongs to Settings;
// the removed main-menu entry must not leave a duplicate main-window dialog behind.
TEST(Settings, MeasuringCacheUsageDoesNotAskToClearAndConfirmationBelongsToItsWindow) {
    MainFixture main;
    ASSERT_TRUE(main.window);
    auto* cache = iv::ThumbnailProvider::diskCache();
    ASSERT_TRUE(cache);
    ASSERT_TRUE(iv::makeCachedThumbnail(cache, QFileInfo(main.dir.filePath("a.jpg")).filesystemFilePath(), 64, {}, iv::ColorSettings::target()));
    auto* action = main.find("settingsAction");
    ASSERT_TRUE(QMetaObject::invokeMethod(action, "trigger"));
    auto* settings = main.find("settingsDialog");
    auto* nav = settings->findChild<QObject*>("settingsSections");
    ASSERT_TRUE(nav);
    nav->setProperty("currentIndex", 2);
    auto* store = main.engine.singletonInstance<iv::ThumbnailStore*>("ImageViewer", "ThumbnailStore");
    ASSERT_TRUE(store);
    ASSERT_TRUE(waitUntil([&] { return store->measuredOnce() && !store->busy(); }));
    EXPECT_EQ(main.find("mainCacheDialogs"), nullptr);
    auto* settingsDialogs = settings->findChild<QObject*>("settingsCacheDialogs");
    ASSERT_TRUE(settingsDialogs);
    auto* settingsConfirm = settingsDialogs->findChild<QObject*>("clearCacheDialog");
    ASSERT_TRUE(settingsConfirm);
    EXPECT_FALSE(settingsConfirm->property("opened").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(settingsDialogs, "requestClear"));
    ASSERT_TRUE(waitUntil([&] { return settingsConfirm->property("opened").toBool(); }));
    QMetaObject::invokeMethod(settingsConfirm, "reject");
    EXPECT_GT(store->count(), 0);
    EXPECT_TRUE(main.warnings.isEmpty()) << main.warnings.join('\n').toStdString();
}

// Settings owns these preferences. Main menus stay text-only, while Settings sections
// have working icons and the dialog belongs to the window that opened it.
TEST(Settings, MainWindowOpensAppearanceFromTools) {
    MainFixture main;
    ASSERT_TRUE(main.window);
    auto* action = main.find("settingsAction");
    auto* dialog = qobject_cast<QQuickWindow*>(main.find("settingsDialog"));
    ASSERT_TRUE(action);
    ASSERT_TRUE(dialog);
    EXPECT_TRUE(QQmlProperty::read(action, "icon.source").toUrl().isEmpty());
    auto* menuBar = QQmlProperty::read(main.window, "menuBar").value<QObject*>();
    ASSERT_TRUE(menuBar);
    QStringList captions;
    for (auto* object : menuBar->findChildren<QObject*>()) {
        if (object->inherits("QQuickMenu")) captions << object->property("title").toString().remove('&');
        if (object->inherits("QQuickMenuItem")) captions << object->property("text").toString().remove('&');
    }
    EXPECT_TRUE(captions.join('\n').contains("Settings"));
    for (const char* removed : {"Transparency", "Colour Management", "Display Profile", "Use sRGB Display", "Clear Thumbnail Cache"}) {
        EXPECT_FALSE(captions.join('\n').contains(QString::fromLatin1(removed))) << removed;
    }
    ASSERT_TRUE(QMetaObject::invokeMethod(action, "trigger"));
    ASSERT_TRUE(waitUntil([&] { return dialog->isExposed(); }));
    EXPECT_EQ(dialog->transientParent(), main.window);
    EXPECT_EQ(dialog->modality(), Qt::WindowModal);
    auto* sections = dialog->findChild<QObject*>("settingsSections");
    ASSERT_TRUE(sections);
    EXPECT_EQ(sections->property("count").toInt(), 3);
    for (int index = 0; index < sections->property("count").toInt(); ++index) {
        QQuickItem* row = nullptr;
        ASSERT_TRUE(QMetaObject::invokeMethod(sections, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(int, index)));
        ASSERT_TRUE(row);
        const QUrl source = QQmlProperty::read(row, "icon.source").toUrl();
        EXPECT_FALSE(source.isEmpty());
        EXPECT_FALSE(iv::renderIcon(QFileInfo(source.path()).baseName(), QSize(24, 24), Qt::white, Qt::green).isNull());
    }
    dialog->close();
    EXPECT_TRUE(main.warnings.isEmpty()) << main.warnings.join('\n').toStdString();
}
