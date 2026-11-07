pragma Singleton

import QtQuick

// The screens connected, by their size in pixels, each size once, as targets for crop and
// resize. Follows screens being added and removed.
QtObject {
    // { width, height, name }, the way each screen stands.
    readonly property var sizes: {
        const list = []
        for (const screen of Qt.application.screens) {
            const width = Math.round(screen.width * screen.devicePixelRatio)
            const height = Math.round(screen.height * screen.devicePixelRatio)
            if (!list.some(s => s.width === width && s.height === height))
                list.push({ width: width, height: height, name: "Screen " + width + " x " + height })
        }
        return list
    }
}
