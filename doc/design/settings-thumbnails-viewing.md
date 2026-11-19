# Settings: Thumbnails and Viewing

Approved next pages, using the accepted Appearance layout and controls. See the
[Thumbnails screen](settings-thumbnails-draft.svg) and
[Viewing screen](settings-viewing-draft.svg). Figures in the cache example are illustrative.

## Thumbnails

* Image bounding-box presets: 164 × 164 (existing default), 160 × 120, 200 × 150,
  240 × 160, 320 × 240 and 400 × 300. Padding and the filename are additional;
  pictures retain their aspect ratio. Keep the current browser layout until a size
  is explicitly chosen. The square default currently means a 180 × 200 cell.
* Apply changes the browser grid and requests appropriately sized images. Preserve
  selection/current image and pane focus, and bring the current item into view after reflow.
  The strip builder keeps its own compact working list; this preference is for browsing.
* Cache limit in MiB, initially 512; show measured bytes and entry count beside it.
  Measurement and clearing remain background work. Display loading, unavailable and
  failure states rather than substituting a misleading zero.
* Clear cache is an immediate action, separately confirmed. Explain that rebuilding
  thumbnails takes time and Cancel cannot restore the cleared cache. Disable duplicate
  requests while clearing and refresh usage after completion.
* Keep cache entries at different edge sizes: no global invalidation on a size change.
  Fit the returned image into both requested dimensions, including portrait images,
  and verify physical pixel requests on high-DPI displays. The current provider takes
  only the longest edge, which needs care for rectangular bounds.

## Viewing

* Transparency: Dark squares, Light squares, Gray. This replaces the View-menu controls.
* Colour management: enabled/disabled.
* Display profile: sRGB, or a chosen ICC/ICM file; show the chosen profile name/path.
  Validate a new file before accepting it, keeping the previous valid choice on error.
  Do not label sRGB as “From the system”: automatic discovery is not implemented.
* Output: 8 or 10 bits per channel, explicitly marked “after restart”; show actual
  negotiated output separately. A 10-bit request is not a promise of 10-bit output.
  An environment override must be visible when it takes precedence over the preference.
* No auto-contrast, histogram, Info panel, sharp-resampling or dithering preferences.
  Their existing controls and behaviour stay where agreed.

## Apply and implementation

Keep one draft across all pages. Apply saves changes across all sections; OK also
closes. Cancel discards unapplied edits. Appearance retains its live runtime preview;
the new pages take effect on Apply. Reset affects only the selected section's draft.
Cache clearing is the explicitly separate immediate operation.

Extend the C++ draft/persistence model for validated preferences, using the existing
Appearance session for palette preview. Keep page QML separate so SettingsDialog stays
navigation, layout and short handlers. SettingsPage configures AppScrollArea, shared
with ToolPanel: a fixed scrollbar gutter, proportional vertical thumb, and focus
scrolling. Settings bars stay visible while content overflows and hide when content
fits; tools fade their bars until hover or movement.
A browser thumbnail-size preference belongs in
Preferences; crop/dialog remembered values still do not. Reuse the existing cache,
profile validation and file chooser rather than adding parallel implementations.
Ensure opening the cache page does not trigger the main window's clear-cache prompt:
ordinary usage measurement and a requested clear must be distinguished.

Both screens use the roles and shared sizes documented in
[the Appearance specification](settings-appearance-draft.md). The mockups add no
colours or application control types. The brush is Tabler's existing MIT-licensed
outline icon, copied from the local set into assets/icons/tabler, and used for the
Appearance section. Viewing uses the existing image icon and Thumbnails the grid icon;
the application's main menus remain text-only. Their former CMS, transparency and
clear-cache entries are replaced by these pages.

Tests should cover persisted dimensions and rejection of invalid values; rectangular
portrait/landscape thumbnails and cache reuse across sizes; grid reflow without losing
selection; switching pages without losing edits; Apply/Cancel/reset; profile validation;
requested versus actual output and restart wording; and confirmed cache clearing with
independent usage measurement. Tests use only generated images and temporary settings/cache.
