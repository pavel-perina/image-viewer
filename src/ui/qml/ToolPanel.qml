import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// What every panel of a tool is: a sheet beside the image that slides in from the right, with
// a heading, the tool's own controls, and at the bottom the buttons to apply or leave, with
// one more on the left where a tool has a use for it, such as Reset. A tool's panel derives
// from this and adds only its controls, which are laid out in a column.
Rectangle {
    id: panel

    property string title
    // In view; it slides in and out, unless the page says not to animate.
    property bool revealed: false
    property bool animated: true
    // Whether it ends in buttons at all: a tool that changes nothing has none.
    property bool buttons: true
    property string applyText: "Apply"
    property bool applyEnabled: true
    // The name the apply button is found by.
    property string applyName
    // The button on the left; none while this is empty.
    property string extraText
    // Whether the controls take all the height, as a list does; otherwise they keep to the top.
    property bool fills: false

    signal applyRequested()
    signal cancelRequested()
    signal extraRequested()
    // Enter in a field took its value, and the keys can go back to the image.
    signal doneEditing()

    // A field has the keys, so Enter is its own rather than the tool's.
    readonly property bool editing: {
        for (let item = Window.activeFocusItem; item; item = item.parent) {
            if (item === panel)
                return true
        }
        return false
    }

    default property alias content: column.data

    width: Theme.panelWidth
    color: Theme.panel
    anchors.top: parent ? parent.top : undefined
    anchors.bottom: parent ? parent.bottom : undefined
    x: parent ? (revealed ? parent.width - width : parent.width) : 0
    visible: parent ? x < parent.width : false

    Behavior on x {
        enabled: panel.animated
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
    }

    // Keeps the mouse from reaching the image underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    // The right margin is left to the scroll bar, which sits in it; the heading and the buttons
    // keep it themselves.
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.panelMargin
        anchors.rightMargin: 0
        spacing: Theme.panelSpacing

        Label {
            Layout.rightMargin: Theme.panelMargin
            text: panel.title
            font.bold: true
            font.pointSize: Theme.titleSize
        }

        // The controls scroll when the window is too low for them, between the heading and the
        // buttons, which stay. A panel that fills takes the height given and scrolls within.
        Flickable {
            id: scroller
            objectName: "toolPanelScroller"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: width
            contentHeight: column.height
            boundsBehavior: Flickable.StopAtBounds
            // Only when there is something to scroll, so that a drag that fits stays a drag.
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {}

            ColumnLayout {
                id: column
                // The scroll bar takes the margin, so the width is the same whether it scrolls
                // or not. Were it narrower while scrolling, a column of wrapped text that only
                // just fits could keep switching between the two widths, laid out anew each frame.
                width: scroller.width - Theme.panelMargin
                height: panel.fills ? scroller.height : implicitHeight
                spacing: Theme.panelSpacing
            }

            // Tab to a field out of view brings it in.
            function reveal(item) {
                if (!item || !interactive)
                    return
                for (let up = item; up; up = up.parent) {
                    if (up === column) {
                        const top = item.mapToItem(column, 0, 0).y
                        const bottom = top + item.height
                        if (top < contentY)
                            contentY = top
                        else if (bottom > contentY + height)
                            contentY = Math.min(bottom - height, contentHeight - height)
                        return
                    }
                }
            }
            Connections {
                target: panel.Window.window
                function onActiveFocusItemChanged() { scroller.reveal(panel.Window.activeFocusItem) }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: Theme.panelMargin
            visible: panel.buttons
            Button {
                focusPolicy: Qt.NoFocus
                visible: panel.extraText !== ""
                text: panel.extraText
                onClicked: panel.extraRequested()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Cancel"
                onClicked: panel.cancelRequested()
            }
            Button {
                objectName: panel.applyName
                text: panel.applyText
                highlighted: true
                enabled: panel.applyEnabled
                onClicked: panel.applyRequested()
            }
        }
    }
}
