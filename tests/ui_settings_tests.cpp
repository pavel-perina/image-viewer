#include "ui_test_support.h"

#include <limits>
#include "ui/appearance_draft.h"
#include "ui/preferences.h"
#include "ui/theme_palette.h"

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
struct SettingsWindowFixture : testing::Test {
    iv::Preferences& preferences = *iv::Preferences::instance();
    iv::PaletteChoices saved{preferences.hue(), preferences.accentChroma(), preferences.surfaceChroma()};
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

// Settings is reachable through the real main-window action and belongs to its window.
TEST(Settings, MainWindowOpensAppearanceFromTools) {
    MainFixture main;
    ASSERT_TRUE(main.window);
    auto* action = main.find("settingsAction");
    auto* dialog = qobject_cast<QQuickWindow*>(main.find("settingsDialog"));
    ASSERT_TRUE(action);
    ASSERT_TRUE(dialog);
    ASSERT_TRUE(QMetaObject::invokeMethod(action, "trigger"));
    ASSERT_TRUE(waitUntil([&] { return dialog->isExposed(); }));
    EXPECT_EQ(dialog->transientParent(), main.window);
    EXPECT_EQ(dialog->modality(), Qt::WindowModal);
    dialog->close();
    EXPECT_TRUE(main.warnings.isEmpty()) << main.warnings.join('\n').toStdString();
}
