import QtQuick
import QtQuick.Templates as T
import ImageViewer

// A menu on a raised surface with a light edge. Its lines are this style's MenuItem.
T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    margins: 0
    padding: 4
    overlap: 2

    delegate: MenuItem {}

    contentItem: ListView {
        implicitHeight: contentHeight
        model: control.contentModel
        interactive: Window.window ? contentHeight + control.topPadding + control.bottomPadding > Window.window.height : false
        clip: true
        currentIndex: control.currentIndex
        T.ScrollIndicator.vertical: T.ScrollIndicator {}
    }

    background: Rectangle {
        implicitWidth: 200
        implicitHeight: Theme.controlHeight
        radius: Theme.controlRadius
        color: Theme.raised
        border.width: 1
        border.color: Theme.light
    }
}
