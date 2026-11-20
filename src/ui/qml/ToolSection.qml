import QtQuick
import QtQuick.Layouts
import ImageViewer

// A section of a tool's panel: a PaneHeader on a band that collapses it, then its controls in a
// column. Where the tool runs stages in order, sections are stacked in that order. The header
// marks the section in use, has Reset for it alone, and when switchable a box that leaves it out
// while keeping its values; its controls are then disabled, still showing them. A section that
// does not apply to what is open (unavailable) keeps a caption that folds, says why, and has its
// box and controls disabled. The panel keeps the values and whether it is collapsed.
ColumnLayout {
    id: section

    property alias title: header.text
    property alias collapsed: header.collapsed
    property alias inUse: header.inUse
    property alias switchable: header.switchable
    property alias on: header.on
    property alias switchTip: header.switchTip
    property alias resettable: header.resettable
    property bool available: true
    property string unavailableText
    signal collapseToggled()
    signal switched(bool on)
    signal resetRequested()

    default property alias content: body.data

    Layout.fillWidth: true
    // A little room above, so that a section's last control does not run into the next caption.
    Layout.topMargin: 4
    spacing: Theme.panelSpacing

    PaneHeader {
        id: header
        objectName: section.objectName + "Header"
        Layout.fillWidth: true
        collapsible: true
        resettable: true
        banded: true
        switchEnabled: section.available
        detail: section.available ? "" : section.unavailableText
        focused: true
        onToggled: section.collapseToggled()
        onSwitched: (on) => section.switched(on)
        onResetRequested: section.resetRequested()
    }
    ColumnLayout {
        id: body
        visible: !header.collapsed
        enabled: header.on && section.available
        Layout.fillWidth: true
        Layout.leftMargin: 4
        spacing: Theme.panelSpacing
    }
}
