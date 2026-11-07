import QtQuick
import QtQuick.Controls
import ImageViewer

// A menu item that also shows its action's shortcut, a little dimmer, at the right.
MenuItem {
    id: item

    readonly property string shortcutText: action ? KeyNames.text(action.shortcut) : ""

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding + (shortcutText ? shortcutLabel.implicitWidth + 32 : 0))

    Label {
        id: shortcutLabel
        objectName: "shortcutText"
        anchors.right: parent.right
        anchors.rightMargin: item.rightPadding
        anchors.verticalCenter: parent.verticalCenter
        visible: text !== ""
        text: item.shortcutText
        color: item.highlighted ? palette.highlightedText : palette.windowText
        opacity: item.enabled ? 0.55 : 0.3
    }
}
