pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// A combo box for a long list of names: it opens with a field on top to type a few letters
// in, fzf style, and the names that fit below it, best first, with the letters matched in
// bold and underlined. Up and Down move, Enter takes the name marked, Escape leaves the
// choice as it was. Nothing in it is about what the names are; the matching is
// FuzzyListModel's.
ComboBox {
    id: control

    // The names to choose from, in the order to show while nothing is typed.
    property var names: []
    // The name chosen, shown in the closed box. It need not be one of the names.
    property string currentName
    // Each name drawn in the font of that name, for a list of font families.
    property bool namesInTheirFont: false
    // Rows shown at most before the list scrolls.
    property int rowsShown: 12

    // A name was taken from the list.
    signal picked(string name)

    displayText: currentName
    model: []

    FuzzyListModel {
        id: matches
        items: control.names
        filter: field.text
        // The best fit is the one marked; with nothing typed, the current name.
        onModelReset: list.currentIndex = filter === "" ? Math.max(0, rowOf(control.currentName)) : 0
    }

    function take(row) {
        const name = matches.textAt(row)
        if (name === "")
            return
        popup.close()
        control.picked(name)
    }

    popup: Popup {
        objectName: "searchComboPopup"
        y: control.height
        width: Math.max(control.width, 260)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 420)
        padding: 4
        margins: 8

        onAboutToShow: {
            field.text = ""
            list.currentIndex = Math.max(0, matches.rowOf(control.currentName))
        }
        onOpened: {
            list.positionViewAtIndex(list.currentIndex, ListView.Center)
            field.forceActiveFocus()
        }

        background: Rectangle {
            color: Theme.base
            border.width: 1
            border.color: Theme.frame
        }

        contentItem: ColumnLayout {
            spacing: 4

            TextField {
                id: field
                objectName: "searchComboField"
                Layout.fillWidth: true
                placeholderText: "Type to find"

                Keys.onUpPressed: list.decrementCurrentIndex()
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onPressed: (event) => {
                    const page = Math.max(1, Math.floor(list.height / 26) - 1)
                    if (event.key === Qt.Key_PageDown) {
                        list.currentIndex = Math.min(list.count - 1, list.currentIndex + page)
                        event.accepted = true
                    } else if (event.key === Qt.Key_PageUp) {
                        list.currentIndex = Math.max(0, list.currentIndex - page)
                        event.accepted = true
                    }
                }
                Keys.onReturnPressed: control.take(list.currentIndex)
                Keys.onEnterPressed: control.take(list.currentIndex)
                Keys.onEscapePressed: control.popup.close()
            }

            ListView {
                id: list
                objectName: "searchComboList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                implicitHeight: Math.min(count, control.rowsShown) * 26
                clip: true
                model: matches
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                ScrollBar.vertical: AppScrollBar {}

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: "Nothing fits"
                    color: Theme.textMuted
                }

                delegate: Rectangle {
                    id: row
                    required property int index
                    required property string text
                    required property string marked
                    readonly property bool current: ListView.isCurrentItem

                    width: ListView.view.width
                    height: 26
                    radius: 2
                    color: current ? Theme.highlight : rowHover.hovered ? Theme.hover : "transparent"

                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        textFormat: Text.StyledText
                        text: row.marked
                        color: row.current ? Theme.highlightedText : Theme.text
                        font.family: control.namesInTheirFont ? row.text : control.font.family
                    }
                    HoverHandler { id: rowHover }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: control.take(row.index)
                    }
                }
            }
        }
    }
}
