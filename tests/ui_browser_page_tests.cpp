#include "ui_test_support.h"

TEST(BrowserPage, ClicksWithModifiersReachTheSelection) {
    BrowserPageFixture f;
    ASSERT_EQ(f.selected(), QStringLiteral("a"));
    f.clickFile(2, Qt::ControlModifier);
    EXPECT_EQ(f.selected(), QStringLiteral("ac"));
    EXPECT_EQ(f.context.currentIndex(), 2);
    f.clickFile(1);
    EXPECT_EQ(f.selected(), QStringLiteral("b"));
    f.clickFile(3, Qt::ShiftModifier);
    EXPECT_EQ(f.selected(), QStringLiteral("bcd"));
}

TEST(BrowserPage, DoubleClickWithCtrlTogglesOnce) {
    BrowserPageFixture f;
    f.clickFile(2, Qt::ControlModifier);
    f.clickFile(2, Qt::ControlModifier);
    EXPECT_EQ(f.selected(), QStringLiteral("ac"));
}

// Why: a click on empty space, here below the only row, clears the selection; the current image
// stays current.
TEST(BrowserPage, ClickOnEmptySpaceClearsTheSelection) {
    BrowserPageFixture f;
    f.clickFile(1);
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    f.clickAt(grid->mapToScene(QPointF(90, 500)), Qt::NoModifier);
    EXPECT_EQ(f.selected(), QString());
    EXPECT_EQ(f.context.currentIndex(), 1);
}

// Why: with the grid scrolled down, a click on a thumbnail selected it and the grid's own
// handler for clicks between thumbnails then cleared it again, as it added the scroll to a
// point that already had it. Only the frame of the current image was left. Input: 60 more
// images, the grid scrolled to the end, a click and a right click as the mouse gives them,
// press then release a little later. The truth is the selection, and that the file menu is
// asked for once.
TEST(BrowserPage, AClickSelectsAThumbnailWhenTheGridIsScrolled) {
    BrowserPageFixture f;
    for (int i = 0; i < 60; ++i) {
        writeJpeg(f.dir.filePath(QStringLiteral("m%1.jpg").arg(i, 2, 10, QLatin1Char('0'))));
    }
    f.context.refresh();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 64; }, 5000));
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    const int last = 63;
    QMetaObject::invokeMethod(grid, "positionViewAtEnd");
    ASSERT_TRUE(waitUntil([&] { return grid->property("contentY").toDouble() > 400; }));
    QQuickItem* cell = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, last));
    ASSERT_NE(cell, nullptr);
    const QPoint at = cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2)).toPoint();

    QTest::mouseMove(&f.view, at);
    QTest::mousePress(&f.view, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseRelease(&f.view, Qt::LeftButton, Qt::NoModifier, at, 120);
    EXPECT_EQ(f.context.currentIndex(), last);
    EXPECT_EQ(f.context.selectedPaths(), QStringList{f.dir.filePath(QStringLiteral("m59.jpg"))});
    EXPECT_TRUE(cell->property("selected").toBool());

    QSignalSpy menu(f.view.rootObject(), SIGNAL(fileMenuRequested()));
    QTest::mousePress(&f.view, Qt::RightButton, Qt::NoModifier, at);
    QTest::mouseRelease(&f.view, Qt::RightButton, Qt::NoModifier, at, 120);
    EXPECT_EQ(menu.count(), 1) << "for the thumbnail, not also for the space between";
}

// Why: a click on a thumbnail that is half out of view scrolls the grid to show it, during the
// release that the handler for empty space looks at too; what lies under the release has then
// moved. Input: 60 more images, the grid scrolled so that the last row is half hidden at the
// bottom, a click near the bottom of the visible half of its first thumbnail. The truth is that
// the grid scrolled and the thumbnail is selected.
TEST(BrowserPage, AClickOnAHalfHiddenThumbnailScrollsToItAndSelectsIt) {
    BrowserPageFixture f;
    for (int i = 0; i < 60; ++i) {
        writeJpeg(f.dir.filePath(QStringLiteral("m%1.jpg").arg(i, 2, 10, QLatin1Char('0'))));
    }
    f.context.refresh();
    ASSERT_TRUE(waitUntil([&] { return f.context.fileCount() == 64; }, 5000));
    auto* grid = qobject_cast<QQuickItem*>(f.find("QQuickGridView"));
    ASSERT_NE(grid, nullptr);
    const int columns = grid->property("columns").toInt();
    const int firstOfLastRow = (63 / columns) * columns;
    QMetaObject::invokeMethod(grid, "positionViewAtEnd");
    ASSERT_TRUE(waitUntil([&] { return grid->property("contentY").toDouble() > 400; }));
    grid->setProperty("contentY", grid->property("contentY").toDouble() - 100);  // half of the last row
    QQuickItem* cell = nullptr;
    QMetaObject::invokeMethod(grid, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cell), Q_ARG(int, firstOfLastRow));
    ASSERT_NE(cell, nullptr);
    const double visible = grid->height() - grid->mapFromItem(cell, QPointF(0, 0)).y();
    ASSERT_GT(visible, 20);
    ASSERT_LT(visible, cell->height() - 20);
    const double before = grid->property("contentY").toDouble();

    const QPoint at = cell->mapToScene(QPointF(cell->width() / 2, visible - 10)).toPoint();
    QTest::mouseMove(&f.view, at);
    QTest::mousePress(&f.view, Qt::LeftButton, Qt::NoModifier, at);
    QTest::mouseRelease(&f.view, Qt::LeftButton, Qt::NoModifier, at, 120);
    EXPECT_GT(grid->property("contentY").toDouble(), before) << "scrolled to show it";
    EXPECT_EQ(f.context.currentIndex(), firstOfLastRow);
    EXPECT_TRUE(cell->property("selected").toBool());
}

TEST(BrowserPage, TreeLabelsStayRightWhenASubfolderAppears) {
    TreeFixture f;
    const QDir dir(f.context.directory());
    ASSERT_TRUE(dir.mkpath(QStringLiteral("a")));
    ASSERT_TRUE(dir.mkpath(QStringLiteral("b")));
    const QStringList flat{QStringLiteral("a=a"), QStringLiteral("b=b"), QStringLiteral("inner=inner")};
    ASSERT_TRUE(waitUntil([&] { return treeRows(f) == flat; })) << treeRows(f).join(u' ').toStdString();

    // Created before a is expanded, so expanding it finds c while the rows below move down.
    ASSERT_TRUE(dir.mkpath(QStringLiteral("a/c")));
    f.settle();
    QMetaObject::invokeMethod(f.tree, "expand", Q_ARG(int, f.rowOf(f.path(QStringLiteral("a")))));
    const QStringList withC{QStringLiteral("a=a"), QStringLiteral("c=a/c"), QStringLiteral("b=b"), QStringLiteral("inner=inner")};
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == withC; }, 2000)) << treeRows(f).join(u' ').toStdString();

    // And created while it is expanded.
    ASSERT_TRUE(dir.mkpath(QStringLiteral("a/d")));
    const QStringList withD{QStringLiteral("a=a"), QStringLiteral("c=a/c"), QStringLiteral("d=a/d"), QStringLiteral("b=b"), QStringLiteral("inner=inner")};
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == withD; }, 2000)) << treeRows(f).join(u' ').toStdString();

    // Collapsing and expanding again finds them without reading the disk.
    const int a = f.rowOf(f.path(QStringLiteral("a")));
    QMetaObject::invokeMethod(f.tree, "collapse", Q_ARG(int, a));
    const QStringList collapsed{QStringLiteral("a=a"), QStringLiteral("b=b"), QStringLiteral("inner=inner")};
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == collapsed; }, 2000)) << treeRows(f).join(u' ').toStdString();
    QMetaObject::invokeMethod(f.tree, "expand", Q_ARG(int, a));
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == withD; }, 2000)) << treeRows(f).join(u' ').toStdString();

    // Only the row of the opened folder is highlighted.
    const QString b = f.path(QStringLiteral("b"));
    const QString opened = f.context.directory();
    f.context.openPath(b);
    f.settle();
    QQuickItem* bRow = nullptr;
    QMetaObject::invokeMethod(f.tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, bRow), Q_ARG(QModelIndex, f.model->indexForPath(b)));
    ASSERT_NE(bRow, nullptr);
    EXPECT_TRUE(bRow->property("highlighted").toBool());
    QQuickItem* cRow = nullptr;
    QMetaObject::invokeMethod(f.tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, cRow), Q_ARG(QModelIndex, f.model->indexForPath(QDir(opened).filePath(QStringLiteral("a/c")))));
    ASSERT_NE(cRow, nullptr);
    EXPECT_FALSE(cRow->property("highlighted").toBool());
}

TEST(BrowserPage, TreeLabelFollowsARenamedFolder) {
    TreeFixture f;
    const QDir dir(f.context.directory());
    ASSERT_TRUE(dir.mkpath(QStringLiteral("a")));
    const QStringList before{QStringLiteral("a=a"), QStringLiteral("inner=inner")};
    ASSERT_TRUE(waitUntil([&] { return treeRows(f) == before; })) << treeRows(f).join(u' ').toStdString();
    ASSERT_TRUE(f.context.rename(dir.filePath(QStringLiteral("a")), QStringLiteral("z")).isEmpty());
    const QStringList after{QStringLiteral("inner=inner"), QStringLiteral("z=z")};
    EXPECT_TRUE(waitUntil([&] { return treeRows(f) == after; }, 3000)) << treeRows(f).join(u' ').toStdString();
}

TEST(BrowserPage, TreeExpandsTheOpenedFolder) {
    TreeFixture f;
    const QString inner = f.path(QStringLiteral("inner"));
    EXPECT_TRUE(waitUntil([&] { return f.rowOf(inner) > f.rowOf(f.context.directory()); }));
}

TEST(BrowserPage, TreeShowsTheOtherFoldersOnTheWayToAnOpenedOne) {
    TreeFixture f;
    const QDir dir(f.context.directory());
    ASSERT_TRUE(dir.mkpath(QStringLiteral("inner/other")));
    ASSERT_TRUE(dir.mkpath(QStringLiteral("inner/deeper/deepest")));
    ASSERT_TRUE(dir.mkpath(QStringLiteral("inner/deeper/beside")));
    const QString target = dir.filePath(QStringLiteral("inner/deeper/deepest"));

    // Nothing has listed these yet, as when a path comes from the command line.
    f.context.openPath(target);
    for (const char* name : {"inner/other", "inner/deeper/beside"}) {
        const QString folder = dir.filePath(QString::fromLatin1(name));
        EXPECT_TRUE(waitUntil([&] { return f.rowOf(folder) >= 0; })) << name;
    }
    f.settle();
    QQuickItem* row = nullptr;
    QMetaObject::invokeMethod(f.tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(QModelIndex, f.model->indexForPath(target)));
    EXPECT_NE(row, nullptr) << "the opened folder stays in sight as the rest fills in above it";
}

TEST(BrowserPage, ClickingAFolderOpensAndExpandsIt) {
    TreeFixture f;
    const QString inner = f.path(QStringLiteral("inner"));
    ASSERT_TRUE(waitUntil([&] { return !f.nameOf(inner).isNull(); }));
    f.clickAt(f.nameOf(inner), Qt::NoModifier);
    EXPECT_EQ(f.context.directory(), inner);
    const QString deeper = QDir(inner).filePath(QStringLiteral("deeper"));
    EXPECT_TRUE(waitUntil([&] { return f.rowOf(deeper) > f.rowOf(inner); }));
}

TEST(BrowserPage, DoubleClickingAFolderLeavesItExpanded) {
    TreeFixture f;
    const QString inner = f.path(QStringLiteral("inner"));
    ASSERT_TRUE(waitUntil([&] { return !f.nameOf(inner).isNull(); }));
    const QPointF name = f.nameOf(inner);
    f.clickAt(name, Qt::NoModifier);
    f.clickAt(name, Qt::NoModifier);
    f.settle();
    EXPECT_EQ(f.context.directory(), inner);
    const QString deeper = QDir(inner).filePath(QStringLiteral("deeper"));
    EXPECT_TRUE(waitUntil([&] { return f.rowOf(deeper) > f.rowOf(inner); }));
}

TEST(BrowserPage, PathBarShowsCrumbsBoldLastAndListsSubfolders) {
    TreeFixture f;
    auto* bar = f.view.rootObject()->findChild<QObject*>(QStringLiteral("pathBar"));
    ASSERT_NE(bar, nullptr);
    const QStringList names = bar->property("crumbNames").toStringList();
    ASSERT_GE(names.size(), 2);
    EXPECT_EQ(names.back(), QFileInfo(f.dir.path()).fileName());
    EXPECT_EQ(bar->property("boldCrumb").toString(), names.back());

    QMetaObject::invokeMethod(bar, "openSubfolders", Q_ARG(QVariant, f.dir.path()), Q_ARG(QVariant, QVariant()));
    EXPECT_TRUE(waitUntil([&] { return bar->property("menuFolders").toStringList() == QStringList{QStringLiteral("inner")}; }));
    const QString inner = f.path(QStringLiteral("inner"));
    QMetaObject::invokeMethod(bar, "chooseSubfolder", Q_ARG(QVariant, QStringLiteral("inner")));
    EXPECT_EQ(f.context.directory(), inner);
    EXPECT_EQ(bar->property("boldCrumb").toString(), QStringLiteral("inner"));
    EXPECT_FALSE(bar->property("menuOpen").toBool());
}

TEST(BrowserPage, PathBarEditsAndKeepsAProblemOnScreen) {
    TreeFixture f;
    auto* bar = f.view.rootObject()->findChild<QObject*>(QStringLiteral("pathBar"));
    ASSERT_NE(bar, nullptr);
    const QString start = f.context.directory();

    QMetaObject::invokeMethod(bar, "edit");
    EXPECT_TRUE(bar->property("editing").toBool());
    EXPECT_EQ(bar->property("editText").toString(), QDir::toNativeSeparators(start));

    QMetaObject::invokeMethod(bar, "submit", Q_ARG(QVariant, f.path(QStringLiteral("missing"))));
    EXPECT_TRUE(bar->property("editing").toBool());
    EXPECT_FALSE(bar->property("problem").toString().isEmpty());
    EXPECT_EQ(f.context.directory(), start);

    const QString inner = f.path(QStringLiteral("inner"));
    QMetaObject::invokeMethod(bar, "submit", Q_ARG(QVariant, inner));
    EXPECT_FALSE(bar->property("editing").toBool());
    EXPECT_EQ(f.context.directory(), inner);
    EXPECT_TRUE(bar->property("problem").toString().isEmpty());
}

TEST(BrowserPage, PreviewShowsHandlesOnlyWhenZoomedIntoPartOfTheImage) {
    BrowserPageFixture f;
    f.view.rootObject()->setProperty("active", true);
    f.context.setCurrentIndex(0);
    auto* preview = f.view.rootObject()->property("preview").value<ImageView*>();
    ASSERT_NE(preview, nullptr);
    ASSERT_TRUE(waitUntil([&] { return preview->status() == ImageView::Status::Ready; })) << preview->source().toStdString();
    auto* horizontal = f.view.rootObject()->findChild<QQuickItem*>(QStringLiteral("horizontalHandle"));
    ASSERT_NE(horizontal, nullptr);
    EXPECT_FALSE(horizontal->isVisible()) << "all of it fits";

    for (int i = 0; i < 20 && preview->visibleArea().width() >= 0.5; ++i) {
        preview->zoomIn();
    }
    ASSERT_LT(preview->visibleArea().width(), 0.5);
    EXPECT_TRUE(horizontal->isVisible());

    preview->fitToView();
    EXPECT_FALSE(horizontal->isVisible()) << "fitted again";
}

TEST(BrowserPage, PreviewUsesTheSharpSetting) {
    BrowserPageFixture f;
    auto* preview = f.view.rootObject()->property("preview").value<ImageView*>();
    ASSERT_NE(preview, nullptr);
    EXPECT_FALSE(preview->resampling());
    f.view.rootObject()->setProperty("sharpResampling", true);
    EXPECT_TRUE(preview->resampling());
}

TEST(BrowserPage, ThumbnailsAreShownOnePixelForOne) {
    BrowserPageFixture f;
    QQuickItem* image = nullptr;
    // Delegates are children of the items they show in, but not of the page as objects.
    const std::function<QQuickItem*(QQuickItem*)> loadedThumbnail = [&](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == QStringLiteral("thumbnailImage") && item->property("status").toInt() == 1 && item->width() > 0) {
            return item;
        }
        for (QQuickItem* child : item->childItems()) {
            if (QQuickItem* found = loadedThumbnail(child)) {
                return found;
            }
        }
        return nullptr;
    };
    ASSERT_TRUE(waitUntil([&] { return (image = loadedThumbnail(f.view.contentItem())) != nullptr; }));
    EXPECT_EQ(image->property("sourceSize").toSize(), QSize(164, 164));
    EXPECT_LE(std::max(image->width(), image->height()), 164);
    const double scale = f.view.effectiveDevicePixelRatio();
    EXPECT_DOUBLE_EQ(image->width() * scale, std::round(image->width() * scale));
    const QPointF corner = image->mapToScene(QPointF(0, 0)) * scale;
    EXPECT_DOUBLE_EQ(corner.x(), std::round(corner.x()));
    EXPECT_DOUBLE_EQ(corner.y(), std::round(corner.y()));
}

TEST(BrowserPage, PageKeysMoveWholeRowsAndStopAtTheEnds) {
    BrowserPageFixture f;
    f.view.resize(700, 300);  // room for one row of three at a time, as the fixture has four
    QCoreApplication::processEvents();
    f.context.setCurrentIndex(0);
    QObject* page = f.view.rootObject();
    QMetaObject::invokeMethod(page, "movePage", Q_ARG(QVariant, 1));
    const int columns = f.find("QQuickGridView")->property("columns").toInt();
    ASSERT_GT(columns, 0);
    EXPECT_EQ(f.context.currentIndex(), std::min(columns, 3));
    QMetaObject::invokeMethod(page, "movePage", Q_ARG(QVariant, 100));
    EXPECT_EQ(f.context.currentIndex(), 3) << "stops at the last, rather than not moving";
    QMetaObject::invokeMethod(page, "extendSelectionByPage", Q_ARG(QVariant, -100));
    EXPECT_EQ(f.context.currentIndex(), 0);
    EXPECT_EQ(f.context.selectedCount(), 4);
    QMetaObject::invokeMethod(page, "movePage", Q_ARG(QVariant, -1));
    EXPECT_EQ(f.context.currentIndex(), 0);
}

TEST(BrowserPage, ScrollbarAppearsOverItsListInTheAccentColourAndFades) {
    BrowserPageFixture f;
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    QQuickItem* bar = nullptr;
    for (QQuickItem* item : f.view.rootObject()->findChildren<QQuickItem*>()) {
        if (item->inherits("QQuickScrollBar") && item->parentItem() == grid) {
            bar = item;
        }
    }
    ASSERT_NE(bar, nullptr);
    EXPECT_FALSE(bar->isVisible()) << "four files fit, so nothing to scroll";

    f.view.resize(700, 300);  // two rows, and room for one
    ASSERT_TRUE(waitUntil([&] { return bar->isVisible() && bar->property("size").toDouble() < 1; }));

    const auto moveTo = [&](QPointF at) {
        QMouseEvent move(QEvent::MouseMove, at, at, f.view.mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&f.view, &move);
        QCoreApplication::processEvents();
    };
    const QPointF overList = grid->mapToScene(QPointF(50, 50));
    const QPointF elsewhere = f.view.rootObject()->mapToScene(QPointF(5, f.view.height() - 5));

    // Out of sight while the mouse is elsewhere and nothing moves.
    moveTo(elsewhere);
    EXPECT_TRUE(waitUntil([&] { return bar->opacity() == 0; }, 2000));

    moveTo(overList);
    EXPECT_TRUE(waitUntil([&] { return bar->opacity() == 1; }, 2000)) << "over the list";

    // The handle is the accent colour at 20%, over whatever lies under it.
    const QPoint handle = bar->mapToScene(QPointF(bar->width() / 2, 10)).toPoint();
    const QImage with = f.view.grabWindow();
    bar->setVisible(false);
    const QImage without = f.view.grabWindow();
    bar->setVisible(true);
    const QColor under = without.pixelColor(handle);
    // Theme's accent, whatever the palette is.
    QObject* theme = f.view.engine()->singletonInstance<QObject*>("ImageViewer", "Theme");
    ASSERT_NE(theme, nullptr);
    const QColor accent = theme->property("accent").value<QColor>();
    const QColor seen = with.pixelColor(handle);
    EXPECT_NEAR(seen.red(), under.red() * 0.8 + accent.red() * 0.2, 2);
    EXPECT_NEAR(seen.green(), under.green() * 0.8 + accent.green() * 0.2, 2);
    EXPECT_NEAR(seen.blue(), under.blue() * 0.8 + accent.blue() * 0.2, 2);

    moveTo(elsewhere);
    EXPECT_TRUE(waitUntil([&] { return bar->opacity() == 0; }, 2000)) << "and it fades again";
}

TEST(BrowserPage, SubfolderListStaysInsideTheWindowUnderTheLastCaret) {
    TreeFixture f;
    auto* bar = f.view.rootObject()->findChild<QObject*>(QStringLiteral("pathBar"));
    ASSERT_NE(bar, nullptr);
    auto* anchor = qobject_cast<QQuickItem*>(bar);
    ASSERT_NE(anchor, nullptr);
    QQuickItem corner(anchor);  // as far right as a caret can be
    corner.setPosition(QPointF(anchor->width() - 1, 0));
    QMetaObject::invokeMethod(bar, "openSubfolders", Q_ARG(QVariant, f.dir.path()), Q_ARG(QVariant, QVariant::fromValue(&corner)));
    QObject* menu = bar->property("menu").value<QObject*>();
    ASSERT_NE(menu, nullptr);
    EXPECT_LE(menu->property("x").toDouble() + menu->property("width").toDouble(), f.view.width());
}

TEST(BrowserPage, RightClickSelectsAnUnselectedFileAndKeepsASelection) {
    BrowserPageFixture f;
    QSignalSpy menus(f.view.rootObject(), SIGNAL(fileMenuRequested()));
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    f.clickFile(0);
    f.clickFile(1, Qt::ControlModifier);
    ASSERT_EQ(f.selected(), QStringLiteral("ab"));

    const auto rightClick = [&](int index) {
        const QPointF point = grid->mapToScene(QPointF(90 + 180 * index, 100));
        const QPointF global = f.view.mapToGlobal(point);
        QMouseEvent press(QEvent::MouseButtonPress, point, point, global, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&f.view, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, point, point, global, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&f.view, &release);
        QCoreApplication::processEvents();
    };
    rightClick(1);
    EXPECT_EQ(menus.count(), 1);
    EXPECT_EQ(f.selected(), QStringLiteral("ab"));
    rightClick(3);
    EXPECT_EQ(menus.count(), 2);
    EXPECT_EQ(f.selected(), QStringLiteral("d"));
}

TEST(BrowserPage, StatusSaysWhatIsInTheFolder) {
    BrowserPageFixture f;
    QQuickItem* status = findNamed(f.view.rootObject(), QStringLiteral("browserStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->property("text").toString().startsWith(QStringLiteral("1 directory | 4 files (")));
    EXPECT_TRUE(status->property("text").toString().contains(QStringLiteral("| 1 selected (")));
}

TEST(BrowserPage, PreviewCollapsesAndDescribesItsImage) {
    BrowserPageFixture f;
    auto* preview = qobject_cast<ImageView*>(f.view.rootObject()->property("preview").value<QObject*>());
    ASSERT_NE(preview, nullptr);
    f.view.rootObject()->setProperty("active", true);
    ASSERT_TRUE(waitUntil([&] { return preview->status() == ImageView::Status::Ready; }));
    QQuickItem* summary = findNamed(f.view.rootObject(), QStringLiteral("previewSummary"));
    ASSERT_NE(summary, nullptr);
    const QString text = summary->property("text").toString();
    EXPECT_TRUE(text.startsWith(QStringLiteral("64x64 0.00MP 24bit JPEG "))) << text.toStdString();
    EXPECT_TRUE(QRegularExpression(QStringLiteral(" \\d+(B|kB) \\d{4}-\\d\\d-\\d\\d \\d\\d:\\d\\d:\\d\\d$")).match(text).hasMatch()) << text.toStdString();
    QQuickItem* camera = findNamed(f.view.rootObject(), QStringLiteral("previewCamera"));
    ASSERT_NE(camera, nullptr);
    EXPECT_FALSE(camera->isVisible()) << "this image has no EXIF";

    // The two buttons on the line.
    QQuickItem* actual = findNamed(f.view.rootObject(), QStringLiteral("previewActualSize"));
    QQuickItem* fit = findNamed(f.view.rootObject(), QStringLiteral("previewFit"));
    ASSERT_NE(actual, nullptr);
    ASSERT_NE(fit, nullptr);
    QMetaObject::invokeMethod(fit, "clicked");
    EXPECT_GT(preview->zoom(), 1) << "64 pixels fill the panel";
    QMetaObject::invokeMethod(actual, "clicked");
    EXPECT_DOUBLE_EQ(preview->zoom(), 1);

    // The title closes it, which leaves room for the tree and stops reading images.
    QQuickItem* header = findNamed(f.view.rootObject(), QStringLiteral("previewHeader"));
    ASSERT_NE(header, nullptr);
    QQuickItem* panel = header->parentItem()->parentItem();
    const qreal openHeight = panel->height();
    f.clickAt(header->mapToScene(QPointF(header->width() / 2, header->height() / 2)), Qt::NoModifier);
    EXPECT_TRUE(waitUntil([&] { return panel->height() < 40; })) << panel->height();
    EXPECT_EQ(preview->source(), QString());
    EXPECT_FALSE(summary->isVisible());

    f.clickAt(header->mapToScene(QPointF(header->width() / 2, header->height() / 2)), Qt::NoModifier);
    EXPECT_TRUE(waitUntil([&] { return panel->height() > 100; })) << panel->height();
    EXPECT_NEAR(panel->height(), openHeight, 1);
    EXPECT_EQ(preview->source(), f.context.currentPath());
}

TEST(PathBar, BackArrowOffersHistoryAndTheStarOffersFolders) {
    BrowserPageFixture f;
    QSettings().remove(QStringLiteral("Folders"));
    QQuickItem* bar = f.view.rootObject()->property("pathBar").value<QQuickItem*>();
    ASSERT_NE(bar, nullptr);
    const QString first = f.context.directory();
    const QString inner = QDir(first).filePath(QStringLiteral("inner"));
    const QString deeper = QDir(inner).filePath(QStringLiteral("deeper"));
    f.context.openPath(inner);
    f.context.openPath(deeper);

    QQuickItem* back = findNamed(bar, QStringLiteral("backButton"));
    ASSERT_NE(back, nullptr);
    QObject* history = bar->findChild<QObject*>(QStringLiteral("historyMenu"));
    ASSERT_NE(history, nullptr);
    QMetaObject::invokeMethod(back, "secondaryActivated");
    EXPECT_TRUE(waitUntil([&] { return history->property("visible").toBool(); }));
    EXPECT_EQ(history->property("count").toInt(), 2);
    QMetaObject::invokeMethod(history, "close");

    QQuickItem* star = findNamed(bar, QStringLiteral("folderButton"));
    ASSERT_NE(star, nullptr);
    EXPECT_FALSE(star->property("lit").toBool());
    QMetaObject::invokeMethod(star, "activated");
    EXPECT_TRUE(star->property("menuOpen").toBool());
    QStringList texts = rowTexts(star->property("menuRows").toList());
    EXPECT_TRUE(texts.value(0).startsWith(QStringLiteral("Add ")));
    EXPECT_EQ(texts.value(1), QStringLiteral("Recent"));
    EXPECT_TRUE(texts.contains(QDir::toNativeSeparators(inner)));
    EXPECT_FALSE(texts.contains(QDir::toNativeSeparators(deeper))) << "not where we already are";

    QVariantList rows = star->property("menuRows").toList();
    QMetaObject::invokeMethod(star, "choose", Q_ARG(QVariant, rows.first()));
    EXPECT_TRUE(star->property("lit").toBool());
    QMetaObject::invokeMethod(star, "activated");
    texts = rowTexts(star->property("menuRows").toList());
    EXPECT_TRUE(texts.value(0).startsWith(QStringLiteral("Remove ")));
    EXPECT_EQ(texts.value(1), QStringLiteral("Favorites"));
    EXPECT_EQ(texts.value(2), QDir::toNativeSeparators(deeper));

    // Choosing a recent folder goes there.
    rows = star->property("menuRows").toList();
    const auto toInner = std::ranges::find_if(rows, [&](const QVariant& row) { return row.toMap().value(QStringLiteral("path")).toString() == inner; });
    ASSERT_NE(toInner, rows.end());
    QMetaObject::invokeMethod(star, "choose", Q_ARG(QVariant, *toInner));
    EXPECT_EQ(f.context.directory(), inner);
    EXPECT_FALSE(star->property("lit").toBool());
    QSettings().remove(QStringLiteral("Folders"));
}

TEST(PathBar, TheStarListsPlacesThatFoldAway) {
    BrowserPageFixture f;
    QSettings().remove(QStringLiteral("FolderButton"));
    QQuickItem* bar = f.view.rootObject()->property("pathBar").value<QQuickItem*>();
    QQuickItem* star = findNamed(bar, QStringLiteral("folderButton"));
    ASSERT_NE(star, nullptr);
    const auto placesRow = [&] {
        const QVariantList rows = star->property("menuRows").toList();
        const auto found = std::ranges::find_if(rows, [](const QVariant& row) { return row.toMap().value(QStringLiteral("kind")) == QStringLiteral("places"); });
        return found == rows.end() ? QVariant() : *found;
    };
    const auto hasHome = [&] {
        const QString home = QDir::homePath();
        const QVariantList rows = star->property("menuRows").toList();
        return std::ranges::any_of(rows, [&](const QVariant& row) { return row.toMap().value(QStringLiteral("path")).toString() == home; });
    };

    QMetaObject::invokeMethod(star, "activated");
    ASSERT_TRUE(placesRow().isValid());
    EXPECT_TRUE(hasHome());

    QMetaObject::invokeMethod(star, "choose", Q_ARG(QVariant, placesRow()));
    EXPECT_TRUE(star->property("menuOpen").toBool()) << "folding keeps the menu open";
    EXPECT_FALSE(hasHome());
    QMetaObject::invokeMethod(star, "choose", Q_ARG(QVariant, placesRow()));
    EXPECT_TRUE(hasHome());

    QVariantList rows = star->property("menuRows").toList();
    const auto toHome = std::ranges::find_if(rows, [](const QVariant& row) { return row.toMap().value(QStringLiteral("path")).toString() == QDir::homePath(); });
    QMetaObject::invokeMethod(star, "choose", Q_ARG(QVariant, *toHome));
    EXPECT_FALSE(star->property("menuOpen").toBool());
    EXPECT_EQ(f.context.directory(), QDir::homePath());
    QSettings().remove(QStringLiteral("FolderButton"));
}

TEST(PathBar, AClickInItsListsDoesNotReachTheThumbnailsBelow) {
    TreeFixture f;
    QSettings().remove(QStringLiteral("Folders"));
    QQuickItem* bar = f.view.rootObject()->property("pathBar").value<QQuickItem*>();
    ASSERT_NE(bar, nullptr);
    QQuickItem* star = findNamed(bar, QStringLiteral("folderButton"));
    ASSERT_NE(star, nullptr);
    QQuickItem* grid = f.find("QQuickGridView");
    ASSERT_NE(grid, nullptr);
    const QString selectedBefore = f.selected();

    // The first row of the list, over the thumbnail of another file.
    QMetaObject::invokeMethod(star, "activated");
    ASSERT_TRUE(star->property("menuOpen").toBool());
    f.settle();
    const QPointF onRow = star->mapToScene(QPointF(200, star->height() + 13));
    int below = -1;
    QMetaObject::invokeMethod(grid, "indexAt", Q_RETURN_ARG(int, below), Q_ARG(qreal, grid->mapFromScene(onRow).x()), Q_ARG(qreal, grid->mapFromScene(onRow).y()));
    ASSERT_GE(below, 1) << "over a file other than the current one";
    f.clickAt(onRow, Qt::NoModifier);
    f.settle();
    EXPECT_FALSE(star->property("menuOpen").toBool());
    EXPECT_TRUE(star->property("lit").toBool()) << "the click did what the row says";
    EXPECT_EQ(f.selected(), selectedBefore);
    QSettings().remove(QStringLiteral("Folders"));
}

TEST(BrowserPage, TreeShowsTheOpenFolderAtStartupHoweverDeep) {
    const QString deep = QStringLiteral("d1/d2/d3/d4/d5/d6/d7/d8/d9/d10/d11/d12/d13/d14/d15/d16/d17/d18/d19/d20/"
                                        "d21/d22/d23/d24/d25/d26/d27/d28/d29/d30/d31/d32/d33/d34/d35/d36/d37/d38/d39/d40");
    MainFixture f([&](const QTemporaryDir& dir) {
        QDir(dir.path()).mkpath(deep);
        writeJpeg(dir.filePath(deep + QStringLiteral("/a.jpg")));
    }, deep);
    ASSERT_NE(f.window, nullptr);
    auto* tree = qobject_cast<QQuickItem*>(f.find(QStringLiteral("folderTree")));
    ASSERT_NE(tree, nullptr);
    auto* model = qobject_cast<iv::DirectoryTreeModel*>(tree->property("model").value<QObject*>());
    const QString folder = f.dir.filePath(deep);
    // Shown without anyone scrolling, and still shown once the folders on the way have loaded.
    const auto shown = [&] {
        QQuickItem* row = nullptr;
        QMetaObject::invokeMethod(tree, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, row), Q_ARG(QModelIndex, model->indexForPath(folder)));
        if (!row || !row->isVisible()) {
            return false;
        }
        const QPointF centre = row->mapToItem(tree, QPointF(row->width() / 2, row->height() / 2));
        return tree->contains(centre);
    };
    EXPECT_TRUE(waitUntil(shown)) << "shown at all";
    waitUntil([] { return false; }, 1000);
    EXPECT_TRUE(shown()) << "still shown after the loading settles";
}
