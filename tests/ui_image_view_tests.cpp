#include "ui_test_support.h"

TEST(ImageView, RendersTheImageFittedAndCentred) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("quad.jpg"));
    JpegOptions options;
    options.width = 800;
    options.height = 800;
    options.pixel = [](int x, int y) {
        if (y < 400) {
            return x < 400 ? Rgb{220, 30, 30} : Rgb{30, 220, 30};
        }
        return x < 400 ? Rgb{30, 30, 220} : Rgb{230, 230, 30};
    };
    writeJpeg(path, options);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    ASSERT_EQ(fixture.view->status(), ImageView::Status::Ready);
    EXPECT_EQ(fixture.view->imageWidth(), 800);
    EXPECT_DOUBLE_EQ(fixture.view->zoom(), 0.5);

    const QImage image = fixture.grab();
    expectColor(image, 100, 100, QColor(220, 30, 30));
    expectColor(image, 300, 100, QColor(30, 220, 30));
    expectColor(image, 100, 300, QColor(30, 30, 220));
    expectColor(image, 300, 300, QColor(230, 230, 30));
}

TEST(ImageView, AppliesExifOrientation) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("rotated.jpg"));
    JpegOptions options;
    options.width = 800;
    options.height = 400;
    options.orientation = 6;  // rotate 90 degrees clockwise to display
    options.pixel = [](int x, int) { return x < 400 ? Rgb{220, 30, 30} : Rgb{30, 30, 220}; };
    writeJpeg(path, options);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    EXPECT_EQ(fixture.view->imageWidth(), 400);
    EXPECT_EQ(fixture.view->imageHeight(), 800);

    // Stored left half (red) ends up on top after a clockwise quarter turn.
    const QImage image = fixture.grab();
    expectColor(image, 200, 100, QColor(220, 30, 30));
    expectColor(image, 200, 300, QColor(30, 30, 220));
}

TEST(ImageView, ActualSizeDrawsTheFullResolutionLevelAcrossTiles) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    JpegOptions options;
    options.width = 4400;
    options.height = 4400;
    // The tile boundary at 2048 lies in the middle of the view at 100%.
    options.pixel = [](int x, int) { return x < 2048 ? Rgb{220, 30, 30} : Rgb{30, 30, 220}; };
    writeJpeg(path, options);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    fixture.view->actualSize();
    EXPECT_DOUBLE_EQ(fixture.view->zoom(), 1.0);

    // At 100% and centred, view x=0 is image x=2000, so the boundary is at view x=48.
    const QImage image = fixture.grab();
    expectColor(image, 20, 200, QColor(220, 30, 30));
    expectColor(image, 100, 200, QColor(30, 30, 220));
}

TEST(ImageView, ShowsPagesAndResetsThemForTheNextImage) {
    QTemporaryDir dir;
    TiffImage red;
    red.width = 200;
    red.height = 200;
    red.pixel = [](int, int) { return Rgb{220, 30, 30}; };
    TiffImage blue = red;
    blue.width = 100;
    blue.pixel = [](int, int) { return Rgb{30, 30, 220}; };
    const QString multi = dir.filePath(QStringLiteral("multi.tif"));
    iv::test::writeTiff(QFileInfo(multi).filesystemFilePath(), {red, blue});
    const QString single = dir.filePath(QStringLiteral("single.jpg"));
    writeJpeg(single);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(multi));
    ASSERT_EQ(fixture.view->status(), ImageView::Status::Ready);
    EXPECT_EQ(fixture.view->pageCount(), 2);
    EXPECT_EQ(fixture.view->page(), 0);
    EXPECT_EQ(fixture.view->imageWidth(), 200);

    fixture.view->setPage(1);
    ASSERT_TRUE(waitUntil([&] { return fixture.view->page() == 1; }));
    EXPECT_EQ(fixture.view->imageWidth(), 100);
    expectColor(fixture.grab(), 200, 200, QColor(30, 30, 220));

    // Pages that do not exist are ignored.
    fixture.view->setPage(5);
    fixture.view->setPage(-1);
    EXPECT_EQ(fixture.view->page(), 1);

    // Nothing about the previous file survives into the next one.
    ASSERT_TRUE(fixture.load(single));
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Ready; }));
    EXPECT_EQ(fixture.view->page(), 0);
    EXPECT_EQ(fixture.view->pageCount(), 1);
    EXPECT_EQ(fixture.view->imageWidth(), 64);

    ASSERT_TRUE(fixture.load(multi));
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Ready; }));
    EXPECT_EQ(fixture.view->page(), 0);
    EXPECT_EQ(fixture.view->imageWidth(), 200);
}

TEST(ImageView, SixteenBitGrayStretchesOnRequestAndContrastResetsPerImage) {
    QTemporaryDir dir;
    // A narrow range of 16-bit values, which would look black without stretching.
    TiffImage gray;
    gray.width = 200;
    gray.height = 200;
    gray.bits = 16;
    gray.samples = 1;
    gray.pixel = [](int x, int) { return x < 100 ? Rgb{2, 2, 2} : Rgb{4, 4, 4}; };
    const QString grayPath = dir.filePath(QStringLiteral("gray.tif"));
    iv::test::writeTiff(QFileInfo(grayPath).filesystemFilePath(), {gray});

    TiffImage photo = gray;
    photo.samples = 3;
    photo.pixel = [](int, int) { return Rgb{128, 128, 128}; };
    const QString photoPath = dir.filePath(QStringLiteral("photo.tif"));
    iv::test::writeTiff(QFileInfo(photoPath).filesystemFilePath(), {photo});

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(grayPath));
    EXPECT_TRUE(fixture.view->contrastAdjustable());
    EXPECT_FALSE(fixture.view->autoContrast());
    QImage image = fixture.grab();
    EXPECT_LT(image.pixelColor(250, 200).red(), 20);  // the true values are almost black

    fixture.view->setAutoContrast(true);
    EXPECT_TRUE(fixture.view->autoContrast());
    image = fixture.grab();
    EXPECT_LT(image.pixelColor(150, 200).red(), 40);   // bottom of the stretched range
    EXPECT_GT(image.pixelColor(250, 200).red(), 215);  // top of it

    // The manual choice belongs to that image and does not carry over.
    ASSERT_TRUE(fixture.load(photoPath));
    EXPECT_FALSE(fixture.view->autoContrast());
    ASSERT_TRUE(fixture.load(grayPath));
    EXPECT_FALSE(fixture.view->autoContrast());
}

TEST(ImageView, KeptAutoContrastCarriesOverToEveryImageWithMoreThanEightBits) {
    QTemporaryDir dir;
    TiffImage gray;
    gray.width = 100;
    gray.height = 100;
    gray.bits = 16;
    gray.samples = 1;
    gray.pixel = [](int x, int) { return x < 50 ? Rgb{2, 2, 2} : Rgb{4, 4, 4}; };
    const QString first = dir.filePath(QStringLiteral("first.tif"));
    const QString second = dir.filePath(QStringLiteral("second.tif"));
    iv::test::writeTiff(QFileInfo(first).filesystemFilePath(), {gray});
    iv::test::writeTiff(QFileInfo(second).filesystemFilePath(), {gray});
    const QString jpeg = dir.filePath(QStringLiteral("plain.jpg"));
    writeJpeg(jpeg);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(first));
    ASSERT_FALSE(fixture.view->autoContrast());
    fixture.view->setKeepAutoContrast(true);
    EXPECT_TRUE(fixture.view->autoContrast()) << "on for the image shown too";
    ASSERT_TRUE(fixture.load(second));
    EXPECT_TRUE(fixture.view->autoContrast());
    ASSERT_TRUE(fixture.load(jpeg));
    EXPECT_FALSE(fixture.view->autoContrast()) << "8 bits are shown as they are";

    fixture.view->setKeepAutoContrast(false);
    ASSERT_TRUE(fixture.load(first));
    EXPECT_FALSE(fixture.view->autoContrast());
}

TEST(ImageView, EightBitImagesHaveNoContrastToAdjust) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("plain.jpg"));
    writeJpeg(path);
    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    EXPECT_FALSE(fixture.view->contrastAdjustable());
}

TEST(ImageView, DrawsAFlatLargeTiffThroughOverviewsAndRealTiles) {
    QTemporaryDir dir;
    TiffImage large;
    large.width = 5000;
    large.height = 5000;
    large.tileSize = 512;
    large.compression = 8;  // deflate
    large.pixel = [](int x, int) { return x < 2500 ? Rgb{220, 30, 30} : Rgb{30, 30, 220}; };
    const QString path = dir.filePath(QStringLiteral("flat.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {large});

    ViewFixture fixture;
    fixture.view->setSource(path);
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() != ImageView::Status::Loading; }, 20000));
    ASSERT_EQ(fixture.view->status(), ImageView::Status::Ready);
    QImage image = fixture.grab();
    expectColor(image, 100, 200, QColor(220, 30, 30));
    expectColor(image, 300, 200, QColor(30, 30, 220));

    // At 100% the view is centred on the colour boundary, using real tiles.
    fixture.view->actualSize();
    image = fixture.grab();
    expectColor(image, 100, 200, QColor(220, 30, 30));
    expectColor(image, 300, 200, QColor(30, 30, 220));
}

TEST(ImageView, DetailsFollowTheFileAndPage) {
    QTemporaryDir dir;
    TiffImage first;
    first.width = 100;
    TiffImage second;
    second.width = 50;
    second.bits = 16;
    const QString path = dir.filePath(QStringLiteral("two.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {first, second});

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Name")), QStringLiteral("two.tif"));
    EXPECT_FALSE(detail(*fixture.view, QStringLiteral("Size")).isEmpty());
    EXPECT_FALSE(detail(*fixture.view, QStringLiteral("Modified")).isEmpty());
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Page")), QStringLiteral("1 of 2"));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Bit depth")), QStringLiteral("8-bit"));

    fixture.view->setPage(1);
    ASSERT_TRUE(waitUntil([&] { return fixture.view->page() == 1; }));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Page")), QStringLiteral("2 of 2"));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Bit depth")), QStringLiteral("16-bit"));
    EXPECT_EQ(detail(*fixture.view, QStringLiteral("Name")), QStringLiteral("two.tif"));  // survives a page change

    fixture.view->setSource(dir.filePath(QStringLiteral("missing.tif")));
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Error; }));
    EXPECT_TRUE(fixture.view->details().isEmpty());
}

TEST(ImageView, TheLocationLinksToAMap) {
    QTemporaryDir dir;
    iv::test::JpegOptions options;
    options.exif = iv::test::exampleExifBlock(true);
    const QString path = dir.filePath(QStringLiteral("located.jpg"));
    iv::test::writeFile(QFileInfo(path).filesystemFilePath(), encodeJpeg(options));

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    QVariantMap coordinates;
    for (const QVariant& row : fixture.view->details()) {
        if (row.toMap().value(QStringLiteral("label")) == QStringLiteral("Coordinates")) {
            coordinates = row.toMap();
        }
    }
    EXPECT_EQ(coordinates.value(QStringLiteral("value")).toString(), QStringLiteral("49.19183° N, 16.60500° E"));
    EXPECT_EQ(coordinates.value(QStringLiteral("link")).toString(),
              QStringLiteral("https://geohack.toolforge.org/geohack.php?params=49.191833_N_16.605000_E"));
    EXPECT_FALSE(detail(*fixture.view, QStringLiteral("Altitude")).isEmpty());
}

TEST(ImageView, ComputesHistogramOnlyWhenEnabledAndFollowsThePage) {
    QTemporaryDir dir;
    TiffImage bright;
    bright.pixel = [](int, int) { return Rgb{200, 200, 200}; };
    TiffImage dark = bright;
    dark.pixel = [](int, int) { return Rgb{20, 20, 20}; };
    const QString path = dir.filePath(QStringLiteral("two.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {bright, dark});

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    QCoreApplication::processEvents();
    EXPECT_FALSE(histogramOf(*fixture.view));

    fixture.view->setHistogramEnabled(true);
    ASSERT_TRUE(waitUntil([&] { return bool(histogramOf(*fixture.view)); }));
    EXPECT_TRUE(histogramOf(*fixture.view)->gray);
    EXPECT_GT(histogramOf(*fixture.view)->channels[0][200], 0u);

    fixture.view->setPage(1);
    ASSERT_TRUE(waitUntil([&] {
        const auto histogram = histogramOf(*fixture.view);
        return histogram && histogram->channels[0][20] > 0;
    }));

    // A failed open leaves nothing behind.
    fixture.view->setSource(dir.filePath(QStringLiteral("missing.tif")));
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Error; }));
    EXPECT_FALSE(histogramOf(*fixture.view));
}

TEST(ImageView, ChangingContrastDoesNotLoseAPendingHistogram) {
    QTemporaryDir dir;
    TiffImage gray;
    gray.bits = 16;
    gray.samples = 1;
    const QString path = dir.filePath(QStringLiteral("gray.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {gray});

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    fixture.view->setHistogramEnabled(true);
    fixture.view->setAutoContrast(false);  // cancels the work queue group
    ASSERT_TRUE(waitUntil([&] { return bool(histogramOf(*fixture.view)); }));
}

TEST(HistogramView, DrawsGrayAndRgbDifferently) {
    auto gray = emptyHistogram(256, true);
    gray->channels[0][100] = 50;
    const QImage grayImage = drawHistogram(gray);
    const QColor grayBar = grayImage.pixelColor(100, 99);
    EXPECT_EQ(grayBar.red(), grayBar.green());
    EXPECT_EQ(grayBar.green(), grayBar.blue());
    EXPECT_EQ(barHeight(grayImage, 100), 100);
    EXPECT_EQ(barHeight(grayImage, 50), 0);

    auto rgb = emptyHistogram(256, false);
    rgb->channels[0][50] = 50;
    rgb->channels[1][100] = 50;
    rgb->channels[2][150] = 50;
    const QImage rgbImage = drawHistogram(rgb);
    EXPECT_GT(rgbImage.pixelColor(50, 99).red(), rgbImage.pixelColor(50, 99).green() + 100);
    EXPECT_GT(rgbImage.pixelColor(100, 99).green(), rgbImage.pixelColor(100, 99).red() + 100);
    EXPECT_GT(rgbImage.pixelColor(150, 99).blue(), rgbImage.pixelColor(150, 99).red() + 100);
}

TEST(HistogramView, UsesThePaletteForChannelsAndOverlaps) {
    const auto colorOf = [](std::initializer_list<int> channels) {
        auto histogram = emptyHistogram(256, false);
        for (const int c : channels) {
            histogram->channels[static_cast<std::size_t>(c)][100] = 50;
        }
        return drawHistogram(histogram).pixelColor(100, 99).name();
    };
    EXPECT_EQ(colorOf({0}), QStringLiteral("#e22023"));
    EXPECT_EQ(colorOf({1}), QStringLiteral("#009800"));
    EXPECT_EQ(colorOf({2}), QStringLiteral("#6868e7"));
    EXPECT_EQ(colorOf({0, 1}), QStringLiteral("#d3d16b"));
    EXPECT_EQ(colorOf({1, 2}), QStringLiteral("#00e7e7"));
    EXPECT_EQ(colorOf({0, 2}), QStringLiteral("#ffa5ef"));
    EXPECT_EQ(colorOf({0, 1, 2}), QStringLiteral("#cccccc"));

    auto gray = emptyHistogram(256, true);
    gray->channels[0][100] = 50;
    EXPECT_EQ(drawHistogram(gray).pixelColor(100, 99).name(), QStringLiteral("#cccccc"));
}

TEST(HistogramView, SixteenBitBinsAreCombinedIntoColumns) {
    auto histogram = emptyHistogram(65536, true);
    // 256 columns of 256 bins each: these all land in column 128.
    histogram->channels[0][32768] = 10;
    histogram->channels[0][32900] = 10;
    histogram->channels[0][1000] = 10;  // column 3, half as tall
    const QImage image = drawHistogram(histogram);
    EXPECT_EQ(barHeight(image, 128), 100);
    EXPECT_EQ(barHeight(image, 3), 50);
}

TEST(HistogramView, LogScaleLiftsSmallCounts) {
    auto histogram = emptyHistogram(256, true);
    histogram->channels[0][100] = 100000;
    histogram->channels[0][150] = 100;
    EXPECT_LT(barHeight(drawHistogram(histogram, false), 150), 3);
    EXPECT_GT(barHeight(drawHistogram(histogram, true), 150), 30);
}

TEST(HistogramView, AClippedEndDoesNotFlattenALinearScale) {
    auto histogram = emptyHistogram(256, true);
    histogram->channels[0][0] = 1000000;
    histogram->channels[0][100] = 100;
    const QImage image = drawHistogram(histogram);
    EXPECT_EQ(barHeight(image, 100), 100);
    EXPECT_EQ(barHeight(image, 0), 100);  // cut off at the top
}

TEST(HistogramView, FitRangeSpreadsTheValuesInUse) {
    auto histogram = emptyHistogram(65536, true);
    histogram->measuredSize = {10, 10};
    histogram->channels[0][1000] = 10;
    histogram->channels[0][4000] = 10;  // like 12-bit data in 16-bit samples

    iv::HistogramView view;
    view.setHistogram(QVariant::fromValue(std::shared_ptr<const iv::Histogram>(histogram)));
    EXPECT_TRUE(view.description().startsWith(QStringLiteral("0 to 65535")));
    view.setFitRange(true);
    EXPECT_TRUE(view.description().startsWith(QStringLiteral("1000 to 4000")));

    const QImage image = drawHistogram(histogram, false, true);
    EXPECT_GT(barHeight(image, 0), 0);
    EXPECT_GT(barHeight(image, 255), 0);
    EXPECT_EQ(barHeight(image, 128), 0);
}

TEST(HistogramView, EmptyIsSafe) {
    iv::HistogramView view;
    EXPECT_TRUE(view.description().isEmpty());
    view.setHistogram(QVariant());
    view.setFitRange(true);
    EXPECT_TRUE(view.description().isEmpty());
    const QImage image = drawHistogram(nullptr);
    EXPECT_EQ(barHeight(image, 100), 0);
}

TEST(ImageView, ReportsErrorsAndRecovers) {
    QTemporaryDir dir;
    const QString bad = dir.filePath(QStringLiteral("bad.jpg"));
    QFile file(bad);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("not an image");
    file.close();
    const QString good = dir.filePath(QStringLiteral("good.jpg"));
    writeJpeg(good);

    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(bad));
    EXPECT_EQ(fixture.view->status(), ImageView::Status::Error);
    EXPECT_FALSE(fixture.view->errorString().isEmpty());

    ASSERT_TRUE(fixture.load(good));
    EXPECT_EQ(fixture.view->status(), ImageView::Status::Ready);
    EXPECT_TRUE(fixture.view->errorString().isEmpty());
}

TEST(ImageView, RapidSwitchingEndsOnTheLastImage) {
    QTemporaryDir dir;
    JpegOptions small;
    small.width = 32;
    small.height = 16;
    const QString last = dir.filePath(QStringLiteral("last.jpg"));
    writeJpeg(last, small);

    JpegOptions large;
    large.width = 2000;
    large.height = 2000;
    const QString first = dir.filePath(QStringLiteral("first.jpg"));
    writeJpeg(first, large);

    ViewFixture fixture;
    for (int i = 0; i < 20; ++i) {
        fixture.view->setSource(first);
        fixture.view->setSource(last);
    }
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Ready; }));
    EXPECT_EQ(fixture.view->imageWidth(), 32);
}

TEST(ImageView, DestroyingWhileLoadingIsSafe) {
    QTemporaryDir dir;
    JpegOptions large;
    large.width = 3000;
    large.height = 3000;
    const QString path = dir.filePath(QStringLiteral("large.jpg"));
    writeJpeg(path, large);

    for (int i = 0; i < 5; ++i) {
        ViewFixture fixture;
        fixture.view->setSource(path);
    }
    QCoreApplication::processEvents();
    SUCCEED();
}

TEST(ColorView, ConvertsAndFollowsTheSetting) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(writeAdobeJpeg(dir, QStringLiteral("a.jpg"))));
    fixture.view->fitToView();
    const QColor managed = fixture.grab().pixelColor(200, 200);
    EXPECT_LT(managed.red(), 30);  // sRGB has to go well below the file's 60 to match Adobe's green
    EXPECT_NEAR(managed.green(), 200, 4);

    iv::ColorSettings::instance()->setEnabled(false);
    const QColor unmanaged = fixture.grab().pixelColor(200, 200);
    EXPECT_NEAR(unmanaged.red(), 60, 4);
    EXPECT_NEAR(unmanaged.green(), 200, 4);
    EXPECT_NEAR(unmanaged.blue(), 90, 4);

    iv::ColorSettings::instance()->setEnabled(true);
    EXPECT_NEAR(fixture.grab().pixelColor(200, 200).red(), managed.red(), 1);
}

TEST(ColorView, FollowsADisplayProfile) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(writeAdobeJpeg(dir, QStringLiteral("a.jpg"))));
    fixture.view->fitToView();
    EXPECT_LT(fixture.grab().pixelColor(200, 200).red(), 30);

    // Showing Adobe RGB on an Adobe RGB display changes nothing.
    const QString profile = dir.filePath(QStringLiteral("display.icc"));
    iv::test::writeFile(QFileInfo(profile).filesystemFilePath(), iv::adobeRgbProfile());
    EXPECT_EQ(iv::ColorSettings::instance()->useDisplayProfile(QUrl::fromLocalFile(profile)), QString());
    EXPECT_EQ(iv::ColorSettings::instance()->displayProfileName(), QStringLiteral("Adobe RGB (1998)"));
    EXPECT_NEAR(fixture.grab().pixelColor(200, 200).red(), 60, 4);

    iv::ColorSettings::instance()->useSrgbDisplay();
    EXPECT_EQ(iv::ColorSettings::instance()->displayProfileName(), QStringLiteral("sRGB"));
    EXPECT_LT(fixture.grab().pixelColor(200, 200).red(), 30);
}

TEST(ColorView, RefusesFilesThatAreNoDisplayProfile) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    const QString junk = dir.filePath(QStringLiteral("junk.icc"));
    iv::test::writeFile(QFileInfo(junk).filesystemFilePath(), {1, 2, 3, 4});
    EXPECT_FALSE(iv::ColorSettings::instance()->useDisplayProfile(QUrl::fromLocalFile(junk)).isEmpty());
    EXPECT_FALSE(iv::ColorSettings::instance()->useDisplayProfile(QUrl::fromLocalFile(dir.filePath(QStringLiteral("none.icc")))).isEmpty());
    EXPECT_FALSE(iv::ColorSettings::instance()->hasDisplayProfile());
}

TEST(ColorView, AChangeWhileLoadingIsNotLost) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    ViewFixture fixture;
    fixture.view->setSource(writeAdobeJpeg(dir, QStringLiteral("a.jpg")));
    iv::ColorSettings::instance()->setEnabled(false);
    ASSERT_TRUE(waitUntil([&] { return fixture.view->status() == ImageView::Status::Ready; }));
    fixture.view->fitToView();
    EXPECT_NEAR(fixture.grab().pixelColor(200, 200).red(), 60, 4);
}

TEST(ColorView, ThumbnailsGetNewUrlsWhenTheSettingChanges) {
    const ColorSettingsGuard guard;
    QTemporaryDir dir;
    writeAdobeJpeg(dir, QStringLiteral("a.jpg"));
    BrowseContext context;
    context.openPath(dir.path());
    ASSERT_TRUE(waitUntil([&] { return context.fileCount() == 1; }));
    QAbstractItemModel* model = context.files();
    const int role = model->roleNames().key("thumbnail");
    const QString before = model->data(model->index(0, 0), role).toString();
    int changes = 0;
    QObject::connect(model, &QAbstractItemModel::dataChanged, [&](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
        changes += roles.contains(role) ? 1 : 0;
    });
    iv::ColorSettings::instance()->setEnabled(false);
    EXPECT_EQ(changes, 1);
    EXPECT_NE(model->data(model->index(0, 0), role).toString(), before);
}

TEST(ImageView, ResamplingNeedsTheGpuAndLeavesTheSoftwareRendererAlone) {
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("a.jpg"));
    JpegOptions options;
    options.width = 800;
    options.height = 800;
    options.pixel = [](int, int) { return Rgb{200, 40, 40}; };
    writeJpeg(path, options);
    ViewFixture fixture;
    ASSERT_TRUE(fixture.load(path));
    fixture.view->setResampling(true);
    fixture.view->zoomOut();
    if (fixture.window.rendererInterface()->graphicsApi() != QSGRendererInterface::Software) {
        GTEST_SKIP() << "for the software renderer";
    }
    EXPECT_FALSE(fixture.view->resampleActive());
    expectColor(fixture.grab(), 200, 200, QColor(200, 40, 40));
}

// Needs a GPU or at least a GL driver, so run this one by hand on a real display or Xvfb:
//   QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl iv_ui_tests --gtest_filter='ResampledView.*'
TEST(ResampledView, MatchesTheKernelComputedOnTheCpu) {
    QTemporaryDir dir;
    const int width = 600;
    const int height = 500;
    const auto pattern = [](int x, int y) {
        const auto v = static_cast<std::uint8_t>((x / 7 + y / 5) % 2 != 0 ? 230 : 25);
        return Rgb{v, static_cast<std::uint8_t>((x * 255) / 600), static_cast<std::uint8_t>(255 - v)};
    };
    TiffImage image;
    image.width = width;
    image.height = height;
    image.pixel = pattern;
    const QString path = dir.filePath(QStringLiteral("a.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});

    BrowseContext context;
    QQuickView window;
    window.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    auto* view = window.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    window.rootObject()->setProperty("sharpResampling", true);
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    ASSERT_TRUE(waitUntil([&] { return view->resampleActive(); }));

    QImage shown;
    const auto settle = [&] {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        shown = grabIfShown(window);
    };
    settle();

    const auto expected = [&](int px, int py) {
        const double ratio = view->resampleRatio();
        const double spacing = view->resampleSpacing();
        const QRectF bounds = view->resampleBounds();
        const double cx = view->resamplePhase().x() + (px + 0.5) * ratio - bounds.x();
        const double cy = view->resamplePhase().y() + (py + 0.5) * ratio - bounds.y();
        std::array<double, 3> sum{};
        double weights = 0;
        const int radiusX = static_cast<int>(std::ceil(2.5 * spacing));
        for (int j = static_cast<int>(std::floor(cy)) - radiusX; j <= static_cast<int>(std::floor(cy)) + radiusX; ++j) {
            const double wy = mks2013((j + 0.5 - cy) / spacing);
            for (int i = static_cast<int>(std::floor(cx)) - radiusX; i <= static_cast<int>(std::floor(cx)) + radiusX; ++i) {
                const double w = wy * mks2013((i + 0.5 - cx) / spacing);
                const Rgb c = pattern(std::clamp(i, 0, width - 1), std::clamp(j, 0, height - 1));
                sum[0] += w * srgbToLinear(c.r / 255.0);
                sum[1] += w * srgbToLinear(c.g / 255.0);
                sum[2] += w * srgbToLinear(c.b / 255.0);
                weights += w;
            }
        }
        return std::array<int, 3>{static_cast<int>(std::lround(255 * linearToSrgb(sum[0] / weights))),
                                  static_cast<int>(std::lround(255 * linearToSrgb(sum[1] / weights))),
                                  static_cast<int>(std::lround(255 * linearToSrgb(sum[2] / weights)))};
    };

    const auto compareAll = [&](int tolerance) {
        if (shown.isNull()) {
            ADD_FAILURE() << "the window was closed";
            return;
        }
        const QRectF rect = view->resampleImageRect();
        int worst = 0;
        int checked = 0;
        for (int y = 0; y < shown.height(); ++y) {
            for (int x = 0; x < shown.width(); ++x) {
                const double u = (x + 0.5) / shown.width();
                const double v = (y + 0.5) / shown.height();
                if (u < rect.left() || v < rect.top() || u >= rect.right() || v >= rect.bottom()) {
                    continue;
                }
                const auto e = expected(x, y);
                const QColor c = shown.pixelColor(x, y);
                worst = std::max({worst, std::abs(c.red() - e[0]), std::abs(c.green() - e[1]), std::abs(c.blue() - e[2])});
                ++checked;
            }
        }
        EXPECT_GT(checked, 1000);
        EXPECT_LE(worst, tolerance);
    };
    compareAll(2);  // minifying
    view->zoomIn();
    view->zoomIn();
    view->zoomIn();
    ASSERT_TRUE(view->resampleActive());
    settle();
    compareAll(2);  // stretched less or not at all, panned to wherever the zoom left it

    // Panning, even onto tiles that are not there yet, must not take the kernel away, as dropping
    // to another filter and back shows as flicker.
    ASSERT_TRUE(view->resampleActive());
    int dropped = 0;
    const QMetaObject::Connection watching = QObject::connect(view, &ImageView::resampleChanged, view, [&] {
        dropped += view->resampleActive() ? 0 : 1;
    });
    const auto sendMouse = [&](QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, at, at, window.mapToGlobal(at), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &event);
        QCoreApplication::processEvents();
    };
    const QPointF start(window.width() / 2.0, window.height() / 2.0);
    sendMouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    for (int step = 1; step <= 40; ++step) {
        sendMouse(QEvent::MouseMove, start + QPointF(step * 2 % 60, step % 30), Qt::NoButton, Qt::LeftButton);
        QTest::qWait(10);
    }
    sendMouse(QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
    QTest::qWait(600);
    QObject::disconnect(watching);
    EXPECT_EQ(dropped, 0);

    // Turned off, the view shows again at once.
    window.rootObject()->setProperty("sharpResampling", false);
    ASSERT_FALSE(view->resampleActive());
    settle();
    ASSERT_FALSE(shown.isNull()) << "the window was closed";
    const QPointF middle = view->imageToItem({width / 2.0, height / 2.0}) * window.devicePixelRatio();
    const Rgb inside = pattern(width / 2, height / 2);
    const QColor c = shown.pixelColor(middle.toPoint());
    EXPECT_NEAR(c.green(), inside.g, 40) << "the picture, not the background";
}

// Needs a GPU, like the test above:
//   QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl iv_ui_tests --gtest_filter='ColorPreview.*'
TEST(ColorPreview, TheShaderShowsWhatApplyingGives) {
    QTemporaryDir dir;
    TiffImage image;
    image.width = 300;
    image.height = 200;
    image.pixel = [](int x, int y) {
        return Rgb{static_cast<std::uint8_t>(x * 255 / 299), static_cast<std::uint8_t>(y * 255 / 199),
                   static_cast<std::uint8_t>((x + y) % 256)};
    };
    const QString path = dir.filePath(QStringLiteral("a.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});
    // A 9-point cube that warms the picture and swaps green and blue a little.
    const QString cubePath = dir.filePath(QStringLiteral("look.cube"));
    {
        QFile cube(cubePath);
        ASSERT_TRUE(cube.open(QIODevice::WriteOnly));
        QTextStream out(&cube);
        out << "LUT_3D_SIZE 9\n";
        for (int b = 0; b < 9; ++b) {
            for (int g = 0; g < 9; ++g) {
                for (int r = 0; r < 9; ++r) {
                    const double rr = r / 8.0;
                    const double gg = g / 8.0;
                    const double bb = b / 8.0;
                    out << std::min(1.0, rr * 1.1 + 0.02) << ' ' << 0.7 * gg + 0.3 * bb << ' ' << 0.3 * gg + 0.6 * bb << '\n';
                }
            }
        }
    }

    BrowseContext context;
    QQuickView window;
    window.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    auto* view = window.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    view->actualSize();
    const auto grab = [&] {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 500) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return grabIfShown(window);
    };
    ASSERT_EQ(view->loadColorCube(QUrl::fromLocalFile(cubePath)), QString());

    const QPointF topLeft = view->imageToItem({0, 0}) * window.devicePixelRatio();
    const auto compare = [&](const QVariantMap& settings) {
        view->previewColors(settings);
        const QImage preview = grab();
        view->adjustColors(settings);
        EXPECT_TRUE(view->colorPreviewActive()) << "until the result shows";
        EXPECT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
        EXPECT_FALSE(view->colorPreviewActive());
        const QImage applied = grab();
        view->undo();
        EXPECT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
        if (preview.isNull() || applied.isNull()) {
            ADD_FAILURE() << "the window was closed";
            return;
        }
        int worst = 0;
        int off = 0;  // by more than 2
        int changed = 0;
        int samples = 0;
        for (int y = 2; y < 198; y += 3) {
            for (int x = 2; x < 298; x += 3) {
                const int px = static_cast<int>(topLeft.x()) + x;
                const int py = static_cast<int>(topLeft.y()) + y;
                const QColor a = preview.pixelColor(px, py);
                const QColor b = applied.pixelColor(px, py);
                const Rgb original = image.pixel(x, y);
                const QColor c(original.r, original.g, original.b);
                const int difference = std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue())});
                worst = std::max(worst, difference);
                off += difference > 2 ? 1 : 0;
                changed += (b != c) ? 1 : 0;
                ++samples;
            }
        }
        EXPECT_GT(changed, 1000) << "the edit shows";
        // The shader reads half floats, which hold 8-bit values to about 1/4096. That is invisible
        // except at the edge of the gamut, where fitting halves the chroma step by step and a
        // colour may take one step more: a few pixels in a thousand, by a few levels.
        const std::string named = QJsonDocument(QJsonObject::fromVariantMap(settings)).toJson(QJsonDocument::Compact).toStdString();
        EXPECT_LE(off * 200, samples) << named;
        EXPECT_LE(worst, 10) << named;
    };
    // Levels and the cube.
    compare({{QStringLiteral("black"), 0.1}, {QStringLiteral("white"), 0.9}, {QStringLiteral("gamma"), 1.4}});
    // Oklab lightness and chroma, then the cube.
    compare({{QStringLiteral("lightness"), 0.7}, {QStringLiteral("saturation"), 1.6}, {QStringLiteral("contrast"), 1.3}});
    // Colours pushed far beyond the range, fitted by chroma and clipped.
    compare({{QStringLiteral("saturation"), 2.0}, {QStringLiteral("brightness"), 0.05}});
    compare({{QStringLiteral("saturation"), 2.0}, {QStringLiteral("brightness"), 0.05}, {QStringLiteral("gamutMap"), false}});
    view->clearColorCube();
    // Everything but a cube, with blacks lifted twice over.
    compare({{QStringLiteral("black"), 0.05}, {QStringLiteral("gamma"), 0.8}, {QStringLiteral("outBlack"), 0.1},
             {QStringLiteral("outWhite"), 0.9}, {QStringLiteral("brightness"), 0.1}, {QStringLiteral("lightness"), 1.2},
             {QStringLiteral("saturation"), 0.3}});
}

// Needs a GPU, like the tests above. With sharp resampling, the adjustment comes before the
// kernel, as it does once applied: adjusting after it would stretch its halos too.
TEST(ColorPreview, SharpResamplingSharpensTheAdjustedPicture) {
    QTemporaryDir dir;
    TiffImage image;
    image.width = 600;
    image.height = 500;
    image.pixel = [](int x, int y) {
        const auto v = static_cast<std::uint8_t>((x / 7 + y / 5) % 2 != 0 ? 200 : 40);
        return Rgb{v, static_cast<std::uint8_t>((x * 255) / 600), static_cast<std::uint8_t>(255 - v)};
    };
    const QString path = dir.filePath(QStringLiteral("a.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});

    BrowseContext context;
    QQuickView window;
    window.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    auto* view = window.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    window.rootObject()->setProperty("sharpResampling", true);
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    ASSERT_TRUE(waitUntil([&] { return view->resampleActive(); }));
    const auto grab = [&] {
        waitUntil([] { return false; }, 500);
        return grabIfShown(window);
    };
    // Steep levels, which stretch whatever the kernel overshoots.
    const QVariantMap settings{{QStringLiteral("black"), 0.25}, {QStringLiteral("white"), 0.6}};
    view->previewColors(settings);
    const QImage preview = grab();
    view->adjustColors(settings);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready && !view->colorPreviewActive(); }));
    ASSERT_TRUE(waitUntil([&] { return view->resampleActive(); }));
    const QImage applied = grab();
    ASSERT_FALSE(preview.isNull() || applied.isNull());
    const QRectF rect(view->imageToItem({0, 0}) * window.devicePixelRatio(), view->imageToItem({600, 500}) * window.devicePixelRatio());
    int off = 0;
    int samples = 0;
    for (int y = static_cast<int>(rect.top()) + 4; y < static_cast<int>(rect.bottom()) - 4; y += 2) {
        for (int x = static_cast<int>(rect.left()) + 4; x < static_cast<int>(rect.right()) - 4; x += 2) {
            const QColor a = preview.pixelColor(x, y);
            const QColor b = applied.pixelColor(x, y);
            off += std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue())}) > 3 ? 1 : 0;
            ++samples;
        }
    }
    ASSERT_GT(samples, 1000);
    EXPECT_LE(off * 100, samples) << off << " of " << samples << " differ by more than 3";

    // Previewing again and leaving sharp resampling, which takes its adjustment along, leaves the
    // picture shown.
    view->previewColors(settings);
    waitUntil([] { return false; }, 300);
    view->actualSize();
    ASSERT_TRUE(waitUntil([&] { return !view->resampleActive(); }));
    const QImage after = grab();
    ASSERT_FALSE(after.isNull());
    const QPointF middle = view->imageToItem({300, 250}) * window.devicePixelRatio();
    EXPECT_NE(after.pixelColor(middle.toPoint()), QColor(0x20, 0x20, 0x20)) << "not the background";
}

// Needs a GPU, like the tests above. A 12-bit gradient in a 16-bit file uses only the bottom
// sixteenth of the range; levels that stretch it must show the steps between, not sixteen bands.
TEST(ColorPreview, LevelsStretchTheBitsBeyondEight) {
    QTemporaryDir dir;
    TiffImage image;
    image.width = 300;
    image.height = 100;
    image.bits = 16;
    image.samples = 1;
    // TiffImage takes 8-bit values for 16-bit files too, so the 16-bit ones come from wide16.
    image.wide16 = [](int x, int, int) { return static_cast<std::uint16_t>(x * 4095 / 299); };
    const QString path = dir.filePath(QStringLiteral("twelve.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});

    BrowseContext context;
    QQuickView window;
    window.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    auto* view = window.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    ASSERT_FALSE(view->autoContrast());
    view->actualSize();
    view->previewColors({{QStringLiteral("white"), 16.0 / 255.0}});
    waitUntil([] { return false; }, 500);
    const QImage shot = grabIfShown(window);
    ASSERT_FALSE(shot.isNull());
    const QPointF topLeft = view->imageToItem({0, 0}) * window.devicePixelRatio();
    QSet<int> values;
    for (int x = 2; x < 298; ++x) {
        values.insert(shot.pixelColor(static_cast<int>(topLeft.x()) + x, static_cast<int>(topLeft.y()) + 50).red());
    }
    EXPECT_GT(values.size(), 100) << "distinct levels across the gradient";
}

// Needs a GPU, like the tests above. A gradient over four 8-bit steps: rounded, it shows five
// bands; dithered, the average down each column follows the gradient, with or without sharp
// resampling, while each pixel stays within a step of it.
TEST(ColorPreview, DeepImagesAreDitheredRatherThanBanded) {
    QTemporaryDir dir;
    TiffImage image;
    image.width = 800;
    image.height = 600;
    image.bits = 16;
    image.samples = 1;
    image.wide16 = [](int x, int, int) { return static_cast<std::uint16_t>((128.0 + 4.0 * x / 799) * 257); };
    const QString path = dir.filePath(QStringLiteral("ramp.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});

    BrowseContext context;
    QQuickView window;
    window.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    auto* view = window.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    window.rootObject()->setProperty("sharpResampling", false);
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    EXPECT_TRUE(view->highPrecision());

    const auto measure = [&](const char* how) {
        waitUntil([] { return false; }, 500);
        const QImage shot = grabIfShown(window);
        ASSERT_FALSE(shot.isNull());
        const QRect rect = QRectF(view->imageToItem({0, 0}) * window.devicePixelRatio(), view->imageToItem({800, 600}) * window.devicePixelRatio())
                               .toRect()
                               .intersected(shot.rect())
                               .adjusted(4, 4, -4, -4);
        ASSERT_GT(rect.width(), 300);
        QSet<int> means;
        int worst = 0;
        for (int x = rect.left(); x <= rect.right(); ++x) {
            int sum = 0;
            for (int y = rect.top(); y <= rect.bottom(); ++y) {
                sum += shot.pixelColor(x, y).red();
            }
            const double mean = static_cast<double>(sum) / rect.height();
            means.insert(static_cast<int>(std::lround(mean * 8)));
            for (int y = rect.top(); y <= rect.bottom(); ++y) {
                worst = std::max(worst, static_cast<int>(std::ceil(std::abs(shot.pixelColor(x, y).red() - mean))));
            }
        }
        // Four steps in eighths are 32 at most, and rounded they would be 5.
        EXPECT_GT(means.size(), 20) << how << ": distinct column averages, in eighths of a step";
        EXPECT_LE(worst, 2) << how << ": the grain";
    };
    measure("shown as it is");
    window.rootObject()->setProperty("sharpResampling", true);
    ASSERT_TRUE(waitUntil([&] { return view->resampleActive(); }));
    measure("with sharp resampling");
}

// Needs a GPU, like the tests above. Deep colour keeps its channels in their places on the way
// to the screen, which gray images would not show.
TEST(ColorPreview, DeepColourKeepsItsChannels) {
    QTemporaryDir dir;
    TiffImage image;
    image.width = 100;
    image.height = 100;
    image.bits = 16;
    image.samples = 3;
    image.wide16 = [](int, int, int sample) { return std::uint16_t{sample == 0 ? std::uint16_t{0x8080} : sample == 1 ? std::uint16_t{0x4040} : std::uint16_t{0x2020}}; };
    const QString path = dir.filePath(QStringLiteral("colour.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});

    BrowseContext context;
    QQuickView window;
    window.engine()->addImageProvider(QLatin1StringView(iv::ThumbnailProvider::kProviderId), new iv::ThumbnailProvider);
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    auto* view = window.rootObject()->property("view").value<ImageView*>();
    ASSERT_NE(view, nullptr);
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    ASSERT_TRUE(view->highPrecision());
    view->actualSize();
    waitUntil([] { return false; }, 500);
    const QImage shot = grabIfShown(window);
    ASSERT_FALSE(shot.isNull());
    const QColor shown = shot.pixelColor((view->imageToItem({50, 50}) * window.devicePixelRatio()).toPoint());
    EXPECT_NEAR(shown.red(), 128, 1);
    EXPECT_NEAR(shown.green(), 64, 1);
    EXPECT_NEAR(shown.blue(), 32, 1);
}

// Needs a GPU, like the tests above.
TEST(ColorPreview, BeforeAndAfterSplitTheShaderAtTheLine) {
    QTemporaryDir dir;
    TiffImage image;
    image.width = 300;
    image.height = 200;
    image.pixel = [](int x, int y) { return Rgb{200, static_cast<std::uint8_t>(60 + y / 2), static_cast<std::uint8_t>(40 + x / 4)}; };
    const QString path = dir.filePath(QStringLiteral("a.tif"));
    iv::test::writeTiff(QFileInfo(path).filesystemFilePath(), {image});
    BrowseContext context;
    QQuickView window;
    window.setInitialProperties({{QStringLiteral("context"), QVariant::fromValue(&context)},
                                 {QStringLiteral("active"), true},
                                 {QStringLiteral("fullScreen"), true}});
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.resize(400, 400);
    window.loadFromModule("ImageViewer", "ViewerPage");
    window.show();
    ASSERT_TRUE(waitUntil([&] { return window.isExposed(); }));
    QCoreApplication::processEvents();
    if (window.rendererInterface()->graphicsApi() == QSGRendererInterface::Software) {
        GTEST_SKIP() << "needs a GPU";
    }
    QObject* page = window.rootObject();
    auto* view = page->property("view").value<ImageView*>();
    auto* splitter = page->findChild<QObject*>(QStringLiteral("compareSplitter"));
    context.openPath(path);
    ASSERT_TRUE(waitUntil([&] { return view->status() == ImageView::Status::Ready; }));
    view->actualSize();
    QMetaObject::invokeMethod(page, "startAdjust");
    page->findChild<QObject*>(QStringLiteral("adjustPanel"))->setProperty("saturation", 0);
    ASSERT_TRUE(view->colorPreviewActive());
    QMetaObject::invokeMethod(page, "toggleBeforeAfter");
    ASSERT_TRUE(waitUntil([&] { return splitter->property("active").toBool(); }));
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    const QImage shot = grabIfShown(window);
    ASSERT_FALSE(shot.isNull()) << "the window was closed";
    const double line = splitter->property("lineX").toDouble();
    const auto at = [&](double itemX) {
        const QPointF imagePoint(std::clamp(view->itemToImage(QPointF(itemX, 0)).x(), 0.0, 299.0), 100);
        return shot.pixelColor((view->mapToScene(view->imageToItem(imagePoint)) * window.devicePixelRatio()).toPoint());
    };
    const QColor left = at(line - 20);
    const QColor right = at(line + 20);
    EXPECT_GT(left.red() - left.blue(), 60) << "the picture as it was";
    EXPECT_NEAR(right.red(), right.blue(), 3) << "through the preview, without colour";
}

TEST(ImageView, HoldingTheButtonShowsThatPlaceAtActualSize) {
    LoupeFixture f;
    ASSERT_DOUBLE_EQ(f.view->zoom(), 0.25);
    EXPECT_EQ(f.view->cursor().shape(), Qt::BitmapCursor) << "a magnifying glass";

    // Near the top left, which is a quarter of the way in on the image.
    f.press(QPointF(100, 100));
    EXPECT_DOUBLE_EQ(f.view->zoom(), 1);
    QRectF area = f.view->visibleArea();
    EXPECT_NEAR(area.x(), (400 - 100) / 1600.0, 0.001) << "image x 400 stays under the mouse at view x 100";
    EXPECT_NEAR(area.y(), (400 - 100) / 1600.0, 0.001);
    EXPECT_EQ(f.view->cursor().shape(), Qt::BitmapCursor);

    // Moving looks around while it is held.
    f.move(QPointF(80, 100));
    area = f.view->visibleArea();
    EXPECT_NEAR(area.x(), (400 - 80) / 1600.0, 0.001);
    EXPECT_DOUBLE_EQ(f.view->zoom(), 1);

    f.release(QPointF(80, 100));
    EXPECT_DOUBLE_EQ(f.view->zoom(), 0.25);
    EXPECT_EQ(f.view->visibleArea(), QRectF(0, 0, 1, 1));
    f.view->setSize(QSizeF(200, 200));
    EXPECT_DOUBLE_EQ(f.view->zoom(), 0.125) << "it fits the view again, not one zoom";
}

TEST(ImageView, CursorIsAHandWhereThereIsSomethingToPanTo) {
    LoupeFixture f;
    f.view->actualSize();
    EXPECT_EQ(f.view->cursor().shape(), Qt::OpenHandCursor);
    f.press(QPointF(200, 200));
    EXPECT_DOUBLE_EQ(f.view->zoom(), 1) << "stays as it is";
    EXPECT_EQ(f.view->cursor().shape(), Qt::ClosedHandCursor);
    f.move(QPointF(150, 200));
    EXPECT_EQ(f.view->cursor().shape(), Qt::ClosedHandCursor);
    const QRectF panned = f.view->visibleArea();
    f.release(QPointF(150, 200));
    EXPECT_EQ(f.view->cursor().shape(), Qt::OpenHandCursor);
    EXPECT_EQ(f.view->visibleArea(), panned) << "panning stays where it went";
}

TEST(ImageView, SmallImageShownWholeHasNoLoupeOrHand) {
    ViewFixture f;
    QTemporaryDir dir;
    writeJpeg(dir.filePath(QStringLiteral("small.jpg")));
    ASSERT_TRUE(f.load(dir.filePath(QStringLiteral("small.jpg"))));
    ASSERT_DOUBLE_EQ(f.view->zoom(), 1);
    EXPECT_EQ(f.view->cursor().shape(), Qt::ArrowCursor);
}

TEST(ImageView, AnotherImageArrivingWhileHeldKeepsItsOwnView) {
    LoupeFixture f;
    JpegOptions options;
    options.width = 800;
    options.height = 800;
    writeJpeg(f.dir.filePath(QStringLiteral("other.jpg")), options);
    f.press(QPointF(100, 100));
    ASSERT_DOUBLE_EQ(f.view->zoom(), 1);
    ASSERT_TRUE(f.load(f.dir.filePath(QStringLiteral("other.jpg"))));
    const double zoom = f.view->zoom();
    f.release(QPointF(100, 100));
    EXPECT_DOUBLE_EQ(f.view->zoom(), zoom) << "the zoom of the first would not suit it";
}

TEST(ImageView, WheelCanAskForAnotherImageAndCtrlZooms) {
    LoupeFixture f;
    const double zoom = f.view->zoom();
    f.wheel(120);
    EXPECT_GT(f.view->zoom(), zoom) << "zooms unless told otherwise";

    f.view->fitToView();
    f.view->setWheelNavigates(true);
    QSignalSpy requests(f.view, &ImageView::navigateRequested);
    const double fitted = f.view->zoom();
    f.wheel(-120);
    ASSERT_EQ(requests.count(), 1);
    EXPECT_EQ(requests.takeFirst().at(0).toInt(), 1) << "towards the user is the next";
    f.wheel(120);
    ASSERT_EQ(requests.count(), 1);
    EXPECT_EQ(requests.takeFirst().at(0).toInt(), -1);
    EXPECT_DOUBLE_EQ(f.view->zoom(), fitted);

    // A touchpad's small steps add up to one, and turning back starts over.
    f.wheel(-50);
    f.wheel(-50);
    EXPECT_EQ(requests.count(), 0);
    f.wheel(-20);
    EXPECT_EQ(requests.count(), 1);
    requests.clear();
    f.wheel(-100);
    f.wheel(100);
    EXPECT_EQ(requests.count(), 0);

    f.wheel(120, Qt::ControlModifier);
    EXPECT_GT(f.view->zoom(), fitted);
    EXPECT_EQ(requests.count(), 0);
}
