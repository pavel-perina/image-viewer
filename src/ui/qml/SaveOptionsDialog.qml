pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The options of the format chosen in Save As, remembered per format, and what to do with the
// metadata. Accepting starts the save.
AppDialog {
    id: dialog
    objectName: "saveOptionsDialog"

    required property BrowseContext context
    property string source
    property int page: 0
    property var edited  // ImageView.editedImage, saved instead of the page of the file if set
    property string target
    property string format  // as BrowseContext.saveFormat gives it

    readonly property bool lossyChoice: format === "jpeg" || format === "avif"
                                        || ((format === "webp" || format === "jxl") && !losslessCheck.checked)

    title: "Save As " + ({jpeg: "JPEG", png: "PNG", tiff: "TIFF", webp: "WebP", avif: "AVIF", jxl: "JPEG XL"}[format] ?? "")
    width: Math.min(460, parent.width - 32)
    standardButtons: Dialog.Save | Dialog.Cancel

    // The values per format, as the dialog last left them.
    Settings {
        id: settings
        category: "SaveAs"
        property int jpegQuality: 90
        property string jpegChroma: "4:2:0"
        property bool jpegProgressive: true
        property int pngLevel: 6
        property string tiffCompression: "deflate"
        property bool webpLossless: false
        property int webpQuality: 85
        property int webpEffort: 4
        property int avifQuality: 70
        property int avifSpeed: 6
        property string avifChroma: "4:2:0"
        property bool jxlLossless: false
        property int jxlQuality: 90
        property int jxlEffort: 7
        property bool keepMetadata: true
        property bool removeLocation: false
    }

    function options() {
        const common = {keepMetadata: metadataCheck.checked, removeLocation: locationCheck.checked}
        switch (format) {
        case "jpeg": return Object.assign(common, {quality: quality.value, chroma: chroma.currentText, progressive: progressiveCheck.checked})
        case "png": return Object.assign(common, {compressionLevel: pngLevel.value})
        case "tiff": return Object.assign(common, {compression: ["none", "lzw", "deflate"][tiffCompression.currentIndex]})
        case "webp": return Object.assign(common, {lossless: losslessCheck.checked, quality: quality.value, effort: effort.value})
        case "avif": return Object.assign(common, {quality: quality.value, speed: effort.value, chroma: chroma.currentText})
        case "jxl": return Object.assign(common, {lossless: losslessCheck.checked, quality: quality.value, effort: effort.value})
        }
        return common
    }

    // The options last chosen for a format, for saving without asking.
    function storedOptions(format) {
        const common = {keepMetadata: settings.keepMetadata, removeLocation: settings.removeLocation}
        switch (format) {
        case "jpeg": return Object.assign(common, {quality: settings.jpegQuality, chroma: settings.jpegChroma, progressive: settings.jpegProgressive})
        case "png": return Object.assign(common, {compressionLevel: settings.pngLevel})
        case "tiff": return Object.assign(common, {compression: settings.tiffCompression})
        case "webp": return Object.assign(common, {lossless: settings.webpLossless, quality: settings.webpQuality, effort: settings.webpEffort})
        case "avif": return Object.assign(common, {quality: settings.avifQuality, speed: settings.avifSpeed, chroma: settings.avifChroma})
        case "jxl": return Object.assign(common, {lossless: settings.jxlLossless, quality: settings.jxlQuality, effort: settings.jxlEffort})
        }
        return common
    }

    function remember() {
        settings.keepMetadata = metadataCheck.checked
        settings.removeLocation = locationCheck.checked
        switch (format) {
        case "jpeg":
            settings.jpegQuality = quality.value
            settings.jpegChroma = chroma.currentText
            settings.jpegProgressive = progressiveCheck.checked
            break
        case "png":
            settings.pngLevel = pngLevel.value
            break
        case "tiff":
            settings.tiffCompression = ["none", "lzw", "deflate"][tiffCompression.currentIndex]
            break
        case "webp":
            settings.webpLossless = losslessCheck.checked
            settings.webpQuality = quality.value
            settings.webpEffort = effort.value
            break
        case "avif":
            settings.avifQuality = quality.value
            settings.avifSpeed = effort.value
            settings.avifChroma = chroma.currentText
            break
        case "jxl":
            settings.jxlLossless = losslessCheck.checked
            settings.jxlQuality = quality.value
            settings.jxlEffort = effort.value
            break
        }
    }

    // Fills the controls for the format, as they were left for it.
    function load() {
        const chromas = ["4:4:4", "4:2:2", "4:2:0"]
        metadataCheck.checked = settings.keepMetadata
        locationCheck.checked = settings.removeLocation
        losslessCheck.checked = format === "webp" ? settings.webpLossless : settings.jxlLossless
        progressiveCheck.checked = settings.jpegProgressive
        pngLevel.value = settings.pngLevel
        tiffCompression.currentIndex = Math.max(0, ["none", "lzw", "deflate"].indexOf(settings.tiffCompression))
        switch (format) {
        case "jpeg":
            quality.value = settings.jpegQuality
            chroma.currentIndex = Math.max(0, chromas.indexOf(settings.jpegChroma))
            break
        case "webp":
            quality.value = settings.webpQuality
            effort.from = 0; effort.to = 6
            effort.value = settings.webpEffort
            break
        case "avif":
            quality.value = settings.avifQuality
            chroma.currentIndex = Math.max(0, chromas.indexOf(settings.avifChroma))
            effort.from = 0; effort.to = 10
            effort.value = settings.avifSpeed
            break
        case "jxl":
            quality.value = settings.jxlQuality
            effort.from = 1; effort.to = 9
            effort.value = settings.jxlEffort
            break
        }
    }

    onAboutToShow: load()
    onAccepted: {
        remember()
        context.saveAs(source, page, target, options(), edited)
        edited = undefined  // the edited picture, at full size, is not kept here
    }

    component ValueSlider: RowLayout {
        property alias from: slider.from
        property alias to: slider.to
        property alias value: slider.value
        Layout.fillWidth: true
        Slider {
            id: slider
            Layout.fillWidth: true
            stepSize: 1
            snapMode: Slider.SnapAlways
        }
        Label {
            Layout.preferredWidth: 28
            horizontalAlignment: Text.AlignRight
            text: slider.value
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 10

        Label {
            Layout.fillWidth: true
            text: dialog.target.substring(dialog.target.lastIndexOf("/") + 1)
            font.weight: Font.DemiBold
            elide: Text.ElideMiddle
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12

            CheckBox {
                id: losslessCheck
                objectName: "losslessCheck"
                Layout.columnSpan: 2
                visible: dialog.format === "webp" || dialog.format === "jxl"
                text: "Lossless"
            }

            Label { text: "Quality"; visible: quality.visible }
            ValueSlider {
                id: quality
                objectName: "qualitySlider"
                visible: dialog.lossyChoice
                from: 1
                to: 100
            }

            Label { text: "Chroma"; visible: chroma.visible }
            ComboBox {
                id: chroma
                visible: dialog.format === "jpeg" || dialog.format === "avif"
                Layout.fillWidth: true
                model: ["4:4:4", "4:2:2", "4:2:0"]
            }

            Label { text: "Compression"; visible: pngLevel.visible }
            ValueSlider {
                id: pngLevel
                visible: dialog.format === "png"
                from: 0
                to: 9
            }

            Label { text: "Compression"; visible: tiffCompression.visible }
            ComboBox {
                id: tiffCompression
                visible: dialog.format === "tiff"
                Layout.fillWidth: true
                model: ["None", "LZW", "Deflate"]
            }

            Label {
                text: dialog.format === "avif" ? "Speed" : "Effort"
                visible: effort.visible
            }
            ValueSlider {
                id: effort
                visible: dialog.format === "webp" || dialog.format === "avif" || dialog.format === "jxl"
            }

            CheckBox {
                id: progressiveCheck
                Layout.columnSpan: 2
                visible: dialog.format === "jpeg"
                text: "Progressive"
            }
        }

        Label {
            Layout.fillWidth: true
            visible: text !== ""
            wrapMode: Text.Wrap
            opacity: 0.8
            text: {
                if (dialog.format === "avif")
                    return "Higher speed is quicker and gives a larger file."
                if (dialog.format === "webp" || dialog.format === "jxl")
                    return "More effort is slower and gives a smaller file."
                if (dialog.format === "png")
                    return "Compression changes only the time and the size, never the picture."
                return ""
            }
        }

        Label {
            objectName: "recompressNote"
            Layout.fillWidth: true
            visible: dialog.format === "jpeg" && dialog.context.allJpeg([dialog.source])
            wrapMode: Text.Wrap
            text: "Saving a JPEG as JPEG compresses it again and loses a little quality each time."
                  + (dialog.edited ? "" : " To rotate or flip without loss, use Lossless JPEG in the Edit menu.")
        }

        ColumnLayout {
            spacing: 0
            CheckBox {
                id: metadataCheck
                objectName: "keepMetadataCheck"
                text: "Keep camera data (EXIF and XMP)"
            }
            CheckBox {
                id: locationCheck
                objectName: "removeLocationCheck"
                Layout.leftMargin: 24
                enabled: metadataCheck.checked
                text: "Remove the location"
            }
        }
    }
}
