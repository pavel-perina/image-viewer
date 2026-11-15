import QtQuick
import QtQuick.Controls

// A narrow button with a padlock, closed while checked, that keeps the setting of the button
// beside it.
IconButton {
    implicitWidth: Theme.toolIconSize + 4
    leftPadding: 2
    rightPadding: 2
    focusPolicy: Qt.NoFocus
    iconName: checked ? "locked" : "unlocked"
}
