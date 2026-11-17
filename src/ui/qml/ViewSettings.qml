pragma Singleton

import QtQuick

// Derived rendering values only. Saved choices belong to Preferences.
QtObject {
    // What transparent parts are shown on: "dark" or "light" squares, or "plain" mid gray.
    readonly property color backdropFirst: Preferences.transparency === "light" ? Theme.checksLightFirst
                                           : Preferences.transparency === "plain" ? Theme.transparencyPlain : Theme.checksDarkFirst
    readonly property color backdropSecond: Preferences.transparency === "light" ? Theme.checksLightSecond
                                            : Preferences.transparency === "plain" ? Theme.transparencyPlain : Theme.checksDarkSecond

    // Bits per channel of the window's output, and the step the last pass dithers by: one step
    // of the output, or 0 with IV_NO_DITHER=1 (see display_output.h).
    readonly property int outputBits: DisplayOutput.bits
    readonly property real ditherStep: DisplayOutput.dither ? 1 / (Math.pow(2, outputBits) - 1) : 0

}
