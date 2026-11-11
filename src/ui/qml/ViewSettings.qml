pragma Singleton

import QtCore
import QtQuick

// How images are shown in every view, kept between sessions.
QtObject {
    id: root

    // Auto Contrast for every image with more than 8 bits, not only for those that need it.
    property bool keepAutoContrast: false

    // Bits per channel of the window's output, and the step the last pass dithers by.
    property int outputBits: 8
    readonly property real ditherStep: 1 / (Math.pow(2, outputBits) - 1)

    readonly property Settings store: Settings {
        category: "View"
        property alias keepAutoContrast: root.keepAutoContrast
    }
}
