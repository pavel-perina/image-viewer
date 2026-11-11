import QtQuick
import QtQuick.Controls

// A narrow button with a padlock, closed while checked, that keeps the setting of the button
// beside it. Drawn until there is an icon.
ToolButton {
    id: button

    property string tip
    implicitWidth: 22
    focusPolicy: Qt.NoFocus
    display: AbstractButton.IconOnly

    ToolTip.visible: tip !== "" && hovered
    ToolTip.delay: 600
    ToolTip.text: tip

    contentItem: Canvas {
        implicitWidth: 14
        implicitHeight: 16
        property color ink: button.enabled ? palette.buttonText : Theme.textDisabled
        property bool closed: button.checked
        onInkChanged: requestPaint()
        onClosedChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = ink
            ctx.fillStyle = ink
            ctx.lineWidth = 1.5
            // The shackle, lifted and out of the body on the left when open.
            ctx.beginPath()
            const top = closed ? 5 : 3.5
            ctx.moveTo(3.5, closed ? 8 : 5.5)
            ctx.lineTo(3.5, top)
            ctx.arc(7, top, 3.5, Math.PI, 0)
            ctx.lineTo(10.5, 8)
            ctx.stroke()
            // The body.
            ctx.fillRect(1.5, 8, 11, 7)
        }
    }
}
