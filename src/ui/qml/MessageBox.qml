import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// A caption, a short text, an icon for its kind and buttons, as message boxes are everywhere. Enter
// presses the button in focus, Escape rejects, and Ctrl+C copies the message.
AppDialog {
    id: box

    property string kind: "info"  // info, question, warning or error
    property string text
    // Which of the standard buttons starts in focus. The first if unset.
    property int focusedButton: Dialog.NoButton
    // Shown under the text, such as check boxes.
    property alias extras: extraColumn.data

    width: Math.min(560, parent ? parent.width - 32 : 560)

    // The button that starts in focus, which Enter presses from anywhere but another button.
    function defaultButton() {
        for (const which of [focusedButton, Dialog.Ok, Dialog.Yes]) {
            const button = which !== Dialog.NoButton ? standardButton(which) : null
            if (button)
                return button
        }
        return null
    }

    function isFooterButton(item) {
        for (let parent = item; parent; parent = parent.parent) {
            if (parent === box.footer)
                return typeof item.click === "function"
        }
        return false
    }

    onOpened: {
        const button = defaultButton()
        if (button)
            button.forceActiveFocus()
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: box.opened
        onActivated: {
            // A check box is toggled with Space, as in any dialog.
            const item = box.contentItem.Window.activeFocusItem
            const button = item && box.isFooterButton(item) ? item : box.defaultButton()
            if (button)
                button.click()
        }
    }
    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: box.opened
        onActivated: {
            copier.selectAll()
            copier.copy()
        }
    }
    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 14

        // QML has no clipboard of its own. In here, as a dialog takes its size from a single child,
        // and a layout passes over what is not visible.
        TextEdit {
            id: copier
            visible: false
            textFormat: TextEdit.PlainText
            text: box.title + "\n\n" + box.text
        }

        Item {
            objectName: "messageIcon"
            Layout.alignment: Qt.AlignTop
            implicitWidth: 32
            implicitHeight: 32

            Canvas {
                id: icon
                anchors.fill: parent
                Connections {
                    target: box
                    function onKindChanged() { icon.requestPaint() }
                }
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = box.kind === "error" ? Theme.dangerFill : box.kind === "warning" ? Theme.warningFill : Theme.accent
                    ctx.beginPath()
                    if (box.kind === "warning") {
                        ctx.moveTo(16, 2)
                        ctx.lineTo(31, 29)
                        ctx.lineTo(1, 29)
                        ctx.closePath()
                    } else {
                        ctx.arc(16, 16, 15, 0, 2 * Math.PI)
                    }
                    ctx.fill()
                }
            }
            Label {
                anchors.fill: parent
                anchors.topMargin: box.kind === "warning" ? 4 : 0
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.onFill
                font.bold: true
                font.pixelSize: 20
                text: box.kind === "error" ? "×" : box.kind === "warning" ? "!" : box.kind === "question" ? "?" : "i"
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                objectName: "messageText"
                Layout.fillWidth: true
                text: box.text
                textFormat: Text.PlainText
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }
            ColumnLayout {
                id: extraColumn
                Layout.fillWidth: true
                visible: children.length > 0
                spacing: 0
            }
        }
    }
}
