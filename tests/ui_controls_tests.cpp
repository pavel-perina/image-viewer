// The controls' own style (src/ui/controls): buttons, check boxes, radio buttons, text fields,
// combo boxes, spin boxes, sliders and menus. Input: a gallery window with each control in its
// states, made up here, in the palette the program uses. The truth is what the controls
// promise people: the default button stands out, a check box shows when it is off, menu lines
// start their words at one place, a decimal spin box keeps its decimals, and so on. Set IV_GALLERY_PNG to a file name to have the
// gallery saved as a picture, to look at the style.

#include "ui_test_support.h"

#include <QDir>
#include <QFontInfo>
#include <QFontMetricsF>

#include "ui/themed_icon.h"

namespace {

// Every control in the states worth seeing, in the window's palette from Theme, as Main.qml gives it.
constexpr const char* galleryQml = R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

ApplicationWindow {
    id: window
    width: 640; height: 480; visible: true
    color: Theme.window
    palette.window: Theme.window
    palette.windowText: Theme.text
    palette.base: Theme.base
    palette.text: Theme.text
    palette.button: Theme.button
    palette.buttonText: Theme.text
    palette.highlight: Theme.highlight
    palette.highlightedText: Theme.highlightedText
    palette.light: Theme.light
    palette.mid: Theme.mid
    palette.dark: Theme.dark

    property alias apply: apply
    property alias cancel: cancel
    property alias off: off
    property alias on: on
    property alias offLimits: offLimits
    property alias radioOn: radioOn
    property alias field: field
    property alias decimal: decimal
    property alias combo: combo
    property alias slider: slider
    property alias menu: menu

    ActionGroup { id: sortGroup }

    GridLayout {
        x: 16; y: 16
        columns: 2
        columnSpacing: 32
        rowSpacing: 12

        ColumnLayout {
            spacing: 10
            Label { text: "BUTTONS"; color: Theme.textMuted; font.pointSize: Theme.smallSize }
            RowLayout {
                spacing: 8
                Button { text: "Reset" }
                Button { id: cancel; text: "Cancel" }
                Button { id: apply; text: "Apply"; highlighted: true }
            }
            RowLayout {
                spacing: 8
                Button { text: "Quiet"; flat: true }
                Button { text: "Disabled"; enabled: false }
                Button { text: "Apply"; highlighted: true; enabled: false }
            }
            Label { text: "CHECK BOXES"; color: Theme.textMuted; font.pointSize: Theme.smallSize }
            RowLayout {
                spacing: 12
                CheckBox { id: off; text: "Off" }
                CheckBox { id: on; text: "On"; checked: true }
                CheckBox { text: "Partly"; tristate: true; checkState: Qt.PartiallyChecked }
                CheckBox { text: "Disabled"; enabled: false }
            }
            RowLayout {
                spacing: 12
                CheckBox { id: offLimits; text: "Disabled, on"; checked: true; enabled: false }
            }
            Label { text: "RADIO BUTTONS"; color: Theme.textMuted; font.pointSize: Theme.smallSize }
            RowLayout {
                spacing: 12
                RadioButton { id: radioOn; text: "Fit"; checked: true }
                RadioButton { text: "Fill" }
                RadioButton { text: "Disabled, on"; checked: true; enabled: false; autoExclusive: false }
            }
            Label { text: "SLIDER"; color: Theme.textMuted; font.pointSize: Theme.smallSize }
            Slider { id: slider; Layout.preferredWidth: 260; value: 0.72 }
        }

        ColumnLayout {
            spacing: 10
            Label { text: "COMBO BOX"; color: Theme.textMuted; font.pointSize: Theme.smallSize }
            ComboBox { id: combo; Layout.preferredWidth: 220; model: ["Lanczos", "Bicubic", "Nearest"] }
            Label { text: "TEXT FIELD"; color: Theme.textMuted; font.pointSize: Theme.smallSize }
            TextField { id: field; Layout.preferredWidth: 220; placeholderText: "Name" }
            Label { text: "SPIN BOXES"; color: Theme.textMuted; font.pointSize: Theme.smallSize }
            RowLayout {
                spacing: 8
                SpinBox { from: 0; to: 1000; value: 128; editable: true }
                DecimalSpinBox { id: decimal; realFrom: 0; realTo: 10; realValue: 1.25; realStepSize: 0.25 }
            }
        }
    }

    AppMenu {
        id: menu
        x: 360; y: 200
        AppMenuItem { text: "Open..." }
        AppMenuItem { text: "Show Info"; checkable: true; checked: true }
        AppMenuItem { text: "Show Thumbnails"; checkable: true }
        AppMenuItem { text: "Show Hidden"; checkable: true; checked: true; enabled: false }
        MenuSeparator {}
        Action { text: "By Name"; checkable: true; checked: true; ActionGroup.group: sortGroup }
        Action { text: "By Date"; checkable: true; ActionGroup.group: sortGroup }
        AppMenu { title: "Recent"; AppMenuItem { text: "holiday" } }
    }
}
)";

struct Gallery {
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QStringList warnings;

    Gallery() {
        QObject::connect(&engine, &QQmlEngine::warnings, [this](const QList<QQmlError>& list) {
            for (const QQmlError& warning : list) {
                warnings << warning.toString();
            }
        });
        QQmlComponent component(&engine);
        component.setData(galleryQml, QUrl());
        EXPECT_TRUE(component.isReady()) << component.errorString().toStdString();
        root.reset(component.create());
        window = qobject_cast<QQuickWindow*>(root.get());
        EXPECT_TRUE(waitUntil([&] { return window && window->isExposed(); }));
    }

    QQuickItem* item(const char* name) const { return root->property(name).value<QQuickItem*>(); }
    QObject* object(const char* name) const { return root->property(name).value<QObject*>(); }

    QColor pixelAt(QQuickItem* in, QPointF at) const {
        return window->grabWindow().pixelColor(in->mapToScene(at).toPoint());
    }
};

double distance(const QColor& a, const QColor& b) {
    return std::hypot(a.red() - b.red(), a.green() - b.green(), a.blue() - b.blue());
}

}  // namespace

// Why: the button that does the job must be found at a glance among the others (Apply among
// Reset and Cancel). Measured in the picture: the middle of Apply, beside its text, differs
// clearly from the same place in Cancel, and from the window.
TEST(Controls, TheDefaultButtonStandsOut) {
    Gallery g;
    QQuickItem* apply = g.item("apply");
    QQuickItem* cancel = g.item("cancel");
    const QColor applied = g.pixelAt(apply, QPointF(4, apply->height() / 2));
    const QColor cancelled = g.pixelAt(cancel, QPointF(4, cancel->height() / 2));
    const QColor window = g.window->color();
    // Neutral ordinary buttons reduce RGB distance slightly; also require a visible green
    // accent so a plain gray button cannot pass just by being a little lighter.
    EXPECT_GT(distance(applied, cancelled), 35) << applied.name().toStdString() << " " << cancelled.name().toStdString();
    EXPECT_GT(applied.green() - applied.red(), 20) << "the primary action carries the green accent";
    EXPECT_GT(distance(applied, window), 60);
    EXPECT_EQ(g.warnings, QStringList());

    if (const QString file = qEnvironmentVariable("IV_GALLERY_PNG"); !file.isEmpty()) {
        QMetaObject::invokeMethod(g.object("menu"), "open");
        waitUntil([] { return false; }, 300);
        g.window->grabWindow().save(file);
    }
}

// Why: Fusion's box hardly shows when off on a dark palette, so it is not seen as one. Off, the
// box has an edge that stands out from the window; on, it is filled.
TEST(Controls, ACheckBoxShowsWhenItIsOff) {
    Gallery g;
    auto* off = g.item("off")->property("indicator").value<QQuickItem*>();
    auto* on = g.item("on")->property("indicator").value<QQuickItem*>();
    ASSERT_NE(off, nullptr);
    // The strongest difference along the box's middle row, its edges included.
    const QImage picture = g.window->grabWindow();
    const QPoint left = off->mapToScene(QPointF(0, off->height() / 2)).toPoint();
    double strongest = 0;
    for (int x = left.x() - 1; x <= left.x() + static_cast<int>(off->width()) + 1; ++x) {
        strongest = std::max(strongest, distance(picture.pixelColor(x, left.y()), g.window->color()));
    }
    EXPECT_GT(strongest, 80);
    const QColor filled = g.pixelAt(on, QPointF(3, 3));
    EXPECT_GT(distance(filled, g.window->color()), 120) << filled.name().toStdString();
}

// Why: words in a menu start at one place, whether a line has a check box, is a plain action,
// opens a submenu, or has nothing; before, a checkable line pushed its words right. And a
// plain Action in an AppMenu shows its check box like an AppMenuItem does.
TEST(Controls, MenuLinesStartTheirWordsAtOnePlace) {
    Gallery g;
    auto* menu = g.object("menu");
    QMetaObject::invokeMethod(menu, "open");
    ASSERT_TRUE(waitUntil([&] { return menu->property("opened").toBool(); }));
    QList<double> starts;
    int checkable = 0;
    for (int i = 0; i < menu->property("count").toInt(); ++i) {
        QQuickItem* line = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, line), Q_ARG(int, i));
        auto* words = line ? line->property("contentItem").value<QQuickItem*>() : nullptr;
        if (!words || line->inherits("QQuickMenuSeparator")) {
            continue;
        }
        starts << words->mapToScene(QPointF(0, 0)).x();
        checkable += line->property("checkable").toBool();
    }
    ASSERT_EQ(starts.size(), 7);
    for (double start : starts) {
        EXPECT_DOUBLE_EQ(start, starts.front());
    }
    EXPECT_EQ(checkable, 5) << "the three AppMenuItems and the two Actions";
}

// Why: Qt's SpinBox counts in whole numbers. The decimal one shows and steps its decimals, and
// takes them typed.
TEST(Controls, ADecimalSpinBoxKeepsItsDecimals) {
    Gallery g;
    QObject* box = g.item("decimal");
    EXPECT_EQ(box->property("displayText").toString(), QStringLiteral("1.25"));
    QMetaObject::invokeMethod(box, "increase");
    EXPECT_EQ(box->property("displayText").toString(), QStringLiteral("1.50"));
    QSignalSpy modified(box, SIGNAL(realValueModified(double)));
    g.item("decimal")->forceActiveFocus();
    QTest::keyClick(g.window, Qt::Key_Up);
    EXPECT_DOUBLE_EQ(box->property("realValue").toDouble(), 1.75);
    EXPECT_EQ(modified.count(), 1);
}

// Why: the list opens below the field with its entries, and the one picked is shown.
TEST(Controls, AComboBoxListsItsEntriesAndShowsThePickedOne) {
    Gallery g;
    QQuickItem* combo = g.item("combo");
    EXPECT_EQ(combo->property("displayText").toString(), QStringLiteral("Lanczos"));
    combo->forceActiveFocus();
    QTest::keyClick(g.window, Qt::Key_Down);
    EXPECT_EQ(combo->property("displayText").toString(), QStringLiteral("Bicubic"));
    auto* popup = combo->property("popup").value<QObject*>();
    QMetaObject::invokeMethod(popup, "open");
    ASSERT_TRUE(waitUntil([&] { return popup->property("opened").toBool(); }));
    EXPECT_GE(popup->property("y").toDouble(), combo->height()) << "below the field";
}

// Why: the groove is filled with the accent up to the handle, and not beyond it.
TEST(Controls, ASliderIsFilledUpToItsHandle) {
    Gallery g;
    QQuickItem* slider = g.item("slider");
    auto* handle = slider->property("handle").value<QQuickItem*>();
    const double middle = handle->y() + handle->height() / 2;
    const QColor before = g.pixelAt(slider, QPointF(handle->x() - 10, middle));
    const QColor after = g.pixelAt(slider, QPointF(handle->x() + handle->width() + 10, middle));
    QObject* theme = g.engine.singletonInstance<QObject*>("ImageViewer", "Theme");
    const QColor accent = theme->property("accent").value<QColor>();
    EXPECT_LT(distance(before, accent), 30) << before.name().toStdString();
    EXPECT_GT(distance(after, accent), 80) << after.name().toStdString();
}

// Why: a large solid selection competes with the thumbnail, and current is not the same
// as selected. Render every combination on a neutral window: selection has a quiet fill
// and a visible edge, current keeps a bright independent frame, and loss of grid focus
// softens selection without hiding current. Pixel differences are the visual promise;
// the exact fill formula is deliberately not asserted.
TEST(Controls, ThumbnailSelectionIsQuietAndCurrentRemainsDistinct) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import ImageViewer
        ApplicationWindow {
            width: 200; height: 220; visible: true
            color: Theme.window
            property alias cell: cell
            ThumbnailCell { id: cell; width: 180; height: 200; name: "sample.png" }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    ASSERT_NE(window, nullptr);
    ASSERT_TRUE(waitUntil([&] { return window->isExposed(); }));
    auto* cell = root->property("cell").value<QQuickItem*>();
    ASSERT_NE(cell, nullptr);
    const QColor background = window->color();
    double focusedFill = 0;
    for (bool focused : {true, false}) {
        for (bool selected : {false, true}) {
            for (bool current : {false, true}) {
                SCOPED_TRACE(testing::Message() << "focus=" << focused << " selected=" << selected << " current=" << current);
                cell->setProperty("keysHere", focused);
                cell->setProperty("selected", selected);
                cell->setProperty("current", current);
                const QImage picture = grabIfShown(*window);
                ASSERT_FALSE(picture.isNull());
                const QColor fill = picture.pixelColor(90, 20);
                const QColor outside = picture.pixelColor(2, 90);
                const QColor currentEdge = picture.pixelColor(selected ? 5 : 2, 90);
                if (selected) {
                    const double strength = distance(fill, background);
                    EXPECT_GT(strength, 5);
                    EXPECT_LT(strength, 45) << "the picture should dominate the fill";
                    EXPECT_GT(distance(outside, fill), 35) << "selection edge stays visible";
                    if (!current) {
                        if (focused) focusedFill = strength;
                        else EXPECT_LT(strength, focusedFill);
                    }
                } else {
                    EXPECT_LT(distance(fill, background), 2);
                }
                if (current) {
                    EXPECT_GT(distance(currentEdge, fill), 200);
                } else if (!selected) {
                    EXPECT_LT(distance(outside, background), 2);
                }
            }
        }
    }
}

// Why: what the style does not redo is to come from Fusion, which suits a dense desktop
// program, and not from Basic, made for touch: Basic's tree rows are 50 pixels high and its
// tab buttons and list rows 40. It once fell to Basic unnoticed, as the fallback was declared
// in a way Qt does not take. Input: a tab button and a list row, neither of them redone. The
// truth is Fusion's own sizes, well below Basic's.
TEST(Controls, WhatTheStyleDoesNotRedoComesFromFusion) {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        Item {
            property alias tool: tool
            property alias row: row
            TabButton { id: tool; text: "Fit" }
            ItemDelegate { id: row; text: "Home" }
        }
    )", QUrl());
    ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
    std::unique_ptr<QObject> root(component.create());
    ASSERT_NE(root, nullptr);
    auto* tool = root->property("tool").value<QQuickItem*>();
    auto* row = root->property("row").value<QQuickItem*>();
    EXPECT_LE(tool->implicitHeight(), 28);
    EXPECT_LE(row->implicitHeight(), 28);
}

namespace {

// The line of a menu that reads text, once the menu is open.
QQuickItem* menuLine(QObject* menu, const QString& text) {
    for (int i = 0; i < menu->property("count").toInt(); ++i) {
        QQuickItem* line = nullptr;
        QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, line), Q_ARG(int, i));
        if (line && line->property("text").toString() == text) {
            return line;
        }
    }
    return nullptr;
}

}  // namespace

// Why: a disabled option that is on must still read as a box, ticked; filled with nothing,
// as it once was, its tick floated with no edge, while one that is off kept its edge. Input:
// a disabled check box that is on, and a disabled ticked line of a menu. The truth is their
// marks: each keeps an edge of at least a pixel, and its tick shows.
TEST(Controls, ADisabledOptionThatIsOnKeepsItsEdge) {
    Gallery g;
    auto* box = g.item("offLimits")->property("indicator").value<QQuickItem*>();
    ASSERT_NE(box, nullptr);
    EXPECT_GE(box->property("border").value<QObject*>()->property("width").toDouble(), 1.0);

    auto* menu = g.object("menu");
    QMetaObject::invokeMethod(menu, "open");
    ASSERT_TRUE(waitUntil([&] { return menu->property("opened").toBool(); }));
    QQuickItem* line = menuLine(menu, QStringLiteral("Show Hidden"));
    ASSERT_NE(line, nullptr);
    auto* mark = line->property("indicator").value<QQuickItem*>();
    EXPECT_TRUE(mark->isVisible());
    EXPECT_GE(mark->property("border").value<QObject*>()->property("width").toDouble(), 1.0);
}

// Why: where only one of a group can be chosen (Sort By), its lines show round marks with a
// dot, as radio buttons do, so the menu says so before it is tried; a box with a tick says
// each can be turned on alone. Input: the gallery's menu, whose By Name and By Date are in an
// exclusive group and whose Show Info is not, and a radio button. The truth is the marks'
// shape: round (radius half the width) with the dot shown for the one chosen, square otherwise.
TEST(Controls, ChoicesOfOneShowRoundMarks) {
    Gallery g;
    auto* menu = g.object("menu");
    QMetaObject::invokeMethod(menu, "open");
    ASSERT_TRUE(waitUntil([&] { return menu->property("opened").toBool(); }));
    QQuickItem* byName = menuLine(menu, QStringLiteral("By Name"));
    QQuickItem* showInfo = menuLine(menu, QStringLiteral("Show Info"));
    ASSERT_NE(byName, nullptr);
    ASSERT_NE(showInfo, nullptr);
    auto* round = byName->property("indicator").value<QQuickItem*>();
    auto* square = showInfo->property("indicator").value<QQuickItem*>();
    EXPECT_DOUBLE_EQ(round->property("radius").toDouble(), round->width() / 2);
    EXPECT_LT(square->property("radius").toDouble(), square->width() / 2);
    auto* dot = round->findChild<QQuickItem*>(QStringLiteral("menuRadioDot"));
    ASSERT_NE(dot, nullptr);
    EXPECT_TRUE(dot->isVisible());

    auto* radio = g.item("radioOn")->property("indicator").value<QQuickItem*>();
    EXPECT_DOUBLE_EQ(radio->property("radius").toDouble(), radio->width() / 2);
}

// Why: the program brings its own font, so that it looks alike on every system and does not
// depend on what is installed (the tests run with an empty home, where nothing of the user's
// is). Input: the gallery, after useProgramFont as main() and the tests call it. The truth is
// the family a button's text is drawn in: the bundled Inter, at the size asked for.
TEST(Controls, TheControlsUseTheProgramsOwnFont) {
    Gallery g;
    const QFont font = g.item("apply")->property("font").value<QFont>();
    EXPECT_TRUE(QFontInfo(font).family().startsWith(QStringLiteral("Inter"))) << QFontInfo(font).family().toStdString();
    EXPECT_DOUBLE_EQ(font.pointSizeF(), 10);
}

// Numbers that change under the cursor (the probe, the zoom, fields being stepped) must not
// shift the text after them, and numbers in a column must line up: so every digit is as wide.
// Inter's own digits are not (a 1 is narrower than a 0); the font's tabular figures are. The
// widths are measured on a control's font, which is what people see, not on the app font.
TEST(Controls, EveryDigitIsAsWideAsTheOthers) {
    Gallery g;
    const QFontMetricsF metrics(g.item("apply")->property("font").value<QFont>());
    const double zero = metrics.horizontalAdvance(QStringLiteral("0000000000"));
    for (const QString& digits : {QStringLiteral("1111111111"), QStringLiteral("4444444444"), QStringLiteral("7777777777")}) {
        EXPECT_DOUBLE_EQ(metrics.horizontalAdvance(digits), zero) << digits.toStdString();
    }
}

// Why: icons follow the palette, so the drawings carry placeholder colours (white or
// currentColor for the ink, pure green for the accent; assets/icons/README.md) that must become
// the theme's however the editor wrote them, while a colour drawn on purpose stays. Input: a
// made-up SVG with each spelling of each placeholder and one red; the truth is the README's rule.
TEST(Icons, PlaceholdersTakeTheThemesColoursAndOtherColoursStay) {
    const QByteArray svg = R"svg(<svg><path style="fill:white;stroke:rgb(0,255,0);"/>)svg"
                           R"svg(<path fill="#FFFFFF" stroke="#00ff00"/><path style="fill:rgb(255, 255, 255)"/>)svg"
                           R"svg(<path style="fill:lime"/><path style="fill:#ff0000;stroke:whitesmoke"/>)svg"
                           R"svg(<path stroke="currentColor"/><path fill="currentcolor"/></svg>)svg";
    const QString result = QString::fromUtf8(iv::recolorIcon(svg, QColor(0x12, 0x34, 0x56), QColor(0xab, 0xcd, 0xef)));
    EXPECT_EQ(result.count(QStringLiteral("#123456")), 5) << result.toStdString();
    EXPECT_EQ(result.count(QStringLiteral("#abcdef")), 3) << result.toStdString();
    EXPECT_TRUE(result.contains(QStringLiteral("fill:#ff0000"))) << result.toStdString();
    EXPECT_TRUE(result.contains(QStringLiteral("stroke:whitesmoke"))) << result.toStdString();
}

// Why: an icon exported with a colour that is not a placeholder, or one that does not load,
// would show the wrong colour or nothing on some palette. Input: every icon in the resources,
// drawn in a red ink and a blue accent; no solid pixel may be left white, green or black (an
// unreplaced currentColor), and each icon must draw something, some of it in the ink.
TEST(Icons, EveryIconDrawsInTheThemesColours) {
    const QColor ink(200, 40, 30);
    const QColor accent(30, 80, 210);
    const QStringList names = QDir(QStringLiteral(":/icons")).entryList({QStringLiteral("*.svg")});
    EXPECT_GE(names.size(), 12);
    for (QString name : names) {
        name.chop(4);
        const QImage image = iv::renderIcon(name, QSize(24, 24), ink, accent);
        ASSERT_FALSE(image.isNull()) << name.toStdString();
        int drawn = 0, inInk = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.alpha() < 255) {
                    drawn += pixel.alpha() > 0;
                    continue;
                }
                ++drawn;
                EXPECT_NE(pixel, QColor(Qt::white)) << name.toStdString() << " at " << x << "," << y;
                EXPECT_NE(pixel, QColor(0, 255, 0)) << name.toStdString() << " at " << x << "," << y;
                // currentColor left as it is draws black
                EXPECT_NE(pixel, QColor(Qt::black)) << name.toStdString() << " at " << x << "," << y;
                inInk += distance(pixel, ink) < 4;
            }
        }
        EXPECT_GT(drawn, 10) << name.toStdString();
        EXPECT_GT(inInk, 0) << name.toStdString();  // the accent is only in icons drawn with it
    }
}

// A tool bar with the menu bar above it and buttons in each state, for the tests of the tool
// bar's own controls. Hover is turned on for the one under the mouse, as the offscreen platform
// turns hover effects off.
constexpr const char* toolBarQml = R"(
import QtQuick
import QtQuick.Controls
import ImageViewer
ApplicationWindow {
    width: 400; height: 120; visible: true
    color: Theme.window
    property alias bar: bar
    property alias rest: rest
    property alias hovered: hovered
    property alias pressed: pressed
    property alias on: on
    property alias separator: separator
    menuBar: MenuBar { Menu { title: "File" } }
    header: ToolBar {
        id: bar
        Row {
            IconButton { id: rest; iconName: "zoom-in"; text: "Zoom In" }
            IconButton { id: hovered; hoverEnabled: true; iconName: "zoom-out"; text: "Zoom Out" }
            IconButton { id: pressed; iconName: "zoom-fit-best"; text: "Fit"; down: true }
            IconButton { id: on; iconName: "auto-contrast"; text: "Auto Contrast"; checkable: true; checked: true }
            ToolSeparator { id: separator }
        }
    }
}
)";

struct ToolBarPage {
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QObject* theme = nullptr;

    ToolBarPage() {
        QQmlComponent component(&engine);
        component.setData(toolBarQml, QUrl());
        EXPECT_TRUE(component.isReady()) << component.errorString().toStdString();
        root.reset(component.create());
        window = qobject_cast<QQuickWindow*>(root.get());
        EXPECT_TRUE(waitUntil([&] { return window && window->isExposed(); }));
        theme = engine.singletonInstance<QObject*>("ImageViewer", "Theme");
    }
    QQuickItem* item(const char* name) const { return root->property(name).value<QQuickItem*>(); }
    QColor colour(const char* role) const { return theme->property(role).value<QColor>(); }
    QColor pixelAt(QQuickItem* in, QPointF at) const {
        return window->grabWindow().pixelColor(in->mapToScene(at).toPoint());
    }
    // A button's background, above its icon, clear of the rounded corners.
    QColor backgroundOf(const char* name) const {
        QQuickItem* button = item(name);
        return pixelAt(button, QPointF(button->width() / 2, 2));
    }
};

// Why: tool bar buttons are flat, the icon alone, so that a bar of them is quiet; the mouse
// over one, pressing it and it being on each show, and on must read apart from pressed (a
// toggle such as Auto Contrast would otherwise look stuck mid-click). Input: one button in each
// state. The truth: at rest the bar's own colour; hovered and pressed lighter, pressed the
// more; on nearer the accent than at rest, and unlike pressed.
TEST(Controls, ToolButtonsAreFlatUntilHoveredPressedOrOn) {
    ToolBarPage page;
    QQuickItem* hovered = page.item("hovered");
    QTest::mouseMove(page.window, hovered->mapToScene(QPointF(hovered->width() / 2, hovered->height() / 2)).toPoint());
    ASSERT_TRUE(waitUntil([&] { return hovered->property("hovered").toBool(); }));
    const QColor window = page.colour("window");
    const QColor accent = page.colour("accent");
    const QColor rest = page.backgroundOf("rest");
    const QColor hover = page.backgroundOf("hovered");
    const QColor pressed = page.backgroundOf("pressed");
    const QColor on = page.backgroundOf("on");
    EXPECT_LT(distance(rest, window), 3) << rest.name().toStdString();
    EXPECT_GT(hover.lightnessF(), rest.lightnessF()) << hover.name().toStdString();
    EXPECT_GT(pressed.lightnessF(), hover.lightnessF()) << pressed.name().toStdString();
    EXPECT_LT(distance(on, accent), distance(rest, accent)) << on.name().toStdString();
    EXPECT_GT(distance(on, pressed), 10) << on.name().toStdString() << " " << pressed.name().toStdString();
}

// Why: groups of tool bar buttons are parted by a line that shows, as thin as the one between
// a spin box's number and its arrows; Fusion's was all but invisible. Input: a separator in the
// bar. The truth: one pixel of Theme.mid at its middle, the bar's colour beside it.
TEST(Controls, AToolSeparatorIsAThinLineThatShows) {
    ToolBarPage page;
    QQuickItem* separator = page.item("separator");
    const double middle = separator->height() / 2;
    const double line = std::floor(separator->width() / 2);
    EXPECT_LT(distance(page.pixelAt(separator, QPointF(line, middle)), page.colour("mid")), 3);
    EXPECT_LT(distance(page.pixelAt(separator, QPointF(line - 2, middle)), page.colour("window")), 3);
    EXPECT_LT(distance(page.pixelAt(separator, QPointF(line + 2, middle)), page.colour("window")), 3);
}

// Why: the line between the menu bar and the tool bar is to part them without standing out:
// Fusion's light line drew the eye on a dark window. Input: the bar at the top of a window,
// under its menu bar. The truth: the bar's top row is Theme.dark, darker than the window.
TEST(Controls, TheToolBarIsPartedFromTheMenuByADarkLine) {
    ToolBarPage page;
    QQuickItem* bar = page.item("bar");
    const QColor top = page.pixelAt(bar, QPointF(bar->width() / 2, 0));
    EXPECT_LT(distance(top, page.colour("dark")), 3) << top.name().toStdString();
    EXPECT_LT(top.lightnessF(), page.colour("window").lightnessF());
}
