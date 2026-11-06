pragma Singleton

import QtQuick

// Colours that do not come from the system palette. The final look will add more here, and
// derive others from these.
QtObject {
    readonly property color accent: "#5a93aa"

    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }
}
