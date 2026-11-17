import QtQuick
import QtQuick.Controls
import ImageViewer

// A settings page reserves a gutter for its persistent overflow scrollbar. Position it
// against the viewport, so scrolling never moves the bar or covers a field's controls.
ScrollView {
    id: control
    contentWidth: availableWidth
    clip: true
    leftPadding: mirrored && effectiveScrollBarWidth > 0 ? effectiveScrollBarWidth + Theme.panelSpacing : 0
    rightPadding: !mirrored && effectiveScrollBarWidth > 0 ? effectiveScrollBarWidth + Theme.panelSpacing : 0

    ScrollBar.vertical: AppScrollBar {
        objectName: "settingsPageScrollBar"
        parent: control
        x: control.mirrored ? 0 : control.width - width
        y: control.topPadding
        height: control.availableHeight
        interactive: true
        autoHide: false
    }
}
