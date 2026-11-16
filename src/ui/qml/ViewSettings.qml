pragma Singleton

import QtCore
import QtQuick

// How images and folders are shown in every view, kept between sessions.
QtObject {
    id: root

    // Auto Contrast for every image with more than 8 bits, not only for those that need it.
    property bool keepAutoContrast: false

    // Hidden folders in the tree, the lists and the folder counts, in the browser and the file
    // chooser alike. In the View menu until there is a settings dialog.
    property bool showHiddenFolders: false

    // What transparent parts are shown on: "dark" or "light" squares, or "plain" mid gray.
    property string transparency: "dark"
    readonly property color backdropFirst: transparency === "light" ? Theme.checksLightFirst
                                           : transparency === "plain" ? Theme.transparencyPlain : Theme.checksDarkFirst
    readonly property color backdropSecond: transparency === "light" ? Theme.checksLightSecond
                                            : transparency === "plain" ? Theme.transparencyPlain : Theme.checksDarkSecond

    // Bits per channel of the window's output, and the step the last pass dithers by: one step
    // of the output, or 0 with IV_NO_DITHER=1 (see display_output.h).
    readonly property int outputBits: DisplayOutput.bits
    readonly property real ditherStep: DisplayOutput.dither ? 1 / (Math.pow(2, outputBits) - 1) : 0

    readonly property Settings store: Settings {
        category: "View"
        property alias keepAutoContrast: root.keepAutoContrast
        property alias showHiddenFolders: root.showHiddenFolders
        property alias transparency: root.transparency
    }
}
