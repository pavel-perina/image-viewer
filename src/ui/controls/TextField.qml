import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import ImageViewer

// A field to type in: the colour of a field with an edge, rounded as the combo box and the
// spin box are and as high, so that a field, a list to choose from and a button in one row
// line up. The edge lightens under the mouse; with the keys, a ring in the accent.
T.TextField {
    id: control

    implicitWidth: implicitBackgroundWidth + leftInset + rightInset
                   || Math.max(contentWidth, placeholder.implicitWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    padding: 4
    leftPadding: Theme.controlPadding - 4
    rightPadding: Theme.controlPadding - 4

    color: enabled ? Theme.text : Theme.textDisabled
    selectionColor: Theme.highlight
    selectedTextColor: Theme.highlightedText
    placeholderTextColor: Theme.placeholderText
    verticalAlignment: TextInput.AlignVCenter

    PlaceholderText {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        verticalAlignment: control.verticalAlignment
        elide: Text.ElideRight
        renderType: control.renderType
        visible: !control.length && !control.preeditText && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: Theme.controlHeight
        radius: Theme.controlRadius
        color: control.enabled ? Theme.base : Theme.buttonDisabled
        border.width: 1
        border.color: !control.enabled ? Theme.buttonDisabled
                      : control.hovered || control.activeFocus ? Theme.light : Theme.mid

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Theme.controlRadius + 2
            visible: control.activeFocus && !control.readOnly
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }
}
