// SearchCombo: a combo box for long lists, which opens with a field to type a few letters in
// and the names that fit below it, best first. Input: a window with the combo and a made-up
// list of names; keys typed into it. The truth is what the list shows and what is picked.

#include "ui_test_support.h"

#include "ui/fuzzy_list_model.h"

using namespace iv;
using namespace iv::test;

namespace {

struct ComboFixture {
    QQmlEngine engine;
    std::unique_ptr<QObject> root;
    QQuickWindow* window = nullptr;
    QObject* combo = nullptr;
    std::unique_ptr<QSignalSpy> picked;

    ComboFixture() {
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
                width: 600; height: 500; visible: true
                property alias combo: combo
                SearchCombo {
                    id: combo
                    x: 20; y: 20; width: 240
                    names: ["Harmonious", "Iosevka", "Iosevka Term", "Liberation Mono", "Open Sans", "PT Sans"]
                    currentName: "Open Sans"
                }
            }
        )", QUrl());
        EXPECT_TRUE(component.isReady()) << component.errorString().toStdString();
        root.reset(component.create());
        window = qobject_cast<QQuickWindow*>(root.get());
        combo = root->property("combo").value<QObject*>();
        picked = std::make_unique<QSignalSpy>(combo, SIGNAL(picked(QString)));
        EXPECT_TRUE(waitUntil([&] { return window->isExposed(); }));
    }

    QObject* popup() const { return combo->findChild<QObject*>(QStringLiteral("searchComboPopup")); }
    QQuickItem* field() const { return combo->findChild<QQuickItem*>(QStringLiteral("searchComboField")); }
    QQuickItem* list() const { return combo->findChild<QQuickItem*>(QStringLiteral("searchComboList")); }
    FuzzyListModel* matches() const { return combo->findChild<FuzzyListModel*>(); }

    void open() {
        QMetaObject::invokeMethod(popup(), "open");
        ASSERT_TRUE(waitUntil([&] { return popup()->property("opened").toBool() && field()->hasActiveFocus(); }));
    }

    // QTest::keyClicks is for widgets; a window takes the keys one at a time.
    void type(const char* text) const {
        for (const char* c = text; *c; ++c) {
            QTest::keyClick(window, *c);
        }
    }

    // The names shown, top to bottom.
    QStringList shown() const {
        QStringList out;
        for (int row = 0; row < matches()->rowCount(); ++row) {
            out.append(matches()->textAt(row));
        }
        return out;
    }
    QString marked() const { return matches()->textAt(list()->property("currentIndex").toInt()); }
};

}  // namespace

// Why: the whole use once. It opens on the name chosen with the field ready for typing;
// letters narrow the list to the names that fit, best first and marked, and Enter takes it.
TEST(SearchCombo, TypingNarrowsTheListAndEnterTakesTheBestFit) {
    ComboFixture f;
    EXPECT_EQ(f.combo->property("displayText").toString(), QStringLiteral("Open Sans"));
    f.open();
    EXPECT_EQ(f.shown().size(), 6) << "all, while nothing is typed";
    EXPECT_EQ(f.marked(), QStringLiteral("Open Sans")) << "it opens on the name chosen";

    f.type("mono");
    EXPECT_EQ(f.shown(), (QStringList{"Liberation Mono", "Harmonious"}));
    EXPECT_EQ(f.marked(), QStringLiteral("Liberation Mono"));
    QTest::keyClick(f.window, Qt::Key_Return);
    ASSERT_EQ(f.picked->count(), 1);
    EXPECT_EQ(f.picked->at(0).at(0).toString(), QStringLiteral("Liberation Mono"));
    EXPECT_TRUE(waitUntil([&] { return !f.popup()->property("visible").toBool(); }));
}

// Why: the field stays on top and the list is moved with the keys, as in a menu.
TEST(SearchCombo, ArrowsMoveTheMarkAndEscapeLeavesTheChoice) {
    ComboFixture f;
    f.open();
    f.type("iose");
    EXPECT_EQ(f.shown(), (QStringList{"Iosevka", "Iosevka Term"}));
    QTest::keyClick(f.window, Qt::Key_Down);
    EXPECT_EQ(f.marked(), QStringLiteral("Iosevka Term"));
    QTest::keyClick(f.window, Qt::Key_Up);
    EXPECT_EQ(f.marked(), QStringLiteral("Iosevka"));
    QTest::keyClick(f.window, Qt::Key_Escape);
    EXPECT_TRUE(waitUntil([&] { return !f.popup()->property("visible").toBool(); }));
    EXPECT_EQ(f.picked->count(), 0);

    f.open();
    EXPECT_EQ(f.field()->property("text").toString(), QString()) << "it opens with nothing typed";
}

// Why: the letters matched are shown bold and underlined, and a name is escaped, so that one with '&' or
// '<' shows as written.
TEST(SearchCombo, TheLettersMatchedAreBold) {
    FuzzyListModel model;
    model.setItems({QStringLiteral("PT Sans"), QStringLiteral("A&B <x>")});
    model.setFilter(QStringLiteral("ps"));
    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.data(model.index(0), FuzzyListModel::MarkedRole).toString(), QStringLiteral("<b><u>P</u></b>T <b><u>S</u></b>ans"));
    model.setFilter(QStringLiteral("ab"));
    EXPECT_EQ(model.data(model.index(0), FuzzyListModel::MarkedRole).toString(), QStringLiteral("<b><u>A</u></b>&amp;<b><u>B</u></b> &lt;x&gt;"));
}
