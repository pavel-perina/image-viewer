import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The caption over a pane of the browser (Folders, Preview) or a section of a tool's panel, so
// that they read alike. One that can be collapsed has its caret at the right, out of the
// caption's way, so that captions start at one place whether they collapse or not; a click
// anywhere on it collapses or opens. A section may also have a box that turns it on and off
// while keeping its values, and the arrow that resets values elsewhere, enabled while the
// section is in use, which so shows it in use when folded too; and stand on a band, raised a
// little from the panel, so that sections read apart from their controls.
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
    property bool switchEnabled: true
    property string switchTip
    property bool resettable: false
    property bool banded: false
    signal toggled()
    signal switched(bool on)
    signal resetRequested()

    implicitHeight: 26

    Rectangle {
        objectName: "paneHeaderBand"
        anchors.fill: parent
        visible: header.banded
        radius: Theme.controlRadius
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.surfaceTop }
            GradientStop { position: 1; color: Theme.surfaceBottom }
        }
        border.color: Theme.paneFrame
    }
    Rectangle {
        anchors.fill: parent
        radius: header.banded ? Theme.controlRadius : 0
        color: header.collapsible && hover.hovered ? Theme.hover : "transparent"
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: header.switchable ? 4 : 8
        anchors.rightMargin: 8
        spacing: 4

        CheckBox {
            objectName: "paneHeaderSwitch"
            visible: header.switchable
            enabled: header.switchEnabled
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
            // On a band the caption stands out already; bold would shout.
            font.weight: header.banded ? Font.Medium : header.focused ? Font.DemiBold : Font.Normal
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
        IconButton {
            objectName: "paneHeaderReset"
            visible: header.resettable
            enabled: header.inUse
            focusPolicy: Qt.NoFocus
            iconName: "edit-reset"
            iconSize: 16
            padding: 2
            text: "Reset " + header.text
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
