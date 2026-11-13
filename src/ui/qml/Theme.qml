pragma Singleton
import QtQuick

// Every colour of the interface, and the sizes that pages share. Main.qml gives all of Fusion's palette roles from here, so none
// is left to the system theme, which would mix a light system palette with these dark panels.
QtObject {
    // Fusion's palette roles.
    readonly property color window: "#1e2329"
    readonly property color base: "#161a1f"          // lists and text fields
    readonly property color alternateBase: "#20262d"
    readonly property color button: "#2a3139"
    readonly property color text: "#d7dde4"
    readonly property color placeholderText: "#7d8893"
    readonly property color highlight: "#3d7ab8"     // selection
    readonly property color highlightedText: "#ffffff"
    readonly property color light: "#3c4550"         // the bevels Fusion draws around controls
    readonly property color midlight: "#333b44"
    readonly property color mid: "#3a434d"
    readonly property color dark: "#12161a"
    readonly property color shadow: "#0a0c0f"
    readonly property color link: "#8ab4f8"
    readonly property color linkVisited: "#b49af8"
    readonly property color toolTip: "#2a3139"

    // The disabled group.
    readonly property color textDisabled: "#6b747e"
    readonly property color buttonDisabled: "#242a31"
    readonly property color highlightDisabled: "#3a4048"

    // The rest, set in QML.
    readonly property color accent: highlight       // focus, scroll bar handles, marks
    readonly property color textMuted: "#9aa4ae"    // captions and secondary values
    readonly property color raised: "#2c333b"       // a dialog over the window
    readonly property color header: "#232930"       // the header row of a list
    readonly property color hover: withAlpha(text, 0.12)
    readonly property color frame: withAlpha(text, 0.25)
    readonly property color warning: "#e0a050"
    readonly property color danger: "#e06c60"
    readonly property color warningFill: "#d9992b"  // behind a white glyph
    readonly property color dangerFill: "#c9443f"
    readonly property color onFill: "#ffffff"       // a glyph on warningFill and the like
    readonly property color caret: withAlpha(text, 0.55)  // the small triangles that unfold
    readonly property color scrim: "#59000000"      // over the window, under a dialog
    readonly property color dialogShadow: "#0d000000"  // one of the layers that make it
    readonly property color folder: "#e2b955"
    readonly property color folderShade: "#c9a043"

    // Over the image. The surround stays neutral gray, as a tint would shift how colours look,
    // and what is drawn on pictures stays light whatever the palette.
    readonly property color imageBackground: "#202020"
    readonly property color panel: "#e6181c21"      // tool panels and the info panel
    readonly property color bubble: "#d0181c21"     // short messages
    readonly property color badge: "#a0181c21"
    readonly property color onImage: "#e0e0e0"
    readonly property color onImageBright: "#f0f0f0"
    readonly property color onImageShade: "#404040"
    readonly property color swatchFrame: "#606060"  // neutral, so it does not tint the swatch
    readonly property color scrollHandle: "#a6ffffff"      // the bars over the image
    readonly property color scrollHandleEdge: "#99000000"

    // What the tools draw on the image: see OverlayStyle.
    readonly property color toolLine: "#ffffff"
    readonly property color toolOutline: "#c8000000"  // under and around lines
    readonly property color toolDim: "#96000000"      // over what a tool leaves out
    readonly property color toolFaint: "#46ffffff"    // a grid to judge by
    readonly property color guideVertical: "#4fc3f7"
    readonly property color guideHorizontal: "#ffb74d"
    // The colours offered for annotations. These are drawn into pictures, not part of the look.
    // They are the nine of ColorBrewer's qualitative scheme Set1 (colorbrewer2.org, by Cynthia
    // Brewer and Mark Harrower), in its order, then white and black.
    readonly property list<color> annotationColours: ["#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00",
                                                      "#ffff33", "#a65628", "#f781bf", "#999999",
                                                      "#ffffff", "#000000"]

    // Sizes that more than one page uses. Text follows the size the system asks for.
    readonly property real titleSize: Qt.application.font.pointSize + 1  // the heading of a panel
    readonly property real smallSize: Qt.application.font.pointSize - 1  // hints and captions
    readonly property string monospace: "monospace"                      // values read in columns
    readonly property int panelWidth: 280   // the tool panels beside the image
    readonly property int panelMargin: 12   // between a panel's edge and what is in it
    readonly property int panelSpacing: 8   // between a panel's rows

    function withAlpha(color, alpha) { return Qt.rgba(color.r, color.g, color.b, alpha) }
}
