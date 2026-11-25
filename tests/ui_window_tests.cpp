#include "ui_test_support.h"
#include "ui/app_info.h"
#include "ui/display_output.h"
#include "ui/retired_settings.h"
#include "ui/theme_palette.h"

#include <QDesktopServices>

namespace {

// Takes the URLs the program hands to the desktop, so a test opens no file manager.
class UrlCatcher : public QObject {
    Q_OBJECT
public:
    explicit UrlCatcher(const QString& scheme) : m_scheme(scheme) {
        QDesktopServices::setUrlHandler(m_scheme, this, "open");
    }
    ~UrlCatcher() override { QDesktopServices::unsetUrlHandler(m_scheme); }
    QList<QUrl> opened;

public slots:
    void open(const QUrl& url) { opened.append(url); }

private:
    QString m_scheme;
};

}  // namespace

TEST(ViewerPage, HandlesShowThePartInViewWhileItMovesAndLeaveTheMouseToTheImage) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("big.jpg"));
    JpegOptions options;
    options.width = 4000;
    options.height = 2000;
    writeJpeg(path, options);
    BrowseContext context;
    QQuickView view;
    QObject::connect(view.engine(), &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            ADD_FAILURE() << warning.toString().toStdString();
        }
    });
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                               {QStringLiteral("active"), true},
                               {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1000, 500);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    ASSERT_NE(view.rootObject(), nullptr);
    view.rootObject()->setProperty("panelAnimated", false);
    context.openPath(path);
    auto* image = view.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(image, nullptr);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->width() == 1000; }));
    auto* horizontal = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("horizontalHandle"));
    auto* vertical = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("verticalHandle"));
    ASSERT_NE(horizontal, nullptr);
    ASSERT_NE(vertical, nullptr);

    auto* bars = view.rootObject()->findChild<QQuickItem*>(QStringLiteral("imageScrollBars"));
    ASSERT_NE(bars, nullptr);
    const auto send = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, at, view.mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&view, &event);
        QCoreApplication::processEvents();
    };
    const auto drag = [&](QPointF from, QPointF by) {
        send(QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 4; ++step) {
            send(QEvent::MouseMove, from + by * step / 4, Qt::NoButton, Qt::LeftButton);
        }
        send(QEvent::MouseButtonRelease, from + by, Qt::LeftButton, Qt::NoButton);
    };

    EXPECT_FALSE(horizontal->isVisible()) << "all of it fits";
    EXPECT_FALSE(vertical->isVisible());

    image->actualSize();
    ASSERT_TRUE(horizontal->isVisible());
    ASSERT_TRUE(vertical->isVisible());
    // The tracks leave a margin at each end and the corner where the handles would meet.
    constexpr double kAcross = 1000 - 2 * 2 - 10;
    constexpr double kDown = 500 - 2 * 2 - 10;
    EXPECT_DOUBLE_EQ(horizontal->width(), 0.25 * kAcross);
    EXPECT_DOUBLE_EQ(vertical->height(), 0.25 * kDown);

    // A press on a handle's strip drags the image like anywhere else.
    const QRectF before = image->visibleArea();
    const QPointF onHandle = horizontal->mapToScene(QPointF(horizontal->width() / 2, horizontal->height() / 2));
    drag(onHandle, QPointF(-200, -100));
    const QRectF after = image->visibleArea();
    EXPECT_DOUBLE_EQ(after.x(), before.x() + 200.0 / 4000);
    EXPECT_DOUBLE_EQ(after.y(), before.y() + 100.0 / 2000);
    EXPECT_DOUBLE_EQ(horizontal->x(), 2 + after.x() * kAcross);
    EXPECT_DOUBLE_EQ(vertical->y(), 2 + after.y() * kDown);

    // Seen as it moved, and gone about half a second after it stopped.
    EXPECT_EQ(bars->opacity(), 1);
    QElapsedTimer sinceStop;
    sinceStop.start();
    EXPECT_TRUE(waitUntil([&] { return bars->opacity() == 0; }, 3000));
    EXPECT_GE(sinceStop.elapsed(), 300);
    EXPECT_LE(sinceStop.elapsed(), 1000);
    const double zoomBefore = image->zoom();
    const QPointF middle(500, 250);
    QWheelEvent wheel(middle, view.mapToGlobal(middle), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&view, &wheel);
    EXPECT_GT(image->zoom(), zoomBefore);
    EXPECT_EQ(bars->opacity(), 1) << "zooming shows them again at once";

    // A tiny part of the image still gives a visible handle, which reaches the end of its track.
    while (image->visibleArea().width() * kAcross > 32 && image->zoom() < 32) {
        image->zoomIn();
    }
    ASSERT_LT(image->visibleArea().width() * kAcross, 32);
    EXPECT_DOUBLE_EQ(horizontal->width(), 32);
    drag(onHandle, QPointF(-100000, 0));
    EXPECT_DOUBLE_EQ(image->visibleArea().right(), 1);
    EXPECT_DOUBLE_EQ(horizontal->x() + horizontal->width(), 2 + kAcross);

    image->fitToView();
    EXPECT_FALSE(horizontal->isVisible()) << "fitted again";
    EXPECT_FALSE(vertical->isVisible());
}

TEST(ViewerPage, PanelTakesRoomFromTheImageExceptAtFullScreen) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    writeJpeg(path);
    BrowseContext context;
    QQuickView view;
    QObject::connect(view.engine(), &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            ADD_FAILURE() << warning.toString().toStdString();
        }
    });
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                               {QStringLiteral("active"), true},
                               {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(1000, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* const page = view.rootObject();
    ASSERT_NE(page, nullptr);
    page->setProperty("panelAnimated", false);
    const auto* image = page->property("view").value<ImageView*>();
    ASSERT_NE(image, nullptr);

    page->setProperty("panelPinned", false);
    EXPECT_TRUE(waitUntil([&] { return image->width() == 1000; }));

    page->setProperty("panelPinned", true);
    EXPECT_TRUE(waitUntil([&] { return image->width() < 1000; }));
    const qreal withPanel = image->width();
    EXPECT_GT(withPanel, 500);

    // At full screen the panel goes over the image, shown or not.
    page->setProperty("fullScreen", true);
    EXPECT_TRUE(waitUntil([&] { return image->width() == 1000; }));
    page->setProperty("panelRevealed", true);
    QCoreApplication::processEvents();
    EXPECT_EQ(image->width(), 1000);

    page->setProperty("fullScreen", false);
    EXPECT_TRUE(waitUntil([&] { return image->width() == withPanel; }));
    page->setProperty("panelPinned", false);  // the settings outlive the test
}

// Why: a setting the program has dropped (sharp resampling, now always on) must not linger
// in the file with the value an older version saved. Input: a settings file with a retired
// key and one still in use. The truth: the retired one is gone, the other kept as it was.
TEST(Settings, ThoseNoLongerUsedAreForgottenAndTheOthersKept) {
    const QTemporaryDir dir;
    QSettings settings(dir.filePath(QStringLiteral("settings.ini")), QSettings::IniFormat);
    settings.setValue(QStringLiteral("InfoPanel/sharpResampling"), false);
    settings.setValue(QStringLiteral("InfoPanel/panelPinned"), true);
    iv::forgetRetiredSettings(settings);
    EXPECT_FALSE(settings.contains(QStringLiteral("InfoPanel/sharpResampling")));
    EXPECT_TRUE(settings.value(QStringLiteral("InfoPanel/panelPinned")).toBool());
}

TEST(Main, LoadsWithItsFileActionsWithoutWarnings) {
    MainFixture f(1);
    ASSERT_NE(f.window, nullptr);
    waitUntil([] { return false; }, 200);
    EXPECT_EQ(f.warnings, QStringList());
}

// Why: browser details belong beside their panes, while the viewer still needs them in
// the app status bar. Input: a loaded image switched browser -> viewer -> browser;
// the same details are visible in the app bar only in viewer mode.
TEST(Main, ImageStatusDetailsBelongToTheViewerRatherThanDuplicatingThePreview) {
    MainFixture f(1);
    ASSERT_NE(f.window, nullptr);
    auto* preview = f.window->property("activeView").value<ImageView*>();
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(waitUntil([&] { return preview->status() == ImageView::Status::Ready; }));
    for (const char* name : {"statusResolution", "statusZoom", "statusImagePosition"}) {
        auto* item = qobject_cast<QQuickItem*>(f.find(name));
        ASSERT_NE(item, nullptr);
        EXPECT_FALSE(item->isVisible()) << name;
    }
    f.window->setProperty("viewing", true);
    ASSERT_TRUE(waitUntil([&] {
        auto* view = f.window->property("activeView").value<ImageView*>();
        return view && view->status() == ImageView::Status::Ready;
    }));
    for (const char* name : {"statusResolution", "statusZoom", "statusImagePosition"}) {
        EXPECT_TRUE(qobject_cast<QQuickItem*>(f.find(name))->isVisible()) << name;
    }
    f.window->setProperty("viewing", false);
    for (const char* name : {"statusResolution", "statusZoom", "statusImagePosition"}) {
        EXPECT_FALSE(qobject_cast<QQuickItem*>(f.find(name))->isVisible()) << name;
    }
}

// Why: the tool bar's buttons show icons instead of words, so their names and keys must still
// be found, in the tip, and an icon name that is not in the resources would leave a blank
// button. Input: the whole window; the truth is the actions' names and shortcuts, with the
// mnemonic and a menu's "..." left out.
TEST(Main, ToolBarButtonsShowIconsAndNameThemselvesInTheirTips) {
    MainFixture f(1);
    ASSERT_NE(f.window, nullptr);
    const QList<std::pair<QString, QString>> buttons = {
        {QStringLiteral("modeButton"), QStringLiteral("View")},
        {QStringLiteral("previousButton"), QStringLiteral("Previous")},
        {QStringLiteral("nextButton"), QStringLiteral("Next")},
        {QStringLiteral("previousPageButton"), QStringLiteral("Prev Page")},
        {QStringLiteral("nextPageButton"), QStringLiteral("Next Page")},
        {QStringLiteral("zoomInButton"), QStringLiteral("Zoom In (+)")},
        {QStringLiteral("zoomOutButton"), QStringLiteral("Zoom Out (-)")},
        {QStringLiteral("fitButton"), QStringLiteral("Fit (F)")},
        {QStringLiteral("actualSizeButton"), QStringLiteral("1:1 (1)")},
        {QStringLiteral("autoContrastButton"), QStringLiteral("Auto Contrast")},
        {QStringLiteral("cropButton"), QStringLiteral("Crop (X)")},
        {QStringLiteral("straightenButton"), QStringLiteral("Straighten (S)")},
        {QStringLiteral("rotateAnticlockwiseButton"), QStringLiteral("Rotate 90° Anticlockwise")},
        {QStringLiteral("rotateClockwiseButton"), QStringLiteral("Rotate 90° Clockwise")},
        {QStringLiteral("annotateButton"), QStringLiteral("Annotate (D)")},
        {QStringLiteral("resizeButton"), QStringLiteral("Resize (Ctrl+R)")},
        {QStringLiteral("adjustButton"), QStringLiteral("Adjust Colours (A)")},
        {QStringLiteral("beforeAfterButton"), QStringLiteral("Before and After (B)")},
        {QStringLiteral("originalButton"), QStringLiteral("Original (hold)")},
        {QStringLiteral("probeButton"), QStringLiteral("Probe Colours (P)")},
        {QStringLiteral("fullScreenButton"), QStringLiteral("Full Screen (F11)")},
    };
    for (const auto& [name, tip] : buttons) {
        QObject* button = f.find(name);
        ASSERT_NE(button, nullptr) << name.toStdString();
        EXPECT_EQ(button->property("tip").toString(), tip) << name.toStdString();
        auto* icon = button->property("contentItem").value<QQuickItem*>();
        ASSERT_NE(icon, nullptr) << name.toStdString();
        EXPECT_TRUE(icon->property("valid").toBool()) << name.toStdString();
    }
    EXPECT_EQ(f.warnings, QStringList());

    // Set IV_TOOLBAR_PNG to a file name to have the window saved, to look at the icons.
    if (const QString file = qEnvironmentVariable("IV_TOOLBAR_PNG"); !file.isEmpty()) {
        f.window->grabWindow().save(file);
    }
}

TEST(Main, EditingThePathTurnsOffShortcutsForKeysATextFieldLeaves) {
    MainFixture f(0);
    ASSERT_NE(f.window, nullptr);
    QObject* window = f.window;
    auto* bar = f.find(QStringLiteral("pathBar"));
    ASSERT_NE(bar, nullptr);

    EXPECT_FALSE(window->property("typing").toBool());
    QMetaObject::invokeMethod(bar, "edit");
    EXPECT_TRUE(window->property("typing").toBool());
    bar->setProperty("editing", false);
    EXPECT_FALSE(window->property("typing").toBool());
}

TEST(KeyNames, WritesShortcutsAsTheSystemDoes) {
    EXPECT_EQ(iv::KeyNames::text(QStringLiteral("Ctrl+Shift+N")), QKeySequence(QStringLiteral("Ctrl+Shift+N")).toString(QKeySequence::NativeText));
    EXPECT_EQ(iv::KeyNames::text(QStringLiteral("F5")), QStringLiteral("F5"));
    EXPECT_EQ(iv::KeyNames::text(static_cast<int>(QKeySequence::Copy)), QKeySequence::keyBindings(QKeySequence::Copy).value(0).toString(QKeySequence::NativeText));
    EXPECT_FALSE(iv::KeyNames::text(static_cast<int>(QKeySequence::Copy)).isEmpty());
    EXPECT_EQ(iv::KeyNames::text(QVariant()), QString());
    EXPECT_EQ(iv::KeyNames::text(QString()), QString());
}

TEST(AppMenuItem, ShowsTheShortcutOfItsAction) {
    QQmlEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            ADD_FAILURE() << warning.toString().toStdString();
        }
    });
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 400; visible: true
            property alias menu: menu
            property alias plain: plain
            property alias standard: standard
            property alias none: none
            property alias longest: longest
            Action { id: renameAction; text: "&Rename"; shortcut: "F2" }
            Action { id: longAction; text: "Delete Permanently Something Rather Long..."; shortcut: "Ctrl+Shift+Alt+Del" }
            Action { id: copyAction; text: "&Copy"; shortcut: StandardKey.Copy }
            Action { id: modeAction; text: "View" }
            AppMenu {
                id: menu
                AppMenuItem { id: longest; action: longAction }
                AppMenuItem { id: plain; action: renameAction }
                AppMenuItem { id: standard; action: copyAction }
                AppMenuItem { id: none; action: modeAction }
            }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    QMetaObject::invokeMethod(root->property("menu").value<QObject*>(), "open");
    const auto label = [&](const char* name) { return findNamed(root->property(name).value<QQuickItem*>(), QStringLiteral("shortcutText")); };
    ASSERT_NE(label("plain"), nullptr);
    EXPECT_EQ(label("plain")->property("text").toString(), QStringLiteral("F2"));
    EXPECT_TRUE(label("plain")->isVisible());
    EXPECT_LT(label("plain")->opacity(), 1);
    EXPECT_FALSE(label("standard")->property("text").toString().isEmpty());
    EXPECT_FALSE(label("none")->isVisible());
    // The menu makes room for it beside the longest name.
    auto* item = root->property("plain").value<QQuickItem*>();
    EXPECT_GE(item->property("implicitWidth").toReal(), label("plain")->implicitWidth() + 32);
    auto* menu = root->property("menu").value<QObject*>();
    auto* longest = root->property("longest").value<QQuickItem*>();
    EXPECT_GE(menu->property("width").toReal(), longest->implicitWidth());
    EXPECT_GE(longest->width(), longest->implicitWidth()) << "nothing runs into the shortcut";
}

TEST(MessageBox, TakesTheHeightOfAllItsText) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 400; visible: true
            property alias box: box
            MessageBox { id: box; title: "A caption"; standardButtons: Dialog.Ok }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* box = root->property("box").value<QObject*>();
    box->setProperty("text", QStringLiteral("one"));
    const qreal oneLine = box->property("implicitContentHeight").toReal();
    box->setProperty("text", QStringLiteral("one\n\nthree\nfour\nfive"));
    QMetaObject::invokeMethod(box, "open");
    EXPECT_TRUE(waitUntil([&] { return box->property("opened").toBool(); }));
    auto* content = box->property("contentItem").value<QQuickItem*>();
    EXPECT_TRUE(waitUntil([&] { return content->height() > std::max(oneLine, 32.0) + 30; }, 1000))
        << "the names of files to delete are in there, but it is " << content->height();
}

// Why: each kind of message shows its own sign, in its colour, so that a warning or an error
// is told from news at a glance; a name missing from the resources would leave a blank space.
// Input: a message box of each kind. The truth: the icon is found, named for the kind, in the
// accent for info and questions and in Theme's warning and danger fills for the others.
TEST(MessageBox, EachKindShowsItsSignInItsColour) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 400; visible: true
            property alias box: box
            MessageBox { id: box; title: "A caption"; text: "Text"; standardButtons: Dialog.Ok }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* box = root->property("box").value<QObject*>();
    QObject* theme = engine.singletonInstance<QObject*>("ImageViewer", "Theme");
    const auto colour = [&](const char* role) { return theme->property(role).value<QColor>(); };
    const QList<std::pair<QString, QColor>> kinds = {
        {QStringLiteral("info"), colour("accent")},
        {QStringLiteral("question"), colour("accent")},
        {QStringLiteral("warning"), colour("warningFill")},
        {QStringLiteral("error"), colour("dangerFill")},
    };
    for (const auto& [kind, ink] : kinds) {
        box->setProperty("kind", kind);
        auto* icon = box->findChild<QQuickItem*>(QStringLiteral("messageIcon"));
        ASSERT_NE(icon, nullptr);
        EXPECT_EQ(icon->property("name").toString(), QStringLiteral("message-") + kind);
        EXPECT_TRUE(icon->property("valid").toBool()) << kind.toStdString();
        EXPECT_EQ(icon->property("ink").value<QColor>(), ink) << kind.toStdString();
    }
}

TEST(MessageBox, KeysWorkAsInAStandardMessageBox) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 400; visible: true
            property int accepts: 0
            property int rejects: 0
            property int outside: 0
            property alias box: box
            Shortcut { sequences: [StandardKey.Copy, "Return"]; onActivated: outside++ }
            MessageBox {
                id: box
                kind: "warning"
                title: "A caption"
                text: "Some text."
                standardButtons: Dialog.Yes | Dialog.No
                onAccepted: accepts++
                onRejected: rejects++
            }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    ASSERT_NE(window, nullptr);
    window->requestActivate();
    ASSERT_TRUE(waitUntil([&] { return window->isExposed(); }));
    auto* box = root->property("box").value<QObject*>();
    const auto show = [&] {
        QMetaObject::invokeMethod(box, "open");
        EXPECT_TRUE(waitUntil([&] { return box->property("opened").toBool(); }));
    };

    show();
    QGuiApplication::clipboard()->clear();
    QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(QGuiApplication::clipboard()->text(), QStringLiteral("A caption\n\nSome text."));
    EXPECT_TRUE(box->property("visible").toBool()) << "copying leaves it open";

    // Enter presses the button in focus, the first one unless the dialog says otherwise.
    QTest::keyClick(window, Qt::Key_Return);
    EXPECT_EQ(root->property("accepts").toInt(), 1);
    EXPECT_TRUE(waitUntil([&] { return !box->property("visible").toBool(); }));

    box->setProperty("focusedButton", QQuickDialogNoButton());
    show();
    QTest::keyClick(window, Qt::Key_Return);
    EXPECT_EQ(root->property("rejects").toInt(), 1) << "a dangerous question can start at No";
    EXPECT_TRUE(waitUntil([&] { return !box->property("visible").toBool(); }));

    show();
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_EQ(root->property("rejects").toInt(), 2);
    EXPECT_EQ(root->property("outside").toInt(), 0) << "none of these reach the window's shortcuts";
}

TEST(MessageBox, EnterOnACheckBoxPressesTheDefaultButton) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 400; visible: true
            property int accepts: 0
            property alias box: box
            property alias check: check
            MessageBox {
                id: box
                title: "A caption"
                text: "Some text."
                standardButtons: Dialog.Ok | Dialog.Cancel
                extras: [CheckBox { id: check; text: "An option" }]
                onAccepted: accepts++
            }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    window->requestActivate();
    ASSERT_TRUE(waitUntil([&] { return window->isExposed(); }));
    auto* box = root->property("box").value<QObject*>();
    auto* check = root->property("check").value<QQuickItem*>();
    QMetaObject::invokeMethod(box, "open");
    ASSERT_TRUE(waitUntil([&] { return box->property("opened").toBool(); }));

    check->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_Space);
    EXPECT_TRUE(check->property("checked").toBool()) << "Space toggles";
    QTest::keyClick(window, Qt::Key_Return);
    EXPECT_TRUE(check->property("checked").toBool()) << "Enter does not";
    EXPECT_EQ(root->property("accepts").toInt(), 1);
}

TEST(ViewerPage, WheelMovesThroughTheImages) {
    QTemporaryDir dir;
    for (const char* name : {"a.jpg", "b.jpg", "c.jpg"}) {
        writeJpeg(dir.filePath(QString::fromLatin1(name)));
    }
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(600, 400);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    ASSERT_NE(view.rootObject(), nullptr);
    auto* image = view.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(image, nullptr);
    // The info panel starts by sliding out of the way, and the image has no width until then.
    ASSERT_TRUE(waitUntil([&] { return view.isExposed() && image->width() > 500; }));
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 3 && context.currentIndex() == 0; }));
    const auto wheel = [&](int delta) {
        const QPointF at(100, 100);
        QWheelEvent event(at, view.mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &event);
    };
    wheel(120);
    EXPECT_EQ(context.currentIndex(), 0) << "stays at the first";
    wheel(-120);
    EXPECT_EQ(context.currentIndex(), 1);
    wheel(-360);
    EXPECT_EQ(context.currentIndex(), 2) << "stops at the last";
    wheel(120);
    EXPECT_EQ(context.currentIndex(), 1);
}

TEST(Main, StraightenFromTheBrowserOpensTheImageFirst) {
    MainFixture f([](const QTemporaryDir& dir) {
        writeTiltedJpeg(dir);
        QFile::copy(dir.filePath(QStringLiteral("tilted.jpg")), dir.filePath(QStringLiteral("tilted2.jpg")));
    }, QString());
    ASSERT_NE(f.window, nullptr);
    QQuickWindow* window = f.window;
    BrowseContext* context = f.context;
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 2; }));
    ASSERT_FALSE(window->property("viewing").toBool());

    // Two chosen: nothing to straighten.
    context->selectAll();
    QTest::keyClick(window, Qt::Key_S);
    EXPECT_FALSE(window->property("viewing").toBool());

    context->clickFile(1, false, false);
    ASSERT_EQ(context->selectedPaths().size(), 1);
    QTest::keyClick(window, Qt::Key_S);
    EXPECT_TRUE(window->property("viewing").toBool());
    auto* image = window->property("activeView").value<ImageView*>();
    ASSERT_TRUE(waitUntil([&] { return window->property("straightening").toBool(); }));
    EXPECT_TRUE(image->source().endsWith(QStringLiteral("tilted2.jpg")));
    EXPECT_EQ(image->status(), ImageView::Status::Ready);
}

// Why: like Crop and Straighten, rotating from the browser opens the one chosen picture in the
// viewer and turns it there; with two chosen, or while a tool is at work, there is nothing it
// could turn. Input: a folder of two JPEGs, both chosen, then one, then Rotate 90° Clockwise, then
// Crop started. The truth: nothing happens with two; with one the viewer opens on it, turned
// (32 x 64 becomes 64 x 32) and modified; all five commands are off while cropping.
TEST(Main, RotateFromTheBrowserOpensTheImageAndTurnsIt) {
    MainFixture f([](const QTemporaryDir& dir) {
        writeTurnedJpeg(dir, QStringLiteral("a.jpg"));
        writeTurnedJpeg(dir, QStringLiteral("b.jpg"));
    }, QString());
    ASSERT_NE(f.window, nullptr);
    QQuickWindow* window = f.window;
    BrowseContext* context = f.context;
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 2; }));
    QObject* rotate = f.find(QStringLiteral("rotateClockwiseAction"));
    ASSERT_NE(rotate, nullptr);

    context->selectAll();
    EXPECT_FALSE(rotate->property("enabled").toBool()) << "two chosen";

    context->clickFile(1, false, false);
    ASSERT_TRUE(rotate->property("enabled").toBool());
    QMetaObject::invokeMethod(rotate, "trigger");
    EXPECT_TRUE(window->property("viewing").toBool());
    auto* image = window->property("activeView").value<ImageView*>();
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->modified(); }));
    EXPECT_TRUE(image->source().endsWith(QStringLiteral("b.jpg")));
    EXPECT_EQ(QSize(image->imageWidth(), image->imageHeight()), QSize(64, 32));

    QTest::keyClick(window, Qt::Key_X);
    ASSERT_TRUE(waitUntil([&] { return window->property("cropping").toBool(); }));
    for (const char* name : {"rotateClockwiseAction", "rotateAnticlockwiseAction", "rotate180Action", "flipHorizontalAction",
                             "flipVerticalAction"}) {
        QObject* action = f.find(QLatin1String(name));
        ASSERT_NE(action, nullptr) << name;
        EXPECT_FALSE(action->property("enabled").toBool()) << name << " while cropping";
    }
    EXPECT_EQ(f.warnings, QStringList());
}

TEST(Main, DeleteWhileStraighteningRemovesTheLineNotTheFile) {
    MainFixture f([](const QTemporaryDir& dir) { writeTiltedJpeg(dir); }, QStringLiteral("tilted.jpg"));
    ASSERT_NE(f.window, nullptr);
    QQuickWindow* window = f.window;
    const QString path = f.dir.filePath(QStringLiteral("tilted.jpg"));
    auto* image = window->property("activeView").value<ImageView*>();
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 400; }));

    QTest::keyClick(window, Qt::Key_S);
    ASSERT_TRUE(window->property("straightening").toBool());
    auto* overlay = window->findChild<QQuickItem*>(QStringLiteral("straightenOverlay"));
    ASSERT_NE(overlay, nullptr);
    waitUntil([] { return false; }, 300);
    const auto at = [&](double x, double y) { return image->mapToScene(image->imageToItem(QPointF(x, y))).toPoint(); };
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, at(50, 100));
    QTest::mouseMove(window, at(200, 110));
    QTest::mouseMove(window, at(350, 121));
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, at(350, 121));
    ASSERT_EQ(overlay->property("lines").toList().size(), 1);
    ASSERT_EQ(overlay->property("selected").toInt(), 0);

    QTest::keyClick(window, Qt::Key_Delete);
    EXPECT_EQ(overlay->property("lines").toList().size(), 0);
    waitUntil([] { return false; }, 300);
    EXPECT_TRUE(QFile::exists(path));
    EXPECT_TRUE(window->property("straightening").toBool());
}

TEST(Main, LeavingAnEditedImageAsksFirst) {
    MainFixture f([](const QTemporaryDir& dir) {
        writeTurnedJpeg(dir, QStringLiteral("a.jpg"));
        writeTurnedJpeg(dir, QStringLiteral("b.jpg"));
    }, QStringLiteral("a.jpg"));
    ASSERT_NE(f.window, nullptr);
    QObject* window = f.window;
    BrowseContext* context = f.context;
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 2; }));
    auto* image = window->property("activeView").value<ImageView*>();
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->source().endsWith(QStringLiteral("a.jpg")); }));
    auto* dialog = window->findChild<QObject*>(QStringLiteral("unsavedDialog"));
    ASSERT_NE(dialog, nullptr);

    // Without changes it just goes.
    QMetaObject::invokeMethod(window, "next");
    EXPECT_EQ(context->currentIndex(), 1);
    EXPECT_FALSE(dialog->property("opened").toBool());
    QMetaObject::invokeMethod(window, "previous");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->source().endsWith(QStringLiteral("a.jpg")); }));

    image->crop(QRect(0, 0, 16, 16));
    ASSERT_TRUE(waitUntil([&] { return image->modified() && image->status() == ImageView::Status::Ready; }));
    EXPECT_TRUE(window->property("title").toString().startsWith(QLatin1Char('*')));

    QMetaObject::invokeMethod(window, "next");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(context->currentIndex(), 0) << "stays until answered";
    QMetaObject::invokeMethod(dialog, "reject");
    ASSERT_TRUE(waitUntil([&] { return !dialog->property("visible").toBool(); }));
    EXPECT_EQ(context->currentIndex(), 0);
    EXPECT_TRUE(image->modified());

    QMetaObject::invokeMethod(window, "next");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    QMetaObject::invokeMethod(dialog, "discarded");
    EXPECT_EQ(context->currentIndex(), 1);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->source().endsWith(QStringLiteral("b.jpg")); }));
    EXPECT_FALSE(image->modified());
}

TEST(Compare, TwoFilesZoomAndScrollTogether) {
    QTemporaryDir dir;
    const QString a = writeGradient(dir, QStringLiteral("a.jpg"), 400, 300, false);
    const QString b = writeGradient(dir, QStringLiteral("b.jpg"), 400, 300, true);
    ComparePage f(a);
    ASSERT_TRUE(settled(f.image));
    QMetaObject::invokeMethod(f.page, "compareWith", Q_ARG(QVariant, b));
    EXPECT_EQ(f.comparing(), QStringLiteral("files"));
    ASSERT_TRUE(waitUntil([&] { return f.active(); }));
    EXPECT_EQ(f.splitter->property("firstLabel").toString(), QStringLiteral("b.jpg"));

    // Far enough to scroll, from whatever fit the layout gave so far.
    for (int i = 0; i < 8 && f.image->visibleArea().width() >= 0.8; ++i) {
        f.image->zoomIn();
    }
    ASSERT_LT(f.image->visibleArea().width(), 0.8);
    EXPECT_DOUBLE_EQ(f.other->zoom(), f.image->zoom());
    EXPECT_EQ(f.other->visibleArea(), f.image->visibleArea());
    f.other->panBy(-40, -30);
    EXPECT_EQ(f.image->visibleArea(), f.other->visibleArea()) << "either one leads";

    // A drag left of the line reaches the picture under it, and both follow.
    const QRectF before = f.image->visibleArea();
    const QPoint start = f.image->mapToScene(QPointF(f.image->width() * 0.2, f.image->height() / 2)).toPoint();
    QTest::mousePress(&f.view, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(&f.view, start + QPoint(30, 20));
    QTest::mouseMove(&f.view, start + QPoint(60, 40));
    QTest::mouseRelease(&f.view, Qt::LeftButton, Qt::NoModifier, start + QPoint(60, 40));
    EXPECT_NE(f.image->visibleArea(), before);
    EXPECT_EQ(f.image->visibleArea(), f.other->visibleArea());

    // Another image ends it.
    f.image->setSource(b);
    EXPECT_EQ(f.comparing(), QString());
    EXPECT_EQ(f.image->linkedView(), nullptr);
}

TEST(Compare, PicturesOfDifferentSizesAreRefused) {
    QTemporaryDir dir;
    const QString a = writeGradient(dir, QStringLiteral("a.jpg"), 400, 300, false);
    const QString b = writeGradient(dir, QStringLiteral("b.jpg"), 300, 300, false);
    ComparePage f(a);
    ASSERT_TRUE(settled(f.image));
    QMetaObject::invokeMethod(f.page, "compareWith", Q_ARG(QVariant, b));
    ASSERT_TRUE(waitUntil([&] { return f.comparing().isEmpty(); }));
    EXPECT_FALSE(f.page->property("compareNotice").toString().isEmpty());
    EXPECT_FALSE(f.active());
}

TEST(Compare, BeforeAndAfterShowTheOriginalLeftOfTheLine) {
    QTemporaryDir dir;
    const QString a = writeGradient(dir, QStringLiteral("a.jpg"), 400, 300, false);
    ComparePage f(a);
    ASSERT_TRUE(settled(f.image));
    EXPECT_FALSE(f.page->property("canCompareEdit").toBool()) << "nothing to compare yet";
    f.image->grayscale(0);
    ASSERT_TRUE(settled(f.image));
    QMetaObject::invokeMethod(f.page, "toggleBeforeAfter");
    ASSERT_TRUE(waitUntil([&] { return f.active(); }));
    waitUntil([] { return false; }, 200);
    const QImage shot = f.view.grabWindow();
    const auto colourAt = [&](double x) {
        return shot.pixelColor(f.image->mapToScene(f.image->imageToItem(QPointF(x, 150))).toPoint() * f.view.devicePixelRatio());
    };
    const double line = f.splitter->property("lineX").toDouble();
    const double leftX = f.image->itemToImage(QPointF(line - 40, 0)).x();
    const double rightX = f.image->itemToImage(QPointF(line + 40, 0)).x();
    const QColor left = colourAt(leftX);
    const QColor right = colourAt(rightX);
    EXPECT_GT(left.blue() - left.red(), 40) << "the original, in colour";
    EXPECT_NEAR(right.red(), right.blue(), 3) << "the edit, gray";

    QMetaObject::invokeMethod(f.page, "toggleBeforeAfter");
    EXPECT_EQ(f.comparing(), QString());

    // Holding shows the original alone, for as long as it is held.
    f.page->setProperty("holdingOriginal", true);
    EXPECT_EQ(f.comparing(), QStringLiteral("edit"));
    ASSERT_TRUE(waitUntil([&] { return f.active(); }));
    EXPECT_GE(f.splitter->property("lineX").toDouble(), f.image->width() - 1);
    f.page->setProperty("holdingOriginal", false);
    EXPECT_EQ(f.comparing(), QString());
}

TEST(KeyHold, TellsWhileAKeyIsHeldAndIgnoresRepeats) {
    QQuickWindow window;
    iv::KeyHold hold;
    hold.setWindow(&window);
    hold.setKey(Qt::Key_O);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_O, Qt::NoModifier, QStringLiteral("o"));
    QKeyEvent repeat(QEvent::KeyRelease, Qt::Key_O, Qt::NoModifier, QStringLiteral("o"), true);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_O, Qt::NoModifier, QStringLiteral("o"));
    QKeyEvent other(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier, QStringLiteral("p"));
    QCoreApplication::sendEvent(&window, &press);
    EXPECT_TRUE(hold.held());
    QCoreApplication::sendEvent(&window, &repeat);
    EXPECT_TRUE(hold.held()) << "an auto-repeated release is not letting go";
    QCoreApplication::sendEvent(&window, &other);
    EXPECT_TRUE(hold.held());
    QCoreApplication::sendEvent(&window, &release);
    EXPECT_FALSE(hold.held());
    hold.setEnabled(false);
    QCoreApplication::sendEvent(&window, &press);
    EXPECT_FALSE(hold.held());
}

TEST(FileChooser, ChoosingAnotherTypeChangesTheExtension) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    EXPECT_EQ(f.chooser->property("typeIndex").toInt(), 0);
    QMetaObject::invokeMethod(f.part("typeBox"), "activated", Q_ARG(int, 1));
    EXPECT_EQ(f.name(), QStringLiteral("a.png"));
    EXPECT_EQ(f.part("typeBox")->property("currentIndex").toInt(), 1);
    // Only files of the type are listed.
    EXPECT_TRUE(waitUntil([&] { return f.listing()->property("count").toInt() == 2; }));  // sub and b.png

    f.click(f.item("acceptButton"));
    ASSERT_EQ(f.chosen->size(), 1);
    EXPECT_EQ(f.chosen->at(0).at(0).toString(), f.dir.filePath(QStringLiteral("a.png")));
    EXPECT_EQ(f.cancelled->size(), 0);
}

TEST(FileChooser, TypingAKnownExtensionChoosesItsTypeAndANameWithoutOneGetsTheType) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    QQuickItem* field = f.item("nameField");
    ASSERT_TRUE(waitUntil([&] { return field->hasActiveFocus(); }));
    QTest::keyClick(f.window, Qt::Key_A, Qt::ControlModifier);
    typeText(f.window, QStringLiteral("new.png"));
    EXPECT_EQ(f.name(), QStringLiteral("new.png"));
    EXPECT_EQ(f.chooser->property("typeIndex").toInt(), 1);

    QTest::keyClick(f.window, Qt::Key_A, Qt::ControlModifier);
    typeText(f.window, QStringLiteral("plain"));
    QTest::keyClick(f.window, Qt::Key_Return);
    ASSERT_EQ(f.chosen->size(), 1);
    EXPECT_EQ(f.chosen->at(0).at(0).toString(), f.dir.filePath(QStringLiteral("plain.png")));
}

TEST(FileChooser, SaveSavesTheTypedNameEvenWithAFolderSelected) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("new.jpg"));
    f.pick(QStringLiteral("sub"));
    EXPECT_EQ(f.name(), QStringLiteral("new.jpg")) << "a folder does not take the place of the name";
    f.click(f.item("acceptButton"));
    ASSERT_EQ(f.chosen->size(), 1);
    EXPECT_EQ(f.chosen->at(0).at(0).toString(), f.dir.filePath(QStringLiteral("new.jpg")));
}

TEST(FileChooser, ANameOfAFolderOpensItAndKeepsTheFileName) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("new.jpg"));
    f.item("nameField")->setProperty("text", QStringLiteral("sub"));
    f.click(f.item("acceptButton"));
    EXPECT_EQ(f.chosen->size(), 0);
    EXPECT_TRUE(waitUntil([&] { return f.directory() == f.dir.filePath(QStringLiteral("sub")); }));
    EXPECT_EQ(f.name(), QStringLiteral("new.jpg"));
    EXPECT_TRUE(f.chooser->property("opened").toBool());
}

TEST(FileChooser, ReplacingAFileAsksAndCancelKeepsTheChooser) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("x.jpg"));
    f.pick(QStringLiteral("a.jpg"));
    EXPECT_EQ(f.name(), QStringLiteral("a.jpg"));
    f.click(f.item("acceptButton"));
    QObject* replace = f.part("replaceBox");
    ASSERT_TRUE(waitUntil([&] { return replace->property("opened").toBool(); }));
    EXPECT_EQ(f.chosen->size(), 0);

    QMetaObject::invokeMethod(replace, "reject");
    ASSERT_TRUE(waitUntil([&] { return !replace->property("visible").toBool(); }));
    EXPECT_TRUE(f.chooser->property("opened").toBool());
    EXPECT_EQ(f.chosen->size(), 0);
    EXPECT_EQ(f.cancelled->size(), 0);

    f.click(f.item("acceptButton"));
    ASSERT_TRUE(waitUntil([&] { return replace->property("opened").toBool(); }));
    QMetaObject::invokeMethod(replace, "accept");
    ASSERT_EQ(f.chosen->size(), 1);
    EXPECT_EQ(f.chosen->at(0).at(0).toString(), f.dir.filePath(QStringLiteral("a.jpg")));
}

TEST(FileChooser, EscapeCancels) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    QTest::keyClick(f.window, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return f.cancelled->size() == 1; }));
    EXPECT_EQ(f.chosen->size(), 0);
}

namespace {

// The folder menu, name dialog and buttons of a chooser, which has its own of each.
QQuickItem* chooserItem(const ChooserFixture& f, const char* name) {
    return f.chooser->findChild<QQuickItem*>(QString::fromLatin1(name));
}

QObject* chooserObject(const ChooserFixture& f, const char* name) {
    return f.chooser->findChild<QObject*>(QString::fromLatin1(name));
}

// Answers the chooser's name dialog with a name, as typing it and pressing OK would.
void nameInChooser(const ChooserFixture& f, const QString& name) {
    QObject* dialog = chooserObject(f, "chooserNameDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    dialog->findChild<QObject*>(QStringLiteral("nameField"))->setProperty("text", name);
    QMetaObject::invokeMethod(dialog, "submit");
}

}  // namespace

// Why: saving into a folder that does not exist yet needed a trip to the file manager. The
// chooser makes one where it is, as other systems' save dialogs do, and goes into it.
// Input: the save chooser in a folder, New Folder named "made", then Save with the name typed
// before. The truth: the folder exists, the chooser shows it, and the name kept goes there.
TEST(FileChooser, NewFolderMakesTheFolderOpensItAndSavesThere) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("new.jpg"));
    f.click(chooserItem(f, "newFolderButton"));
    nameInChooser(f, QStringLiteral("made"));
    const QString made = f.dir.filePath(QStringLiteral("made"));
    EXPECT_TRUE(waitUntil([&] { return f.directory() == made; })) << "in " << f.directory().toStdString();
    EXPECT_TRUE(QFileInfo(made).isDir());
    EXPECT_EQ(f.name(), QStringLiteral("new.jpg")) << "the name typed is kept";
    f.click(f.item("acceptButton"));
    ASSERT_EQ(f.chosen->size(), 1);
    EXPECT_EQ(f.chosen->at(0).at(0).toString(), made + QStringLiteral("/new.jpg"));
}

// Why: a name in use must not be taken over, nor the chooser moved anywhere.
// Input: New Folder named "sub", which exists. The truth: the dialog stays open and says why,
// and the chooser is still in the folder it was.
TEST(FileChooser, NewFolderTurnsDownANameInUse) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("new.jpg"));
    QMetaObject::invokeMethod(chooserObject(f, "chooserFolderMenu"), "createIn", Q_ARG(QVariant, f.dir.path()));
    nameInChooser(f, QStringLiteral("sub"));
    QObject* dialog = chooserObject(f, "chooserNameDialog");
    EXPECT_TRUE(dialog->property("opened").toBool());
    EXPECT_FALSE(dialog->findChild<QObject*>(QStringLiteral("nameProblem"))->property("text").toString().isEmpty());
    EXPECT_EQ(f.directory(), f.dir.path());
}

// Why: the chooser's folder tree offers what the browser's does, from the same code, so the
// two cannot drift apart. Input: the menu for "sub", then each action of it. The truth: the
// same four items; Open in File Manager hands over that folder; renaming the folder shown
// keeps the chooser in it under its new name; Move to Trash asks, and No keeps the folder.
TEST(FileChooser, FolderMenuHasTheBrowsersActions) {
    ChooserFixture f(QStringLiteral("save"));
    UrlCatcher catcher(QStringLiteral("file"));
    f.start(QStringLiteral("new.jpg"));
    QObject* folders = chooserObject(f, "chooserFolderMenu");
    ASSERT_NE(folders, nullptr);
    const QString sub = f.dir.filePath(QStringLiteral("sub"));

    QMetaObject::invokeMethod(folders, "popup", Q_ARG(QVariant, sub));
    QObject* menu = folders->findChild<QObject*>(QStringLiteral("folderMenu"));
    ASSERT_TRUE(waitUntil([&] { return menu->property("opened").toBool(); }));
    QStringList texts;
    for (int i = 0; i < menu->property("count").toInt(); ++i) {
        QQuickItem* entry = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, entry), Q_ARG(int, i));
        const QString text = entry ? entry->property("text").toString() : QString();
        if (!text.isEmpty())
            texts << text;
    }
    EXPECT_EQ(texts, (QStringList{"&New Folder...", "&Rename...", "Move to &Trash...", "&Open in File Manager"}));
    QMetaObject::invokeMethod(folders->findChild<QObject*>(QStringLiteral("openFolderInFileManagerItem")), "click");
    ASSERT_EQ(catcher.opened.size(), 1);
    EXPECT_EQ(catcher.opened.first(), QUrl::fromLocalFile(sub));

    QMetaObject::invokeMethod(f.chooser, "openFolder", Q_ARG(QVariant, sub));
    ASSERT_TRUE(waitUntil([&] { return f.directory() == sub; }));
    QMetaObject::invokeMethod(folders, "rename", Q_ARG(QVariant, sub));
    nameInChooser(f, QStringLiteral("renamed"));
    const QString renamed = f.dir.filePath(QStringLiteral("renamed"));
    EXPECT_TRUE(waitUntil([&] { return f.directory() == renamed; })) << "in " << f.directory().toStdString();

    QMetaObject::invokeMethod(folders, "trash", Q_ARG(QVariant, renamed));
    QObject* question = folders->findChild<QObject*>(QStringLiteral("trashFolderDialog"));
    ASSERT_TRUE(waitUntil([&] { return question->property("opened").toBool(); }));
    QMetaObject::invokeMethod(question, "reject");
    ASSERT_TRUE(waitUntil([&] { return !question->property("visible").toBool(); }));
    EXPECT_TRUE(QFileInfo(renamed).isDir()) << "No keeps the folder";
}

// Why: the chooser is large and worth resizing, so it is a window of its own that the system
// decorates, moves and resizes, rather than a sheet drawn inside the main window, which could
// do none of it. Input: the chooser opened over its window. The truth is the windows: two of
// them, the chooser marked a dialog, modal, and belonging to the window it was declared in.
// Why: Escape closes the chooser, but while the path is being edited it only ends that, as
// it does in the browser; the chooser's own Escape waits for what has the keys to pass it on.
// Input: the path bar of the chooser put in editing, then Escape twice. The truth is the
// chooser open after the first, with the editing ended, and cancelled after the second.
TEST(FileChooser, EscapeWhileEditingThePathOnlyEndsTheEditing) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    auto* bar = f.chooser->findChild<QQuickItem*>(QStringLiteral("chooserPathBar"));
    ASSERT_NE(bar, nullptr);
    QMetaObject::invokeMethod(bar, "edit");
    ASSERT_TRUE(waitUntil([&] { return bar->property("editing").toBool(); }));
    QTest::keyClick(f.window, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return !bar->property("editing").toBool(); }));
    EXPECT_TRUE(f.chooser->property("opened").toBool());
    EXPECT_EQ(f.cancelled->size(), 0);

    f.item("fileList")->forceActiveFocus();
    QTest::keyClick(f.window, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return f.cancelled->size() == 1; }));
}

TEST(FileChooser, IsADialogWindowOfItsOwnOverItsWindow) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    ASSERT_NE(f.window, nullptr);
    EXPECT_NE(f.window, f.owner);
    EXPECT_EQ(f.window->transientParent(), f.owner);
    EXPECT_EQ(f.window->flags() & Qt::WindowType_Mask, Qt::Dialog);
    EXPECT_NE(f.window->modality(), Qt::NonModal);
    EXPECT_TRUE(f.window->isVisible());
}

// Why: a chooser made larger stays so; opening it at its first size each time would undo it.
// Input: the chooser opened, resized, closed and opened again. The truth is its size then,
// which is kept in the settings (of the test's own home).
// Why: Brief and Details are icons, so their names are in the tips, and an icon missing from
// the resources would leave them blank. Input: the chooser; the truth is the buttons' names.
TEST(FileChooser, TheViewButtonsShowIconsAndNameThemselves) {
    ChooserFixture f(QStringLiteral("open"));
    f.start(QString());
    for (const auto& [name, tip] : {std::pair{"briefButton", "Brief"}, std::pair{"detailsButton", "Details"}}) {
        auto* button = f.chooser->findChild<QQuickItem*>(QString::fromLatin1(name));
        ASSERT_NE(button, nullptr) << name;
        EXPECT_EQ(button->property("tip").toString(), QString::fromLatin1(tip));
        auto* icon = button->property("contentItem").value<QQuickItem*>();
        ASSERT_NE(icon, nullptr) << name;
        EXPECT_TRUE(icon->property("valid").toBool()) << name;
    }
}

TEST(FileChooser, KeepsTheSizeItWasGiven) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    f.window->resize(760, 520);
    ASSERT_TRUE(waitUntil([&] { return f.window->width() == 760 && f.window->height() == 520; }));
    QMetaObject::invokeMethod(f.chooser, "close");
    ASSERT_TRUE(waitUntil([&] { return !f.window->isVisible(); }));
    f.window->resize(400, 300);  // as if the window had been made anew at another size

    f.start(QStringLiteral("a.jpg"));
    EXPECT_EQ(f.window->width(), 760);
    EXPECT_EQ(f.window->height(), 520);
}

TEST(FileChooser, OpenEntersFoldersAndOpensFiles) {
    ChooserFixture f(QStringLiteral("open"));
    f.chooser->setProperty("types", QVariantList{QVariantMap{{"label", "All files"}, {"extensions", QStringList{}}}});
    f.start(QString());
    QQuickItem* list = f.item("fileList");
    ASSERT_TRUE(waitUntil([&] { return list->hasActiveFocus(); }));
    QTest::keyClick(f.window, Qt::Key_Down);  // the first entry, the folder
    EXPECT_EQ(list->property("currentIndex").toInt(), 0);
    QTest::keyClick(f.window, Qt::Key_Return);
    EXPECT_TRUE(waitUntil([&] { return f.directory() == f.dir.filePath(QStringLiteral("sub")); }));
    EXPECT_EQ(f.chosen->size(), 0);
    QTest::keyClick(f.window, Qt::Key_Backspace);
    EXPECT_TRUE(waitUntil([&] { return f.directory() == f.dir.path(); }));

    ASSERT_TRUE(waitUntil([&] { return f.listing()->property("count").toInt() == 3; }));
    f.pick(QStringLiteral("b.png"));
    EXPECT_EQ(f.name(), QStringLiteral("b.png"));
    f.click(f.item("acceptButton"));
    ASSERT_EQ(f.chosen->size(), 1);
    EXPECT_EQ(f.chosen->at(0).at(0).toString(), f.dir.filePath(QStringLiteral("b.png")));
}

// Why: in the chooser, typing in the list filters it at once, without a field taking room
// until then; the best fit becomes current so that Enter opens it, and Esc gives the whole list
// and the keys back. Input: a.jpg, b.png and sub; "b" typed, Enter; then "/" and Esc. The truth
// is the filter's field and text, the rows, and the file chosen.
TEST(FileChooser, TypingInTheListFiltersItAndEnterOpensTheBestFit) {
    ChooserFixture f(QStringLiteral("open"));
    f.chooser->setProperty("types", QVariantList{QVariantMap{{"label", "All files"}, {"extensions", QStringList{}}}});
    f.start(QString());
    QQuickItem* list = f.item("fileList");
    ASSERT_TRUE(waitUntil([&] { return list->hasActiveFocus(); }));
    auto* field = f.chooser->findChild<QQuickItem*>(QStringLiteral("filterField"));
    ASSERT_NE(field, nullptr);
    EXPECT_FALSE(field->isVisible());

    QTest::keyClick(f.window, Qt::Key_B);
    EXPECT_TRUE(field->isVisible());
    EXPECT_TRUE(field->hasActiveFocus());
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("b"));
    EXPECT_EQ(f.listing()->property("count").toInt(), 2) << "sub holds no b";
    QTest::keyClick(f.window, Qt::Key_P);
    EXPECT_EQ(f.listing()->property("count").toInt(), 1);
    EXPECT_EQ(list->property("currentIndex").toInt(), 0);
    QTest::keyClick(f.window, Qt::Key_Return);
    ASSERT_EQ(f.chosen->size(), 1);
    EXPECT_EQ(f.chosen->at(0).at(0).toString(), f.dir.filePath(QStringLiteral("b.png")));
}

// Why: Esc leaves the filter: the whole list comes back and the keys return to it, so that the
// arrows move in it again. Input: "/" in the list, then "a" and Esc. The truth is the field,
// the rows and where the keys are.
TEST(FileChooser, EscapeInTheFilterGivesTheWholeListBack) {
    ChooserFixture f(QStringLiteral("open"));
    f.chooser->setProperty("types", QVariantList{QVariantMap{{"label", "All files"}, {"extensions", QStringList{}}}});
    f.start(QString());
    QQuickItem* list = f.item("fileList");
    ASSERT_TRUE(waitUntil([&] { return list->hasActiveFocus(); }));
    auto* field = f.chooser->findChild<QQuickItem*>(QStringLiteral("filterField"));
    QTest::keyClick(f.window, Qt::Key_Slash);
    EXPECT_TRUE(field->hasActiveFocus());
    EXPECT_EQ(field->property("text").toString(), QString()) << "/ only opens it";
    QTest::keyClick(f.window, Qt::Key_A);
    EXPECT_EQ(f.listing()->property("count").toInt(), 1);
    QTest::keyClick(f.window, Qt::Key_Escape);
    EXPECT_FALSE(field->isVisible());
    EXPECT_TRUE(list->hasActiveFocus());
    EXPECT_EQ(f.listing()->property("count").toInt(), 3);
    EXPECT_EQ(f.chosen->size(), 0);
    EXPECT_EQ(f.cancelled->size(), 0) << "Esc in the filter does not leave the chooser";
}

TEST(FileChooser, FolderModeChoosesTheSelectedFolderOrElseTheOneShown) {
    ChooserFixture f(QStringLiteral("folder"));
    f.start(QString());
    EXPECT_EQ(f.listing()->property("count").toInt(), 1) << "folders only";
    f.click(f.item("acceptButton"));
    ASSERT_EQ(f.chosen->size(), 1);
    EXPECT_EQ(f.chosen->at(0).at(0).toString(), f.dir.path());

    f.start(QString());
    f.pick(QStringLiteral("sub"));
    EXPECT_EQ(f.item("acceptButton")->property("text").toString(), QStringLiteral("Choose “sub”"));
    f.click(f.item("acceptButton"));
    ASSERT_EQ(f.chosen->size(), 2);
    EXPECT_EQ(f.chosen->at(1).at(0).toString(), f.dir.filePath(QStringLiteral("sub")));
}

// Why: the name, the type and the two buttons beside them are read as two rows; a field
// taller than the list below it, or a button off its field's line, looks careless. Input: the
// chooser saving. The truth is the items' places and sizes in the window: each row's field
// and button are as high as each other and start at the same height.
TEST(FileChooser, EachFieldIsAsHighAsItsButtonAndOnItsLine) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    QQuickItem* name = f.item("nameField");
    QQuickItem* type = f.item("typeBox");
    QQuickItem* save = f.item("acceptButton");
    auto* cancel = f.chooser->findChild<QQuickItem*>(QStringLiteral("cancelButton"));
    ASSERT_NE(cancel, nullptr);
    const auto top = [](QQuickItem* item) { return item->mapToScene(QPointF()).y(); };
    EXPECT_EQ(name->height(), type->height());
    EXPECT_EQ(name->height(), save->height());
    EXPECT_EQ(type->height(), cancel->height());
    EXPECT_EQ(top(name), top(save));
    EXPECT_EQ(top(type), top(cancel));
    EXPECT_EQ(save->width(), cancel->width());
}

// Why: the places, the folders and the files are three lists side by side and are to read as
// such: on the same colour, each framed, and with the same gap between them across as down.
// Input: the chooser saving, in the window's palette from Theme. The truth is the frames'
// places, and the colour of an empty spot of each list against Theme.base.
TEST(FileChooser, ThePlacesTheFoldersAndTheFilesAreListsAlike) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    auto* places = f.chooser->findChild<QQuickItem*>(QStringLiteral("placesView"));
    QQuickItem* files = f.item("fileList");
    ASSERT_NE(places, nullptr);
    QQuickItem* placesFrame = places->parentItem();
    QQuickItem* tree = f.chooser->findChild<QQuickItem*>(QStringLiteral("chooserTree"));
    ASSERT_NE(tree, nullptr);
    QQuickItem* treeFrame = tree->parentItem()->parentItem();  // the tree, its loader, the frame
    const QRectF placesBox = placesFrame->mapRectToScene(placesFrame->boundingRect());
    const QRectF treeBox = treeFrame->mapRectToScene(treeFrame->boundingRect());
    const QRectF filesBox = files->mapRectToScene(files->boundingRect());
    const double down = treeBox.top() - placesBox.bottom();
    const double across = filesBox.left() - placesBox.right();
    EXPECT_GT(down, 0);
    EXPECT_EQ(down, across);

    QObject* theme = f.engine.singletonInstance<QObject*>("ImageViewer", "Theme");
    const QColor base = theme->property("base").value<QColor>();
    const QImage shot = f.window->grabWindow();
    const auto near = [&](QPointF at) {
        const QColor c = shot.pixelColor(at.toPoint());
        return std::abs(c.red() - base.red()) + std::abs(c.green() - base.green()) + std::abs(c.blue() - base.blue()) <= 6;
    };
    EXPECT_TRUE(near(placesBox.bottomLeft() + QPointF(placesBox.width() / 2, -6))) << "below the last place";
    EXPECT_TRUE(near(treeBox.bottomLeft() + QPointF(treeBox.width() - 30, -3))) << "the tree, near its foot";
    EXPECT_TRUE(near(filesBox.bottomLeft() + QPointF(filesBox.width() / 2, -10))) << "below the files";
}

// Why: letters typed into the name must not crop, straighten or move between images, as the
// main window's shortcuts would. The chooser is a window of its own, so its keys are its own.
// Input: the save chooser over a browser with one image, and S, X, Space and Backspace typed in
// it. The truth is the name holding what was typed and the main window still browsing.
TEST(Main, TypingASaveNameDoesNotReachTheWindowShortcuts) {
    MainFixture f(1);
    ASSERT_NE(f.window, nullptr);
    QQuickWindow* window = f.window;
    BrowseContext* context = f.context;
    const QTemporaryDir& dir = f.dir;
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 1 && context->selectedCount() == 1; }));

    auto* chooser = window->findChild<QObject*>(QStringLiteral("saveDialog"));
    ASSERT_NE(chooser, nullptr);
    auto* chooserWindow = qobject_cast<QQuickWindow*>(chooser);
    ASSERT_NE(chooserWindow, nullptr);
    QMetaObject::invokeMethod(chooser, "start", Q_ARG(QVariant, dir.path()), Q_ARG(QVariant, QStringLiteral("a.jpg")));
    auto* field = chooser->property("nameField").value<QQuickItem*>();
    ASSERT_TRUE(waitUntil([&] { return field->hasActiveFocus() && chooserWindow->isExposed(); }));
    // S straightens and X crops the selected image, Space and Backspace move between images.
    QTest::keyClick(chooserWindow, Qt::Key_End);
    typeText(chooserWindow, QStringLiteral("sx p"));
    QTest::keyClick(chooserWindow, Qt::Key_Backspace);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("a.jpgsx "));
    EXPECT_FALSE(window->property("viewing").toBool());
    EXPECT_TRUE(chooser->property("opened").toBool());
    QTest::keyClick(chooserWindow, Qt::Key_Escape);
}

TEST(Keys, TheThumbnailsHaveTheKeysAfterEditingThePathOrAName) {
    MainFixture f;
    ASSERT_NE(f.window, nullptr);
    QObject* grid = f.find(QStringLiteral("thumbnailGrid"));
    ASSERT_TRUE(waitUntil([&] { return grid->property("activeFocus").toBool(); })) << "they start with them";
    f.context->setCurrentIndex(0);
    QTest::keyClick(f.window, Qt::Key_Right);
    EXPECT_EQ(f.context->currentIndex(), 1);

    // A hidden path field used to keep the focus, and with it the arrows.
    QTest::keyClick(f.window, Qt::Key_L, Qt::ControlModifier);
    ASSERT_TRUE(f.window->property("typing").toBool());
    QTest::keyClick(f.window, Qt::Key_Escape);
    EXPECT_TRUE(grid->property("activeFocus").toBool());
    QTest::keyClick(f.window, Qt::Key_Right);
    EXPECT_EQ(f.context->currentIndex(), 2);

    QTest::keyClick(f.window, Qt::Key_F2);
    QObject* names = f.action(QStringLiteral("nameDialog"));
    ASSERT_TRUE(waitUntil([&] { return names->property("opened").toBool(); }));
    QTest::keyClick(f.window, Qt::Key_Escape);
    ASSERT_TRUE(waitUntil([&] { return !names->property("visible").toBool(); }));
    QTest::keyClick(f.window, Qt::Key_Left);
    EXPECT_EQ(f.context->currentIndex(), 1);
}

TEST(Keys, ThumbnailsMoveByRowPageAndEndAndChooseWithShift) {
    MainFixture f(6);
    ASSERT_NE(f.window, nullptr);
    f.context->setCurrentIndex(0);
    QTest::keyClick(f.window, Qt::Key_End);
    EXPECT_EQ(f.context->currentIndex(), 5);
    QTest::keyClick(f.window, Qt::Key_Home, Qt::ShiftModifier);
    EXPECT_EQ(f.context->currentIndex(), 0);
    EXPECT_EQ(f.context->selectedPaths().size(), 6);
    QTest::keyClick(f.window, Qt::Key_Down);
    const int columns = f.find(QStringLiteral("thumbnailGrid"))->property("columns").toInt();
    EXPECT_EQ(f.context->currentIndex(), columns < 6 ? columns : 0);
    EXPECT_EQ(f.context->selectedPaths().size(), 1);
}

TEST(Keys, EnterOpensTheImageAndGoesBack) {
    MainFixture f;
    ASSERT_NE(f.window, nullptr);
    f.context->setCurrentIndex(1);
    QTest::keyClick(f.window, Qt::Key_Return);
    ASSERT_TRUE(f.window->property("viewing").toBool());
    QTest::keyClick(f.window, Qt::Key_Right);
    EXPECT_EQ(f.context->currentIndex(), 2) << "the large view moves on";
    QTest::keyClick(f.window, Qt::Key_Return);
    EXPECT_FALSE(f.window->property("viewing").toBool());
    EXPECT_TRUE(waitUntil([&] { return f.find(QStringLiteral("thumbnailGrid"))->property("activeFocus").toBool(); }));
    QTest::keyClick(f.window, Qt::Key_Backspace);
    EXPECT_TRUE(waitUntil([&] { return f.context->directory() == QFileInfo(f.dir.path()).absolutePath(); }))
        << "Backspace in the browser goes to the parent folder";
}

TEST(Keys, WindowShortcutsStillWorkWhileThePanesHaveTheKeys) {
    MainFixture f(1, {QStringLiteral("sub")});
    ASSERT_NE(f.window, nullptr);
    const QString sub = f.dir.filePath(QStringLiteral("sub"));
    f.context->openPath(sub);
    ASSERT_TRUE(waitUntil([&] { return f.context->directory() == sub; }));
    ASSERT_TRUE(f.find(QStringLiteral("thumbnailGrid"))->property("activeFocus").toBool());
    QTest::keyClick(f.window, Qt::Key_Left, Qt::AltModifier);
    EXPECT_TRUE(waitUntil([&] { return f.context->directory() == f.dir.path(); })) << "Alt+Left from the thumbnails";
    qobject_cast<QQuickItem*>(f.find(QStringLiteral("folderTree")))->forceActiveFocus();
    QTest::keyClick(f.window, Qt::Key_Right, Qt::AltModifier);
    EXPECT_TRUE(waitUntil([&] { return f.context->directory() == sub; })) << "Alt+Right from the tree";
}

TEST(Keys, TheTreeMovesBetweenFoldersAsAFileManagerDoes) {
    MainFixture f(1, {QStringLiteral("one/deep"), QStringLiteral("two")});
    ASSERT_NE(f.window, nullptr);
    const QString root = f.dir.path();
    auto* tree = qobject_cast<QQuickItem*>(f.find(QStringLiteral("folderTree")));
    ASSERT_NE(tree, nullptr);
    tree->forceActiveFocus();
    const auto key = [&](Qt::Key k, const QString& expected) {
        QTest::keyClick(f.window, k);
        return waitUntil([&] { return f.context->directory() == expected; });
    };
    auto* model = qobject_cast<iv::DirectoryTreeModel*>(tree->property("model").value<QObject*>());
    const auto rowOf = [&](const QString& path) {
        int row = -1;
        QMetaObject::invokeMethod(tree, "rowAtIndex", Q_RETURN_ARG(int, row), Q_ARG(QModelIndex, model->indexForPath(path)));
        return row;
    };
    const auto isExpanded = [&](const QString& path) {
        const int row = rowOf(path);
        bool open = false;
        QMetaObject::invokeMethod(tree, "isExpanded", Q_RETURN_ARG(bool, open), Q_ARG(int, row));
        return open;
    };
    const QString one = root + QStringLiteral("/one");
    // The model lists folders in the background.
    ASSERT_TRUE(waitUntil([&] { return isExpanded(root) && rowOf(one) > rowOf(root) && rowOf(root + QStringLiteral("/two")) > rowOf(one); }));
    EXPECT_TRUE(key(Qt::Key_Down, one));
    EXPECT_FALSE(isExpanded(one)) << "moving does not unfold";
    EXPECT_TRUE(key(Qt::Key_Down, root + QStringLiteral("/two"))) << "so the next is beside it, not in it";
    EXPECT_TRUE(key(Qt::Key_Up, one));
    QTest::keyClick(f.window, Qt::Key_Right);
    EXPECT_TRUE(waitUntil([&] { return isExpanded(one); })) << "Right unfolds";
    ASSERT_TRUE(waitUntil([&] { return rowOf(one + QStringLiteral("/deep")) > rowOf(one); })) << "its subfolders are listed";
    EXPECT_TRUE(key(Qt::Key_Right, one + QStringLiteral("/deep"))) << "then goes in";
    EXPECT_TRUE(key(Qt::Key_Left, one)) << "Left on a closed folder goes to its parent";
    QTest::keyClick(f.window, Qt::Key_Left);
    EXPECT_TRUE(waitUntil([&] { return !isExpanded(one); })) << "and folds an open one";
    EXPECT_EQ(f.context->directory(), one);
    EXPECT_TRUE(key(Qt::Key_Backspace, root));
    EXPECT_TRUE(tree->hasActiveFocus());
    QTest::keyClick(f.window, Qt::Key_Return);
    EXPECT_TRUE(f.find(QStringLiteral("thumbnailGrid"))->property("activeFocus").toBool()) << "Enter goes to the files";
}

TEST(Keys, RenameAndDeleteInTheTreeAreForTheFolder) {
    MainFixture f(2, {QStringLiteral("sub")});
    ASSERT_NE(f.window, nullptr);
    const QString folder = f.dir.filePath(QStringLiteral("sub"));
    writeJpeg(folder + QStringLiteral("/x.jpg"));
    f.context->openPath(folder);
    ASSERT_TRUE(waitUntil([&] { return f.context->fileCount() == 1; }));
    f.context->setCurrentIndex(0);
    auto* tree = qobject_cast<QQuickItem*>(f.find(QStringLiteral("folderTree")));
    tree->forceActiveFocus();

    QTest::keyClick(f.window, Qt::Key_F2);
    QObject* names = f.action(QStringLiteral("nameDialog"));
    ASSERT_TRUE(waitUntil([&] { return names->property("opened").toBool(); }));
    EXPECT_EQ(names->property("title").toString(), QStringLiteral("Rename Folder"));
    EXPECT_EQ(names->property("initialText").toString(), QStringLiteral("sub"));
    QTest::keyClick(f.window, Qt::Key_Escape);
    ASSERT_TRUE(waitUntil([&] { return !names->property("visible").toBool(); }));
    EXPECT_TRUE(tree->hasActiveFocus()) << "the keys come back to the tree";

    QTest::keyClick(f.window, Qt::Key_Delete);
    QObject* trash = f.action(QStringLiteral("trashFolderDialog"));
    ASSERT_NE(trash, nullptr);
    EXPECT_TRUE(waitUntil([&] { return trash->property("opened").toBool(); })) << "it asks about the folder";
    QTest::keyClick(f.window, Qt::Key_Escape);
    ASSERT_TRUE(waitUntil([&] { return !trash->property("visible").toBool(); }));
    QTest::keyClick(f.window, Qt::Key_Delete, Qt::ShiftModifier);
    waitUntil([] { return false; }, 200);
    EXPECT_TRUE(QFile::exists(folder + QStringLiteral("/x.jpg"))) << "Shift+Del does not delete the files of the folder";
}

// Why: retained selections in two panes must not make Delete's target ambiguous.
// Input: a temporary folder containing one file, focus moved tree -> path -> toolbar
// -> thumbnails. Frames must follow the keys without resizing; Delete asks
// about the folder, edits text, does nothing, then trashes only the selected file.
TEST(Keys, DeleteFollowsThePaneFrameAndTextEditingNeverTrashesFiles) {
    MainFixture f(1, QStringList{QStringLiteral("sub")});
    ASSERT_NE(f.window, nullptr);
    const QString folder = f.dir.filePath(QStringLiteral("sub"));
    const QString file = folder + QStringLiteral("/image.jpg");
    writeJpeg(file);
    ASSERT_TRUE(QFile::exists(file));
    f.context->openPath(folder);
    ASSERT_TRUE(waitUntil([&] { return f.context->fileCount() == 1; }))
        << "directory=" << f.context->directory().toStdString() << " files=" << f.context->fileCount()
        << " expected=" << folder.toStdString() << " disk=" << QDir(folder).entryList(QDir::Files).join(",").toStdString()
        << " exists=" << QFile::exists(file);
    f.context->setCurrentIndex(0);
    auto* tree = qobject_cast<QQuickItem*>(f.find("folderTree"));
    auto* grid = qobject_cast<QQuickItem*>(f.find("thumbnailGrid"));
    auto* foldersFrame = qobject_cast<QQuickItem*>(f.find("foldersFrame"));
    auto* filesFrame = qobject_cast<QQuickItem*>(f.find("thumbnailsFrame"));
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(grid, nullptr);
    ASSERT_NE(foldersFrame, nullptr);
    ASSERT_NE(filesFrame, nullptr);
    const QSizeF size = filesFrame->size();
    tree->forceActiveFocus();
    EXPECT_TRUE(foldersFrame->property("focused").toBool());
    EXPECT_FALSE(filesFrame->property("focused").toBool());
    QTest::keyClick(f.window, Qt::Key_Delete);
    auto* question = f.action(QStringLiteral("trashFolderDialog"));
    ASSERT_TRUE(waitUntil([&] { return question->property("opened").toBool(); }));
    QTest::keyClick(f.window, Qt::Key_Escape);
    ASSERT_TRUE(waitUntil([&] { return !question->property("visible").toBool(); }));
    EXPECT_TRUE(QFile::exists(file));

    QTest::keyClick(f.window, Qt::Key_L, Qt::ControlModifier);
    ASSERT_TRUE(f.window->property("typing").toBool());
    EXPECT_FALSE(foldersFrame->property("focused").toBool());
    EXPECT_FALSE(filesFrame->property("focused").toBool());
    QTest::keyClick(f.window, Qt::Key_Delete);
    EXPECT_EQ(f.find("pathBar")->property("editText").toString(), QString());
    EXPECT_TRUE(QFile::exists(file));
    QTest::keyClick(f.window, Qt::Key_Escape);

    auto* toolbarButton = qobject_cast<QQuickItem*>(f.find("modeButton"));
    ASSERT_NE(toolbarButton, nullptr);
    toolbarButton->forceActiveFocus();
    EXPECT_FALSE(filesFrame->property("focused").toBool());
    QTest::keyClick(f.window, Qt::Key_Delete);
    QTest::keyClick(f.window, Qt::Key_Delete, Qt::ShiftModifier);
    EXPECT_TRUE(QFile::exists(file));
    auto* permanent = f.find("permanentDeleteDialog");
    ASSERT_NE(permanent, nullptr);
    EXPECT_FALSE(permanent->property("opened").toBool());

    grid->forceActiveFocus();
    EXPECT_TRUE(filesFrame->property("focused").toBool());
    EXPECT_FALSE(foldersFrame->property("focused").toBool());
    EXPECT_EQ(filesFrame->size(), size) << "focus changes paint, not layout";
    QTest::keyClick(f.window, Qt::Key_Delete);
    ASSERT_TRUE(waitUntil([&] { return !QFile::exists(file); }));
    EXPECT_TRUE(QDir(folder).exists()) << "the folder was not the target";
    EXPECT_EQ(f.warnings, QStringList());
}

// Why: Places, Directories and both file-list modes share one focus indicator. Moving
// the keys must light exactly that frame, keep other selections inactive, and leave
// geometry unchanged. Places is navigation-only; Delete must not remove a file, and in the
// tree it asks about the folder, as in the browser.
TEST(FileChooser, PaneFramesFollowKeyboardFocusInPlacesTreeBriefAndDetails) {
    ChooserFixture f(QStringLiteral("save"));
    f.start(QStringLiteral("a.jpg"));
    auto* places = f.chooser->findChild<QQuickItem*>("placesView");
    auto* tree = f.chooser->findChild<QQuickItem*>("chooserTree");
    auto* files = f.item("fileList");
    auto* placesFrame = f.chooser->findChild<QQuickItem*>("placesFrame");
    auto* treeFrame = f.chooser->findChild<QQuickItem*>("chooserTreeFrame");
    auto* fileFrame = files ? files->findChild<QQuickItem*>("fileListFrame") : nullptr;
    ASSERT_NE(places, nullptr);
    ASSERT_NE(tree, nullptr);
    ASSERT_NE(files, nullptr);
    ASSERT_NE(placesFrame, nullptr);
    ASSERT_NE(treeFrame, nullptr);
    ASSERT_NE(fileFrame, nullptr);
    QObject* theme = f.engine.singletonInstance<QObject*>("ImageViewer", "Theme");
    const QColor accent = theme->property("paneFocusFrame").value<QColor>();
    const QColor quiet = theme->property("paneFrame").value<QColor>();
    const QList<QQuickItem*> frames{placesFrame, treeFrame, fileFrame};
    const QList<QQuickItem*> contents{places, tree, files};
    for (int index = 0; index < contents.size(); ++index) {
        SCOPED_TRACE(index);
        contents[index]->forceActiveFocus();
        QCoreApplication::processEvents();
        const QImage picture = grabIfShown(*f.window);
        ASSERT_FALSE(picture.isNull());
        for (int other = 0; other < frames.size(); ++other) {
            EXPECT_EQ(frames[other]->property("focused").toBool(), index == other);
            const auto* border = frames[other]->property("border").value<QObject*>();
            ASSERT_NE(border, nullptr);
            EXPECT_EQ(border->property("color").value<QColor>(), index == other ? accent : quiet);
            EXPECT_GT(frames[other]->property("radius").toDouble(), 0);
            if (index == other) {
                const QPoint edge = frames[other]->mapToScene(QPointF(0, frames[other]->height() / 2)).toPoint();
                const QColor painted = picture.pixelColor(edge);
                EXPECT_GT(painted.green(), painted.red()) << "the frame is visibly accented";
                EXPECT_LT(painted.green(), theme->property("accent").value<QColor>().green() - 30)
                    << "a large frame must be quieter than the full accent";
            }
        }
        QTest::keyClick(f.window, Qt::Key_Delete);
        EXPECT_TRUE(QFile::exists(f.dir.filePath(QStringLiteral("a.jpg"))));
        if (contents[index] == tree) {
            // In the tree it is for the folder, as in the browser: asked first, and No keeps it.
            QObject* question = f.chooser->findChild<QObject*>(QStringLiteral("trashFolderDialog"));
            ASSERT_TRUE(waitUntil([&] { return question->property("opened").toBool(); }));
            QMetaObject::invokeMethod(question, "reject");
            ASSERT_TRUE(waitUntil([&] { return !question->property("visible").toBool(); }));
            EXPECT_TRUE(QDir(f.dir.path()).exists());
        }
    }
    const QSizeF before = fileFrame->size();
    files->setProperty("details", true);
    EXPECT_TRUE(fileFrame->property("focused").toBool());
    EXPECT_EQ(fileFrame->size(), before);
    f.item("nameField")->forceActiveFocus();
    EXPECT_FALSE(fileFrame->property("focused").toBool());
}

// Why: Show Hidden Folders is one switch, under Ctrl+H, kept between sessions, which every
// listing follows. Input: the window on a folder with a hidden subfolder. The truth is the
// menu action, the saved setting and the browser's folder count.
TEST(Main, CtrlHShowsHiddenFoldersAndKeepsIt) {
    MainFixture f([](const QTemporaryDir& dir) {
        writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
        ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral(".hidden")));
        hideOnWindows(dir.filePath(QStringLiteral(".hidden")));
    }, QString());
    ASSERT_NE(f.window, nullptr);
    QObject* settings = f.engine.singletonInstance<QObject*>("ImageViewer", "Preferences");
    ASSERT_NE(settings, nullptr);
    ASSERT_FALSE(settings->property("showHiddenFolders").toBool());
    ASSERT_TRUE(waitUntil([&] { return f.context->summary().startsWith(QStringLiteral("0 directories")); }))
        << f.context->summary().toStdString();

    QTest::keyClick(f.window, Qt::Key_H, Qt::ControlModifier);
    EXPECT_TRUE(settings->property("showHiddenFolders").toBool());
    EXPECT_TRUE(f.context->showHiddenFolders());
    EXPECT_TRUE(waitUntil([&] { return f.context->summary().startsWith(QStringLiteral("1 directory")); }))
        << f.context->summary().toStdString();

    QTest::keyClick(f.window, Qt::Key_H, Qt::ControlModifier);
    EXPECT_FALSE(settings->property("showHiddenFolders").toBool());
    EXPECT_FALSE(f.context->showHiddenFolders());
}

// Why: Auto Contrast is one toggle that stays as it is set: turned on, the next images have it
// too, and turned off, they do not, with no second button to keep it. The button is lit
// while the image shown has it. Input: two dim 16-bit gray images, which load without it,
// stepped between. The truth is the view's auto contrast, the button's state and the setting
// that is kept between sessions.
TEST(Main, AutoContrastStaysAsItIsSetForTheNextImages) {
    MainFixture f([](const QTemporaryDir& dir) {
        TiffImage gray;
        gray.width = 100;
        gray.height = 100;
        gray.bits = 16;
        gray.samples = 1;
        gray.pixel = [](int x, int) { return x < 50 ? Rgb{2, 2, 2} : Rgb{4, 4, 4}; };
        iv::test::writeTiff(QFileInfo(dir.filePath(QStringLiteral("a.tif"))).filesystemFilePath(), {gray});
        iv::test::writeTiff(QFileInfo(dir.filePath(QStringLiteral("b.tif"))).filesystemFilePath(), {gray});
    }, QStringLiteral("a.tif"));
    ASSERT_NE(f.window, nullptr);
    auto* view = f.window->property("activeView").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    ASSERT_TRUE(waitUntil([&] { return view->contrastAdjustable(); }));
    QObject* button = f.find(QStringLiteral("autoContrastButton"));
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(f.find(QStringLiteral("keepAutoContrastButton")), nullptr) << "no second button";
    ASSERT_FALSE(view->autoContrast());
    EXPECT_FALSE(button->property("checked").toBool());

    QMetaObject::invokeMethod(button, "click");
    EXPECT_TRUE(view->autoContrast());
    EXPECT_TRUE(button->property("checked").toBool());
    EXPECT_TRUE(view->keepAutoContrast());
    QTest::keyClick(f.window, Qt::Key_Right);
    ASSERT_TRUE(waitUntil([&] { return view->source().endsWith(QStringLiteral("b.tif")) && view->contrastAdjustable(); }));
    EXPECT_TRUE(view->autoContrast()) << "on for the next image";
    EXPECT_TRUE(button->property("checked").toBool());

    QMetaObject::invokeMethod(button, "click");
    EXPECT_FALSE(view->autoContrast());
    EXPECT_FALSE(view->keepAutoContrast());
    QTest::keyClick(f.window, Qt::Key_Left);
    ASSERT_TRUE(waitUntil([&] { return view->source().endsWith(QStringLiteral("a.tif")) && view->contrastAdjustable(); }));
    EXPECT_FALSE(view->autoContrast()) << "off for the next image";
    EXPECT_FALSE(button->property("checked").toBool());
}

TEST(Main, AboutListsWhatItIsBuiltWithAndTheLicenceTextsWhenInstalled) {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.2.3"));
    {
        MainFixture f(0);
        ASSERT_NE(f.window, nullptr);
        QObject* dialog = f.find(QStringLiteral("aboutDialog"));
        ASSERT_NE(dialog, nullptr);
        QMetaObject::invokeMethod(dialog, "open");
        ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
        EXPECT_EQ(f.find(QStringLiteral("aboutVersion"))->property("text").toString(), QStringLiteral("Version 1.2.3"));
        // Without the installer's folder, as in a build folder, there is nothing to open.
        EXPECT_FALSE(f.find(QStringLiteral("licenceTextsButton"))->property("visible").toBool());

        QVariant text;
        QMetaObject::invokeMethod(dialog, "plainText", Q_RETURN_ARG(QVariant, text));
        const QString copied = text.toString();
        EXPECT_TRUE(copied.startsWith(QStringLiteral("Image Viewer 1.2.3\n"))) << copied.toStdString();
        EXPECT_TRUE(copied.contains(QStringLiteral("\nQt ") + QString::fromLatin1(qVersion()) + QStringLiteral(" (LGPL-3.0)")));
        EXPECT_TRUE(copied.contains(QStringLiteral("\nLittle CMS ")));
        EXPECT_TRUE(copied.contains(QStringLiteral("\nInter typeface 4.1 (SIL OFL 1.1)")));
        EXPECT_EQ(f.warnings, QStringList());
    }
    QTemporaryDir licences;
    iv::AppInfo::setLicencesFolderForTesting(licences.path());
    {
        MainFixture f(0);
        ASSERT_NE(f.window, nullptr);
        QObject* dialog = f.find(QStringLiteral("aboutDialog"));
        QMetaObject::invokeMethod(dialog, "open");
        ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
        EXPECT_TRUE(f.find(QStringLiteral("licenceTextsButton"))->property("visible").toBool());
    }
    iv::AppInfo::setLicencesFolderForTesting({});
    QCoreApplication::setApplicationVersion({});
}

// Why: the Windows installer puts the licence texts beside the program, and the AppImage in the
// usual Linux place, share/imageviewer/licenses beside bin. Input: a program folder with neither,
// with the Linux one, and with both. The truth: none found, then the Linux one, then the one
// beside the program first.
// Why: on Linux a window shows the program's icon only if the program gives one, unless the
// desktop finds an installed desktop entry; the AppImage and the Windows build had theirs, the
// running program did not. Input: the icon drawn from the picture the installers use. Truth:
// every size window managers ask for, each with the picture in it, not blank.
TEST(AppInfo, ProgramIconComesAtTheSizesWindowManagersAskFor) {
    const QIcon icon = iv::AppInfo::programIcon();
    ASSERT_FALSE(icon.isNull()) << "the picture is in the resources";
    const QList<QSize> sizes = icon.availableSizes();
    for (const int size : {16, 32, 48, 256}) {
        EXPECT_TRUE(sizes.contains(QSize(size, size))) << size;
    }
    const QImage image = icon.pixmap(QSize(64, 64)).toImage();
    int drawn = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            drawn += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
        }
    }
    EXPECT_GT(drawn, 64 * 64 / 20) << "the picture is drawn, not left transparent";
}

TEST(AboutDialog, LicenceTextsAreFoundBesideTheProgramOrInShare) {
    QTemporaryDir root;
    const QString bin = root.filePath(QStringLiteral("usr/bin"));
    ASSERT_TRUE(QDir().mkpath(bin));
    EXPECT_EQ(iv::AppInfo::licencesFolderFor(bin), QString());
    const QString shared = root.filePath(QStringLiteral("usr/share/imageviewer/licenses"));
    ASSERT_TRUE(QDir().mkpath(shared));
    EXPECT_EQ(iv::AppInfo::licencesFolderFor(bin), QDir::cleanPath(shared));
    ASSERT_TRUE(QDir().mkpath(bin + QStringLiteral("/licenses")));
    EXPECT_EQ(iv::AppInfo::licencesFolderFor(bin), QDir::cleanPath(bin + QStringLiteral("/licenses")));
}

// Why: Enter and Esc are one rule for every tool, kept by the page, not a shortcut per tool.
// Input: the window showing a picture, each tool started in turn. The truth is the window's
// own state: Esc leaves the tool and stays on the image, Enter applies an edit and stays too,
// and Enter in a field of the tool's panel is the field's, so the tool goes on.
TEST(Main, EnterAppliesAndEscLeavesWhicheverToolIsAtWork) {
    MainFixture f([](const QTemporaryDir& dir) { writeTiltedJpeg(dir); }, QStringLiteral("tilted.jpg"));
    ASSERT_NE(f.window, nullptr);
    auto* image = f.window->property("activeView").value<ImageView*>();
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 400; }));
    auto* adjust = f.window->findChild<QQuickItem*>(QStringLiteral("adjustPanel"));
    auto* straighten = f.window->findChild<QQuickItem*>(QStringLiteral("straightenPanel"));
    ASSERT_NE(adjust, nullptr);
    ASSERT_NE(straighten, nullptr);
    QQuickItem* page = adjust->parentItem();

    for (const char* start : {"startCrop", "startStraighten", "startAdjust", "startAnnotate", "startProbe"}) {
        QMetaObject::invokeMethod(page, start);
        ASSERT_TRUE(f.window->property("toolActive").toBool()) << start;
        QTest::keyClick(f.window, Qt::Key_Escape);
        EXPECT_FALSE(f.window->property("toolActive").toBool()) << start;
        EXPECT_TRUE(f.window->property("viewing").toBool()) << start;
    }
    EXPECT_FALSE(image->modified());

    // A field with the keys keeps Enter.
    QMetaObject::invokeMethod(page, "startStraighten");
    QQuickItem* field = nullptr;
    // The panel slides in, and its fields show only once it is in view.
    waitUntil([&] {
        for (QQuickItem* item : straighten->findChildren<QQuickItem*>()) {
            if (item->inherits("QQuickTextField") && item->isVisible()) {
                field = item;
                return true;
            }
        }
        return false;
    });
    ASSERT_NE(field, nullptr);
    field->forceActiveFocus();
    ASSERT_TRUE(straighten->property("editing").toBool());
    EXPECT_FALSE(page->property("enterApplies").toBool());
    QTest::keyClick(f.window, Qt::Key_Return);
    EXPECT_TRUE(f.window->property("straightening").toBool());
    QTest::keyClick(f.window, Qt::Key_Escape);
    EXPECT_FALSE(f.window->property("toolActive").toBool());

    // Enter applies, and does not go on to leave the image.
    QMetaObject::invokeMethod(page, "startAdjust");
    adjust->setProperty("midtones", 1.5);
    QTest::keyClick(f.window, Qt::Key_Return);
    EXPECT_FALSE(f.window->property("toolActive").toBool());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->modified(); }));
    EXPECT_TRUE(f.window->property("viewing").toBool());
}

// Why: the colours are worked out from a few choices, and changing one must reach everything
// bound to Theme at once, Fusion's palette included, with no page doing anything. Input: the
// main window, its hue turned from willow to a blue. The truth is Theme's window colour, the
// window's own palette, and that the willow comes back with the hue.
// Why: probing is a tool at work, as cropping is, not an option to tick, so the Edit menu shows
// no box for it; the toolbar's Probe is down while it is at work. Input: the main window. The
// truth is the action behind the toolbar's Probe: not checkable.
TEST(Main, TheProbeIsAToolAndNotAnOptionToTick) {
    MainFixture f(1);
    ASSERT_NE(f.window, nullptr);
    QObject* button = f.find(QStringLiteral("probeButton"));
    ASSERT_NE(button, nullptr);
    QObject* action = button->property("action").value<QObject*>();
    ASSERT_NE(action, nullptr);
    EXPECT_FALSE(action->property("checkable").toBool());
}

// Why: a dialog in a window of its own does not inherit the main window's palette, and a role
// it lacks comes from the system theme (white menus on a light desktop). The roles are the
// application's, so any window has them. Input: a bare window with a button, apart from the
// main window, and a change of hue. The truth is the window's palette against Theme, at once.
TEST(Main, AWindowOfItsOwnHasTheThemesColours) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        Window {
            width: 200; height: 100; visible: true
            property color expected: Theme.window
            property color expectedHighlight: Theme.highlight
            property alias button: button
            Button { id: button; text: "Save" }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    ASSERT_NE(root, nullptr);
    auto* button = root->property("button").value<QQuickItem*>();
    const auto paletteColour = [&](const char* role) {
        return button->property("palette").value<QObject*>()->property(role).value<QColor>();
    };
    EXPECT_EQ(paletteColour("window"), root->property("expected").value<QColor>());
    EXPECT_EQ(paletteColour("highlight"), root->property("expectedHighlight").value<QColor>());

    auto* palette = engine.singletonInstance<iv::ThemePalette*>("ImageViewer", "ThemePalette");
    const double willow = palette->hue();
    palette->setHue(250);
    QCoreApplication::processEvents();
    EXPECT_EQ(paletteColour("window"), root->property("expected").value<QColor>()) << "followed at once";
    EXPECT_EQ(paletteColour("highlight"), root->property("expectedHighlight").value<QColor>()) << "accent followed at once";
    EXPECT_GT(paletteColour("highlight").blue(), paletteColour("highlight").green());
    EXPECT_EQ(paletteColour("window").blue(), paletteColour("window").green()) << "default surface stays neutral";
    palette->setHue(willow);
}

// Why: hue changes must reach accents immediately without tinting neutral default surfaces.
// An explicitly chosen surface tint must still propagate to the window and reset cleanly.
TEST(Main, ChangingTheHueRecoloursAccentsAndOnlyTintsSurfacesWhenChosen) {
    MainFixture f(0);
    ASSERT_NE(f.window, nullptr);
    auto* palette = f.engine.singletonInstance<iv::ThemePalette*>("ImageViewer", "ThemePalette");
    QObject* theme = f.engine.singletonInstance<QObject*>("ImageViewer", "Theme");
    ASSERT_NE(palette, nullptr);
    ASSERT_NE(theme, nullptr);
    const double willow = palette->hue();
    const QColor before = theme->property("window").value<QColor>();
    const QColor accentBefore = theme->property("accent").value<QColor>();
    EXPECT_EQ(before.green(), before.blue()) << "default surfaces are neutral";
    EXPECT_GT(accentBefore.green(), accentBefore.blue()) << "willow accent is green";
    EXPECT_EQ(f.window->color(), before) << "the window is painted in it";

    palette->setHue(250);
    EXPECT_EQ(theme->property("window").value<QColor>(), before);
    EXPECT_NE(theme->property("accent").value<QColor>(), accentBefore);
    const double originalTint = palette->surfaceChroma();
    palette->setSurfaceChroma(0.014);
    const QColor blue = theme->property("window").value<QColor>();
    EXPECT_GT(blue.blue(), blue.green());
    EXPECT_EQ(f.window->color(), blue) << "followed by the binding alone";

    palette->setHue(willow);
    palette->setSurfaceChroma(originalTint);
    EXPECT_EQ(theme->property("window").value<QColor>(), before);
    EXPECT_EQ(f.warnings, QStringList());
}

// Why: the window's depth decides the dither step, and 10 bits are asked for by name only.
// Input: surface formats as drivers report them and values of IV_OUTPUT_BITS. The truth is the
// least of the three colour channels, and 8 where a format gives no sizes. About names the API,
// to show whether OpenGL was taken.
TEST(DisplayOutput, ReadsTheLeastColourChannelAndAsksOnlyForTen) {
    QSurfaceFormat format;
    EXPECT_EQ(iv::outputBits(format), 8) << "no sizes given";
    format.setRedBufferSize(10);
    format.setGreenBufferSize(10);
    format.setBlueBufferSize(10);
    EXPECT_EQ(iv::outputBits(format), 10);
    format.setBlueBufferSize(8);
    EXPECT_EQ(iv::outputBits(format), 8);

    EXPECT_EQ(iv::requestedOutputBits("10"), 10);
    EXPECT_EQ(iv::requestedOutputBits(" 10\n"), 10);
    EXPECT_EQ(iv::requestedOutputBits(""), 8);
    EXPECT_EQ(iv::requestedOutputBits("12"), 8) << "nothing Qt Quick could give";

    EXPECT_EQ(iv::graphicsApiName(QSGRendererInterface::OpenGL), QStringLiteral("OpenGL"));
    EXPECT_EQ(iv::graphicsApiName(QSGRendererInterface::Direct3D11), QStringLiteral("Direct3D 11"));
}

// Why: the last pass dithers by one step of the window's output, and IV_NO_DITHER=1 turns that
// off to show whether 10 bits arrive. Input: the window offscreen, which has 8 bits, with and
// without the variable. The truth is ViewSettings.ditherStep and the line in About.
TEST(Main, DithersByOneStepOfTheOutputUnlessTurnedOff) {
    const auto check = [](double step, const QString& about) {
        MainFixture f(0);
        ASSERT_NE(f.window, nullptr);
        QObject* settings = f.engine.singletonInstance<QObject*>("ImageViewer", "ViewSettings");
        ASSERT_NE(settings, nullptr);
        EXPECT_EQ(settings->property("outputBits").toInt(), 8);
        EXPECT_DOUBLE_EQ(settings->property("ditherStep").toDouble(), step);
        QObject* dialog = f.find(QStringLiteral("aboutDialog"));
        ASSERT_NE(dialog, nullptr);
        QMetaObject::invokeMethod(dialog, "open");
        ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
        const QString shown = f.find(QStringLiteral("aboutOutput"))->property("text").toString();
        EXPECT_TRUE(shown.startsWith(QStringLiteral("Display output: ")) && shown.endsWith(about)) << shown.toStdString();
        EXPECT_EQ(f.warnings, QStringList());
    };
    check(1.0 / 255, QStringLiteral("8 bits per channel, dithered"));
    qputenv("IV_NO_DITHER", "1");
    check(0, QStringLiteral("8 bits per channel, not dithered"));
    qunsetenv("IV_NO_DITHER");
}

// Why: the open dialog's list of image types used to be written out in QML and missed formats
// the program had learnt to read. Input: the main window's open dialog. The truth: its image
// types are those core reads, among them the read-only BMP, GIF, TGA and Netpbm.
TEST(Main, TheOpenDialogOffersEveryFormatItReads) {
    MainFixture f;
    ASSERT_NE(f.window, nullptr);
    auto* dialog = f.find(QStringLiteral("openDialog"));
    ASSERT_NE(dialog, nullptr);
    const QVariantList types = dialog->property("types").toList();
    ASSERT_FALSE(types.isEmpty());
    const QStringList extensions = types.first().toMap().value(QStringLiteral("extensions")).toStringList();
    EXPECT_EQ(extensions, BrowseContext::imageExtensions());
    for (const char* wanted : {"jpg", "bmp", "gif", "tga", "pgm", "pfm"}) {
        EXPECT_TRUE(extensions.contains(QLatin1String(wanted))) << wanted;
    }
}

// Why: a dialog in a window of its own is a transient child of the main window, and Qt counts
// the main window as active while it has the keys, so the main window's shortcuts used to fire
// too: Escape in the save dialog left the image and asked about unsaved changes, below it.
// Input: the large view showing an image, the save dialog open over it. The truth: Escape, Enter
// and a menu key (F11) in the dialog reach the dialog only, the main window still shows the
// image, and once the dialog is closed Escape works in the main window again.
TEST(Keys, ADialogWindowKeepsItsKeysFromTheMainWindow) {
    MainFixture f;
    ASSERT_NE(f.window, nullptr);
    f.context->setCurrentIndex(1);
    QTest::keyClick(f.window, Qt::Key_Return);
    ASSERT_TRUE(f.window->property("viewing").toBool());
    auto* dialog = qobject_cast<QQuickWindow*>(f.find(QStringLiteral("saveDialog")));
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(QTest::qWaitForWindowActive(dialog));
    QTest::keyClick(dialog, Qt::Key_Return);
    EXPECT_TRUE(f.window->property("viewing").toBool()) << "Enter in the dialog left the image";
    QTest::keyClick(dialog, Qt::Key_F11);
    EXPECT_FALSE(f.window->windowStates() & Qt::WindowFullScreen) << "a menu key in the dialog acted on the main window";
    QTest::keyClick(dialog, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return !dialog->isVisible(); })) << "Escape closes the dialog";
    EXPECT_TRUE(f.window->property("viewing").toBool()) << "Escape in the dialog left the image";
    // The system gives the keys back to the main window; the offscreen platform does not.
    auto* blocker = dialog->findChild<QObject*>(QStringLiteral("keyBlocker"));
    ASSERT_NE(blocker, nullptr);
    EXPECT_FALSE(blocker->property("visible").toBool()) << "the blocker goes with the dialog";
    f.window->requestActivate();
    ASSERT_TRUE(QTest::qWaitForWindowActive(f.window));
    QTest::keyClick(f.window, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return !f.window->property("viewing").toBool(); }))
        << "once the dialog is closed, the main window has its keys back";
}

// Why: while adjusting, "before" is the picture as it was before the colour preview, and a
// second view holds it. Apply closed the panel but left the split and that picture, which was
// then neither the original nor the result; Cancel already ended it.
// Input: a picture in Adjust, with a change and before and after shown, then Apply. The truth:
// the comparison ends, the second view lets go of its picture, and the change is applied.
TEST(Compare, ApplyingAnAdjustmentEndsBeforeAndAfter) {
    QTemporaryDir dir;
    const QString a = writeGradient(dir, QStringLiteral("a.jpg"), 400, 300, false);
    ComparePage f(a);
    ASSERT_TRUE(settled(f.image));
    QMetaObject::invokeMethod(f.page, "startAdjust");
    ASSERT_EQ(f.page->property("tool").toString(), QStringLiteral("adjust"));
    f.page->findChild<QObject*>(QStringLiteral("adjustPanel"))->setProperty("midtones", 1.5);
    QMetaObject::invokeMethod(f.page, "toggleBeforeAfter");
    ASSERT_EQ(f.comparing(), QStringLiteral("edit"));
    ASSERT_TRUE(waitUntil([&] { return f.active(); }));

    QMetaObject::invokeMethod(f.page, "applyTool");
    EXPECT_EQ(f.page->property("tool").toString(), QString());
    EXPECT_EQ(f.comparing(), QString()) << "the split went with the panel";
    EXPECT_EQ(f.other->status(), iv::ImageView::Status::Null) << "and the picture it held";
    EXPECT_TRUE(waitUntil([&] { return f.image->modified(); })) << "the change is applied";
}


// Why: the folder tree's menu offers the folder to the system's file manager (Explorer,
// Dolphin), for what the program does not do itself.
// Input: a folder "sub" beside the pictures, its menu opened as a right click opens it.
// Truth: the item hands over that folder, not the one being browsed, as a local file URL.
TEST(FolderTree, MenuOpensTheFolderInTheFileManager) {
    MainFixture f(1, QStringList{QStringLiteral("sub")});
    ASSERT_NE(f.window, nullptr);
    UrlCatcher catcher(QStringLiteral("file"));
    const QString folder = f.dir.filePath(QStringLiteral("sub"));
    QObject* menu = f.action(QStringLiteral("folderMenu"));
    ASSERT_NE(menu, nullptr);
    menu->setProperty("path", folder);
    QMetaObject::invokeMethod(menu, "popup");
    ASSERT_TRUE(waitUntil([&] { return menu->property("opened").toBool(); }));
    QObject* item = f.action(QStringLiteral("openFolderInFileManagerItem"));
    ASSERT_NE(item, nullptr);
    EXPECT_TRUE(item->property("enabled").toBool());
    QMetaObject::invokeMethod(item, "click");
    ASSERT_EQ(catcher.opened.size(), 1);
    EXPECT_EQ(catcher.opened.first(), QUrl::fromLocalFile(folder));
    EXPECT_TRUE(waitUntil([&] { return !menu->property("visible").toBool(); })) << "the menu closes";
}

#include "ui_window_tests.moc"
