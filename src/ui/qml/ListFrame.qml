import QtQuick
import ImageViewer

// What a list stands on: the colour of a field and a thin frame, which turns to the accent
// while the list has the keys. Lists side by side (the places, the folders and the files of
// the file chooser) look alike on it. Content is inset by Theme.paneInset so rows
// cannot cover the rounded frame.
Rectangle {
    // The list has the keys.
    property bool focused: false

    color: Theme.base
    radius: Theme.controlRadius
    border.width: 1
    border.color: focused ? Theme.paneFocusFrame : Theme.paneFrame
}
