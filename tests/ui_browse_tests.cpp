#include "ui_test_support.h"

TEST(BrowseContext, SortsNaturallyAndNavigates) {
    QTemporaryDir dir;
    for (const char* name : {"img10.jpg", "img2.JPG", "img1.jpeg"}) {
        writeJpeg(dir.filePath(QString::fromLatin1(name)));
    }
    QFile notes(dir.filePath(QStringLiteral("notes.txt")));
    ASSERT_TRUE(notes.open(QIODevice::WriteOnly));
    notes.close();

    BrowseContext context;
    context.openPath(dir.filePath(QStringLiteral("img2.JPG")));
    EXPECT_EQ(context.currentName(), QStringLiteral("img2.JPG"));  // known before the scan ends

    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 3; }));
    EXPECT_EQ(context.currentIndex(), 1);
    EXPECT_TRUE(context.hasPrevious());
    EXPECT_TRUE(context.hasNext());

    context.next();
    EXPECT_EQ(context.currentName(), QStringLiteral("img10.jpg"));
    EXPECT_FALSE(context.hasNext());
    context.next();
    EXPECT_EQ(context.currentName(), QStringLiteral("img10.jpg"));

    context.previous();
    context.previous();
    EXPECT_EQ(context.currentName(), QStringLiteral("img1.jpeg"));
    EXPECT_FALSE(context.hasPrevious());
}

TEST(BrowseContext, OpeningADirectoryShowsItsFirstImage) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("b.jpg")));
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));

    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 2; }));
    EXPECT_EQ(context.currentName(), QStringLiteral("a.jpg"));
    EXPECT_EQ(context.currentIndex(), 0);
}

TEST(BrowseContext, TwoContextsAreIndependent) {
    QTemporaryDir first, second;
    writeJpeg(first.filePath(QStringLiteral("one.jpg")));
    writeJpeg(second.filePath(QStringLiteral("two.jpg")));

    BrowseContext a, b;
    a.openPath(first.path());
    b.openPath(second.path());
    ASSERT_TRUE(waitUntil([&] { return a.fileCount() == 1 && b.fileCount() == 1; }));
    EXPECT_EQ(a.currentName(), QStringLiteral("one.jpg"));
    EXPECT_EQ(b.currentName(), QStringLiteral("two.jpg"));
}

TEST(BrowseContext, ExposesFilesAsAModel) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    writeJpeg(dir.filePath(QStringLiteral("b.jpg")));

    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 2; }));

    QAbstractItemModel* model = context.files();
    ASSERT_EQ(model->rowCount(), 2);
    const QHash<int, QByteArray> roles = model->roleNames();
    const auto value = [&](int row, const QByteArray& role) {
        return model->data(model->index(row, 0), roles.key(role)).toString();
    };
    EXPECT_EQ(value(1, "name"), QStringLiteral("b.jpg"));
    EXPECT_EQ(value(1, "path"), dir.filePath(QStringLiteral("b.jpg")));
    EXPECT_TRUE(value(1, "thumbnail").startsWith(QStringLiteral("image://thumbnails/")));

    context.setCurrentIndex(1);
    EXPECT_EQ(context.currentName(), QStringLiteral("b.jpg"));
    context.setCurrentIndex(99);
    EXPECT_EQ(context.currentIndex(), 1);
    context.setCurrentIndex(-1);
    EXPECT_EQ(context.currentIndex(), 1);
}

TEST(BrowseContext, EmptyDirectoryHasNoCurrentImage) {
    QTemporaryDir dir;
    BrowseContext context;
    context.openPath(dir.path());
    QCoreApplication::processEvents();
    QThread::msleep(50);
    QCoreApplication::processEvents();
    EXPECT_EQ(context.fileCount(), 0);
    EXPECT_TRUE(context.currentPath().isEmpty());
    EXPECT_FALSE(context.hasNext());
}

TEST(BrowseContext, BreadcrumbsWalkFromTheRoot) {
    QTemporaryDir dir;
    const QString nested = dir.filePath(QStringLiteral("a b/c"));
    ASSERT_TRUE(QDir().mkpath(nested));

    BrowseContext context;
    context.openPath(nested);
    const QVariantList crumbs = context.breadcrumbs();
    ASSERT_GE(crumbs.size(), 3);
    EXPECT_EQ(crumbs.first().toMap().value("path").toString(), QDir::rootPath());
    EXPECT_EQ(crumbs.last().toMap().value("name").toString(), QStringLiteral("c"));
    EXPECT_EQ(crumbs.last().toMap().value("path").toString(), nested);
    EXPECT_EQ(crumbs.at(crumbs.size() - 2).toMap().value("name").toString(), QStringLiteral("a b"));

    context.openPath(QDir::rootPath());
    ASSERT_EQ(context.breadcrumbs().size(), 1);
    EXPECT_EQ(context.breadcrumbs().first().toMap().value("path").toString(), QDir::rootPath());
}

TEST(DirectoryTreeModel, ShowsOneColumnAndMapsPaths) {
    QTemporaryDir dir;
    iv::DirectoryTreeModel model;
    EXPECT_EQ(model.columnCount(), 1);
    const QModelIndex index = model.indexForPath(dir.path());
    ASSERT_TRUE(index.isValid());
    EXPECT_EQ(model.pathForIndex(index), dir.path());
}

// A folder with a visible and a hidden subfolder, and a hidden file, for the tests below: a dot
// name on Linux, the hidden attribute too on Windows.
static void makeHiddenEntries(const QTemporaryDir& dir) {
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("shown")));
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral(".hidden")));
    hideOnWindows(dir.filePath(QStringLiteral(".hidden")));
    touchFile(dir.filePath(QStringLiteral(".secret.jpg")), 10);
    hideOnWindows(dir.filePath(QStringLiteral(".secret.jpg")));
}

// Why: hidden folders show in the folder tree only when asked, and turning that on or off shows
// or drops them at once, without reopening the folder. Input: makeHiddenEntries. The truth is
// the names of the folder's rows in the model.
TEST(DirectoryTreeModel, ShowsHiddenFoldersOnlyWhenAsked) {
    QTemporaryDir dir;
    makeHiddenEntries(dir);
    iv::DirectoryTreeModel model;
    const auto names = [&] {
        const QModelIndex parent = model.indexForPath(dir.path());
        QStringList list;
        for (int row = 0; row < model.rowCount(parent); ++row) {
            list << model.fileName(model.index(row, 0, parent));
        }
        list.sort();
        return list;
    };
    QSignalSpy loaded(&model, &QFileSystemModel::directoryLoaded);
    model.fetchMore(model.indexForPath(dir.path()));
    ASSERT_TRUE(waitUntil([&] { return names() == QStringList{QStringLiteral("shown")}; }));
    EXPECT_FALSE(model.showHidden());

    QSignalSpy changed(&model, &iv::DirectoryTreeModel::showHiddenChanged);
    model.setShowHidden(true);
    EXPECT_EQ(changed.count(), 1);
    EXPECT_TRUE(waitUntil([&] { return names() == (QStringList{QStringLiteral(".hidden"), QStringLiteral("shown")}); }))
        << names().join(u',').toStdString();
    model.setShowHidden(false);
    EXPECT_TRUE(waitUntil([&] { return names() == QStringList{QStringLiteral("shown")}; }));
}

TEST(ThumbnailProvider, DeliversThumbnailsForAnyFileName) {
    QTemporaryDir dir;
#ifdef Q_OS_WIN
    const QString path = dir.filePath(QStringLiteral("100% #1 é.jpg"));  // ? is not allowed there
#else
    const QString path = dir.filePath(QStringLiteral("100% #1 ?é.jpg"));
#endif
    JpegOptions options;
    options.width = 1000;
    options.height = 500;
    writeJpeg(path, options);

    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(path), {}));
    bool finished = false;
    QObject::connect(response.get(), &QQuickImageResponse::finished, response.get(),
                     [&] { finished = true; });
    ASSERT_TRUE(waitUntil([&] { return finished; }));
    EXPECT_TRUE(response->errorString().isEmpty()) << response->errorString().toStdString();

    std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
    ASSERT_TRUE(factory);
    EXPECT_EQ(factory->textureSize(), QSize(256, 128));
}

TEST(ThumbnailProvider, MakesTheRequestedSize) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    JpegOptions options;
    options.width = 1000;
    options.height = 500;
    writeJpeg(path, options);
    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(path), {164, 164}));
    bool finished = false;
    QObject::connect(response.get(), &QQuickImageResponse::finished, response.get(), [&] { finished = true; });
    ASSERT_TRUE(waitUntil([&] { return finished; }));
    std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
    ASSERT_TRUE(factory);
    EXPECT_EQ(factory->textureSize(), QSize(164, 82));
}

// Rectangular presets must bound portrait pictures by height and landscape pictures by
// width. Requests with a shared longest edge may reuse the cached source independently.
TEST(ThumbnailProvider, RectangularBoundsFitPortraitAndLandscapePicturesWithoutUpscaling) {
    QTemporaryDir dir;
    iv::ThumbnailProvider provider;
    const auto request = [&](const QString& path, QSize bounds) {
        std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(path), bounds));
        QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
        EXPECT_TRUE(waitUntil([&] { return !finished.isEmpty() || !response->errorString().isEmpty(); }));
        EXPECT_TRUE(response->errorString().isEmpty());
        std::unique_ptr<QQuickTextureFactory> factory(response->textureFactory());
        return factory ? factory->textureSize() : QSize();
    };
    const QString portrait = dir.filePath("portrait.jpg");
    const QString landscape = dir.filePath("landscape.jpg");
    const QString small = dir.filePath("small.jpg");
    writeJpeg(portrait, {.width = 500, .height = 1000});
    writeJpeg(landscape, {.width = 1000, .height = 500});
    writeJpeg(small, {.width = 32, .height = 16});
    EXPECT_EQ(request(portrait, QSize(320, 240)), QSize(120, 240));
    EXPECT_EQ(request(portrait, QSize(320, 320)), QSize(160, 320));
    EXPECT_EQ(request(landscape, QSize(320, 240)), QSize(320, 160));
    EXPECT_EQ(request(landscape, QSize(240, 320)), QSize(240, 120));
    EXPECT_EQ(request(small, QSize(320, 240)), QSize(32, 16));
}

TEST(ThumbnailProvider, ReportsUnreadableFiles) {
    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(QStringLiteral("/does/not/exist.jpg")), {}));
    // This fails so fast that finished() can come before anyone could connect to it, which
    // Qt allows for when it loads images, so look at the result instead.
    EXPECT_TRUE(waitUntil([&] { return !response->errorString().isEmpty(); }));
}

// Qt deletes a response only after finished(), so a cancelled one must still emit it.
TEST(ThumbnailProvider, CancelledResponsesFinishExactlyOnce) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    JpegOptions options;
    options.width = 2000;
    options.height = 2000;
    writeJpeg(path, options);

    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(path), {}));
    int finishedCount = 0;
    QObject::connect(response.get(), &QQuickImageResponse::finished, response.get(),
                     [&] { ++finishedCount; });
    response->cancel();
    ASSERT_TRUE(waitUntil([&] { return finishedCount > 0; }));

    // Give a task that was already running the time to try finishing a second time.
    QThread::msleep(300);
    QCoreApplication::processEvents();
    EXPECT_EQ(finishedCount, 1);
    EXPECT_FALSE(response->errorString().isEmpty());
}

TEST(ThumbnailProvider, DestroyingPendingResponsesIsSafe) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    JpegOptions options;
    options.width = 3000;
    options.height = 3000;
    writeJpeg(path, options);

    const QString id = thumbnailId(path);
    iv::ThumbnailProvider provider;
    for (int i = 0; i < 50; ++i) {
        std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(id, {}));
        if (i % 2 == 0) {
            response->cancel();
        }
    }
    QThread::msleep(100);
    SUCCEED();
}

TEST(Selection, StartsWithTheOpenedImage) {
    SelectionFixture f;
    EXPECT_EQ(f.names(), QStringLiteral("a"));
    EXPECT_EQ(f.context.currentIndex(), 0);

    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("x.jpg")));
    writeJpeg(dir.filePath(QStringLiteral("y.jpg")));
    BrowseContext context;
    context.openPath(dir.filePath(QStringLiteral("y.jpg")));
    ASSERT_TRUE(waitUntil([&] { return context.selectedCount() == 1; }));
    EXPECT_EQ(QFileInfo(context.selectedPaths().front()).fileName(), QStringLiteral("y.jpg"));
}

TEST(Selection, PlainClickSelectsOnlyThatFile) {
    SelectionFixture f;
    f.context.selectAll();
    EXPECT_EQ(f.context.selectedCount(), 6);
    f.context.clickFile(2, false, false);
    EXPECT_EQ(f.names(), QStringLiteral("c"));
    EXPECT_EQ(f.context.currentIndex(), 2);
    // Also when it is the current one already, where only the selection changes.
    f.context.selectAll();
    f.context.clickFile(2, false, false);
    EXPECT_EQ(f.names(), QStringLiteral("c"));
}

TEST(Selection, CtrlTogglesAndMovesTheCurrentFile) {
    SelectionFixture f;
    f.context.clickFile(2, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ac"));
    EXPECT_EQ(f.context.currentIndex(), 2);
    f.context.clickFile(4, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ace"));
    f.context.clickFile(2, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ae"));
    EXPECT_EQ(f.context.currentIndex(), 2);  // current without being selected
}

TEST(Selection, ShiftSelectsARangeFromTheAnchorInBothDirections) {
    SelectionFixture f;
    f.context.clickFile(2, false, false);
    f.context.clickFile(4, false, true);
    EXPECT_EQ(f.names(), QStringLiteral("cde"));
    EXPECT_EQ(f.context.currentIndex(), 4);
    // The anchor stays, so the range turns around it instead of growing.
    f.context.clickFile(0, false, true);
    EXPECT_EQ(f.names(), QStringLiteral("abc"));
    f.context.clickFile(2, false, true);
    EXPECT_EQ(f.names(), QStringLiteral("c"));
}

TEST(Selection, ShiftRangeStartsAtTheLastCtrlClick) {
    SelectionFixture f;
    f.context.clickFile(3, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ad"));
    f.context.clickFile(5, false, true);  // replaces, as plain shift does
    EXPECT_EQ(f.names(), QStringLiteral("def"));
}

TEST(Selection, CtrlShiftAddsARange) {
    SelectionFixture f;
    f.context.clickFile(4, true, false);
    EXPECT_EQ(f.names(), QStringLiteral("ae"));
    f.context.clickFile(5, true, true);
    EXPECT_EQ(f.names(), QStringLiteral("aef"));
}

TEST(Selection, KeyboardExtendsFromTheAnchor) {
    SelectionFixture f;
    f.context.clickFile(2, false, false);
    f.context.extendSelectionTo(3);
    f.context.extendSelectionTo(4);
    EXPECT_EQ(f.names(), QStringLiteral("cde"));
    EXPECT_EQ(f.context.currentIndex(), 4);
    f.context.extendSelectionTo(3);
    EXPECT_EQ(f.names(), QStringLiteral("cd"));
    f.context.extendSelectionTo(1);
    EXPECT_EQ(f.names(), QStringLiteral("bc"));
    // Past either end nothing happens.
    f.context.extendSelectionTo(-1);
    f.context.extendSelectionTo(99);
    EXPECT_EQ(f.names(), QStringLiteral("bc"));
    EXPECT_EQ(f.context.currentIndex(), 1);
}

TEST(Selection, MovingToAnotherImageSelectsOnlyIt) {
    SelectionFixture f;
    f.context.selectAll();
    f.context.next();
    EXPECT_EQ(f.names(), QStringLiteral("b"));
    f.context.selectAll();
    f.context.setCurrentIndex(4);
    EXPECT_EQ(f.names(), QStringLiteral("e"));
    // And the anchor moves along, for a range started from the keyboard.
    f.context.extendSelectionTo(5);
    EXPECT_EQ(f.names(), QStringLiteral("ef"));
}

TEST(Selection, ClearingKeepsTheCurrentImage) {
    SelectionFixture f;
    f.context.clickFile(3, false, false);
    f.context.clearSelection();
    EXPECT_EQ(f.context.selectedCount(), 0);
    EXPECT_EQ(f.context.currentIndex(), 3);
    EXPECT_FALSE(f.context.currentPath().isEmpty());
}

TEST(Selection, ReachesTheModelAndNotifiesOnlyOnChange) {
    SelectionFixture f;
    QAbstractItemModel* model = f.context.files();
    const int role = model->roleNames().key("selected");
    int changes = 0;
    QObject::connect(&f.context, &BrowseContext::selectionChanged, [&] { ++changes; });
    QList<int> changedRows;
    QObject::connect(model, &QAbstractItemModel::dataChanged, [&](const QModelIndex& first, const QModelIndex& last, const QList<int>& roles) {
        EXPECT_EQ(roles, QList<int>{role});
        for (int row = first.row(); row <= last.row(); ++row) {
            changedRows << row;
        }
    });

    f.context.clickFile(1, true, false);
    EXPECT_EQ(changes, 1);
    EXPECT_EQ(changedRows, QList<int>{1});
    EXPECT_TRUE(model->data(model->index(1, 0), role).toBool());
    EXPECT_FALSE(model->data(model->index(2, 0), role).toBool());

    f.context.selectAll();
    EXPECT_EQ(changes, 2);
    f.context.selectAll();
    EXPECT_EQ(changes, 2);  // nothing changed
}

TEST(Selection, DoesNotSurviveAnotherFolder) {
    SelectionFixture f;
    f.context.selectAll();
    QTemporaryDir other;
    writeJpeg(other.filePath(QStringLiteral("only.jpg")));
    f.context.openPath(other.path());
    EXPECT_EQ(f.context.selectedCount(), 0);  // until the new folder is listed
    ASSERT_TRUE(waitUntil([&] { return f.context.selectedCount() == 1; }));
    EXPECT_EQ(QFileInfo(f.context.selectedPaths().front()).fileName(), QStringLiteral("only.jpg"));
}

TEST(Selection, IgnoresFilesThatDoNotExistAndEmptyFolders) {
    SelectionFixture f;
    f.context.clickFile(-1, false, false);
    f.context.clickFile(99, true, true);
    EXPECT_EQ(f.names(), QStringLiteral("a"));

    QTemporaryDir empty;
    BrowseContext context;
    context.openPath(empty.path());
    ASSERT_TRUE(waitUntil([&] { return context.directory() == QFileInfo(empty.path()).absoluteFilePath(); }));
    context.selectAll();
    context.clearSelection();
    context.extendSelectionTo(0);
    context.clickFile(0, false, false);
    EXPECT_EQ(context.selectedCount(), 0);
    EXPECT_TRUE(context.selectedPaths().isEmpty());
}

TEST(BrowseContext, ListsTiffFiles) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    iv::test::writeTiff(QFileInfo(dir.filePath(QStringLiteral("b.tif"))).filesystemFilePath(), {TiffImage{}});
    iv::test::writeTiff(QFileInfo(dir.filePath(QStringLiteral("c.TIFF"))).filesystemFilePath(), {TiffImage{}});
    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 3; }));
}

TEST(History, GoesBackAndForwardAndRemembersTheImage) {
    FileOpsFixture f(3);
    const QString first = f.dir.path();
    const QString second = f.other.path();
    writeJpeg(f.otherPath(QStringLiteral("x.jpg")));
    f.context.clickFile(2, false, false);
    EXPECT_FALSE(f.context.canGoBack());

    f.context.openPath(second);
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 1; }));
    EXPECT_TRUE(f.context.canGoBack());
    EXPECT_FALSE(f.context.canGoForward());

    f.context.goBack();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 3 && f.context.currentIndex() == 2; }));
    EXPECT_EQ(f.context.directory(), first);
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
    EXPECT_FALSE(f.context.canGoBack());
    EXPECT_TRUE(f.context.canGoForward());

    f.context.goForward();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 1; }));
    EXPECT_EQ(f.context.directory(), second);
    EXPECT_FALSE(f.context.canGoForward());
}

TEST(History, ANewFolderForgetsForwardAndTheSameFolderIsNotRecorded) {
    FileOpsFixture f(2);
    f.context.openPath(f.path(QStringLiteral("b.jpg")));
    EXPECT_FALSE(f.context.canGoBack()) << "another image of the same folder";
    f.context.openPath(f.other.path());
    f.context.goBack();
    EXPECT_TRUE(f.context.canGoForward());
    QTemporaryDir third;
    f.context.openPath(third.path());
    EXPECT_FALSE(f.context.canGoForward());
}

TEST(History, PassesOverFoldersThatAreGoneAndFollowsARename) {
    FileOpsFixture f(1);
    const QDir base(f.other.path());
    ASSERT_TRUE(base.mkpath(QStringLiteral("gone")));
    ASSERT_TRUE(base.mkpath(QStringLiteral("old")));
    f.context.openPath(base.filePath(QStringLiteral("old")));
    f.context.openPath(base.filePath(QStringLiteral("gone")));
    f.context.openPath(f.dir.path());

    ASSERT_TRUE(QDir(base.filePath(QStringLiteral("gone"))).removeRecursively());
    EXPECT_TRUE(f.context.rename(base.filePath(QStringLiteral("old")), QStringLiteral("new")).isEmpty());
    f.settle([] { return true; });

    f.context.goBack();
    EXPECT_EQ(f.context.directory(), base.filePath(QStringLiteral("new")));
}

TEST(History, UpGoesToTheParentAndStopsAtTheRoot) {
    FileOpsFixture f(1);
    const QString parent = QFileInfo(f.dir.path()).absolutePath();
    f.context.goUp();
    EXPECT_EQ(f.context.directory(), parent);
    f.context.goBack();
    EXPECT_EQ(f.context.directory(), f.dir.path());
    f.context.openPath(QDir::rootPath());
    EXPECT_FALSE(f.context.canGoUp());
    const QString root = f.context.directory();
    f.context.goUp();
    EXPECT_EQ(f.context.directory(), root);
}

TEST(TypedPath, UnderstandsWhatPeoplePaste) {
    FileOpsFixture f(2);
    const QString sub = f.path(QStringLiteral("sub dir"));
    ASSERT_TRUE(QDir().mkpath(sub));
    const auto opens = [&](const QString& text, const QString& directory) {
        f.context.openPath(f.dir.path());
        EXPECT_EQ(f.context.openTyped(text), QString()) << text.toStdString();
        EXPECT_EQ(f.context.directory(), directory) << text.toStdString();
    };
    opens(sub, sub);
    opens(sub + QLatin1Char('/'), sub);
    opens(QStringLiteral("  ") + sub + QStringLiteral("\n"), sub);
    opens(QLatin1Char('"') + sub + QLatin1Char('"'), sub);
    opens(QLatin1Char('\'') + sub + QLatin1Char('\''), sub);
    opens(QUrl::fromLocalFile(sub).toString(), sub);
    opens(QUrl::fromLocalFile(sub).toString(QUrl::FullyEncoded), sub);
    opens(QStringLiteral("sub dir"), sub);
    opens(QStringLiteral("sub dir/.."), f.dir.path());
    opens(QStringLiteral("~"), QDir::homePath());
}

TEST(TypedPath, AFileOpensItsFolderWithThatImage) {
    FileOpsFixture f(3);
    f.context.openPath(f.other.path());
    EXPECT_EQ(f.context.openTyped(f.path(QStringLiteral("b.jpg"))), QString());
    EXPECT_EQ(f.context.directory(), f.dir.path());
    EXPECT_EQ(f.context.currentName(), QStringLiteral("b.jpg"));
}

TEST(TypedPath, ProblemsLeaveTheFolderAlone) {
    FileOpsFixture f(1);
    const QString before = f.context.directory();
    for (const QString& text : {QString(), QStringLiteral("   "), f.path(QStringLiteral("missing")), QStringLiteral("https://example.com/a"), QStringLiteral("file://host/share/a")}) {
        EXPECT_FALSE(f.context.openTyped(text).isEmpty()) << text.toStdString();
        EXPECT_EQ(f.context.directory(), before);
    }
    EXPECT_FALSE(f.context.canGoBack());
}

TEST(SubfolderList, ListsFoldersOnlyInNaturalOrderAndFollowsThePath) {
    QTemporaryDir dir;
    for (const char* name : {"b10", "b9", "A", ".hidden"}) {
        ASSERT_TRUE(QDir(dir.path()).mkpath(QString::fromLatin1(name)));
    }
    hideOnWindows(dir.filePath(QStringLiteral(".hidden")));
    writeJpeg(dir.filePath(QStringLiteral("file.jpg")));

    iv::SubfolderList list;
    list.setPath(dir.path());
    EXPECT_TRUE(list.loading());
    ASSERT_TRUE(waitUntil([&] { return !list.loading(); }));
    EXPECT_EQ(list.folders(), (QStringList{QStringLiteral("A"), QStringLiteral("b9"), QStringLiteral("b10")}));

    list.setPath(QDir(dir.path()).filePath(QStringLiteral("A")));
    EXPECT_TRUE(list.folders().isEmpty()) << "nothing of the previous folder shows";
    ASSERT_TRUE(waitUntil([&] { return !list.loading(); }));
    EXPECT_TRUE(list.folders().isEmpty());

    list.setPath(dir.filePath(QStringLiteral("missing")));
    ASSERT_TRUE(waitUntil([&] { return !list.loading(); }));
    EXPECT_TRUE(list.folders().isEmpty());
}

// Why: the path bar's subfolder menu lists hidden folders only when asked, and reads the folder
// again when that changes. Input: makeHiddenEntries. The truth is the list of names.
TEST(SubfolderList, ListsHiddenFoldersOnlyWhenAsked) {
    QTemporaryDir dir;
    makeHiddenEntries(dir);
    iv::SubfolderList list;
    list.setPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return !list.loading(); }));
    EXPECT_EQ(list.folders(), QStringList{QStringLiteral("shown")});

    list.setShowHidden(true);
    ASSERT_TRUE(waitUntil([&] { return !list.loading(); }));
    EXPECT_EQ(list.folders(), (QStringList{QStringLiteral(".hidden"), QStringLiteral("shown")}));
}

TEST(SubfolderList, DestroyingWhileLoadingIsSafe) {
    QTemporaryDir dir;
    for (int i = 0; i < 20; ++i) {
        iv::SubfolderList list;
        list.setPath(dir.path());
    }
}

TEST(BrowseContext, SummarizesFoldersFilesAndSelection) {
    QTemporaryDir dir;
    iv::test::writeFile(QFileInfo(dir.filePath(QStringLiteral("big.jpg"))).filesystemFilePath(), std::vector<std::uint8_t>(5000, 0));
    iv::test::writeFile(QFileInfo(dir.filePath(QStringLiteral("small.jpg"))).filesystemFilePath(), std::vector<std::uint8_t>(1000, 0));
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("one")));
    BrowseContext context;
    EXPECT_EQ(context.summary(), QString());
    QSignalSpy changes(&context, &BrowseContext::summaryChanged);
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 2; }));
    EXPECT_GT(changes.count(), 0);
    EXPECT_EQ(context.summary(), QStringLiteral("1 directory | 2 files (6 kB) | 1 selected (5 kB)"));

    context.clearSelection();
    EXPECT_EQ(context.summary(), QStringLiteral("1 directory | 2 files (6 kB)"));
    context.selectAll();
    EXPECT_EQ(context.summary(), QStringLiteral("1 directory | 2 files (6 kB) | 2 selected (6 kB)"));

    // Folders change on their own, with no image coming or going.
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("two")));
    context.refresh();
    EXPECT_TRUE(waitUntil([&] { return context.summary().startsWith(QStringLiteral("2 directories | ")); }));
}

// Why: the browser's status line counts the folders that can be seen, so a hidden folder counts
// only while hidden folders are shown, and a hidden image is never listed. Input:
// makeHiddenEntries. The truth is the summary's text.
TEST(BrowseContext, CountsHiddenFoldersOnlyWhenTheyAreShown) {
    QTemporaryDir dir;
    makeHiddenEntries(dir);
    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.summary().startsWith(QStringLiteral("1 directory | 0 files")); }))
        << context.summary().toStdString();
    context.setShowHiddenFolders(true);
    EXPECT_TRUE(waitUntil([&] { return context.summary().startsWith(QStringLiteral("2 directories | 0 files")); }))
        << context.summary().toStdString();
}

TEST(BrowseContext, JumpsBackThroughTheHistoryAtOnce) {
    QTemporaryDir dir;
    const QDir root(dir.path());
    for (const char* name : {"a", "b", "c", "gone", "d"}) {
        ASSERT_TRUE(root.mkpath(QString::fromLatin1(name)));
    }
    BrowseContext context;
    const auto path = [&](const char* name) { return root.filePath(QString::fromLatin1(name)); };
    for (const char* name : {"a", "b", "c", "gone", "d"}) {
        context.openPath(path(name));
    }
    EXPECT_EQ(context.backFolders(), (QStringList{path("gone"), path("c"), path("b"), path("a")}));
    ASSERT_TRUE(QDir(path("gone")).removeRecursively());
    EXPECT_EQ(context.backFolders(), (QStringList{path("c"), path("b"), path("a")})) << "not one that went away";

    context.goBackBy(2);  // to b, past c and the one that is gone
    EXPECT_EQ(context.directory(), path("b"));
    EXPECT_EQ(context.backFolders(), QStringList{path("a")});

    // Forward returns through what was passed, the nearest first.
    context.goForward();
    EXPECT_EQ(context.directory(), path("c"));
    context.goForward();
    EXPECT_EQ(context.directory(), path("d"));
    EXPECT_FALSE(context.canGoForward());

    // Asking for more than there is goes to the oldest.
    context.goBackBy(100);
    EXPECT_EQ(context.directory(), path("a"));
    EXPECT_FALSE(context.canGoBack());
}

TEST(FolderLists, KeepsFavoritesAndRecentFoldersBetweenRuns) {
    QSettings().remove(QStringLiteral("Folders"));
    {
        iv::FolderLists lists;
        lists.addFavorite(QStringLiteral("/a"));
        lists.addFavorite(QStringLiteral("/b"));
        lists.addFavorite(QStringLiteral("/c"));
        lists.addFavorite(QStringLiteral("/a"));
        EXPECT_EQ(lists.favorites(), (QStringList{"/a", "/b", "/c"}));
        lists.moveFavorite(2, 1);
        EXPECT_EQ(lists.favorites(), (QStringList{"/a", "/c", "/b"}));
        lists.moveFavorite(0, -1);
        lists.moveFavorite(2, 3);
        EXPECT_EQ(lists.favorites(), (QStringList{"/a", "/c", "/b"}));
        lists.removeFavorite(QStringLiteral("/c"));
        lists.removeFavorite(QStringLiteral("/b"));

        lists.noteVisited(QStringLiteral("/x"));
        lists.noteVisited(QStringLiteral("/y"));
        lists.noteVisited(QStringLiteral("/x"));
        EXPECT_EQ(lists.recent(), (QStringList{"/x", "/y"}));
    }
    {
        iv::FolderLists lists;
        EXPECT_EQ(lists.favorites(), QStringList{"/a"}) << "a single one is still a list";
        EXPECT_EQ(lists.recent(), (QStringList{"/x", "/y"}));
        for (int i = 0; i < 30; ++i) {
            lists.noteVisited(QStringLiteral("/%1").arg(i));
        }
        EXPECT_EQ(lists.recent().size(), 12);
        EXPECT_EQ(lists.recent().front(), QStringLiteral("/29"));
    }
    QSettings().remove(QStringLiteral("Folders"));
}

TEST(ThumbnailStore, MeasuresAndClearsTheThumbnailsOnDisk) {
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("a.jpg")));
    iv::ThumbnailStore store;
    QSignalSpy measured(&store, &iv::ThumbnailStore::measured);
    QSignalSpy cleared(&store, &iv::ThumbnailStore::cleared);
    const auto measure = [&] {
        measured.clear();
        store.measure();
        EXPECT_TRUE(waitUntil([&] { return !measured.isEmpty(); }));
        return measured.isEmpty() ? QVariantList() : measured.first();
    };

    store.clear();
    ASSERT_TRUE(waitUntil([&] { return !cleared.isEmpty(); }));
    EXPECT_TRUE(cleared.takeFirst().at(0).toBool());
    EXPECT_EQ(measure().value(1).toDouble(), 0);

    // A thumbnail made through the provider is kept.
    iv::ThumbnailProvider provider;
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(thumbnailId(dir.filePath(QStringLiteral("a.jpg"))), QSize(64, 64)));
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    ASSERT_TRUE(finished.wait(5000));
    const QVariantList some = measure();
    EXPECT_TRUE(some.value(0).toBool());
    EXPECT_EQ(some.value(1).toDouble(), 1);
    EXPECT_GT(some.value(2).toDouble(), 0);

    store.clear();
    ASSERT_TRUE(waitUntil([&] { return !cleared.isEmpty(); }));
    EXPECT_TRUE(cleared.takeFirst().at(0).toBool());
    EXPECT_EQ(measure().value(1).toDouble(), 0);
}

// Ordinary usage reads must never request confirmation. Only explicitly tagged owners
// receive a clear request; deleting an owner while a read is pending cancels that prompt.
TEST(ThumbnailStore, UsageMeasurementAndClearRequestsHaveSeparateOwners) {
    iv::ThumbnailStore store;
    QSignalSpy measured(&store, &iv::ThumbnailStore::measured);
    QSignalSpy requests(&store, &iv::ThumbnailStore::clearRequested);
    store.measure();
    ASSERT_TRUE(waitUntil([&] { return !store.busy(); }));
    EXPECT_FALSE(measured.isEmpty());
    EXPECT_TRUE(store.measuredOnce());
    EXPECT_TRUE(requests.isEmpty());
    QObject first, second;
    store.requestClear(&first);
    store.requestClear(&second);
    ASSERT_TRUE(waitUntil([&] { return requests.size() == 2; }));
    EXPECT_EQ(requests[0][0].value<QObject*>(), &first);
    EXPECT_EQ(requests[1][0].value<QObject*>(), &second);
    requests.clear();
    {
        QObject temporary;
        store.requestClear(&temporary);
    }
    ASSERT_TRUE(waitUntil([&] { return !store.busy(); }));
    EXPECT_TRUE(requests.isEmpty());
}

TEST(FolderListing, ListsFoldersFirstNaturallyAndFiltersAndSorts) {
    QTemporaryDir dir;
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("zeta")));
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral("Alpha")));
    ASSERT_TRUE(QDir(dir.path()).mkpath(QStringLiteral(".hidden")));
    hideOnWindows(dir.filePath(QStringLiteral(".hidden")));
    touchFile(dir.filePath(QStringLiteral("img10.jpg")), 10);
    touchFile(dir.filePath(QStringLiteral("img2.JPG")), 300);
    touchFile(dir.filePath(QStringLiteral("notes.txt")), 20);

    iv::FolderListing listing;
    listing.setPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return !listing.loading(); }));
    EXPECT_EQ(listingNames(listing), (QStringList{"Alpha", "zeta", "img2.JPG", "img10.jpg", "notes.txt"}));
    EXPECT_TRUE(listing.get(0).value(QStringLiteral("isFolder")).toBool());
    EXPECT_EQ(listing.indexOf(QStringLiteral("notes.txt")), 4);

    listing.setExtensions({QStringLiteral("JPG")});
    ASSERT_TRUE(waitUntil([&] { return !listing.loading() && listing.count() == 4; }));
    EXPECT_EQ(listingNames(listing), (QStringList{"Alpha", "zeta", "img2.JPG", "img10.jpg"}));

    // Folders stay first whichever way the files go.
    listing.setSortKey(iv::FolderListing::SortKey::Size);
    listing.setSortDescending(false);
    EXPECT_EQ(listingNames(listing), (QStringList{"Alpha", "zeta", "img10.jpg", "img2.JPG"}));
    listing.setSortDescending(true);
    EXPECT_EQ(listingNames(listing), (QStringList{"zeta", "Alpha", "img2.JPG", "img10.jpg"}));

    listing.setShowFiles(false);
    ASSERT_TRUE(waitUntil([&] { return !listing.loading() && listing.count() == 2; }));

    EXPECT_EQ(listing.kindOf(dir.path()), QStringLiteral("folder"));
    EXPECT_EQ(listing.kindOf(dir.filePath(QStringLiteral("notes.txt"))), QStringLiteral("file"));
    EXPECT_EQ(listing.kindOf(dir.filePath(QStringLiteral("nothing"))), QString());
}

// Why: the file chooser lists hidden folders only when asked, while hidden files stay out
// either way. Input: makeHiddenEntries. The truth is the listed names.
TEST(FolderListing, ListsHiddenFoldersOnlyWhenAskedAndNeverHiddenFiles) {
    QTemporaryDir dir;
    makeHiddenEntries(dir);
    iv::FolderListing listing;
    listing.setPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return !listing.loading(); }));
    EXPECT_EQ(listingNames(listing), QStringList{QStringLiteral("shown")});

    listing.setShowHiddenFolders(true);
    ASSERT_TRUE(waitUntil([&] { return !listing.loading() && listing.count() == 2; }));
    EXPECT_EQ(listingNames(listing), (QStringList{QStringLiteral(".hidden"), QStringLiteral("shown")}));
}

TEST(FolderListing, ReadsTheFolderAgainWhenItChanges) {
    QTemporaryDir dir;
    iv::FolderListing listing;
    listing.setPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return !listing.loading(); }));
    EXPECT_EQ(listing.count(), 0);
    touchFile(dir.filePath(QStringLiteral("new.png")));
    EXPECT_TRUE(waitUntil([&] { return listing.count() == 1; }));
}
