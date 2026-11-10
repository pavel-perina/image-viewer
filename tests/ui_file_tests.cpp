#include "ui_test_support.h"

TEST(FileOperations, TrashKeepsTheFileRecoverableAndMovesToTheNextImage) {
    FileOpsFixture f;
    f.context.clickFile(2, false, false);
    QSignalSpy resets(f.context.files(), &QAbstractItemModel::modelReset);

    const QString unique = QStringLiteral("trash-%1.jpg").arg(QCoreApplication::applicationPid());
    ASSERT_TRUE(QFile::rename(f.path(QStringLiteral("c.jpg")), f.path(unique)));
    f.context.refresh();
    ASSERT_TRUE(waitUntil([&] { return f.context.currentName() != QStringLiteral("c.jpg"); }));

    f.context.trash({f.path(unique)});
    f.settle([&] { return f.context.fileCount() == 5; });

    EXPECT_TRUE(f.problems.isEmpty()) << f.problems.join(u'|').toStdString();
    EXPECT_FALSE(QFileInfo::exists(f.path(unique)));
    EXPECT_TRUE(isInTrash(unique)) << "looked in " << trashFolder().toStdString();
    EXPECT_EQ(resets.count(), 0) << "a view would lose its scroll position";
}

TEST(FileOperations, RemovingTheCurrentImageShowsTheNextAndTheLastShowsThePrevious) {
    FileOpsFixture f(3);
    f.context.clickFile(1, false, false);
    f.context.trash({f.path(QStringLiteral("b.jpg"))});
    f.settle([&] { return f.context.fileCount() == 2; });
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
    EXPECT_EQ(f.names(), QStringLiteral("c"));

    f.context.trash({f.path(QStringLiteral("c.jpg"))});
    f.settle([&] { return f.context.fileCount() == 1; });
    EXPECT_EQ(f.context.currentName(), QStringLiteral("a.jpg"));

    QSignalSpy emptied(&f.context, &BrowseContext::emptied);
    f.context.trash({f.path(QStringLiteral("a.jpg"))});
    f.settle([&] { return f.context.fileCount() == 0; });
    EXPECT_EQ(f.context.currentPath(), QString());
    EXPECT_EQ(emptied.count(), 1);
}

TEST(FileOperations, PermanentDeleteBypassesTheTrash) {
    FileOpsFixture f(3);
    f.context.clickFile(1, false, false);
    const int trashedBefore = static_cast<int>(QDir(trashFolder()).entryList(QDir::Files).size());

    f.context.deletePermanently({f.path(QStringLiteral("b.jpg"))});
    f.settle([&] { return f.context.fileCount() == 2; });
    EXPECT_TRUE(f.problems.isEmpty());
    EXPECT_FALSE(QFileInfo::exists(f.path(QStringLiteral("b.jpg"))));
    EXPECT_EQ(QDir(trashFolder()).entryList(QDir::Files).size(), trashedBefore);
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
}

TEST(FileOperations, PermanentDeleteReportsProblemsAndLeavesFolders) {
    FileOpsFixture f(1);
    const QString folder = f.path(QStringLiteral("sub"));
    ASSERT_TRUE(QDir().mkpath(folder));
    writeJpeg(QDir(folder).filePath(QStringLiteral("inside.jpg")));

    f.context.deletePermanently({folder, f.path(QStringLiteral("missing.jpg")), f.path(QStringLiteral("a.jpg"))});
    f.settle([&] { return f.context.fileCount() == 0; });
    EXPECT_EQ(f.problems.size(), 2) << f.problems.join(u'|').toStdString();
    EXPECT_TRUE(QFileInfo::exists(QDir(folder).filePath(QStringLiteral("inside.jpg"))));
}

TEST(FileOperations, RefreshFindsChangesAndKeepsCurrentAndSelectionByName) {
    FileOpsFixture f(3);
    f.context.clickFile(1, false, false);
    f.context.clickFile(2, true, false);

    writeJpeg(f.path(QStringLiteral("0.jpg")));  // sorts first, so the rows move
    f.context.refresh();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 4; }));
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
    EXPECT_EQ(f.context.currentIndex(), 3);
    EXPECT_EQ(f.names(), QStringLiteral("bc"));
}

TEST(FileOperations, NoticesChangesInTheFolderByItself) {
    FileOpsFixture f(2);
    writeJpeg(f.path(QStringLiteral("z.jpg")));
    EXPECT_TRUE(waitUntil([&] { return f.context.fileCount() == 3; }));
}

TEST(FileOperations, AChangedFileGetsANewThumbnailUrl) {
    FileOpsFixture f(1);
    const QModelIndex first = f.context.files()->index(0, 0);
    const auto url = [&] { return f.context.files()->data(first, f.context.files()->roleNames().key("thumbnail")).toString(); };
    const QString before = url();

    QFile file(f.path(QStringLiteral("a.jpg")));
    ASSERT_TRUE(file.open(QIODevice::ReadWrite));
    ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
    file.close();
    f.context.refresh();
    EXPECT_TRUE(waitUntil([&] { return url() != before; }));
}

TEST(FileOperations, CopiesKeepTheOriginalAndItsTime) {
    FileOpsFixture f(2);
    const QDateTime old = QDateTime::currentDateTime().addDays(-10);
    {
        QFile file(f.path(QStringLiteral("a.jpg")));
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(old, QFileDevice::FileModificationTime));
    }
    f.context.copyTo({f.path(QStringLiteral("a.jpg"))}, f.other.path(), BrowseContext::SkipExisting);
    f.settle([&] { return QFileInfo::exists(f.otherPath(QStringLiteral("a.jpg"))); });
    EXPECT_TRUE(QFileInfo::exists(f.path(QStringLiteral("a.jpg"))));
    EXPECT_LT(std::abs(QFileInfo(f.otherPath(QStringLiteral("a.jpg"))).lastModified().secsTo(old)), 2);
}

TEST(FileOperations, MovesLeaveTheFolder) {
    FileOpsFixture f(3);
    f.context.moveTo({f.path(QStringLiteral("a.jpg")), f.path(QStringLiteral("b.jpg"))}, f.other.path(), BrowseContext::SkipExisting);
    f.settle([&] { return f.context.fileCount() == 1; });
    EXPECT_TRUE(QFileInfo::exists(f.otherPath(QStringLiteral("a.jpg"))));
    EXPECT_TRUE(QFileInfo::exists(f.otherPath(QStringLiteral("b.jpg"))));
    EXPECT_EQ(f.context.currentName(), QStringLiteral("c.jpg"));
}

TEST(FileOperations, ConflictsFollowThePolicyAndNeverLoseAFile) {
    FileOpsFixture f(1);
    const QString source = f.path(QStringLiteral("a.jpg"));
    const QString existing = f.otherPath(QStringLiteral("a.jpg"));
    f.writeBytes(existing, 7);
    EXPECT_EQ(f.context.conflictCount({source}, f.other.path()), 1);
    const qint64 sourceSize = FileOpsFixture::size(source);

    f.context.copyTo({source}, f.other.path(), BrowseContext::SkipExisting);
    f.settle([&] { return true; });
    EXPECT_EQ(FileOpsFixture::size(existing), 7) << "skip";

    f.context.copyTo({source}, f.other.path(), BrowseContext::KeepBoth);
    f.settle([&] { return QFileInfo::exists(f.otherPath(QStringLiteral("a (1).jpg"))); });
    EXPECT_EQ(FileOpsFixture::size(existing), 7) << "keep both";
    EXPECT_EQ(FileOpsFixture::size(f.otherPath(QStringLiteral("a (1).jpg"))), sourceSize);

    const int trashedBefore = static_cast<int>(QDir(trashFolder()).entryList(QDir::Files).size());
    f.context.copyTo({source}, f.other.path(), BrowseContext::ReplaceExisting);
    f.settle([&] { return FileOpsFixture::size(existing) == sourceSize; });
    EXPECT_TRUE(f.problems.isEmpty());
#ifndef Q_OS_WIN
    EXPECT_EQ(QDir(trashFolder()).entryList(QDir::Files).size(), trashedBefore + 1) << "the old file went to the trash";
#else
    Q_UNUSED(trashedBefore);
#endif
}

TEST(FileOperations, AFileNeverReplacesItself) {
    FileOpsFixture f(1);
    const QString source = f.path(QStringLiteral("a.jpg"));
    const qint64 size = FileOpsFixture::size(source);
    EXPECT_EQ(f.context.conflictCount({source}, f.dir.path()), 0);

    f.context.moveTo({source}, f.dir.path(), BrowseContext::ReplaceExisting);
    f.settle([&] { return true; });
    EXPECT_EQ(FileOpsFixture::size(source), size);
    EXPECT_TRUE(f.problems.isEmpty());

    f.context.copyTo({source}, f.dir.path(), BrowseContext::ReplaceExisting);
    f.settle([&] { return f.context.fileCount() == 2; });
    EXPECT_EQ(FileOpsFixture::size(source), size);
    EXPECT_EQ(FileOpsFixture::size(f.path(QStringLiteral("a (1).jpg"))), size);
    EXPECT_EQ(f.context.currentName(), QStringLiteral("a (1).jpg")) << "the new file is selected";
    EXPECT_EQ(f.names(), QStringLiteral("a (1)"));
}

TEST(FileOperations, FailuresAreReportedPerFileAndTheRestContinue) {
    FileOpsFixture f(2);
    const QStringList paths{f.path(QStringLiteral("missing.jpg")), f.path(QStringLiteral("a.jpg"))};
    f.context.copyTo(paths, f.other.path(), BrowseContext::SkipExisting);
    f.settle([&] { return QFileInfo::exists(f.otherPath(QStringLiteral("a.jpg"))); });
    ASSERT_EQ(f.problems.size(), 1);
    EXPECT_TRUE(f.problems.front().startsWith(QStringLiteral("missing.jpg: ")));
}

TEST(FileOperations, CancellingStopsBeforeTheNextFile) {
    FileOpsFixture f(6);
    QSignalSpy finished(&f.context, &BrowseContext::operationFinished);
    f.context.copyTo(f.context.selectedPaths(), f.other.path(), BrowseContext::SkipExisting);
    f.context.cancelJob();
    f.settle([&] { return finished.count() == 1; });
    EXPECT_FALSE(f.context.busy());
}

TEST(FileOperations, RenameChecksTheNameAndSelectsTheResult) {
    FileOpsFixture f(3);
    const QString a = f.path(QStringLiteral("a.jpg"));
    EXPECT_FALSE(f.context.rename(a, QString()).isEmpty());
    EXPECT_FALSE(f.context.rename(a, QStringLiteral("x/y.jpg")).isEmpty());
    EXPECT_FALSE(f.context.rename(a, QStringLiteral("b.jpg")).isEmpty()) << "exists";
    EXPECT_TRUE(QFileInfo::exists(a));

    EXPECT_TRUE(f.context.rename(a, QStringLiteral("z.jpg")).isEmpty());
    f.settle([&] { return f.context.currentName() == QStringLiteral("z.jpg"); });
    EXPECT_FALSE(QFileInfo::exists(a));
    EXPECT_EQ(f.context.currentIndex(), 2);
    EXPECT_EQ(f.names(), QStringLiteral("z"));
}

TEST(FileOperations, FoldersCanBeCreatedRenamedAndTrashed) {
    FileOpsFixture f(1);
    EXPECT_TRUE(f.context.createFolder(f.dir.path(), QStringLiteral("sub")).isEmpty());
    f.settle([&] { return QFileInfo(f.path(QStringLiteral("sub"))).isDir(); });
    EXPECT_FALSE(f.context.createFolder(f.dir.path(), QStringLiteral("sub")).isEmpty());

    const QString sub = f.path(QStringLiteral("sub"));
    writeJpeg(QDir(sub).filePath(QStringLiteral("inside.jpg")));
    f.context.openPath(sub);
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 1; }));

    // Renaming the folder being shown follows it, and the image stays current.
    EXPECT_TRUE(f.context.rename(sub, QStringLiteral("renamed")).isEmpty());
    const QString renamed = f.path(QStringLiteral("renamed"));
    f.settle([&] { return f.context.directory() == renamed && f.context.fileCount() == 1; });
    EXPECT_EQ(f.context.currentName(), QStringLiteral("inside.jpg"));

    // Trashing it goes to its parent.
    const QString unique = QStringLiteral("folder-%1").arg(QCoreApplication::applicationPid());
    EXPECT_TRUE(f.context.rename(renamed, unique).isEmpty());
    const QString named = f.path(unique);
    f.settle([&] { return f.context.directory() == named && f.context.fileCount() == 1; });
    f.context.trashFolder(named);
    f.settle([&] { return !QFileInfo::exists(named) && f.context.directory() == f.dir.path(); });
    EXPECT_TRUE(isInTrash(unique));
    EXPECT_TRUE(f.problems.isEmpty());
}

TEST(FileOperations, RefusesToTrashHomeOrItsParents) {
    FileOpsFixture f(1);
    for (const QString& folder : {QDir::homePath(), QFileInfo(QDir::homePath()).absolutePath(), QDir::rootPath()}) {
        f.problems.clear();
        f.context.trashFolder(folder);
        EXPECT_FALSE(f.problems.isEmpty()) << folder.toStdString();
        EXPECT_TRUE(QFileInfo::exists(folder));
    }
}

TEST(FileOperations, SortsByEachKeyAndKeepsTheCurrentImage) {
    FileOpsFixture f(0);
    const auto write = [&](const char* name, int bytes, int age) {
        const QString path = f.path(QString::fromLatin1(name));
        f.writeBytes(path, bytes);
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::ReadWrite));
        ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addDays(-age), QFileDevice::FileModificationTime));
    };
    write("a.png", 30, 1);
    write("b.jpg", 10, 2);
    write("c.tif", 20, 3);
    f.context.refresh();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 3; }));
    f.context.clickFile(1, false, false);

    const auto order = [&] {
        QString out;
        const auto* model = f.context.files();
        for (int row = 0; row < model->rowCount(); ++row) {
            out += model->data(model->index(row, 0)).toString().front();
        }
        return out;
    };
    const auto expectOrder = [&](BrowseContext::SortKey key, bool descending, const char* expected) {
        f.context.setSortKey(key);
        f.context.setSortDescending(descending);
        EXPECT_TRUE(waitUntil([&] { return order() == QString::fromLatin1(expected); })) << expected << " but " << order().toStdString();
        EXPECT_EQ(f.context.currentName(), QStringLiteral("b.jpg"));
        EXPECT_EQ(f.context.currentIndex(), order().indexOf(QLatin1Char('b')));
        EXPECT_EQ(f.names(), QStringLiteral("b"));
    };
    expectOrder(BrowseContext::SortBySize, false, "bca");
    expectOrder(BrowseContext::SortByDate, false, "cba");
    expectOrder(BrowseContext::SortByType, false, "bac");
    expectOrder(BrowseContext::SortByName, true, "cba");
    expectOrder(BrowseContext::SortByName, false, "abc");
}

TEST(Clipboard, FilesReachTheClipboardAndASingleOneAlsoAsAnImage) {
    FileOpsFixture f(2);
    const QStringList both{f.path(QStringLiteral("a.jpg")), f.path(QStringLiteral("b.jpg"))};
    f.context.copyFilesToClipboard(both);
    EXPECT_EQ(f.context.clipboardFiles(), both);
    EXPECT_FALSE(QGuiApplication::clipboard()->mimeData()->hasImage());

    f.context.copyFilesToClipboard({both[0]});
    EXPECT_EQ(f.context.clipboardFiles(), QStringList{both[0]});
    EXPECT_TRUE(waitUntil([] { return QGuiApplication::clipboard()->mimeData()->hasImage(); }));
    EXPECT_EQ(f.context.clipboardFiles(), QStringList{both[0]}) << "the image adds to the files";
    EXPECT_EQ(QGuiApplication::clipboard()->image().width(), 64);
    EXPECT_TRUE(f.context.canPaste());
}

TEST(Clipboard, ALateImageDoesNotOverwriteANewerCopy) {
    FileOpsFixture f(1);
    f.context.copyFilesToClipboard({f.path(QStringLiteral("a.jpg"))});
    QGuiApplication::clipboard()->setText(QStringLiteral("newer"));
    waitUntil([] { return false; }, 300);
    EXPECT_EQ(QGuiApplication::clipboard()->text(), QStringLiteral("newer"));
    EXPECT_FALSE(f.context.canPaste());
}

TEST(Clipboard, FullPathsAreText) {
    FileOpsFixture f(1);
    f.context.copyPathsToClipboard({f.path(QStringLiteral("a.jpg"))});
    EXPECT_EQ(QGuiApplication::clipboard()->text(), QDir::toNativeSeparators(f.path(QStringLiteral("a.jpg"))));
}

TEST(Clipboard, PastedImageBecomesATimeNamedPngAndIsSelected) {
    FileOpsFixture f(1);
    QImage image(20, 10, QImage::Format_ARGB32);
    image.fill(qRgba(10, 200, 30, 128));
    QGuiApplication::clipboard()->setImage(image);
    ASSERT_TRUE(f.context.canPaste());
    EXPECT_TRUE(f.context.clipboardFiles().isEmpty());

    const QRegularExpression pattern(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}_\\d{6}\\.png$"));
    f.context.pasteImage();
    f.settle([&] { return f.context.fileCount() == 2; });
    const QString name = f.context.currentName();
    EXPECT_TRUE(pattern.match(name).hasMatch()) << name.toStdString();
    EXPECT_EQ(f.context.selectedCount(), 1);
    const QImage saved(f.path(name));
    EXPECT_EQ(saved.size(), image.size());
    EXPECT_EQ(saved.pixelColor(3, 3).alpha(), 128) << "transparency survives";
}

TEST(Clipboard, TwoImagesInTheSameSecondBothGetFiles) {
    QTemporaryDir dir;
    QImage image(4, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    const QDateTime when(QDate(2026, 12, 24), QTime(12, 59, 59));
    const auto first = iv::saveNewImage(image, dir.path(), when);
    const auto second = iv::saveNewImage(image, dir.path(), when);
    ASSERT_EQ(first.created.size(), 1);
    ASSERT_EQ(second.created.size(), 1);
    EXPECT_EQ(QFileInfo(first.created[0]).fileName(), QStringLiteral("2026-12-24_125959.png"));
    EXPECT_EQ(QFileInfo(second.created[0]).fileName(), QStringLiteral("2026-12-24_125959_1.png"));
}

TEST(Drop, FilesFromAnotherApplicationGoIntoTheFolderShown) {
    MainFixture f(2);
    ASSERT_NE(f.window, nullptr);
    QTemporaryDir other;
    const QString first = other.filePath(QStringLiteral("first.jpg"));
    const QString second = other.filePath(QStringLiteral("second.jpg"));
    writeJpeg(first);
    writeJpeg(second);
    auto* grid = qobject_cast<QQuickItem*>(f.find(QStringLiteral("thumbnailGrid")));
    const QPointF overGrid = grid->mapToScene(QPointF(grid->width() / 2, grid->height() / 2));

    EXPECT_TRUE(dropFiles(f.window, overGrid, {first}));
    EXPECT_TRUE(waitUntil([&] { return QFile::exists(f.dir.filePath(QStringLiteral("first.jpg"))); }));
    EXPECT_TRUE(QFile::exists(first)) << "copied";
    EXPECT_FALSE(f.window->property("viewing").toBool()) << "and not opened";
    ASSERT_TRUE(waitUntil([&] { return !f.context->busy(); }));  // a drop while copying is refused
    EXPECT_TRUE(dropFiles(f.window, overGrid, {second}, Qt::ShiftModifier));
    EXPECT_TRUE(waitUntil([&] { return QFile::exists(f.dir.filePath(QStringLiteral("second.jpg"))) && !QFile::exists(second); }))
        << "moved with Shift";
    EXPECT_TRUE(waitUntil([&] { return f.context->fileCount() == 4; }));
    EXPECT_FALSE(dropFiles(f.window, overGrid, {f.dir.filePath(QStringLiteral("a.jpg"))})) << "not where they are already";
}

TEST(Drop, AFolderOpensAndAFileOpensInTheLargeView) {
    MainFixture f(2);
    ASSERT_NE(f.window, nullptr);
    QTemporaryDir other;
    const QString file = other.filePath(QStringLiteral("dropped.jpg"));
    writeJpeg(file);
    const QPointF middle(f.window->width() / 2, f.window->height() / 2);

    EXPECT_TRUE(dropFiles(f.window, middle, {other.path()}));
    EXPECT_TRUE(waitUntil([&] { return f.context->directory() == other.path() && f.context->fileCount() == 1; }))
        << "a folder opens in the browser";
    f.context->openPath(f.dir.filePath(QStringLiteral("a.jpg")));
    f.window->setProperty("viewing", true);
    EXPECT_TRUE(dropFiles(f.window, middle, {file}));
    EXPECT_TRUE(f.window->property("viewing").toBool());
    EXPECT_EQ(f.context->currentPath(), file) << "a file dropped on the large view opens";
    EXPECT_FALSE(QFile::exists(f.dir.filePath(QStringLiteral("dropped.jpg")))) << "nothing is copied";
}

TEST(Drop, FilesOnAFolderOfTheTreeAreCopiedOrWithShiftMoved) {
    MainFixture f(1, {QStringLiteral("sub")});
    ASSERT_NE(f.window, nullptr);
    const QString sub = f.dir.filePath(QStringLiteral("sub"));
    QTemporaryDir other;
    const QString first = other.filePath(QStringLiteral("first.jpg"));
    const QString second = other.filePath(QStringLiteral("second.jpg"));
    writeJpeg(first);
    writeJpeg(second);
    ASSERT_FALSE(treeRowCentre(f, sub).isNull());

    EXPECT_TRUE(dropFiles(f.window, treeRowCentre(f, sub), {first}));
    EXPECT_TRUE(waitUntil([&] { return QFile::exists(sub + QStringLiteral("/first.jpg")); }));
    EXPECT_TRUE(QFile::exists(first)) << "copied";
    EXPECT_EQ(f.context->directory(), f.dir.path()) << "the folder shown stays";
    ASSERT_TRUE(waitUntil([&] { return !f.context->busy(); }));

    EXPECT_TRUE(dropFiles(f.window, treeRowCentre(f, sub), {second}, Qt::ShiftModifier));
    EXPECT_TRUE(waitUntil([&] { return QFile::exists(sub + QStringLiteral("/second.jpg")) && !QFile::exists(second); }))
        << "moved with Shift";
}

TEST(Drop, NotOnTheFolderTheFilesAreAlreadyIn) {
    MainFixture f(1, {QStringLiteral("sub")});
    ASSERT_NE(f.window, nullptr);
    const QString sub = f.dir.filePath(QStringLiteral("sub"));
    const QString file = f.dir.filePath(QStringLiteral("a.jpg"));
    ASSERT_FALSE(treeRowCentre(f, sub).isNull());
    EXPECT_FALSE(dropFiles(f.window, treeRowCentre(f, f.dir.path()), {file})) << "the folder it is in";
    EXPECT_FALSE(dropFiles(f.window, treeRowCentre(f, sub), {f.dir.path()})) << "a folder into its own subfolder";
    EXPECT_TRUE(dropFiles(f.window, treeRowCentre(f, sub), {file}, Qt::ControlModifier)) << "but elsewhere";
    EXPECT_TRUE(waitUntil([&] { return QFile::exists(sub + QStringLiteral("/a.jpg")); }));
    waitUntil([] { return false; }, 200);
    EXPECT_FALSE(f.find(QStringLiteral("conflictDialog")) && f.find(QStringLiteral("conflictDialog"))->property("visible").toBool());
    EXPECT_EQ(QDir(sub).entryList(QDir::NoDotAndDotDot | QDir::AllEntries), QStringList{QStringLiteral("a.jpg")});
}

TEST(Clipboard, CutFilesAreMarkedForFileManagersAndMovedWhenPastedHere) {
    MainFixture f(1, {QStringLiteral("sub")});
    ASSERT_NE(f.window, nullptr);
    const QString file = f.dir.filePath(QStringLiteral("a.jpg"));
    const QString sub = f.dir.filePath(QStringLiteral("sub"));
    f.context->setCurrentIndex(0);
    QTest::keyClick(f.window, Qt::Key_X, Qt::ControlModifier);
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    ASSERT_NE(mime, nullptr);
    EXPECT_EQ(mime->urls(), QList<QUrl>{QUrl::fromLocalFile(file)});
    EXPECT_EQ(mime->data(QStringLiteral("application/x-kde-cutselection")), QByteArray("1"));
    EXPECT_TRUE(mime->data(QStringLiteral("x-special/gnome-copied-files")).startsWith("cut\n"));
    EXPECT_FALSE(mime->hasImage()) << "a cut file is not offered as a picture";
    EXPECT_TRUE(f.context->clipboardIsCut());
    EXPECT_TRUE(QFile::exists(file)) << "nothing moves until it is pasted";

    f.context->openPath(sub);
    ASSERT_TRUE(waitUntil([&] { return f.context->directory() == sub; }));
    QTest::keyClick(f.window, Qt::Key_V, Qt::ControlModifier);
    EXPECT_TRUE(waitUntil([&] { return QFile::exists(sub + QStringLiteral("/a.jpg")) && !QFile::exists(file); })) << "moved";
    EXPECT_TRUE(f.context->clipboardFiles().isEmpty()) << "and gone from the clipboard";

    f.context->copyFilesToClipboard({sub + QStringLiteral("/a.jpg")});
    EXPECT_FALSE(f.context->clipboardIsCut()) << "a copy is not a cut";
}
