pragma Singleton
import QtQuick

// Every colour of the interface, and the sizes that pages share. Main.qml gives all of Fusion's palette roles from here, so none
// is left to the system theme, which would mix a light system palette with these dark panels.
// Most colours are worked out from a hue and two amounts of colour (ThemePalette,
// core/palette.h); those that must not follow a theme are written here.
QtObject {
    readonly property var roles: ThemePalette.colours

    // Fusion's palette roles.
    readonly property color window: roles.window
    readonly property color base: roles.base          // lists and text fields
    readonly property color alternateBase: roles.alternateBase
    readonly property color button: roles.button
    readonly property color text: roles.text
    readonly property color placeholderText: roles.placeholderText
    readonly property color highlight: roles.highlight     // selection
    readonly property color highlightedText: roles.highlightedText
    readonly property color light: roles.light         // the bevels Fusion draws around controls
    readonly property color midlight: roles.midlight
    readonly property color mid: roles.mid
    readonly property color dark: roles.dark
    readonly property color shadow: roles.shadow
    readonly property color link: roles.link
    readonly property color linkVisited: roles.linkVisited
    readonly property color toolTip: roles.toolTip

    // The disabled group.
    readonly property color textDisabled: roles.textDisabled
    readonly property color buttonDisabled: roles.buttonDisabled
    readonly property color highlightDisabled: roles.highlightDisabled

    // The rest, set in QML.
    readonly property color accent: roles.accent   // focus, scroll bar handles, marks
    readonly property color textMuted: roles.textMuted    // captions and secondary values
    readonly property color raised: roles.raised       // a dialog over the window
    readonly property color header: roles.header       // the header row of a list
    readonly property color surfaceTop: roles.surfaceTop      // a card or group, its gradient
    readonly property color surfaceBottom: roles.surfaceBottom
    readonly property color controlTop: roles.controlTop      // a button, its gradient
    readonly property color controlBottom: roles.controlBottom
    readonly property color defaultButtonTop: roles.defaultButtonTop  // the button that does the job
    readonly property color defaultButtonBottom: roles.defaultButtonBottom
    readonly property color defaultButtonEdge: roles.defaultButtonEdge
    readonly property color hover: withAlpha(text, 0.12)
    readonly property color frame: withAlpha(text, 0.25)
    readonly property color warning: "#e0a050"
    readonly property color danger: "#e06c60"
    readonly property color warningFill: "#d9992b"  // behind a white glyph
    readonly property color dangerFill: "#c9443f"
    readonly property color caret: withAlpha(text, 0.55)  // the small triangles that unfold
    readonly property color scrim: "#59000000"      // over the window, under a dialog
    readonly property color dialogShadow: "#0d000000"  // one of the layers that make it
    readonly property color folder: "#e2b955"
    readonly property color folderShade: "#c9a043"

    // Over the image. The surround stays neutral gray, as a tint would shift how colours look,
    // and what is drawn on pictures stays light whatever the palette.
    readonly property color imageBackground: "#202020"
    // What transparent parts of a picture are shown on, neutral like the surround: squares of
    // two grays, dark or light, or one mid gray.
    readonly property color checksDarkFirst: "#303030"
    readonly property color checksDarkSecond: "#484848"
    readonly property color checksLightFirst: "#c8c8c8"
    readonly property color checksLightSecond: "#f0f0f0"
    readonly property color transparencyPlain: "#808080"
    readonly property color panel: roles.panel      // tool panels and the info panel
    readonly property color bubble: roles.bubble     // short messages
    readonly property color badge: roles.badge
    readonly property color onImage: "#e0e0e0"
    readonly property color onImageBright: "#f0f0f0"
    readonly property color onImageShade: "#404040"
    readonly property color swatchFrame: "#606060"  // neutral, so it does not tint the swatch
    readonly property color rampHatch: "#73000000"  // over colours sRGB cannot show, on a slider
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
    readonly property int controlHeight: 28 // buttons, fields, combo boxes: a multiple of 4, whole at 125%
    readonly property int controlRadius: 4  // their corners
    readonly property int controlPadding: 12  // between a control's edge and its text
    readonly property int menuMarkWidth: 18  // the column of a menu for a check box or an icon
    readonly property int toolIconSize: 24  // an icon on the tool bar, as drawn in assets/icons

    function withAlpha(color, alpha) { return Qt.rgba(color.r, color.g, color.b, alpha) }
}
