import QtQuick
import QtQuick.Templates as T
import QtQuick.Shapes
import ImageViewer

// A field to choose from a list: dark like a text field, with a chevron, and a list that opens
// below it on a raised surface.
T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding,
                             implicitIndicatorHeight + topPadding + bottomPadding)

    leftPadding: padding + (!control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)
    rightPadding: padding + (control.mirrored || !indicator || !indicator.visible ? 0 : indicator.width + spacing)

    // The text of an entry, whatever the model is: a list of strings, of objects, or a model
    // with roles.
    function entryText(model, modelData) {
        if (!control.textRole)
            return modelData !== undefined ? modelData : model.display
        return Array.isArray(control.model) ? modelData[control.textRole] : model[control.textRole]
    }

    delegate: T.ItemDelegate {
        id: entry
        required property var model
        required property int index
        readonly property var modelData: model.modelData

        width: ListView.view.width
        implicitHeight: Theme.controlHeight
        padding: 0
        leftPadding: Theme.controlPadding - 4
        rightPadding: Theme.controlPadding - 4
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled

        contentItem: Text {
            text: control.entryText(entry.model, entry.modelData)
            font: control.font
            color: entry.highlighted ? Theme.highlightedText : Theme.text
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.controlRadius - 1
            color: entry.highlighted ? Theme.highlight : "transparent"
        }
    }

    indicator: Shape {
        x: control.mirrored ? control.padding : control.width - width - control.padding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 10
        height: 6
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: control.enabled ? Theme.textMuted : Theme.textDisabled
            strokeWidth: 1.5
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            startX: 1; startY: 1
            PathLine { x: 5; y: 5 }
            PathLine { x: 9; y: 1 }
        }
    }

    padding: 6
    spacing: 8
    horizontalPadding: Theme.controlPadding - 2

    contentItem: T.TextField {
        text: control.editable ? control.editText : control.displayText
        enabled: control.editable
        autoScroll: control.editable
        readOnly: control.down
        inputMethodHints: control.inputMethodHints
        validator: control.validator
        selectByMouse: control.selectTextByMouse
        font: control.font
        color: control.enabled ? Theme.text : Theme.textDisabled
        selectionColor: Theme.highlight
        selectedTextColor: Theme.highlightedText
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: Theme.controlHeight
        radius: Theme.controlRadius
        color: control.enabled ? Theme.base : Theme.buttonDisabled
        border.width: 1
        border.color: !control.enabled ? Theme.buttonDisabled
                      : control.hovered || control.down ? Theme.light : Theme.mid

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Theme.controlRadius + 2
            visible: control.visualFocus || (control.editable && control.activeFocus)
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
        }
    }

    popup: T.Popup {
        y: control.height + 2
        width: control.width
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, control.Window.height - topMargin - bottomMargin)
        topMargin: 6
        bottomMargin: 6
        padding: 3

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: T.ScrollIndicator {}
        }

        background: Rectangle {
            radius: Theme.controlRadius
            color: Theme.raised
            border.width: 1
            border.color: Theme.light
        }
    }
}
