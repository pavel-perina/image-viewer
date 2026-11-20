#include "ui_test_support.h"

#include "core/oklab.h"
#include "core/white_balance.h"
#include "ui/slider_scale.h"

#include <QMatrix4x4>
#include <QVector3D>

TEST(LosslessJpeg, IsOfferedOnlyWhenEveryFileIsAJpeg) {
    BrowseContext context;
    EXPECT_TRUE(context.allJpeg({QStringLiteral("/a/b.jpg"), QStringLiteral("/a/C.JPEG")}));
    EXPECT_FALSE(context.allJpeg({QStringLiteral("/a/b.jpg"), QStringLiteral("/a/c.png")}));
    EXPECT_FALSE(context.allJpeg({}));
}

TEST(LosslessJpeg, ChangesFilesInPlaceAndTellsWhatShowsThem) {
    FileOpsFixture f(2);
    writeWideJpeg(f.path(QStringLiteral("a.jpg")));
    writeWideJpeg(f.path(QStringLiteral("b.jpg")));
    QSignalSpy edited(&f.context, &BrowseContext::filesEdited);

    f.context.editJpegs({f.path(QStringLiteral("a.jpg")), f.path(QStringLiteral("b.jpg"))}, BrowseContext::RotateClockwise, false);
    f.settle([&] { return edited.count() == 1; });
    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_EQ(edited.front().front().toStringList(), (QStringList{f.path(QStringLiteral("a.jpg")), f.path(QStringLiteral("b.jpg"))}));
    EXPECT_EQ(jpegSize(f.path(QStringLiteral("a.jpg"))), QSize(32, 64));
    EXPECT_EQ(jpegSize(f.path(QStringLiteral("b.jpg"))), QSize(32, 64));

    // An upright file has no orientation to apply, so nothing changes and nothing is reported.
    f.context.editJpegs({f.path(QStringLiteral("a.jpg"))}, BrowseContext::ApplyOrientation, false);
    f.settle([&] { return true; });
    EXPECT_TRUE(f.problems.isEmpty());
    EXPECT_EQ(edited.count(), 1);
}

TEST(LosslessJpeg, ReportsAnEdgeThatNeedsTrimmingAndTrimsWhenAllowed) {
    FileOpsFixture f(1);
    writeWideJpeg(f.path(QStringLiteral("a.jpg")), 40);
    f.context.editJpegs({f.path(QStringLiteral("a.jpg"))}, BrowseContext::FlipVertical, false);
    f.settle([&] { return !f.problems.isEmpty(); });
    ASSERT_EQ(f.problems.size(), 1);
    EXPECT_TRUE(f.problems.front().startsWith(QStringLiteral("a.jpg: "))) << f.problems.front().toStdString();
    EXPECT_TRUE(f.problems.front().contains(QStringLiteral("trimmed"))) << f.problems.front().toStdString();
    EXPECT_EQ(jpegSize(f.path(QStringLiteral("a.jpg"))), QSize(64, 40));

    f.problems.clear();
    f.context.editJpegs({f.path(QStringLiteral("a.jpg"))}, BrowseContext::FlipVertical, true);
    f.settle([&] { return jpegSize(f.path(QStringLiteral("a.jpg"))) == QSize(64, 32); });
    EXPECT_TRUE(f.problems.isEmpty());
}

TEST(LosslessJpeg, ConvertsToJxlAndSelectsTheResult) {
    BrowseContext probe;
    if (!probe.canConvertToJxl()) {
        GTEST_SKIP() << "built without JPEG XL";
    }
    FileOpsFixture f(2);
    f.context.convertToJxl({f.path(QStringLiteral("a.jpg"))}, false);
    f.settle([&] { return f.context.fileCount() == 3; });
    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_TRUE(QFileInfo::exists(f.path(QStringLiteral("a.jxl"))));
    EXPECT_TRUE(QFileInfo::exists(f.path(QStringLiteral("a.jpg"))));
    EXPECT_EQ(f.context.selectedPaths(), QStringList{f.path(QStringLiteral("a.jxl"))});

    // Once more, the existing file is reported and left as it is.
    f.context.convertToJxl({f.path(QStringLiteral("a.jpg"))}, false);
    f.settle([&] { return !f.problems.isEmpty(); });
    EXPECT_TRUE(f.problems.front().contains(QStringLiteral("a.jxl already exists"))) << f.problems.front().toStdString();

    // The original goes to the trash only when asked, and only after its conversion.
    const QString unique = QStringLiteral("jxl-%1.jpg").arg(QCoreApplication::applicationPid());
    ASSERT_TRUE(QFile::rename(f.path(QStringLiteral("b.jpg")), f.path(unique)));
    f.problems.clear();
    f.context.convertToJxl({f.path(unique)}, true);
    f.settle([&] { return QFileInfo::exists(f.path(QFileInfo(unique).baseName() + QStringLiteral(".jxl"))) && !QFileInfo::exists(f.path(unique)); });
    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_TRUE(isInTrash(unique));
}

TEST(LosslessJpeg, AnImageOnScreenLoadsAgainAfterAnEdit) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    writeWideJpeg(path);
    ViewFixture f;
    f.view->setSource(path);
    ASSERT_TRUE(waitUntil([&] { return f.view->status() == ImageView::Status::Ready; }));
    ASSERT_EQ(f.view->imageWidth(), 64);

    ASSERT_TRUE(iv::transformJpegFile(QFileInfo(path).filesystemFilePath(), iv::JpegEdit::RotateClockwise));
    f.view->reload();
    EXPECT_TRUE(waitUntil([&] { return f.view->status() == ImageView::Status::Ready && f.view->imageWidth() == 32; }));
    EXPECT_EQ(f.view->imageHeight(), 64);
}

TEST(LosslessJpeg, AsksFirstUnlessToldNotTo) {
    QTemporaryDir dir;
    writeWideJpeg(dir.filePath(QStringLiteral("a.jpg")));
    writeJpeg(dir.filePath(QStringLiteral("b.jpg")));
    QImage(8, 8, QImage::Format_RGB32).save(dir.filePath(QStringLiteral("c.png")));

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
            width: 800; height: 600; visible: true
            property alias context: context
            property alias actions: actions
            BrowseContext { id: context }
            FileActions { id: actions; context: context; viewing: false }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    context->openPath(dir.filePath(QStringLiteral("a.jpg")));
    ASSERT_TRUE(waitUntil([&] { return context->fileCount() == 3 && context->selectedCount() == 1; }));

    auto* rotate = actions->property("rotateClockwiseAction").value<QObject*>();
    EXPECT_TRUE(actions->property("jpegTargets").toBool());
    EXPECT_TRUE(rotate->property("enabled").toBool());
    context->selectAll();
    EXPECT_FALSE(actions->property("jpegTargets").toBool()) << "c.png is not a JPEG";
    EXPECT_FALSE(rotate->property("enabled").toBool());
    context->clickFile(0, false, false);
    EXPECT_TRUE(rotate->property("enabled").toBool());

    auto* dialog = actions->findChild<QObject*>(QStringLiteral("jpegEditDialog"));
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(rotate, "trigger");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(dialog->property("title").toString(), QStringLiteral("Rotate 90° Clockwise"));
    EXPECT_TRUE(dialog->property("text").toString().startsWith(QStringLiteral("a.jpg will change in place")));
    EXPECT_EQ(jpegSize(dir.filePath(QStringLiteral("a.jpg"))), QSize(64, 32)) << "nothing happens before the answer";

    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    QQuickItem* dontAsk = findNamed(content, QStringLiteral("dontAskCheck"));
    ASSERT_NE(dontAsk, nullptr);
    dontAsk->setProperty("checked", true);
    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && jpegSize(dir.filePath(QStringLiteral("a.jpg"))) == QSize(32, 64); }));

    // From now on it goes straight ahead, until the menu turns asking back on.
    auto* ask = actions->property("askBeforeEditingAction").value<QObject*>();
    EXPECT_FALSE(ask->property("checked").toBool());
    QMetaObject::invokeMethod(rotate, "trigger");
    EXPECT_FALSE(dialog->property("visible").toBool());
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && jpegSize(dir.filePath(QStringLiteral("a.jpg"))) == QSize(64, 32); }));
    QMetaObject::invokeMethod(ask, "toggle");
    EXPECT_TRUE(ask->property("checked").toBool());
    QMetaObject::invokeMethod(rotate, "trigger");
    EXPECT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    QMetaObject::invokeMethod(dialog, "reject");
}

TEST(LosslessJpeg, WarnsWhenTheOriginalsGoToTheTrash) {
    BrowseContext probe;
    if (!probe.canConvertToJxl()) {
        GTEST_SKIP() << "built without JPEG XL";
    }
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 800; height: 600; visible: true
            property alias context: context
            property alias actions: actions
            BrowseContext { id: context }
            FileActions { id: actions; context: context; viewing: false }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    context->openPath(dir.filePath(QStringLiteral("a.jpg")));
    ASSERT_TRUE(waitUntil([&] { return context->selectedCount() == 1; }));

    auto* dialog = actions->findChild<QObject*>(QStringLiteral("jxlDialog"));
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(actions->property("convertToJxlAction").value<QObject*>(), "trigger");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(dialog->property("kind").toString(), QStringLiteral("question"));
    EXPECT_TRUE(dialog->property("text").toString().contains(QStringLiteral("is kept")));

    QQuickItem* trash = findNamed(dialog->property("contentItem").value<QQuickItem*>(), QStringLiteral("trashOriginalsCheck"));
    ASSERT_NE(trash, nullptr);
    QMetaObject::invokeMethod(trash, "toggle");
    QMetaObject::invokeMethod(trash, "toggled");
    EXPECT_EQ(dialog->property("kind").toString(), QStringLiteral("warning"));
    EXPECT_TRUE(dialog->property("text").toString().contains(QStringLiteral("then goes to the trash")));
    // Left as it was found, as the choice is remembered.
    QMetaObject::invokeMethod(trash, "toggle");
    QMetaObject::invokeMethod(trash, "toggled");
    QMetaObject::invokeMethod(dialog, "reject");
}

TEST(SaveAs, OffersOnlyWhatCanBeWritten) {
    BrowseContext context;
    const QVariantList types = context.saveTypes();
    EXPECT_TRUE(std::ranges::any_of(types, [](const QVariant& type) {
        return type.toMap().value(QStringLiteral("extensions")).toStringList() == QStringList{QStringLiteral("png")};
    }));
    EXPECT_EQ(context.saveFormat(QStringLiteral("/a/b.JPG")), QStringLiteral("jpeg"));
    EXPECT_EQ(context.saveFormat(QStringLiteral("/a/b.tiff")), QStringLiteral("tiff"));
    EXPECT_EQ(context.saveFormat(QStringLiteral("/a/b.bmp")), QString());
    EXPECT_EQ(context.saveFormat(QStringLiteral("/a/b")), QString());
}

TEST(SaveAs, WritesTheFormatAndMakesTheResultCurrent) {
    FileOpsFixture f(1);
    QSignalSpy edited(&f.context, &BrowseContext::filesEdited);
    f.context.saveAs(f.path(QStringLiteral("a.jpg")), 0, f.path(QStringLiteral("a.png")), {});
    f.settle([&] { return f.context.fileCount() == 2; });
    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_EQ(f.context.selectedPaths(), QStringList{f.path(QStringLiteral("a.png"))});
    EXPECT_EQ(f.context.currentName(), QStringLiteral("a.png"));
    ASSERT_EQ(edited.count(), 1);
    const auto opened = iv::openImageSource(QFileInfo(f.path(QStringLiteral("a.png"))).filesystemFilePath());
    ASSERT_TRUE(opened);
    EXPECT_EQ((*opened)->subimages().front().stored.container, "PNG");

    f.context.saveAs(f.path(QStringLiteral("a.jpg")), 0, f.path(QStringLiteral("a.bmp")), {});
    f.settle([&] { return !f.problems.isEmpty(); });
    ASSERT_EQ(f.problems.size(), 1);
    EXPECT_TRUE(f.problems.front().startsWith(QStringLiteral("a.bmp: "))) << f.problems.front().toStdString();
}

TEST(SaveAs, TheOptionsReachTheWriter) {
    FileOpsFixture f(1);
    writeJpeg(f.path(QStringLiteral("a.jpg")), {.width = 64, .height = 64, .pixel = [](int x, int y) {
                  return iv::test::Rgb{static_cast<std::uint8_t>(x * 4), static_cast<std::uint8_t>((x ^ y) * 4), static_cast<std::uint8_t>(y * 4)};
              }});
    const auto save = [&](const QString& name, const QVariantMap& options) {
        f.context.saveAs(f.path(QStringLiteral("a.jpg")), 0, f.path(name), options);
        f.settle([&] { return QFileInfo::exists(f.path(name)); });
        EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    };
    save(QStringLiteral("low.jpg"), {{QStringLiteral("quality"), 20}, {QStringLiteral("progressive"), false}});
    save(QStringLiteral("high.jpg"), {{QStringLiteral("quality"), 98}, {QStringLiteral("chroma"), QStringLiteral("4:4:4")}});
    EXPECT_LT(FileOpsFixture::size(f.path(QStringLiteral("low.jpg"))), FileOpsFixture::size(f.path(QStringLiteral("high.jpg"))));
    const auto read = [&](const QString& name) {
        QFile file(f.path(name));
        EXPECT_TRUE(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        return iv::test::decodeJpeg({bytes.begin(), bytes.end()});
    };
    EXPECT_FALSE(read(QStringLiteral("low.jpg")).progressive);
    EXPECT_TRUE(read(QStringLiteral("high.jpg")).progressive);
}

TEST(SaveAs, TheOptionsDialogStartsTheSave) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 800; height: 600; visible: true
            property alias context: context
            property alias actions: actions
            BrowseContext { id: context }
            FileActions { id: actions; context: context; viewing: false }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    context->openPath(dir.filePath(QStringLiteral("a.jpg")));
    ASSERT_TRUE(waitUntil([&] { return context->selectedCount() == 1; }));
    EXPECT_TRUE(actions->property("saveAsAction").value<QObject*>()->property("enabled").toBool());

    auto* dialog = actions->findChild<QObject*>(QStringLiteral("saveOptionsDialog"));
    ASSERT_NE(dialog, nullptr);
    const QString target = dir.filePath(QStringLiteral("b.jpg"));
    QMetaObject::invokeMethod(actions, "chooseSaveOptions", Q_ARG(QVariant, dir.filePath(QStringLiteral("a.jpg"))),
                              Q_ARG(QVariant, 0), Q_ARG(QVariant, target), Q_ARG(QVariant, QVariant()));
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(dialog->property("title").toString(), QStringLiteral("Save As JPEG"));
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    QQuickItem* note = findNamed(content, QStringLiteral("recompressNote"));
    ASSERT_NE(note, nullptr);
    EXPECT_TRUE(note->isVisible()) << "a JPEG saved as JPEG loses quality";
    QQuickItem* quality = findNamed(content, QStringLiteral("qualitySlider"));
    ASSERT_NE(quality, nullptr);
    EXPECT_EQ(quality->property("value").toInt(), 90);
    EXPECT_TRUE(findNamed(content, QStringLiteral("keepMetadataCheck"))->property("checked").toBool());

    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && context->currentName() == QStringLiteral("b.jpg"); }));
    EXPECT_TRUE(QFileInfo::exists(target));
}

TEST(Editing, AGrayJpegIsShownAndEditedInOneChannel) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("gray.jpg"));
    const iv::ImageBuffer gray = iv::ImageBuffer::create({120, 80}, iv::PixelFormat::Gray8);
    for (int y = 0; y < 80; ++y) {
        std::memset(gray.row(y), y < 40 ? 60 : 200, 120);
    }
    ASSERT_TRUE(iv::writeImage(QFileInfo(path).filesystemFilePath(), gray, {}, iv::JpegWriteOptions{}, true));

    ViewFixture f;
    ASSERT_TRUE(f.load(path));
    ASSERT_EQ(f.view->status(), ImageView::Status::Ready);
    EXPECT_TRUE(f.view->contrastAdjustable());
    const QImage shot = f.grab();
    const QColor top = shot.pixelColor(200, 200 - 20);  // shown at 1:1 in the middle
    const QColor bottom = shot.pixelColor(200, 200 + 20);
    EXPECT_NEAR(top.red(), 60, 4);
    EXPECT_EQ(top.red(), top.blue());
    EXPECT_NEAR(bottom.green(), 200, 4);

    f.view->crop(QRect(10, 10, 100, 60));
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 100);
    f.view->resizeImage(50, 30, false);
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 50);
    f.view->grayscale(1);
    ASSERT_TRUE(settled(f.view));
    f.view->straighten(3, 0, 0, 0, true);
    ASSERT_TRUE(settled(f.view));
    EXPECT_LT(f.view->imageWidth(), 50);
    EXPECT_TRUE(f.view->modified());
}

TEST(Editing, CropUndoRedoAndDiscardChangeWhatIsShown) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    EXPECT_TRUE(f.view->editable());
    EXPECT_FALSE(f.view->modified());
    EXPECT_EQ(f.view->imageWidth(), 32);
    EXPECT_EQ(f.view->imageHeight(), 64);

    // Upright pixels, as shown.
    f.view->crop(QRect(0, 8, 32, 40));
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 32);
    EXPECT_EQ(f.view->imageHeight(), 40);
    EXPECT_TRUE(f.view->modified());
    EXPECT_TRUE(f.view->canUndo());
    EXPECT_FALSE(f.view->editedImage().isNull());

    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageHeight(), 64);
    EXPECT_FALSE(f.view->modified());
    EXPECT_TRUE(f.view->canRedo());
    EXPECT_TRUE(f.view->editedImage().isNull());

    f.view->redo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageHeight(), 40);

    f.view->discardEdits();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageHeight(), 64);
    EXPECT_FALSE(f.view->modified());
    EXPECT_FALSE(f.view->canUndo());
}

TEST(Editing, CropsOutsideTheImageOrOfAllOfItAreIgnored) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    f.view->crop(QRect(0, 0, 32, 64));
    f.view->crop(QRect(10, 10, 30, 10));
    QCoreApplication::processEvents();
    EXPECT_FALSE(f.view->modified());
    EXPECT_EQ(f.view->status(), ImageView::Status::Ready);
}

// Why: Save As in the viewer must keep the transparency of a picture, edited or not, and what
// the view hands over differs between the two. Input: a made-up PNG, half transparent red,
// saved as lossless WebP the way the Save As dialog does it: untouched, cropped and undone,
// and cropped. The truth is the WebP read back: the same colour and alpha as the PNG.
TEST(SaveAs, ATransparentPictureKeepsItsTransparencyEditedOrNot) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("clear.png"));
    QImage image(32, 24, QImage::Format_ARGB32);
    image.fill(qRgba(200, 40, 20, 128));
    ASSERT_TRUE(image.save(path));
    ViewFixture f;
    ASSERT_TRUE(f.load(path));
    BrowseContext context;
    if (context.saveFormat(QStringLiteral("a.webp")).isEmpty()) {
        GTEST_SKIP() << "this build writes no WebP";
    }
    const auto saveAndRead = [&](const QString& name) {
        QSignalSpy edited(&context, &BrowseContext::filesEdited);
        const QString target = dir.filePath(name);
        context.saveAs(path, 0, target, {{QStringLiteral("lossless"), true}}, f.view->editedImage());
        EXPECT_TRUE(waitUntil([&] { return edited.count() == 1; }));
        // Read with the program's reader, as Qt may have none for WebP: blue, green, red, alpha.
        const auto opened = iv::openImageSource(QFileInfo(target).filesystemFilePath());
        const auto tile = opened ? (*opened)->readTile({0, 0, 0, 0}, {}) : std::unexpected(opened.error());
        if (!tile || !tile->hasAlpha()) {
            ADD_FAILURE() << (tile ? "the saved picture has no alpha" : tile.error());
            return QRgb(0);
        }
        const auto* p = reinterpret_cast<const std::uint8_t*>(tile->row(5)) + 5 * 4;
        return qRgba(p[2], p[1], p[0], p[3]);
    };
    EXPECT_FALSE(f.view->editedImage().isValid());
    EXPECT_EQ(saveAndRead(QStringLiteral("untouched.webp")), qRgba(200, 40, 20, 128));

    f.view->crop(QRect(4, 4, 20, 10));
    ASSERT_TRUE(settled(f.view));
    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(saveAndRead(QStringLiteral("undone.webp")), qRgba(200, 40, 20, 128));

    f.view->crop(QRect(4, 4, 20, 10));
    ASSERT_TRUE(settled(f.view));
    ASSERT_TRUE(f.view->editedImage().isValid());
    EXPECT_EQ(saveAndRead(QStringLiteral("cropped.webp")), qRgba(200, 40, 20, 128));
}

TEST(Editing, SaveAsWritesTheEditedImageAndMarksItSaved) {
    QTemporaryDir dir;
    const QString path = writeTurnedJpeg(dir);
    ViewFixture f;
    ASSERT_TRUE(f.load(path));
    f.view->crop(QRect(4, 4, 20, 30));
    ASSERT_TRUE(settled(f.view));
    const QVariant edited = f.view->editedImage();
    ASSERT_TRUE(edited.isValid());

    BrowseContext context;
    QSignalSpy saved(&context, &BrowseContext::editedImageSaved);
    const QString target = dir.filePath(QStringLiteral("cropped.jpg"));
    context.saveAs(path, 0, target, {}, edited);
    ASSERT_TRUE(waitUntil([&] { return saved.count() == 1; }));
    // Upright, so the 20 x 30 rectangle is stored as it is shown.
    EXPECT_EQ(jpegSize(target), QSize(20, 30));

    // Edited further meanwhile, it stays unsaved.
    f.view->crop(QRect(0, 0, 10, 10));
    ASSERT_TRUE(settled(f.view));
    f.view->markSaved(saved.front().front());
    EXPECT_TRUE(f.view->modified());
    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    f.view->markSaved(saved.front().front());
    EXPECT_TRUE(f.view->modified()) << "a handle from before is not the image now";
    f.view->markSaved(f.view->editedImage());
    EXPECT_FALSE(f.view->modified());
}

TEST(Editing, TheCropOverlayDragsToWholePixelsAndCropApplies) {
    QTemporaryDir dir;
    writeTurnedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(700, 500);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 64; }));

    QMetaObject::invokeMethod(page, "startCrop");
    EXPECT_TRUE(page->property("cropping").toBool());
    auto* overlay = page->findChild<QQuickItem*>(QStringLiteral("cropOverlay"));
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->property("cropRect").toRect(), QRect(0, 0, 32, 64));
    // The panel slides in and the image refits to the room left.
    ASSERT_TRUE(waitUntil([&] {
        const QPointF corner = image->imageToItem(QPointF(32, 64));
        return image->width() < 500 && corner.x() < image->width();
    }));
    waitUntil([] { return false; }, 300);

    // Drags the bottom right corner to the image position (20.6, 30.4); the edges go to the
    // nearest pixel boundaries.
    const QPointF from = overlay->mapToScene(image->imageToItem(QPointF(32, 64)));
    const QPointF to = overlay->mapToScene(image->imageToItem(QPointF(20.6, 30.4)));
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from.toPoint());
    QTest::mouseMove(&view, ((from + to) / 2).toPoint());
    QTest::mouseMove(&view, to.toPoint());
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to.toPoint());
    EXPECT_EQ(overlay->property("cropRect").toRect(), QRect(0, 0, 21, 30));

    QMetaObject::invokeMethod(page, "applyCrop");
    EXPECT_FALSE(page->property("cropping").toBool());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 30; }));
    EXPECT_EQ(image->imageWidth(), 21);
    EXPECT_TRUE(image->modified());
}

TEST(Editing, APressOutsideTheImageTakesAnEdgeAndAClickDrawsNothing) {
    QTemporaryDir dir;
    writeTurnedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(700, 500);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 64; }));
    QMetaObject::invokeMethod(page, "startCrop");
    auto* overlay = page->findChild<QQuickItem*>(QStringLiteral("cropOverlay"));
    ASSERT_TRUE(waitUntil([&] { return image->width() < 500; }));
    waitUntil([] { return false; }, 300);
    overlay->setProperty("cropRect", QRect(8, 16, 16, 32));
    const auto at = [&](double x, double y) { return overlay->mapToScene(image->imageToItem(QPointF(x, y))).toPoint(); };

    // A click on the image beside the rectangle leaves it alone.
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at(2, 4));
    EXPECT_EQ(overlay->property("cropRect").toRect(), QRect(8, 16, 16, 32));

    // Well left of the image and above it: the top left corner, wherever it was grabbed.
    const QPoint outside = at(0, 0) - QPoint(20, 20);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, outside);
    QTest::mouseMove(&view, outside + (at(4, 8) - at(8, 16)) / 2);
    QTest::mouseMove(&view, outside + (at(4, 8) - at(8, 16)));
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, outside + (at(4, 8) - at(8, 16)));
    EXPECT_EQ(overlay->property("cropRect").toRect(), QRect(4, 8, 20, 40));
}

// Every tool on the image leaves moving the picture alone: the right and middle buttons pan,
// and so does the left one while Space is held, without changing what the tool has. Compared
// with where the picture was, as ImageView reports it.
TEST(Editing, ThePictureCanBeMovedWhileCropping) {
    ToolPage f("startCrop");
    auto* overlay = f.page->findChild<QQuickItem*>(QStringLiteral("cropOverlay"));
    ASSERT_NE(overlay, nullptr);
    // In a corner, so that the drags from the middle start outside it.
    overlay->setProperty("cropRect", QRect(0, 0, 20, 20));
    const QRect rect = overlay->property("cropRect").toRect();

    EXPECT_EQ(f.drag(Qt::RightButton, QPoint(-40, -20)), QPointF(-40, -20));
    EXPECT_EQ(f.drag(Qt::MiddleButton, QPoint(30, 10)), QPointF(30, 10));
    QTest::keyPress(&f.view, Qt::Key_Space);
    EXPECT_EQ(f.drag(Qt::LeftButton, QPoint(-30, -20)), QPointF(-30, -20));
    QTest::keyRelease(&f.view, Qt::Key_Space);
    EXPECT_EQ(overlay->property("cropRect").toRect(), rect);

    // Without Space the left button is the tool's again.
    EXPECT_EQ(f.drag(Qt::LeftButton, QPoint(-30, -20)), QPointF(0, 0));
    EXPECT_NE(overlay->property("cropRect").toRect(), rect);
}

TEST(Straighten, ThePictureCanBeMovedWhileDrawingLines) {
    ToolPage f("startStraighten");
    auto* overlay = f.page->findChild<QQuickItem*>(QStringLiteral("straightenOverlay"));
    ASSERT_NE(overlay, nullptr);

    EXPECT_EQ(f.drag(Qt::RightButton, QPoint(-40, -20)), QPointF(-40, -20));
    EXPECT_EQ(f.drag(Qt::MiddleButton, QPoint(30, 10)), QPointF(30, 10));
    QTest::keyPress(&f.view, Qt::Key_Space);
    EXPECT_EQ(f.drag(Qt::LeftButton, QPoint(-30, -20)), QPointF(-30, -20));
    QTest::keyRelease(&f.view, Qt::Key_Space);
    EXPECT_TRUE(overlay->property("lines").toList().isEmpty());

    EXPECT_EQ(f.drag(Qt::LeftButton, QPoint(-30, -20)), QPointF(0, 0));
    EXPECT_EQ(overlay->property("lines").toList().size(), 1);
}

// The tools take their colours from outside, which in the program is Theme: a line drawn across
// the middle shows in the colour given for horizontal lines, read back from the window.
TEST(Straighten, LinesAreDrawnInTheColoursGiven) {
    ToolPage f("startStraighten");
    auto* overlay = f.page->findChild<QQuickItem*>(QStringLiteral("straightenOverlay"));
    ASSERT_NE(overlay, nullptr);
    EXPECT_EQ(overlay->property("horizontalColor").value<QColor>(), QColor(0xff, 0xb7, 0x4d)) << "Theme.guideHorizontal";
    overlay->setProperty("horizontalColor", QColor(Qt::magenta));

    EXPECT_EQ(f.drag(Qt::LeftButton, QPoint(120, 0)), QPointF(0, 0));
    ASSERT_EQ(overlay->property("lines").toList().size(), 1);
    const QImage shown = grabIfShown(f.view);
    if (shown.isNull()) {
        GTEST_SKIP() << "nothing to grab on this platform";
    }
    const QPointF middle = f.image->mapToScene(QPointF(f.image->width() / 2 + 60, f.image->height() / 2));
    const QColor at = shown.pixelColor((middle * shown.devicePixelRatio()).toPoint());
    EXPECT_GT(at.red(), 200);
    EXPECT_LT(at.green(), 80);
    EXPECT_GT(at.blue(), 200);
}

// Why: what straighten paints around its result and the grid over it are colours of Theme,
// given through the style like the lines, not written into the overlay.
// Input: the Straighten tool as the page sets it up. The truth is the style the overlay was
// given, against Theme's values, and the corner of the result when it can be grabbed.
TEST(Straighten, TheResultIsShownOnTheSurroundOfTheStyle) {
    ToolPage f("startStraighten");
    auto* overlay = f.page->findChild<QQuickItem*>(QStringLiteral("straightenOverlay"));
    ASSERT_NE(overlay, nullptr);
    auto* style = overlay->property("style").value<QObject*>();
    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->property("surround").value<QColor>(), QColor(0x20, 0x20, 0x20)) << "Theme.imageBackground";
    EXPECT_EQ(style->property("faint").value<QColor>(), QColor(255, 255, 255, 0x46)) << "Theme.toolFaint";

    style->setProperty("surround", QColor(Qt::magenta));
    overlay->setProperty("showResult", true);
    const QImage shown = grabIfShown(f.view);
    if (shown.isNull()) {
        GTEST_SKIP() << "nothing to grab on this platform";
    }
    // The result is fitted into 96% of the room, so the corner is surround.
    const QPointF corner = overlay->mapToScene(QPointF(2, 2));
    const QColor at = shown.pixelColor((corner * shown.devicePixelRatio()).toPoint());
    EXPECT_GT(at.red(), 200);
    EXPECT_LT(at.green(), 80);
    EXPECT_GT(at.blue(), 200);
}

TEST(Probe, ThePictureCanBeMovedWhileProbing) {
    ToolPage f("startProbe");
    auto* panel = f.page->findChild<QObject*>(QStringLiteral("probePanel"));
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "clearHistory");

    EXPECT_EQ(f.drag(Qt::RightButton, QPoint(-40, -20)), QPointF(-40, -20));
    EXPECT_EQ(f.drag(Qt::MiddleButton, QPoint(30, 10)), QPointF(30, 10));
    QTest::keyPress(&f.view, Qt::Key_Space);
    EXPECT_EQ(f.drag(Qt::LeftButton, QPoint(-30, -20)), QPointF(-30, -20));
    QTest::keyRelease(&f.view, Qt::Key_Space);
    waitUntil([] { return false; }, 200);
    EXPECT_FALSE(panel->property("probed").isValid() && !panel->property("probed").isNull()) << "panning reads nothing";

    // The left button reads, and moves nothing.
    EXPECT_EQ(f.drag(Qt::LeftButton, QPoint(-30, -20)), QPointF(0, 0));
    EXPECT_TRUE(waitUntil([&] { return panel->property("entries").toList().size() == 1; }));
}

TEST(Probe, ReadsThePixelShownAsTheFileStoresIt) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedTiff(dir)));
    ASSERT_EQ(f.view->imageWidth(), 4);
    QSignalSpy probed(f.view, &ImageView::probed);

    // Shown at 1, 5, which turned back is the stored pixel 5, 2.
    f.view->probe(QPointF(1.7, 5.2));
    ASSERT_TRUE(waitUntil([&] { return probed.count() == 1; }));
    const QVariantMap pixel = probed.front().front().toMap();
    EXPECT_EQ(pixel.value(QStringLiteral("x")).toInt(), 1);
    EXPECT_EQ(pixel.value(QStringLiteral("y")).toInt(), 5);
    EXPECT_EQ(pixel.value(QStringLiteral("depth")).toString(), QStringLiteral("16-bit"));
    EXPECT_EQ(pixel.value(QStringLiteral("values")).toString(), QStringLiteral("25700, 10280, 25700"));
    EXPECT_EQ(pixel.value(QStringLiteral("fractions")).toString(), QStringLiteral("0.39216, 0.15686, 0.39216"));
    EXPECT_EQ(pixel.value(QStringLiteral("hex")).toString(), QStringLiteral("#642864"));
    EXPECT_TRUE(pixel.value(QStringLiteral("srgbHex")).toString().isEmpty()) << "no profile, nothing to convert";

    // Outside what is shown, nothing is read.
    f.view->probe(QPointF(4.5, 1));
    f.view->probe(QPointF(-0.5, 1));
    waitUntil([] { return false; }, 200);
    EXPECT_EQ(probed.count(), 1);
}

TEST(Probe, AClickReadsAndRemembersAndArrowsMoveByAPixel) {
    QTemporaryDir dir;
    writeTurnedTiff(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(700, 500);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 8; }));

    QMetaObject::invokeMethod(page, "startProbe");
    EXPECT_TRUE(page->property("probing").toBool());
    auto* panel = page->findChild<QObject*>(QStringLiteral("probePanel"));
    ASSERT_NE(panel, nullptr);
    QMetaObject::invokeMethod(panel, "clearHistory");
    auto* area = page->findChild<QQuickItem*>(QStringLiteral("probeArea"));
    ASSERT_NE(area, nullptr);
    // The panel slides in and the image refits to the room left.
    ASSERT_TRUE(waitUntil([&] { return image->width() < 500; }));
    waitUntil([] { return false; }, 300);

    // Small images are not enlarged to fit; at 100% a click would land on pixel edges.
    for (int i = 0; i < 12; ++i) {
        image->zoomIn();
    }
    ASSERT_GT(image->imageToItem(QPointF(1, 0)).x() - image->imageToItem(QPointF(0, 0)).x(), 8);
    const QPoint at = image->mapToScene(image->imageToItem(QPointF(2.5, 3.5))).toPoint();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at);
    ASSERT_TRUE(waitUntil([&] { return panel->property("entries").toList().size() == 1; }));
    const QVariantMap read = panel->property("probed").toMap();
    EXPECT_EQ(read.value(QStringLiteral("x")).toInt(), 2);
    EXPECT_EQ(read.value(QStringLiteral("y")).toInt(), 3);
    EXPECT_EQ(read.value(QStringLiteral("values")).toString(), QStringLiteral("15420, 5140, 25700"));

    // A pixel to the right reads the next one without remembering it; Enter remembers it.
    QTest::keyClick(&view, Qt::Key_Right);
    ASSERT_TRUE(waitUntil([&] { return panel->property("probed").toMap().value(QStringLiteral("x")).toInt() == 3; }));
    EXPECT_EQ(panel->property("entries").toList().size(), 1);
    QTest::keyClick(&view, Qt::Key_Return);
    ASSERT_EQ(panel->property("entries").toList().size(), 2);
    EXPECT_EQ(panel->property("entries").toList().front().toMap().value(QStringLiteral("x")).toInt(), 3);

    QMetaObject::invokeMethod(page, "stopProbe");
    EXPECT_FALSE(page->property("probing").toBool());
}

TEST(Editing, ResizeScalesToTheSizeAndRefusesOnesTooLarge) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    EXPECT_TRUE(f.view->resizeProblem(16, 32).isEmpty());
    EXPECT_FALSE(f.view->resizeProblem(0, 32).isEmpty());
    EXPECT_FALSE(f.view->resizeProblem(65535, 65535).isEmpty());

    f.view->resizeImage(16, 32, false);
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 16);
    EXPECT_EQ(f.view->imageHeight(), 32);
    EXPECT_TRUE(f.view->modified());
    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageHeight(), 64);
}

TEST(Editing, GrayscaleKeepsTheSizeAndUndoes) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    for (const int method : {0, 1}) {
        f.view->grayscale(method);
        ASSERT_TRUE(settled(f.view));
        EXPECT_TRUE(f.view->modified());
        EXPECT_EQ(f.view->imageHeight(), 64);
        f.view->undo();
        ASSERT_TRUE(settled(f.view));
        EXPECT_FALSE(f.view->modified());
    }
}

// Why: rotating and flipping are edits like the others: the view shows the result at once,
// marks it modified and undoes it. Input: a JPEG shown 32 x 64 (stored 64 x 32, turned by EXIF),
// turned each way, by half a turn, by a whole one, and flipped both ways. The truth: quarter
// turns swap the size, the rest keep it, every one but the whole turn marks the image modified,
// and undo brings back the size and the unmodified state.
TEST(Editing, RotatingAndFlippingTurnTheImageAndUndo) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTurnedJpeg(dir)));
    ASSERT_EQ(f.view->imageWidth(), 32);
    ASSERT_EQ(f.view->imageHeight(), 64);
    const std::vector<std::pair<std::function<void()>, QSize>> cases{
        {[&] { f.view->rotate(1); }, QSize(64, 32)},
        {[&] { f.view->rotate(-1); }, QSize(64, 32)},
        {[&] { f.view->rotate(2); }, QSize(32, 64)},
        {[&] { f.view->flip(true); }, QSize(32, 64)},
        {[&] { f.view->flip(false); }, QSize(32, 64)},
    };
    for (const auto& [apply, size] : cases) {
        apply();
        ASSERT_TRUE(settled(f.view));
        EXPECT_TRUE(f.view->modified());
        EXPECT_EQ(QSize(f.view->imageWidth(), f.view->imageHeight()), size);
        f.view->undo();
        ASSERT_TRUE(settled(f.view));
        EXPECT_FALSE(f.view->modified());
        EXPECT_EQ(f.view->imageHeight(), 64);
    }
    f.view->rotate(4);
    ASSERT_TRUE(settled(f.view));
    EXPECT_FALSE(f.view->modified()) << "a whole turn is no edit";
}

TEST(Editing, TheResizeDialogKeepsTheRatioOrFillsAnother) {
    QTemporaryDir dir;
    const QString path = writeTurnedJpeg(dir);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import ImageViewer
        Window {
            width: 600; height: 500; visible: true
            property alias view: image
            property alias dialog: dialog
            ImageView { id: image; anchors.fill: parent }
            ResizeDialog { id: dialog; view: image }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* image = root->property("view").value<ImageView*>();
    auto* dialog = root->property("dialog").value<QObject*>();
    image->setSource(path);
    ASSERT_TRUE(settled(image));
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    QQuickItem* width = findNamed(content, QStringLiteral("resizeWidth"));
    QQuickItem* height = findNamed(content, QStringLiteral("resizeHeight"));
    ASSERT_NE(width, nullptr);

    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(width->property("value").toInt(), 32);
    EXPECT_EQ(height->property("value").toInt(), 64);
    // Typing a width sets the height for the ratio.
    width->setProperty("value", 16);
    EXPECT_EQ(height->property("value").toInt(), 32);
    EXPECT_FALSE(dialog->property("otherRatio").toBool());
    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 16; }));
    EXPECT_EQ(image->imageHeight(), 32);

    // A screen of another ratio, filled by cropping the middle.
    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(width->property("value").toInt(), 16) << "starts from the size the image has now";
    const QVariantMap screen{{QStringLiteral("width"), 40}, {QStringLiteral("height"), 30}};
    QMetaObject::invokeMethod(dialog, "applyPreset", Q_ARG(QVariant, screen));
    EXPECT_TRUE(dialog->property("otherRatio").toBool());
    EXPECT_EQ(dialog->property("target").toSizeF(), QSizeF(40, 30));
    QQuickItem* fill = findNamed(content, QStringLiteral("resizeFill"));
    ASSERT_NE(fill, nullptr);
    fill->setProperty("checked", true);
    QMetaObject::invokeMethod(fill, "toggled");
    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 40; }));
    EXPECT_EQ(image->imageHeight(), 30);
}

TEST(Editing, LosslessJpegToolsWaitUntilTheEditsAreGone) {
    QTemporaryDir dir;
    const QString path = writeTurnedJpeg(dir);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 500; visible: true
            property alias context: context
            property alias actions: actions
            property alias view: image
            BrowseContext { id: context }
            ImageView { id: image; anchors.fill: parent; source: context.currentPath }
            FileActions { id: actions; context: context; viewing: true; editView: image }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    auto* image = root->property("view").value<ImageView*>();
    context->openPath(path);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 64; }));
    auto* rotate = actions->property("rotateClockwiseAction").value<QObject*>();
    EXPECT_TRUE(rotate->property("enabled").toBool());

    image->crop(QRect(0, 0, 16, 16));
    ASSERT_TRUE(settled(image));
    EXPECT_FALSE(rotate->property("enabled").toBool()) << "it would turn the file, not the crop shown";
    // Saved, the crop is still what is shown.
    image->markSaved(image->editedImage());
    EXPECT_FALSE(rotate->property("enabled").toBool());
    image->undo();
    ASSERT_TRUE(settled(image));
    EXPECT_TRUE(rotate->property("enabled").toBool());
}

// Why: BMP, GIF, TGA and Netpbm are read but not written, so Save cannot put an edit back over
// such a file; it must go to Save As, with a name in a format that can be written, rather than
// warn of replacing a file it would then fail to write. Input: a BMP, cropped, then Save. The
// truth: no warning of replacing it, and the chooser open with the name ending in .png.
TEST(Editing, SavingAnEditedFileOfAReadOnlyFormatGoesToSaveAs) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("old.bmp"));
    QImage picture(64, 48, QImage::Format_RGB32);
    picture.fill(QColor(200, 40, 20));
    ASSERT_TRUE(picture.save(path, "BMP"));
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 500; visible: true
            property alias context: context
            property alias actions: actions
            property alias view: image
            BrowseContext { id: context }
            ImageView { id: image; anchors.fill: parent; source: context.currentPath }
            FileActions { id: actions; context: context; viewing: true; editView: image }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    auto* image = root->property("view").value<ImageView*>();
    context->openPath(path);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 48; }));
    image->crop(QRect(0, 0, 16, 24));
    ASSERT_TRUE(settled(image));
    auto* save = actions->property("saveAction").value<QObject*>();
    ASSERT_TRUE(save->property("enabled").toBool());
    auto* overwrite = actions->findChild<QObject*>(QStringLiteral("overwriteDialog"));
    auto* chooser = actions->findChild<QObject*>(QStringLiteral("saveDialog"));
    ASSERT_NE(overwrite, nullptr);
    ASSERT_NE(chooser, nullptr);
    QMetaObject::invokeMethod(save, "trigger");
    ASSERT_TRUE(waitUntil([&] { return chooser->property("visible").toBool(); }));
    EXPECT_FALSE(overwrite->property("opened").toBool());
    EXPECT_EQ(chooser->property("lastFileName").toString(), QStringLiteral("old.png"));
    QMetaObject::invokeMethod(chooser, "close");
}

TEST(Editing, SaveOverwritesTheFileAfterAWarning) {
    QTemporaryDir dir;
    const QString path = writeTurnedJpeg(dir);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 600; height: 500; visible: true
            property alias context: context
            property alias actions: actions
            property alias view: image
            BrowseContext { id: context; onEditedImageSaved: (image) => view.markSaved(image) }
            ImageView { id: image; anchors.fill: parent; source: context.currentPath }
            FileActions { id: actions; context: context; viewing: true; editView: image }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* context = root->property("context").value<BrowseContext*>();
    auto* actions = root->property("actions").value<QObject*>();
    auto* image = root->property("view").value<ImageView*>();
    context->openPath(path);
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageHeight() == 64; }));
    auto* save = actions->property("saveAction").value<QObject*>();
    EXPECT_FALSE(save->property("enabled").toBool()) << "nothing to save";

    image->crop(QRect(0, 0, 16, 24));
    ASSERT_TRUE(settled(image));
    ASSERT_TRUE(save->property("enabled").toBool());
    auto* dialog = actions->findChild<QObject*>(QStringLiteral("overwriteDialog"));
    ASSERT_NE(dialog, nullptr);
    QMetaObject::invokeMethod(save, "trigger");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    EXPECT_TRUE(dialog->property("text").toString().startsWith(QStringLiteral("turned.jpg will be replaced")));
    EXPECT_EQ(jpegSize(path), QSize(64, 32)) << "nothing happens before the answer";

    QMetaObject::invokeMethod(dialog, "accept");
    // Stored upright now, as it was shown.
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && jpegSize(path) == QSize(16, 24); }));
    ASSERT_TRUE(waitUntil([&] { return !image->modified() && image->status() == ImageView::Status::Ready; }));
    EXPECT_EQ(image->imageHeight(), 24);

    // Rename keeps the file and saves beside it, under a name not taken.
    image->crop(QRect(0, 0, 8, 8));
    ASSERT_TRUE(settled(image));
    QMetaObject::invokeMethod(save, "trigger");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    QMetaObject::invokeMethod(dialog, "applied");
    auto* names = actions->findChild<QObject*>(QStringLiteral("nameDialog"));
    ASSERT_TRUE(waitUntil([&] { return names->property("opened").toBool(); }));
    auto* content = names->property("contentItem").value<QQuickItem*>();
    QQuickItem* field = findNamed(content, QStringLiteral("nameField"));
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("turned-edited.jpg"));
    field->setProperty("text", QStringLiteral("turned.jpg"));
    QMetaObject::invokeMethod(names, "submit");
    EXPECT_TRUE(names->property("visible").toBool()) << "that name is taken";
    EXPECT_FALSE(findNamed(content, QStringLiteral("nameProblem"))->property("text").toString().isEmpty());
    field->setProperty("text", QStringLiteral("small.png"));
    QMetaObject::invokeMethod(names, "submit");
    const QString copy = dir.filePath(QStringLiteral("small.png"));
    ASSERT_TRUE(waitUntil([&] { return !context->busy() && QImage(copy).size() == QSize(8, 8); }));
    EXPECT_EQ(jpegSize(path), QSize(16, 24)) << "the file stays as it was";
}

TEST(Straighten, FindsTheEdgesFitsTheAnglesAndApplies) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTiltedJpeg(dir)));
    QSignalSpy ready(f.view, &ImageView::straightenReady);
    EXPECT_TRUE(f.view->detectStraightLines().isEmpty()) << "nothing before it is prepared";
    f.view->prepareStraighten();
    ASSERT_TRUE(waitUntil([&] { return ready.count() == 1; }));

    const QVariantList lines = f.view->detectStraightLines();
    ASSERT_GE(lines.size(), 4);
    const QVariantMap fit = f.view->fitStraightLines(lines, 0, 0, true);
    EXPECT_NEAR(fit.value(QStringLiteral("roll")).toDouble(), -4, 0.3);
    EXPECT_EQ(fit.value(QStringLiteral("pitch")).toDouble(), 0) << "rotation only";

    const double roll = fit.value(QStringLiteral("roll")).toDouble();
    const QSize size = f.view->straightenedSize(roll, 0, 0, 0, true);
    EXPECT_LT(size.width(), 400);
    EXPECT_NEAR(static_cast<double>(size.width()) / size.height(), 4.0 / 3, 0.02);
    EXPECT_FALSE(f.view->straightenPreview({roll, 0, 0}, 0, true).isNull());

    f.view->straighten(roll, 0, 0, 0, true);
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), size.width());
    EXPECT_EQ(f.view->imageHeight(), size.height());
    EXPECT_TRUE(f.view->modified());
    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), 400);
}

// Why: with cropping off, straightening keeps the whole turned picture, and the view must say
// the larger size, preview it, apply it with transparent corners and refuse a result too large
// to be saved. Input: a 400 x 300 JPEG turned 10 degrees with cropping off, then a perspective
// that puts part of it beyond the horizon. The truth: the size is that of the turned picture's
// bounding box (larger than 400 x 300), the applied picture has it, the edited image has alpha,
// and the perspective has no size and changes nothing. A finite result too large to be saved
// is refused by the same check of the size.
TEST(Straighten, WithoutCroppingKeepsTheWholePictureWithClearCorners) {
    QTemporaryDir dir;
    ViewFixture f;
    ASSERT_TRUE(f.load(writeTiltedJpeg(dir)));
    QSignalSpy ready(f.view, &ImageView::straightenReady);
    f.view->prepareStraighten();
    ASSERT_TRUE(waitUntil([&] { return ready.count() == 1; }));

    const QSize size = f.view->straightenedSize(10, 0, 0, 0, true, false);
    EXPECT_GT(size.width(), 400);
    EXPECT_GT(size.height(), 300);
    EXPECT_FALSE(f.view->straightenPreview({10, 0, 0}, 0, true, false).isNull());

    f.view->straighten(10, 0, 0, 0, true, false);
    ASSERT_TRUE(settled(f.view));
    EXPECT_EQ(f.view->imageWidth(), size.width());
    EXPECT_EQ(f.view->imageHeight(), size.height());
    const auto edited = f.view->editedImage().value<std::shared_ptr<iv::ImageSource>>();
    ASSERT_TRUE(edited);
    EXPECT_TRUE(edited->subimages()[0].alpha);
    const auto tile = edited->readTile({0, 0, 0, 0}, {});
    ASSERT_TRUE(tile.has_value());
    EXPECT_TRUE(tile->hasAlpha());
    EXPECT_EQ(reinterpret_cast<const std::uint8_t*>(tile->row(0))[3], 0) << "the top left corner is empty";

    f.view->undo();
    ASSERT_TRUE(settled(f.view));
    // So strong a perspective puts part of the picture beyond the horizon: no whole picture.
    EXPECT_EQ(f.view->straightenedSize(0, 80, 0, 10, true, false), QSize(0, 0));
    f.view->straighten(0, 80, 0, 10, true, false);
    QCoreApplication::processEvents();
    EXPECT_FALSE(f.view->modified()) << "it is refused";

}

TEST(Straighten, ALineDrawnOnTheImageSetsTheRotation) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 400; }));

    QMetaObject::invokeMethod(page, "startStraighten");
    EXPECT_TRUE(page->property("straightening").toBool());
    auto* panel = page->findChild<QObject*>(QStringLiteral("straightenPanel"));
    auto* overlay = page->findChild<QQuickItem*>(QStringLiteral("straightenOverlay"));
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(overlay, nullptr);
    ASSERT_TRUE(waitUntil([&] { return panel->property("ready").toBool() && image->width() < 700; }));
    waitUntil([] { return false; }, 300);

    // Along a line that falls 21 pixels over 300: levelled by turning 4 degrees anticlockwise.
    const auto at = [&](double x, double y) { return image->mapToScene(image->imageToItem(QPointF(x, y))).toPoint(); };
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, at(50, 100));
    QTest::mouseMove(&view, at(200, 110));
    QTest::mouseMove(&view, at(350, 121));
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, at(350, 121));
    ASSERT_EQ(overlay->property("lines").toList().size(), 1);
    EXPECT_NEAR(panel->property("roll").toDouble(), -4, 0.3);
    EXPECT_EQ(panel->property("pitch").toDouble(), 0);

    // A click draws nothing.
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at(200, 250));
    EXPECT_EQ(overlay->property("lines").toList().size(), 1);

    QMetaObject::invokeMethod(page, "applyStraighten");
    EXPECT_FALSE(page->property("straightening").toBool());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() < 400; }));
    EXPECT_TRUE(image->modified());
}

TEST(Straighten, TheFocalLengthComesFromTheCameraData) {
    QTemporaryDir dir;
    iv::test::JpegOptions options;
    options.exif = iv::test::exampleExifBlock(true);  // 50 mm, 75 mm in 35mm
    const QString path = dir.filePath(QStringLiteral("camera.jpg"));
    iv::test::writeFile(QFileInfo(path).filesystemFilePath(), encodeJpeg(options));
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready; }));
    ASSERT_EQ(image->focalLength35mm(), 75);

    QMetaObject::invokeMethod(page, "startStraighten");
    auto* panel = page->findChild<QObject*>(QStringLiteral("straightenPanel"));
    auto* field = page->findChild<QObject*>(QStringLiteral("straightenFocal"));
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(field, nullptr);
    EXPECT_EQ(panel->property("focal").toDouble(), 75);
    EXPECT_EQ(field->property("text").toString(), QStringLiteral("75"));
}

TEST(Adjust, ThePanelPreviewsCancelsAndApplies) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(settled(image));
    EXPECT_FALSE(image->grayImage()) << "gray pixels in a colour JPEG are still colour";
    auto* panel = page->findChild<QObject*>(QStringLiteral("adjustPanel"));
    ASSERT_NE(panel, nullptr);

    QMetaObject::invokeMethod(page, "startAdjust");
    ASSERT_TRUE(page->property("adjusting").toBool());
    EXPECT_TRUE(page->property("toolActive").toBool());
    EXPECT_TRUE(image->colorPreviewActive());
    EXPECT_FALSE(panel->property("changes").toBool());
    panel->setProperty("saturation", 50);
    EXPECT_DOUBLE_EQ(image->colorPreviewChroma(), 0.5);
    EXPECT_TRUE(panel->property("changes").toBool());
    // Without shaders there is no preview, but the picture stays.
    waitUntil([] { return false; }, 300);
    const QPoint corner = image->mapToScene(image->imageToItem(QPointF(10, 10))).toPoint() * view.devicePixelRatio();
    EXPECT_GT(view.grabWindow().pixelColor(corner).red(), 180);

    QMetaObject::invokeMethod(page, "cancelAdjust");
    EXPECT_FALSE(image->colorPreviewActive());
    EXPECT_FALSE(image->modified());

    // Opening it again starts afresh.
    QMetaObject::invokeMethod(page, "startAdjust");
    EXPECT_DOUBLE_EQ(panel->property("saturation").toDouble(), 100);
    panel->setProperty("black", 40);
    EXPECT_TRUE(image->colorPreviewTone());
    QMetaObject::invokeMethod(page, "applyAdjust");
    EXPECT_FALSE(page->property("adjusting").toBool());
    // The preview stays up while the edited picture is made, so the change does not flash.
    EXPECT_NE(image->status(), ImageView::Status::Ready);
    EXPECT_TRUE(image->colorPreviewActive());
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->modified(); }));
    EXPECT_FALSE(image->colorPreviewActive());
    image->undo();
    ASSERT_TRUE(settled(image));
    EXPECT_FALSE(image->modified());

    // Another image ends the tool.
    QMetaObject::invokeMethod(page, "startAdjust");
    image->setSource({});
    EXPECT_FALSE(page->property("adjusting").toBool());
    EXPECT_FALSE(image->colorPreviewActive());
}

// Why: the chooser of a colour table is made on first use, by the panel and not among its
// rows, and kept. Input: a colour JPEG in Adjust and two clicks on "Load .cube...". The truth
// is that one chooser exists afterwards, open, and none before the first click.
TEST(Adjust, LoadingAColourTableOpensOneChooser) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    ASSERT_NE(page, nullptr);
    context.openPath(dir.path());
    ASSERT_TRUE(settled(page->property("view").value<ImageView*>()));
    auto* panel = page->findChild<QObject*>(QStringLiteral("adjustPanel"));
    auto* load = page->findChild<QQuickItem*>(QStringLiteral("adjustLoadCube"));
    ASSERT_NE(panel, nullptr);
    ASSERT_NE(load, nullptr);
    QMetaObject::invokeMethod(page, "startAdjust");
    EXPECT_TRUE(panel->findChildren<QObject*>(QStringLiteral("fileChooser")).isEmpty());

    QMetaObject::invokeMethod(load, "clicked");
    const QList<QObject*> choosers = panel->findChildren<QObject*>(QStringLiteral("fileChooser"));
    ASSERT_EQ(choosers.size(), 1);
    ASSERT_TRUE(waitUntil([&] { return choosers.first()->property("opened").toBool(); }));
    QMetaObject::invokeMethod(choosers.first(), "close");
    ASSERT_TRUE(waitUntil([&] { return !choosers.first()->property("visible").toBool(); }));
    QMetaObject::invokeMethod(load, "clicked");
    EXPECT_EQ(panel->findChildren<QObject*>(QStringLiteral("fileChooser")).size(), 1);
    ASSERT_TRUE(waitUntil([&] { return choosers.first()->property("opened").toBool(); }));
}

TEST(Adjust, AGrayPictureTakesLevelsAndLightnessOnly) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("gray.png"));
    const iv::ImageBuffer gray = iv::ImageBuffer::create({60, 40}, iv::PixelFormat::Gray8);
    for (int y = 0; y < 40; ++y) {
        std::memset(gray.row(y), 30 + y * 4, 60);
    }
    ASSERT_TRUE(iv::writeImage(QFileInfo(path).filesystemFilePath(), gray, {}, iv::PngWriteOptions{}, true));
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 600);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(path);
    ASSERT_TRUE(settled(image));
    ASSERT_TRUE(image->grayImage());
    auto* panel = page->findChild<QObject*>(QStringLiteral("adjustPanel"));
    auto* saturation = page->findChild<QQuickItem*>(QStringLiteral("adjustSaturation"));
    auto* load = page->findChild<QQuickItem*>(QStringLiteral("adjustLoadCube"));
    ASSERT_NE(saturation, nullptr);
    ASSERT_NE(load, nullptr);

    QMetaObject::invokeMethod(page, "startAdjust");
    EXPECT_FALSE(saturation->isEnabled());
    EXPECT_FALSE(load->isEnabled());
    panel->setProperty("saturation", 0);
    EXPECT_DOUBLE_EQ(image->colorPreviewChroma(), 1) << "the preview leaves chroma alone";
    EXPECT_FALSE(panel->property("changes").toBool());

    panel->setProperty("midtones", 1.5);
    EXPECT_TRUE(image->colorPreviewLightness());
    QMetaObject::invokeMethod(page, "applyAdjust");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->modified(); }));
    EXPECT_TRUE(image->grayImage()) << "still one channel";
}

TEST(Adjust, LevelsHandlesDragOnTheGradients) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    BrowseContext context;
    QQuickView view;
    view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)}, {QStringLiteral("active"), true}, {QStringLiteral("fullScreen"), false}});
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.resize(900, 700);
    view.loadFromModule("ImageViewer", "ViewerPage");
    view.show();
    QObject* page = view.rootObject();
    auto* image = page->property("view").value<ImageView*>();
    context.openPath(dir.path());
    ASSERT_TRUE(settled(image));
    QMetaObject::invokeMethod(page, "startAdjust");
    auto* panel = page->findChild<QObject*>(QStringLiteral("adjustPanel"));
    auto* input = page->findChild<QQuickItem*>(QStringLiteral("levelsInput"));
    auto* output = page->findChild<QQuickItem*>(QStringLiteral("levelsOutput"));
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);
    ASSERT_TRUE(waitUntil([&] { return image->histogram().isValid(); })) << "the histogram is on while adjusting";
    waitUntil([] { return false; }, 300);  // the panel slides in

    // Positions on a bar for a level, on the row of the handles.
    const auto at = [&](QQuickItem* bar, double level) {
        const double inset = 5.5;
        return bar->mapToScene(QPointF(inset + level / 255 * (bar->width() - 2 * inset), bar->height() - 4)).toPoint();
    };
    const auto drag = [&](QQuickItem* bar, double from, double to) {
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, at(bar, from));
        QTest::mouseMove(&view, at(bar, (from + to) / 2));
        QTest::mouseMove(&view, at(bar, to));
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, at(bar, to));
    };
    drag(input, 0, 51);
    EXPECT_NEAR(panel->property("black").toDouble(), 51, 1);
    // The gamma handle stands halfway between black and white; a quarter of the way is gamma 2.
    const double black = panel->property("black").toDouble();
    drag(input, (black + 255) / 2, black + (255 - black) / 4);
    EXPECT_NEAR(panel->property("gamma").toDouble(), 2, 0.05);
    drag(input, 255, 200);
    EXPECT_NEAR(panel->property("white").toDouble(), 200, 1);
    EXPECT_NEAR(panel->property("gamma").toDouble(), 2, 0.05) << "gamma stays when white moves";
    drag(output, 0, 64);
    EXPECT_NEAR(panel->property("outBlack").toDouble(), 64, 1);
    EXPECT_TRUE(image->colorPreviewTone());

    QTest::mouseDClick(&view, Qt::LeftButton, Qt::NoModifier, at(output, 64));
    EXPECT_EQ(panel->property("outBlack").toDouble(), 0) << "a double click puts it back";
}

namespace {

// A window with one `ToolPanel` of the given properties, holding one row. Any warning of the
// QML engine fails the test.
struct ToolPanelFixture {
    QQmlEngine engine;
    std::unique_ptr<QObject> window;
    QQuickItem* panel = nullptr;

    explicit ToolPanelFixture(const QString& properties) {
        QObject::connect(&engine, &QQmlEngine::warnings, [](const QList<QQmlError>& warnings) {
            for (const QQmlError& warning : warnings) {
                ADD_FAILURE() << warning.toString().toStdString();
            }
        });
        QQmlComponent component(&engine);
        component.setData(QStringLiteral(R"(
            import QtQuick
            import QtQuick.Controls
            import ImageViewer
            ApplicationWindow {
                width: 800; height: 600; visible: true
                ToolPanel {
                    objectName: "panel"
                    animated: false
                    %1
                    Label { objectName: "row"; text: "A row" }
                    Timer { }
                }
            }
        )").arg(properties).toUtf8(), QUrl());
        EXPECT_TRUE(component.isReady()) << component.errorString().toStdString();
        window.reset(component.create());
        panel = window ? window->findChild<QQuickItem*>(QStringLiteral("panel")) : nullptr;
    }

    // The button with this text, if the panel shows one.
    QQuickItem* button(const QString& text) const {
        for (QQuickItem* item : panel->findChildren<QQuickItem*>()) {
            if (item->inherits("QQuickButton") && item->property("text").toString() == text
                && item->isVisible()) {
                return item;
            }
        }
        return nullptr;
    }
};

} // namespace

// Why: every tool's panel is a `ToolPanel`, so what it promises holds for all of them. A tool
// names its apply button, may ask for one more on the left, and hears of each by a signal.
// Input: a revealed panel that asks for "Crop" and "Reset". The truth is which buttons show
// and which signal each click sends.
TEST(ToolPanel, EndsInTheButtonsItsToolAsksFor) {
    ToolPanelFixture f(QStringLiteral(
        R"(revealed: true; title: "A tool"; applyText: "Crop"; applyName: "theApply"; extraText: "Reset")"));
    ASSERT_NE(f.panel, nullptr);
    QSignalSpy applied(f.panel, SIGNAL(applyRequested()));
    QSignalSpy cancelled(f.panel, SIGNAL(cancelRequested()));
    QSignalSpy extra(f.panel, SIGNAL(extraRequested()));

    ASSERT_NE(f.button(QStringLiteral("Crop")), nullptr);
    EXPECT_EQ(f.button(QStringLiteral("Crop")), f.panel->findChild<QQuickItem*>(QStringLiteral("theApply")));
    EXPECT_EQ(f.button(QStringLiteral("Apply")), nullptr);
    for (const char* text : {"Crop", "Cancel", "Reset"}) {
        ASSERT_NE(f.button(QString::fromLatin1(text)), nullptr) << text;
        QMetaObject::invokeMethod(f.button(QString::fromLatin1(text)), "clicked");
    }
    EXPECT_EQ(applied.count(), 1);
    EXPECT_EQ(cancelled.count(), 1);
    EXPECT_EQ(extra.count(), 1);

    // The tool can refuse to apply, as when nothing has changed.
    f.panel->setProperty("applyEnabled", false);
    EXPECT_FALSE(f.button(QStringLiteral("Crop"))->isEnabled());
    // Without a text there is no extra button.
    f.panel->setProperty("extraText", QString());
    EXPECT_EQ(f.button(QStringLiteral("Reset")), nullptr);
    EXPECT_NE(f.button(QStringLiteral("Cancel")), nullptr);
}

// Why: a tool that changes nothing, as the probe, has nothing to apply or cancel.
// Input: a panel with `buttons` off. The truth is that no button shows, and its row still does.
TEST(ToolPanel, HasNoButtonsWhenItsToolChangesNothing) {
    ToolPanelFixture f(QStringLiteral("revealed: true; buttons: false; fills: true"));
    ASSERT_NE(f.panel, nullptr);
    EXPECT_EQ(f.button(QStringLiteral("Apply")), nullptr);
    EXPECT_EQ(f.button(QStringLiteral("Cancel")), nullptr);
    EXPECT_TRUE(f.panel->findChild<QQuickItem*>(QStringLiteral("row"))->isVisible());
}

// Why: the page only says whether a panel is in view; where it stands is the panel's.
// Input: a panel in an 800 px window, hidden and then revealed. The truth is its place: out
// of the window and not drawn, then against the right edge over the whole height, with its
// row inside it below the heading.
TEST(ToolPanel, StandsAtTheRightEdgeWhenRevealed) {
    ToolPanelFixture f(QStringLiteral(R"(title: "A tool")"));
    ASSERT_NE(f.panel, nullptr);
    EXPECT_FALSE(f.panel->isVisible());
    EXPECT_EQ(f.panel->x(), f.panel->parentItem()->width());

    f.panel->setProperty("revealed", true);
    ASSERT_TRUE(waitUntil([&] { return f.panel->isVisible(); }));
    EXPECT_EQ(f.panel->x() + f.panel->width(), f.panel->parentItem()->width());
    EXPECT_EQ(f.panel->height(), f.panel->parentItem()->height());
    QQuickItem* row = f.panel->findChild<QQuickItem*>(QStringLiteral("row"));
    ASSERT_TRUE(waitUntil([&] { return row->width() > 0; }));
    const QRectF place = row->mapRectToItem(f.panel, QRectF(0, 0, row->width(), row->height()));
    EXPECT_TRUE(QRectF(0, 0, f.panel->width(), f.panel->height()).contains(place));
    EXPECT_GT(place.top(), 20) << "the heading comes first";
    EXPECT_LT(place.bottom(), 100) << "rows keep to the top";
}

// Why: Edit > Frame puts frames around the image as an edit, with a colour typed in hex, says
// the size it gives, refuses a frame of nothing, and can be undone. Input: a 40 x 20 JPEG and
// the dialog at its first settings (frames of 1, 12 and 1), the outer colour typed as ff0000;
// then a look with no frames and no shadow. The truth: "framed 68 x 48"; after OK the image is
// 68 x 48, red at its edge and white 2 pixels in; a look of nothing gives a problem; undo
// brings back 40 x 20.
TEST(Editing, TheFrameDialogFramesTheImage) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("small.jpg"));
    JpegOptions options;
    options.width = 40;
    options.height = 20;
    writeJpeg(path, options);
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import ImageViewer
        Window {
            width: 600; height: 500; visible: true
            property alias view: image
            property alias dialog: dialog
            ImageView { id: image; anchors.fill: parent }
            FrameDialog { id: dialog; view: image }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* image = root->property("view").value<ImageView*>();
    auto* dialog = root->property("dialog").value<QObject*>();
    image->setSource(path);
    ASSERT_TRUE(settled(image));

    QMetaObject::invokeMethod(dialog, "open");
    ASSERT_TRUE(waitUntil([&] { return dialog->property("opened").toBool(); }));
    auto* content = dialog->property("contentItem").value<QQuickItem*>();
    QQuickItem* result = findNamed(content, QStringLiteral("frameResult"));
    QQuickItem* outerColour = findNamed(content, QStringLiteral("frameOuterColour"));
    ASSERT_NE(result, nullptr);
    ASSERT_NE(outerColour, nullptr);
    EXPECT_TRUE(result->property("text").toString().endsWith(QStringLiteral("framed 68 × 48 pixels")))
        << result->property("text").toString().toStdString();
    outerColour->setProperty("text", QStringLiteral("ff0000"));
    QMetaObject::invokeMethod(outerColour, "editingFinished");
    EXPECT_EQ(outerColour->property("colour").value<QColor>(), QColor(255, 0, 0));

    QMetaObject::invokeMethod(dialog, "accept");
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 68; }));
    EXPECT_EQ(image->imageHeight(), 48);
    const auto edited = image->editedImage().value<std::shared_ptr<iv::ImageSource>>();
    ASSERT_TRUE(edited);
    const auto pixels = iv::readLevel(*edited, 0, 0, {});
    ASSERT_TRUE(pixels.has_value());
    const auto* edge = reinterpret_cast<const std::uint8_t*>(pixels->row(24));
    EXPECT_EQ(qRgb(edge[2], edge[1], edge[0]), qRgb(255, 0, 0));
    EXPECT_EQ(qRgb(edge[2 * 4 + 2], edge[2 * 4 + 1], edge[2 * 4]), qRgb(255, 255, 255));

    const QVariantMap nothing{{QStringLiteral("frames"), QVariantList{}}, {QStringLiteral("shadow"), 0}};
    EXPECT_FALSE(image->frameProblem(nothing).isEmpty());
    image->undo();
    ASSERT_TRUE(waitUntil([&] { return image->status() == ImageView::Status::Ready && image->imageWidth() == 40; }));
}

namespace {

// A viewer page in a window of its own, showing what is at path once it is ready.
struct AdjustFixture {
    BrowseContext context;
    QQuickView view;
    QObject* page = nullptr;
    ImageView* image = nullptr;
    QObject* panel = nullptr;

    explicit AdjustFixture(const QString& path) {
        view.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                   {QStringLiteral("active"), true},
                                   {QStringLiteral("fullScreen"), false}});
        view.setResizeMode(QQuickView::SizeRootObjectToView);
        view.resize(900, 600);
        view.loadFromModule("ImageViewer", "ViewerPage");
        view.show();
        page = view.rootObject();
        image = page->property("view").value<ImageView*>();
        panel = page->findChild<QObject*>(QStringLiteral("adjustPanel"));
        context.openPath(path);
    }

    QQuickItem* named(const char* name) const { return page->findChild<QQuickItem*>(QLatin1StringView(name)); }
    QQuickItem* in(const char* header, const char* part) const { return findNamed(named(header), QLatin1StringView(part)); }
    // After a frame, so that what has just been shown or hidden is laid out.
    void click(QQuickItem* item) const {
        waitUntil([] { return false; }, 50);
        QTest::mouseClick(const_cast<QQuickView*>(&view), Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    }
};

// The colour a gray of 0.2 in linear light takes under light of that white balance, as 8 bits
// of an RGB with that matrix.
Rgb grayUnder(iv::WhiteBalance balance, const iv::Matrix3& space = iv::rgbToXyz(iv::kSrgbPrimaries)) {
    const auto rgb = iv::whiteBalanceMatrix(balance, space).inverse() * std::array<double, 3>{0.2, 0.2, 0.2};
    const auto eight = [](double c) { return static_cast<std::uint8_t>(std::lround(iv::srgbEncode(c) * 255)); };
    return Rgb{eight(rgb[0]), eight(rgb[1]), eight(rgb[2])};
}

}  // namespace

// Why: Adjust is in sections in the order its stages run, and each shows whether it is in use,
// resets alone, and can be switched off to see whether it helps, losing nothing. Which are
// folded is remembered. Input: the panel over a JPEG; saturation and levels changed, the Oklab
// section folded, switched off and on, and reset; then a new page. The truth: the in-use mark
// following the values even when folded, the preview leaving a section out while its value
// stays, a reset of one section leaving the other, and the fold kept by the new page.
TEST(Adjust, SectionsShowWhatIsInUseResetAloneAndSwitchOff) {
    QTemporaryDir dir;
    writeTiltedJpeg(dir);
    {
        AdjustFixture f(dir.path());
        ASSERT_TRUE(settled(f.image));
        QMetaObject::invokeMethod(f.page, "startAdjust");
        waitUntil([] { return false; }, 300);  // the panel slides in
        QQuickItem* mark = f.in("adjustOklabHeader", "paneHeaderInUse");
        QQuickItem* levelsMark = f.in("adjustLevelsHeader", "paneHeaderInUse");
        ASSERT_NE(mark, nullptr);
        EXPECT_FALSE(mark->isVisible());

        f.panel->setProperty("saturation", 50);
        f.panel->setProperty("black", 40);
        EXPECT_TRUE(mark->isVisible());
        EXPECT_TRUE(levelsMark->isVisible());
        EXPECT_DOUBLE_EQ(f.image->colorPreviewChroma(), 0.5);

        f.click(f.in("adjustOklabHeader", "paneHeaderText"));
        EXPECT_FALSE(f.named("adjustSaturation")->isVisible()) << "folded";
        EXPECT_TRUE(mark->isVisible()) << "in use shows when folded too";

        f.click(f.in("adjustOklabHeader", "paneHeaderSwitch"));
        EXPECT_FALSE(f.panel->property("oklabOn").toBool());
        EXPECT_DOUBLE_EQ(f.image->colorPreviewChroma(), 1) << "left out of the preview";
        EXPECT_DOUBLE_EQ(f.panel->property("saturation").toDouble(), 50) << "but kept";
        EXPECT_TRUE(f.named("adjustOklabHeader")->property("collapsed").toBool()) << "the box does not fold it";
        f.click(f.in("adjustOklabHeader", "paneHeaderSwitch"));
        EXPECT_DOUBLE_EQ(f.image->colorPreviewChroma(), 0.5);

        f.click(f.in("adjustOklabHeader", "paneHeaderReset"));
        EXPECT_DOUBLE_EQ(f.panel->property("saturation").toDouble(), 100);
        EXPECT_FALSE(mark->isVisible());
        EXPECT_DOUBLE_EQ(f.panel->property("black").toDouble(), 40) << "levels are another section";
        QMetaObject::invokeMethod(f.page, "cancelAdjust");
    }
    AdjustFixture again(dir.path());
    EXPECT_TRUE(again.named("adjustOklabHeader")->property("collapsed").toBool());
    EXPECT_FALSE(again.named("adjustLevelsHeader")->property("collapsed").toBool());
    again.click(again.in("adjustOklabHeader", "paneHeaderText"));  // as the other tests expect it
}

// Why: a slider should make about the same change for the same distance everywhere: the
// temperature evenly in mireds, so the warm end, where most corrections are, is not squeezed,
// and an exponent evenly either side of 1. Input: the scales' ends, middles and values beyond
// them. The truth: 2000 K and 15000 K at the ends, the middle at the mean of their mireds (3529
// K, to the step of 50 K: 3550), 6500 K about a fifth from the right; an exponent of 1 halfway
// from 1/4 to 4; values beyond the ends kept at them.
TEST(SliderScale, SpreadsValuesEvenlyAsItsKindSays) {
    iv::SliderScale temperature;
    temperature.setKind(iv::SliderScale::Kind::Reciprocal);
    temperature.setFrom(2000);
    temperature.setTo(15000);
    temperature.setStep(50);
    EXPECT_EQ(temperature.valueAt(0), 2000);
    EXPECT_EQ(temperature.valueAt(1), 15000);
    EXPECT_EQ(temperature.valueAt(0.5), 3550);
    EXPECT_NEAR(temperature.positionOf(6500), 0.8, 0.01);
    EXPECT_EQ(temperature.positionOf(1000), 0);
    EXPECT_EQ(temperature.valueAt(2), 15000);

    iv::SliderScale power;
    power.setKind(iv::SliderScale::Kind::Logarithmic);
    power.setFrom(0.25);
    power.setTo(4);
    power.setStep(0.01);
    EXPECT_DOUBLE_EQ(power.positionOf(1), 0.5);
    EXPECT_DOUBLE_EQ(power.valueAt(0.75), 2);

    iv::SliderScale even;
    even.setFrom(-50);
    even.setTo(50);
    even.setStep(1);
    EXPECT_DOUBLE_EQ(even.positionOf(0), 0.5);
    EXPECT_EQ(even.valueAt(0.123), -38);
}

// Why: the temperature shows kelvins but moves evenly in mireds, so the warm end, where most
// corrections are, is not squeezed; and the edit it makes is applied as previewed. Input: the
// slider's ends and middle, then 3200 K applied to a mid gray. The truth: 2000 K and 15000 K at
// the ends, the middle at the mean of their mireds (3529 K, shown to 50 K), 6500 K about a fifth
// from the right, and the gray made bluer.
TEST(Adjust, TemperatureMovesEvenlyInMiredsAndTungstenMakesItBluer) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("gray.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {{.width = 40, .height = 30}});
    AdjustFixture f(path);
    ASSERT_TRUE(settled(f.image));
    const auto call = [&](const char* name, double value) {
        QVariant result;
        QMetaObject::invokeMethod(f.panel, name, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, value));
        return result.toDouble();
    };
    EXPECT_EQ(call("kelvinAt", 0), 2000);
    EXPECT_EQ(call("kelvinAt", 1), 15000);
    EXPECT_EQ(call("kelvinAt", 0.5), 3550);
    EXPECT_NEAR(call("positionOf", 6500), 0.8, 0.01);

    QMetaObject::invokeMethod(f.page, "startAdjust");
    EXPECT_FALSE(f.image->colorPreviewWhite());
    f.panel->setProperty("temperature", 3200);
    EXPECT_TRUE(f.image->colorPreviewWhite());
    EXPECT_TRUE(f.named("adjustWhiteHeader")->property("inUse").toBool());
    QMetaObject::invokeMethod(f.page, "applyAdjust");
    ASSERT_TRUE(waitUntil([&] { return f.image->status() == ImageView::Status::Ready && f.image->modified(); }));
    QSignalSpy probed(f.image, &ImageView::probed);
    f.image->probe(QPointF(5, 5));
    ASSERT_TRUE(probed.wait());
    const QColor colour = probed.first().first().toMap().value(QStringLiteral("color")).value<QColor>();
    EXPECT_GT(colour.blue(), colour.red() + 10);
}

// Why: Pick gray is how most people will set white balance: click something neutral, or drag
// over it, and it becomes neutral. It reads the picture as it enters Adjust, so picking again
// with white balance already set finds the same; and it refuses what says nothing about the
// light. Input: a picture of a gray under 4000 K light with a magenta tint of 20, beside black;
// a click with a 5-pixel patch, a second click after white balance and levels are set, a drag,
// and a click on the black. The truth: about 4000 K and tint 20 each time (8-bit samples allow
// a little either way), the patch outlined at the size chosen, and a refusal saying it is dark.
TEST(Adjust, PickGrayMakesWhatIsClickedOrDraggedNeutral) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("tinted.tif"));
    const Rgb gray = grayUnder({4000, 20});
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(),
                        {{.width = 60, .height = 40, .pixel = [gray](int x, int) { return x < 40 ? gray : Rgb{1, 1, 1}; }}});
    AdjustFixture f(path);
    ASSERT_TRUE(settled(f.image));
    QMetaObject::invokeMethod(f.page, "startAdjust");
    // The panel slides in and the view narrows; the clicks need it settled.
    ASSERT_TRUE(waitUntil([&] { return f.image->width() < 800; }));
    double width = 0;
    QElapsedTimer still;
    still.start();
    ASSERT_TRUE(waitUntil([&] {
        if (f.image->width() != width) {
            width = f.image->width();
            still.restart();
        }
        return still.elapsed() > 200;
    }));
    for (int i = 0; i < 4; ++i) {
        f.image->zoomIn();
    }
    const QPointF far = f.image->imageToItem(QPointF(60, 40));
    ASSERT_TRUE(f.image->contains(far - QPointF(1, 1))) << "the whole picture in view";
    auto* overlay = f.named("grayPick");
    ASSERT_NE(overlay, nullptr);
    EXPECT_FALSE(overlay->isVisible());
    f.click(f.named("adjustPickGray"));
    ASSERT_TRUE(f.panel->property("picking").toBool());
    EXPECT_TRUE(overlay->isVisible());
    const auto at = [&](double x, double y) { return f.image->mapToScene(f.image->imageToItem(QPointF(x, y))).toPoint(); };
    const auto expectFound = [&](const char* how) {
        EXPECT_NEAR(f.panel->property("temperature").toDouble(), 4000, 80) << how;
        EXPECT_NEAR(f.panel->property("tint").toDouble(), 20, 4) << how;
    };
    QSignalSpy picked(f.image, &ImageView::grayPicked);
    // The result may come while the mouse events are still being handled, so it is counted
    // rather than waited for after them.
    qsizetype seen = 0;
    const auto onePicked = [&] {
        const bool came = waitUntil([&] { return picked.count() > seen; });
        seen = picked.count();
        return came;
    };

    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, at(20.5, 20.5));
    ASSERT_TRUE(onePicked());
    expectFound("a click");
    EXPECT_EQ(overlay->property("patch").toRectF(), QRectF(18, 18, 5, 5));

    f.panel->setProperty("temperature", 9000);
    f.panel->setProperty("black", 30);
    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, at(10.5, 30.5));
    ASSERT_TRUE(onePicked());
    expectFound("again, with white balance and levels set");

    QTest::mousePress(&f.view, Qt::LeftButton, Qt::NoModifier, at(5.5, 5.5));
    QTest::mouseMove(&f.view, at(15.5, 15.5));
    QTest::mouseMove(&f.view, at(30.5, 25.5));
    QTest::mouseRelease(&f.view, Qt::LeftButton, Qt::NoModifier, at(30.5, 25.5));
    ASSERT_TRUE(onePicked());
    expectFound("a drag");
    EXPECT_EQ(overlay->property("patch").toRectF(), QRectF(5, 5, 26, 21));

    QTest::mouseClick(&f.view, Qt::LeftButton, Qt::NoModifier, at(50.5, 20.5));
    ASSERT_TRUE(onePicked());
    expectFound("unchanged by a refusal");
    EXPECT_NE(f.panel->property("pickMessage").toString().indexOf(QStringLiteral("dark")), -1);

    QMetaObject::invokeMethod(f.page, "cancelAdjust");
    EXPECT_FALSE(overlay->isVisible());
    EXPECT_TRUE(overlay->property("patch").toRectF().isEmpty());
}

// Why: Pick gray must make the patch neutral as seen, also in a picture whose RGB is not the
// screen's. The preview works on the picture converted for the screen, so its matrix must be
// the correction carried over into the screen's RGB; with the picture's own, the patch only came
// close to gray. Input: a Display P3 picture, as phones make them, of a gray under 3500 K light
// with a tint of -30, picked; then applied. The truth: the patch's colour in the screen's RGB
// made neutral by the preview's matrix, and the applied picture's patch neutral on screen,
// within a level.
TEST(Adjust, PickGrayIsNeutralOnScreenInAWideGamutPicture) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("p3.tif"));
    const iv::IccBytes p3Profile = iv::profileFromCicp(12, 13);
    const auto p3 = iv::profileRgbToXyz(p3Profile);
    ASSERT_TRUE(p3);
    const Rgb gray = grayUnder({3500, -30}, *p3);
    iv::test::TiffImage tiff{.width = 30, .height = 20};
    tiff.iccProfile = p3Profile;
    tiff.pixel = [gray](int, int) { return gray; };
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {tiff});
    AdjustFixture f(path);
    ASSERT_TRUE(settled(f.image));
    QMetaObject::invokeMethod(f.page, "startAdjust");
    f.panel->setProperty("picking", true);
    QSignalSpy picked(f.image, &ImageView::grayPicked);
    f.image->pickGray(QRectF(10, 5, 5, 5));
    ASSERT_TRUE(picked.count() > 0 || picked.wait());
    EXPECT_NEAR(f.panel->property("temperature").toDouble(), 3500, 60);
    EXPECT_NEAR(f.panel->property("tint").toDouble(), -30, 3);

    // The patch as the screen (sRGB here) gets it, through the preview's matrix.
    const auto linear = [](std::uint8_t v) { return iv::srgbDecode(v / 255.0); };
    const iv::Matrix3 srgb = iv::rgbToXyz(iv::kSrgbPrimaries);
    const auto onScreen = srgb.inverse() * *p3 * std::array<double, 3>{linear(gray.r), linear(gray.g), linear(gray.b)};
    const QMatrix4x4 m = f.image->colorPreviewWhiteMatrix();
    const QVector3D shown = m.map(QVector3D(float(onScreen[0]), float(onScreen[1]), float(onScreen[2])));
    EXPECT_NEAR(shown.x(), shown.y(), 0.01 * shown.y());
    EXPECT_NEAR(shown.y(), shown.z(), 0.01 * shown.y());

    QMetaObject::invokeMethod(f.page, "applyAdjust");
    ASSERT_TRUE(waitUntil([&] { return f.image->status() == ImageView::Status::Ready && f.image->modified(); }));
    QSignalSpy probed(f.image, &ImageView::probed);
    f.image->probe(QPointF(12, 7));
    ASSERT_TRUE(probed.wait());
    const QColor colour = probed.first().first().toMap().value(QStringLiteral("color")).value<QColor>();
    EXPECT_NEAR(colour.red(), colour.green(), 1);
    EXPECT_NEAR(colour.green(), colour.blue(), 1);
}
