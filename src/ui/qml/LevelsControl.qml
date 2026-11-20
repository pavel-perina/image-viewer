pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Levels as handles on gray gradients under the histogram: black, gamma and white on the input,
// the range they map to on the output. Values are 0 to 255 of the samples as stored. The gamma
// handle stands where the output reaches half, so it keeps its place between black and white
// when they move. A double click puts a handle back.
ColumnLayout {
    id: control

    // Drawn by hand, so it shows being disabled itself, as the styled controls do.
    opacity: enabled ? 1 : 0.45

    property var histogram
    property real black: 0
    property real white: 255
    property real gamma: 1
    property real outBlack: 0
    property real outWhite: 255

    signal inputPicked(real black, real gamma, real white)
    signal outputPicked(real outBlack, real outWhite)

    // Handles reach half their width past the ends of the bars.
    readonly property real handleSize: 11
    readonly property real inset: handleSize / 2

    spacing: 2

    HistogramView {
        objectName: "levelsHistogram"
        Layout.fillWidth: true
        Layout.leftMargin: control.inset
        Layout.rightMargin: control.inset
        Layout.preferredHeight: 80
        histogram: control.histogram
    }

    LevelsBar {
        id: inputBar
        objectName: "levelsInput"
        Layout.topMargin: 2
        // Where the output reaches half, between black and white.
        readonly property real gammaLevel: control.black + (control.white - control.black) * Math.pow(0.5, control.gamma)
        levels: [control.black, gammaLevel, control.white]
        fills: ["#000000", "#808080", "#ffffff"]
        onMoved: (index, level) => {
            if (index === 0) {
                control.inputPicked(Math.min(Math.round(level), control.white - 1), control.gamma, control.white)
            } else if (index === 2) {
                control.inputPicked(control.black, control.gamma, Math.max(Math.round(level), control.black + 1))
            } else {
                const t = Math.max(0.01, Math.min(0.99, (level - control.black) / (control.white - control.black)))
                const gamma = Math.max(0.1, Math.min(9.99, Math.log(t) / Math.log(0.5)))
                control.inputPicked(control.black, Math.round(gamma * 100) / 100, control.white)
            }
        }
        onReset: (index) => {
            if (index === 0)
                control.inputPicked(0, control.gamma, control.white)
            else if (index === 2)
                control.inputPicked(control.black, control.gamma, 255)
            else
                control.inputPicked(control.black, 1, control.white)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: control.inset
        Layout.rightMargin: control.inset
        Label { text: Math.round(control.black); color: Theme.textMuted }
        Item { Layout.fillWidth: true }
        Label { objectName: "levelsGamma"; text: control.gamma.toFixed(2); color: Theme.textMuted }
        Item { Layout.fillWidth: true }
        Label { text: Math.round(control.white); color: Theme.textMuted }
    }

    LevelsBar {
        objectName: "levelsOutput"
        Layout.topMargin: 6
        levels: [control.outBlack, control.outWhite]
        fills: ["#000000", "#ffffff"]
        onMoved: (index, level) => {
            if (index === 0)
                control.outputPicked(Math.round(level), control.outWhite)
            else
                control.outputPicked(control.outBlack, Math.round(level))
        }
        onReset: (index) => {
            if (index === 0)
                control.outputPicked(0, control.outWhite)
            else
                control.outputPicked(control.outBlack, 255)
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: control.inset
        Layout.rightMargin: control.inset
        Label { text: "Output " + Math.round(control.outBlack); color: Theme.textMuted }
        Item { Layout.fillWidth: true }
        Label { text: Math.round(control.outWhite); color: Theme.textMuted }
    }

    // A gradient from black to white with triangles under it, one per level. A press takes the
    // nearest triangle; of two on the same spot, the one that can move towards the press.
    component LevelsBar: Item {
        id: bar
        property var levels: []
        property var fills: []
        signal moved(int index, real level)
        signal reset(int index)

        readonly property real trackWidth: width - 2 * control.inset
        function xOf(level) { return control.inset + level / 255 * trackWidth }
        function levelAt(x) { return Math.max(0, Math.min(255, (x - control.inset) / trackWidth * 255)) }

        Layout.fillWidth: true
        implicitHeight: 10 + control.handleSize

        Rectangle {
            x: control.inset
            width: bar.trackWidth
            height: 10
            border.color: Theme.swatchFrame
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: "black" }
                GradientStop { position: 1; color: "white" }
            }
        }

        Repeater {
            model: bar.levels.length
            Canvas {
                id: handle
                required property int index
                readonly property string fill: bar.fills[index]
                x: bar.xOf(bar.levels[index]) - width / 2
                y: 10
                width: control.handleSize
                height: control.handleSize
                onFillChanged: requestPaint()
                onPaint: {
                    const g = getContext("2d")
                    g.reset()
                    g.beginPath()
                    g.moveTo(width / 2, 0.5)
                    g.lineTo(width - 0.5, height - 0.5)
                    g.lineTo(0.5, height - 0.5)
                    g.closePath()
                    g.fillStyle = fill
                    g.fill()
                    g.strokeStyle = Theme.textMuted
                    g.lineWidth = 1
                    g.stroke()
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            property int dragging: -1
            property real offset: 0  // from the press to the handle, so it does not jump
            function nearest(x) {
                const level = bar.levelAt(x)
                let best = 0
                for (let i = 1; i < bar.levels.length; ++i) {
                    const d = Math.abs(bar.levels[i] - level)
                    const bestD = Math.abs(bar.levels[best] - level)
                    // On a tie the later handle wins to the right of it, the earlier to the left.
                    if (d < bestD || (d === bestD && level > bar.levels[i]))
                        best = i
                }
                return best
            }
            onPressed: (mouse) => {
                dragging = nearest(mouse.x)
                offset = bar.levels[dragging] - bar.levelAt(mouse.x)
            }
            onPositionChanged: (mouse) => {
                if (dragging >= 0)
                    bar.moved(dragging, Math.max(0, Math.min(255, bar.levelAt(mouse.x) + offset)))
            }
            onReleased: dragging = -1
            onDoubleClicked: (mouse) => bar.reset(nearest(mouse.x))
        }
    }
}
