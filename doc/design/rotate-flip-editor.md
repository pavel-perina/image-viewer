# Regular rotation and flipping

Proposed editor addition, separate from the existing JPEG file operations.

## Behaviour

* Add Rotate 90° clockwise, Rotate 90° counterclockwise, Rotate 180°, Flip
  horizontally and Flip vertically for every image the editor can edit.
* These are normal document edits: show the result immediately, mark it modified,
  support Undo/Redo, and use the existing Save/Save As and unsaved-changes flow.
* Apply to the upright image after its preceding edits. Rotation after crop, drawing
  or framing transforms that edited result; subsequent tools use the new coordinates.
* Quarter turns swap dimensions. Flips and 180° rotation keep dimensions. Move pixel
  samples exactly, preserving depth, alpha and the colour profile, without resampling.
* Saving to JPEG uses its normal encoder. The existing Lossless JPEG commands retain
  their explicit role as file operations on JPEG coefficients.

## Interface

* Add an Edit → Rotate and Flip submenu containing the five commands.
* Add clockwise and counterclockwise buttons beside the other editing tools in the
  viewer toolbar, using the local Tabler icon set. Flips and 180° are in the submenu.
* Starting an operation from the browser opens its single selected picture in the
  viewer, as Crop/Resize already do. Enable the commands when the editor can accept
  them and an interactive tool is not active.
* Ctrl+R retains its existing Resize meaning. New rotation shortcuts can be chosen
  alongside the wider shortcut scheme.

## Implementation

The exact-pixel operation already exists as core `applyOrientation`, used to make
EXIF-oriented source pictures upright. Reuse it through a new document edit rather
than implementing another rotation routine. Extend Document's size/render visitors,
add thin ImageView entry points, and let QML actions invoke them. Rendering and pixel
work remain on the existing document workers. Check cancellation between rows during
orientation copies so large pictures can stop promptly.

Tests cover exact pixels and dimensions for all five operations, preserved alpha/depth,
EXIF-upright inputs, edit composition and Undo/Redo, and a saved/reopened result. UI
tests cover the actions from both browser and viewer, edit gating, and toolbar icons.

## Batch operations

The edit holds only an orientation, with no reference to the picture it was made on, so it can
be part of the batch operations planned later: the same turn or flip applied to every chosen
picture, alone or with other edits such as resize. For JPEGs, a batch made only of turns, flips
and crops could use the lossless JPEG tools instead of encoding again.

Browser sorting and hidden-folder controls remain in the main browsing interface;
they do not need a Settings page.
