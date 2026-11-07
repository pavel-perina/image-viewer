import QtQuick
import QtQuick.Controls
import ImageViewer

// The lossless JPEG tools, for the menu bar and the file menu alike.
AppMenu {
    required property FileActions actions

    title: "Lossless &JPEG"

    AppMenuItem { action: actions.rotateClockwiseAction }
    AppMenuItem { action: actions.rotateCounterclockwiseAction }
    AppMenuItem { action: actions.rotate180Action }
    AppMenuItem { action: actions.flipHorizontalAction }
    AppMenuItem { action: actions.flipVerticalAction }
    AppMenuItem { action: actions.applyOrientationAction }
    MenuSeparator {}
    AppMenuItem { action: actions.trimEdgesAction }
    AppMenuItem { action: actions.askBeforeEditingAction }
    MenuSeparator { visible: actions.context.canConvertToJxl }
    AppMenuItem {
        action: actions.convertToJxlAction
        visible: actions.context.canConvertToJxl
        height: visible ? implicitHeight : 0
    }
}
