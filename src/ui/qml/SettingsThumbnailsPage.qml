import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

SettingsPage {
    id: page
    required property SettingsDraft draft
    readonly property bool pendingEdits: cacheLimit.pendingEdit
    onVisibleChanged: { if (visible) ThumbnailStore.measure() }

    ThumbnailCacheDialogs { id: cacheDialogs; objectName: "settingsCacheDialogs" }
    ColumnLayout {
        id: form
        width: page.availableWidth
        spacing: Theme.panelSpacing * 2
        Label { text: "Thumbnails"; font.pointSize: Theme.titleSize }
        Label {
            Layout.fillWidth: true
            text: "Image size and storage for the browser."
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.panelSpacing
            Label { text: "Image size"; Layout.preferredWidth: 144 }
            ComboBox {
                objectName: "thumbnailSizeChoice"
                Layout.fillWidth: true
                // Squares, which fit pictures of either orientation alike. A size saved before,
                // not in the list, still shows by displayText.
                model: [
                    {label: "160 × 160 (default)", size: Qt.size(160, 160)},
                    {label: "200 × 200", size: Qt.size(200, 200)},
                    {label: "240 × 240", size: Qt.size(240, 240)},
                    {label: "320 × 320", size: Qt.size(320, 320)},
                    {label: "400 × 400", size: Qt.size(400, 400)}
                ]
                textRole: "label"
                currentIndex: model.findIndex(item => item.size.width === page.draft.thumbnailSize.width && item.size.height === page.draft.thumbnailSize.height)
                displayText: currentIndex >= 0 ? currentText : page.draft.thumbnailSize.width + " × " + page.draft.thumbnailSize.height
                onActivated: index => page.draft.thumbnailSize = model[index].size
                Accessible.name: "Thumbnail image size"
            }
        }
        Label {
            Layout.fillWidth: true
            text: "Pictures fit inside this size; filenames and padding are extra."
            color: Theme.textMuted
            font.pointSize: Theme.smallSize
            wrapMode: Text.WordWrap
        }
        ListFrame {
            Layout.fillWidth: true
            implicitHeight: 112
            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.panelMargin
                spacing: Theme.panelSpacing * 2
                Rectangle {
                    Layout.preferredWidth: 104
                    Layout.preferredHeight: 80
                    radius: Theme.controlRadius
                    color: Theme.thumbnailSelection
                    border.color: Theme.selectionEdge
                    ThemedIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: Theme.panelSpacing
                        width: 56; height: 40
                        name: "view-image"
                        ink: Theme.textMuted
                    }
                    Label { anchors.horizontalCenter: parent.horizontalCenter; anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.panelSpacing; text: "sample.jpg" }
                }
                Label {
                    Layout.fillWidth: true
                    text: "Aspect ratio is preserved.\nLarger sizes also suit high-DPI displays."
                    color: Theme.textMuted
                    wrapMode: Text.WordWrap
                }
            }
        }
        RowLayout {
            spacing: Theme.panelSpacing
            Label { text: "Cache limit"; Layout.preferredWidth: 144 }
            DecimalSpinBox {
                id: cacheLimit
                objectName: "thumbnailCacheLimit"
                Layout.preferredWidth: 128
                decimals: 0
                realFrom: 1; realTo: 65536; realStepSize: 64
                live: false
                Binding on realValue { value: page.draft.thumbnailCacheMiB }
                onRealValueModified: value => page.draft.thumbnailCacheMiB = value
                Accessible.name: "Thumbnail cache limit in MiB"
            }
            Label { text: "MiB" }
        }
        Label {
            objectName: "thumbnailUsage"
            Layout.fillWidth: true
            text: ThumbnailStore.busy ? "Updating cache usage…"
                  : !ThumbnailStore.measuredOnce ? "Cache usage not measured"
                  : ThumbnailStore.available ? "Used: " + ThumbnailStore.usageText : "Thumbnail cache unavailable"
            color: ThumbnailStore.measuredOnce && !ThumbnailStore.available ? Theme.warning : Theme.textMuted
            wrapMode: Text.WordWrap
        }
        Button {
            objectName: "clearThumbnailCacheButton"
            text: "Clear cache…"
            enabled: !ThumbnailStore.busy && ThumbnailStore.measuredOnce && ThumbnailStore.available
            onClicked: cacheDialogs.requestClear()
        }
        Label {
            Layout.fillWidth: true
            text: "Clearing asks first; thumbnails will be rebuilt as needed.\nCancel cannot restore a cleared cache."
            color: Theme.textMuted
            font.pointSize: Theme.smallSize
            wrapMode: Text.WordWrap
        }
    }
}
