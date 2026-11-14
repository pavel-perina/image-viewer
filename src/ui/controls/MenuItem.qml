import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.impl
import QtQuick.Shapes
import ImageViewer

// One line of a menu. Every line keeps the same column on the left, for a check box or an
// icon, so that the words of all lines start at one place whether a line has either or not.
// A checkable line shows its box, ticked or not; another may show an icon there.
T.MenuItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    padding: 4
    verticalPadding: 5
    spacing: 8
    leftPadding: 6 + Theme.menuMarkWidth + spacing
    rightPadding: 12 + (arrow.visible ? arrow.width + spacing : 0)

    icon.width: 16
    icon.height: 16
    icon.color: textColour

    readonly property color textColour: !enabled ? Theme.textDisabled
                                        : highlighted ? Theme.highlightedText : Theme.text

    // The column on the left: the icon, if any and not checkable; centred in it.
    contentItem: IconLabel {
        readonly property bool iconInColumn: !control.checkable && control.icon.source.toString() !== ""
        spacing: control.spacing
        mirrored: control.mirrored
        display: IconLabel.TextOnly
        alignment: Qt.AlignLeft
        text: control.text
        font: control.font
        color: control.textColour

        IconImage {
            x: -(Theme.menuMarkWidth + control.spacing) + (Theme.menuMarkWidth - width) / 2
            anchors.verticalCenter: parent.verticalCenter
            visible: parent.iconInColumn
            source: control.icon.source
            sourceSize.width: control.icon.width
            sourceSize.height: control.icon.height
            color: control.icon.color
        }
    }

    indicator: Rectangle {
        objectName: "menuCheckBox"
        x: 6 + (Theme.menuMarkWidth - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        implicitWidth: 14
        implicitHeight: 14
        visible: control.checkable
        radius: 3
        color: !control.enabled ? "transparent" : control.checked ? Theme.accent : Theme.base
        border.width: control.checked ? 0 : 1
        border.color: control.enabled ? Theme.textMuted : Theme.textDisabled

        Shape {
            anchors.fill: parent
            visible: control.checked
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.base
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                startX: 3; startY: 7.5
                PathLine { x: 5.75; y: 10.25 }
                PathLine { x: 11; y: 4 }
            }
        }
    }

    // A submenu's arrow, at the right.
    arrow: Shape {
        id: arrow
        x: control.mirrored ? control.padding : control.width - width - 10
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 5
        height: 9
        visible: control.subMenu
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: control.textColour
            strokeWidth: 1.5
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            startX: 1; startY: 1
            PathLine { x: 4; y: 4.5 }
            PathLine { x: 1; y: 8 }
        }
    }

    background: Rectangle {
        implicitWidth: 180
        implicitHeight: Theme.controlHeight
        x: 3
        width: control.width - 6
        radius: Theme.controlRadius - 1
        color: control.highlighted ? Theme.highlight : "transparent"
    }
}
