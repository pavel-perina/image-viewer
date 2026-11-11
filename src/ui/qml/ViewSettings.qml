pragma Singleton

import QtCore
import QtQuick

// How images are shown in every view, kept between sessions.
QtObject {
    id: root

    // Auto Contrast for every image with more than 8 bits, not only for those that need it.
    property bool keepAutoContrast: false

    readonly property Settings store: Settings {
        category: "View"
        property alias keepAutoContrast: root.keepAutoContrast
    }
}
