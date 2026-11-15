import QtQuick
import QtQuick.Controls

// A flat button with an icon, for the bar above the thumbnails: a tool button the height of a
// field, so that it looks and answers the mouse as the tool bar's do. A right click is a second
// action (the back button's history).
IconButton {
    id: button

    property string kind  // back, forward, up, refresh or star
    property bool usable: true
    property bool lit: false  // a star that is filled
    property bool menuOpen: false  // the tip would cover it

    signal activated()
    signal secondaryActivated()

    objectName: kind + "Button"
    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    padding: (Theme.controlHeight - Theme.toolIconSize) / 2
    enabled: usable
    focusPolicy: Qt.NoFocus
    text: tip  // names it for accessibility, as it shows no words
    iconName: kind === "star" ? (lit ? "starred" : "non-starred")
        : ({ back: "go-previous", forward: "go-next", up: "go-up", refresh: "view-refresh" })[kind] ?? ""
    onClicked: activated()

    ToolTip.visible: tip !== "" && hovered && !menuOpen

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: button.secondaryActivated()
    }
}
