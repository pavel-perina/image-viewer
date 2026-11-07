import QtQuick
import QtQuick.Controls

// A dialog drawn inside the main window, which the system does not decorate, so it brings its own
// frame, title bar and shadow to read as a window above the rest.
Dialog {
    id: dialog

    readonly property color frameColor: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.3)

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    padding: 16

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.35)
    }

    header: Rectangle {
        implicitHeight: 34
        color: Qt.lighter(palette.window, 1.35)
        radius: 4
        // Square below, where it meets the body.
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: dialog.frameColor
        }
        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: dialog.title
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }

    background: Item {
        // Layers growing outwards, a little darker where they overlap. No shader, so it also
        // shows on the software renderer.
        Repeater {
            model: 10
            Rectangle {
                required property int index
                readonly property int reach: 2 + index * 2
                x: -reach
                y: -reach + 4
                width: parent.width + 2 * reach
                height: parent.height + 2 * reach
                radius: 4 + reach
                color: Qt.rgba(0, 0, 0, 0.05)
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: palette.window
            border.width: 1
            border.color: dialog.frameColor
        }
    }
}
