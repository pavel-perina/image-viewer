import QtQuick
import QtQuick.Controls
import ImageViewer

// A tool bar button that shows an icon from assets/icons in place of its text, in the theme's
// colours: the icon's white in the button's text colour, its green in the accent. The text,
// with the shortcut, becomes the tip, and still names the button for accessibility.
ToolButton {
    id: button

    property string iconName  // as listed in src/ui/CMakeLists.txt, such as "zoom-in"
    readonly property string tip: {
        const name = text.replace("&", "")
        const keys = action ? KeyNames.text(action.shortcut) : ""
        return keys !== "" ? name + " (" + keys + ")" : name
    }

    display: AbstractButton.IconOnly
    contentItem: ThemedIcon {
        objectName: "icon"
        name: button.iconName
        implicitWidth: Theme.toolIconSize
        implicitHeight: Theme.toolIconSize
        ink: button.enabled ? button.palette.buttonText : Theme.textDisabled
        accent: button.enabled ? Theme.accent : Theme.textDisabled
    }

    ToolTip.visible: hovered && tip !== ""
    ToolTip.delay: 600
    ToolTip.text: tip
}
