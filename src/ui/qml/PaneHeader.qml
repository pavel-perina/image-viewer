import QtQuick
import QtQuick.Controls
import ImageViewer

// The caption over a pane of the browser (Folders, Preview), so that the panes read alike. One
// that can be collapsed has its caret at the right, out of the caption's way, so that captions
// start at one place whether they collapse or not; a click anywhere on it collapses or opens.
Item {
    id: header

    property string text
    property string detail
    property bool collapsible: false
    property bool collapsed: false
    property bool focused: false
    signal toggled()

    implicitHeight: 26

    Rectangle {
        anchors.fill: parent
        color: header.collapsible && hover.hovered ? Theme.hover : "transparent"
    }
    Label {
        anchors.left: parent.left
        anchors.right: detailLabel.visible ? detailLabel.left : caret.visible ? caret.left : parent.right
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: header.text
        color: header.focused ? Theme.text : Theme.textMuted
        font.weight: header.focused ? Font.DemiBold : Font.Normal
        elide: Text.ElideRight
    }
    Label {
        id: detailLabel
        objectName: "paneHeaderDetail"
        anchors.right: caret.visible ? caret.left : parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        visible: text !== ""
        text: header.detail
        color: Theme.textMuted
        font.pointSize: Theme.smallSize
    }
    Caret {
        id: caret
        visible: header.collapsible
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        rotationAngle: header.collapsed ? 0 : 90
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
