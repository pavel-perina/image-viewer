import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The caption over a pane of the browser (Folders, Preview) or a section of a tool's panel, so
// that they read alike. One that can be collapsed has its caret at the right, out of the
// caption's way, so that captions start at one place whether they collapse or not; a click
// anywhere on it collapses or opens. A section may also show that it is in use, by a mark in the
// accent that stays when collapsed, have a box that turns it on and off while keeping its
// values, and a Reset while in use.
Item {
    id: header

    property string text
    property string detail
    property bool collapsible: false
    property bool collapsed: false
    property bool focused: false
    property bool inUse: false
    property bool switchable: false
    property bool on: true
    property string switchTip
    property bool resettable: false
    signal toggled()
    signal switched(bool on)
    signal resetRequested()

    implicitHeight: 26

    Rectangle {
        anchors.fill: parent
        color: header.collapsible && hover.hovered ? Theme.hover : "transparent"
    }
    Rectangle {
        objectName: "paneHeaderInUse"
        visible: header.inUse
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 5
        anchors.bottomMargin: 5
        width: 3
        radius: 1
        color: Theme.accent
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: header.switchable ? 4 : 8
        anchors.rightMargin: 8
        spacing: 4

        CheckBox {
            objectName: "paneHeaderSwitch"
            visible: header.switchable
            focusPolicy: Qt.NoFocus
            padding: 2
            checked: header.on
            onToggled: header.switched(checked)
            ToolTip.visible: hovered && header.switchTip !== ""
            ToolTip.delay: 600
            ToolTip.text: header.switchTip
        }
        Label {
            objectName: "paneHeaderText"
            Layout.fillWidth: true
            text: header.text
            color: header.focused ? Theme.text : Theme.textMuted
            font.weight: header.focused ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }
        Label {
            objectName: "paneHeaderDetail"
            visible: text !== ""
            Layout.rightMargin: 4
            text: header.detail
            color: Theme.textMuted
            font.pointSize: Theme.smallSize
        }
        ToolButton {
            objectName: "paneHeaderReset"
            visible: header.resettable && header.inUse
            focusPolicy: Qt.NoFocus
            Layout.preferredHeight: header.height - 4
            padding: 2
            horizontalPadding: 6
            text: "Reset"
            font.pointSize: Theme.smallSize
            onClicked: header.resetRequested()
        }
        Caret {
            visible: header.collapsible
            rotationAngle: header.collapsed ? 0 : 90
        }
    }
    HoverHandler {
        id: hover
        enabled: header.collapsible
    }
    TapHandler {
        enabled: header.collapsible
        onTapped: header.toggled()
    }
}
