import QtQuick
import QtQuick.Controls
import ImageViewer

// Settings uses the same scrolling and focus handling as the tools, keeping the bar
// visible throughout overflow. A page that fits hides the bar and does not scroll.
AppScrollArea {
    autoHideScrollBar: false
    verticalScrollBar.objectName: "settingsPageScrollBar"
}
