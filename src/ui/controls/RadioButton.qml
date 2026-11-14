import QtQuick
import QtQuick.Templates as T
import ImageViewer

// One of a few choices of which one is taken: the check box's look, round, with a dot for the
// one taken. Off, a clear edge on the colour of a field; disabled, the edge and a dimmed dot.
T.RadioButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    spacing: 8

    indicator: Rectangle {
        objectName: "radioButtonIndicator"
        implicitWidth: 16
        implicitHeight: 16
        x: control.text ? (control.mirrored ? control.width - width - control.rightPadding : control.leftPadding)
                        : control.leftPadding + (control.availableWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: width / 2
        readonly property bool filled: control.checked && control.enabled
        color: !control.enabled ? Theme.buttonDisabled : filled ? Theme.accent : Theme.base
        border.width: filled ? 0 : 1
        border.color: !control.enabled ? Theme.textDisabled
                      : control.hovered ? Theme.text : Theme.textMuted

        Rectangle {
            anchors.centerIn: parent
            width: 6
            height: 6
            radius: 3
            visible: control.checked
            color: control.enabled ? Theme.base : Theme.textDisabled
        }
        // The focus ring.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: width / 2
            visible: control.visualFocus
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }

    contentItem: Text {
        leftPadding: control.indicator && !control.mirrored ? control.indicator.width + control.spacing : 0
        rightPadding: control.indicator && control.mirrored ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
