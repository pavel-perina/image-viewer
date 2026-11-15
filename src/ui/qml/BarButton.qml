import QtQuick
import QtQuick.Controls

// A flat button with an icon from assets/icons, for the bar above the thumbnails.
Rectangle {
    id: button

    property string icon  // back, forward, up, refresh or star
    property bool usable: true
    property bool lit: false  // a star that is filled
    property string tip
    property bool menuOpen: false  // the tip would cover it
    readonly property color ink: usable ? palette.windowText : Theme.textDisabled

    signal activated()
    signal secondaryActivated()

    objectName: icon + "Button"
    width: Theme.controlHeight
    height: Theme.controlHeight
    radius: 3
    color: hover.hovered && usable ? Theme.hover : "transparent"

    ToolTip.visible: tip !== "" && hover.hovered && !menuOpen
    ToolTip.delay: 600
    ToolTip.text: tip

    ThemedIcon {
        objectName: "icon"
        anchors.centerIn: parent
        width: Theme.toolIconSize
        height: Theme.toolIconSize
        name: button.icon === "star" ? (button.lit ? "starred" : "non-starred")
            : ({ back: "go-previous", forward: "go-next", up: "go-up", refresh: "view-refresh" })[button.icon] ?? ""
        ink: button.ink
        accent: button.usable ? Theme.accent : Theme.textDisabled
    }

    HoverHandler { id: hover }
    TapHandler {
        enabled: button.usable
        onTapped: button.activated()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: button.secondaryActivated()
    }
}
