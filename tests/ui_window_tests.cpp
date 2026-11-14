#include "ui_test_support.h"
#include "ui/app_info.h"
#include "ui/theme_palette.h"

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

TEST(Main, LoadsWithItsFileActionsWithoutWarnings) {
    MainFixture f(1);
    ASSERT_NE(f.window, nullptr);
    waitUntil([] { return false; }, 200);
    EXPECT_EQ(f.warnings, QStringList());
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

TEST(Main, TypingASaveNameDoesNotReachTheWindowShortcuts) {
    MainFixture f(1);
    ASSERT_NE(f.window, nullptr);
    QQuickWindow* window = f.window;
    BrowseContext* context = f.context;
    const QTemporaryDir& dir = f.dir;
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 1 && context->selectedCount() == 1; }));

    auto* chooser = window->findChild<QObject*>(QStringLiteral("saveDialog"));
    ASSERT_NE(chooser, nullptr);
    QMetaObject::invokeMethod(chooser, "start", Q_ARG(QVariant, dir.path()), Q_ARG(QVariant, QStringLiteral("a.jpg")));
    auto* field = chooser->property("nameField").value<QQuickItem*>();
    ASSERT_TRUE(waitUntil([&] { return field->hasActiveFocus(); }));
    // S straightens and X crops the selected image, Space and Backspace move between images.
    QTest::keyClick(window, Qt::Key_End);
    typeText(window, QStringLiteral("sx p"));
    QTest::keyClick(window, Qt::Key_Backspace);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("a.jpgsx "));
    EXPECT_FALSE(window->property("viewing").toBool());
    EXPECT_TRUE(chooser->property("opened").toBool());
    QTest::keyClick(window, Qt::Key_Escape);
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
    QObject* names = f.find(QStringLiteral("nameDialog"));
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
    QObject* names = f.find(QStringLiteral("nameDialog"));
    ASSERT_TRUE(waitUntil([&] { return names->property("opened").toBool(); }));
    EXPECT_EQ(names->property("title").toString(), QStringLiteral("Rename Folder"));
    EXPECT_EQ(names->property("initialText").toString(), QStringLiteral("sub"));
    QTest::keyClick(f.window, Qt::Key_Escape);
    ASSERT_TRUE(waitUntil([&] { return !names->property("visible").toBool(); }));
    EXPECT_TRUE(tree->hasActiveFocus()) << "the keys come back to the tree";

    QTest::keyClick(f.window, Qt::Key_Delete);
    QObject* trash = f.find(QStringLiteral("trashFolderDialog"));
    ASSERT_NE(trash, nullptr);
    EXPECT_TRUE(waitUntil([&] { return trash->property("opened").toBool(); })) << "it asks about the folder";
    QTest::keyClick(f.window, Qt::Key_Escape);
    ASSERT_TRUE(waitUntil([&] { return !trash->property("visible").toBool(); }));
    QTest::keyClick(f.window, Qt::Key_Delete, Qt::ShiftModifier);
    waitUntil([] { return false; }, 200);
    EXPECT_TRUE(QFile::exists(folder + QStringLiteral("/x.jpg"))) << "Shift+Del does not delete the files of the folder";
}

TEST(Main, TheLockKeepsAutoContrastAndTurningItOffReleasesTheLock) {
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
    QObject* lock = f.find(QStringLiteral("keepAutoContrastButton"));
    QObject* button = f.find(QStringLiteral("autoContrastButton"));
    ASSERT_NE(lock, nullptr);
    ASSERT_NE(button, nullptr);
    ASSERT_FALSE(view->autoContrast());

    QMetaObject::invokeMethod(lock, "click");
    EXPECT_TRUE(lock->property("checked").toBool());
    EXPECT_TRUE(view->autoContrast());
    QTest::keyClick(f.window, Qt::Key_Right);
    ASSERT_TRUE(waitUntil([&] { return view->source().endsWith(QStringLiteral("b.tif")) && view->contrastAdjustable(); }));
    EXPECT_TRUE(view->autoContrast()) << "kept for the next image";

    QMetaObject::invokeMethod(button, "click");
    EXPECT_FALSE(view->autoContrast());
    EXPECT_FALSE(lock->property("checked").toBool()) << "turned off, it is no longer kept";
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
TEST(Main, ChangingTheHueRecoloursTheWindowAtOnce) {
    MainFixture f(0);
    ASSERT_NE(f.window, nullptr);
    auto* palette = f.engine.singletonInstance<iv::ThemePalette*>("ImageViewer", "ThemePalette");
    QObject* theme = f.engine.singletonInstance<QObject*>("ImageViewer", "Theme");
    ASSERT_NE(palette, nullptr);
    ASSERT_NE(theme, nullptr);
    const double willow = palette->hue();
    const QColor before = theme->property("window").value<QColor>();
    EXPECT_GT(before.green(), before.blue()) << "willow is green";
    EXPECT_EQ(f.window->color(), before) << "the window is painted in it";

    palette->setHue(250);
    const QColor blue = theme->property("window").value<QColor>();
    EXPECT_GT(blue.blue(), blue.green());
    EXPECT_EQ(f.window->color(), blue) << "followed by the binding alone";

    palette->setHue(willow);
    EXPECT_EQ(theme->property("window").value<QColor>(), before);
    EXPECT_EQ(f.warnings, QStringList());
}
