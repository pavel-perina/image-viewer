import QtQuick
import QtQuick.Controls

// A flat button with a drawn icon, for the bar above the thumbnails.
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
    width: 26
    height: 26
    radius: 3
    color: hover.hovered && usable ? Theme.hover : "transparent"

    ToolTip.visible: tip !== "" && hover.hovered && !menuOpen
    ToolTip.delay: 600
    ToolTip.text: tip

    Canvas {
        anchors.centerIn: parent
        width: 16
        height: 16
        property color ink: button.ink
        property bool lit: button.lit
        onInkChanged: requestPaint()
        onLitChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = ink
            ctx.fillStyle = ink
            ctx.lineWidth = 1.5
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            if (button.icon === "refresh") {
                ctx.arc(8, 8, 5, -0.6, 4.2)
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(13.5, 1.5)
                ctx.lineTo(13.5, 5.5)
                ctx.lineTo(9.5, 5.5)
                ctx.stroke()
            } else if (button.icon === "star") {
                for (let i = 0; i < 10; ++i) {
                    const radius = i % 2 === 0 ? 6.5 : 2.75
                    const angle = -Math.PI / 2 + i * Math.PI / 5
                    ctx.lineTo(8 + radius * Math.cos(angle), 8.5 + radius * Math.sin(angle))
                }
                ctx.closePath()
                if (lit)
                    ctx.fill()
                else
                    ctx.stroke()
            } else {
                // An arrow with a shaft, unlike the carets of the path beside it.
                const turn = button.icon === "back" ? Math.PI : button.icon === "up" ? -Math.PI / 2 : 0
                ctx.translate(8, 8)
                ctx.rotate(turn)
                ctx.moveTo(-5.5, 0)
                ctx.lineTo(5.5, 0)
                ctx.moveTo(1.5, -4)
                ctx.lineTo(5.5, 0)
                ctx.lineTo(1.5, 4)
                ctx.stroke()
            }
        }
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
