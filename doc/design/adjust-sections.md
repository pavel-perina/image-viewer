# Adjust in sections, and white balance

Three steps, each usable on its own: the panel in collapsible sections with nothing new, then
white balance as the first section, then a strength and an export for the colour table. Steps 1
and 2 are built, as described in `decisions.md` under Colour adjustment, and the strength of
step 3; its export is proposed.
Where the build differs from what follows:

* The correction scales the picture's R, G and B, normalised to keep a neutral's brightness,
  instead of Bradford adaptation, which far from 6500 K turned ordinary colours negative.
* Profile matrices are referred back to D65 from the D50 that ICC keeps them at.
* Colours white balance pushes out of range are fitted towards the gray of their luminance in
  the picture's RGB, not in Oklab, which assumes sRGB.
* The patch is outlined once picked and while dragging, not under the hovering mouse.
* The colour table's Clear is the section's Reset.
* In use is shown by the section's reset arrow being enabled, not by a mark in the accent.

## 1. The panel in sections

Adjust already runs its stages in a fixed order, which the panel shows with muted captions:
Levels, then Oklab, then the colour table. This step makes those captions into sections and
changes no behaviour.

* Each section has a `PaneHeader` that collapses it. The header shows that the section is in
  use, in the accent colour, also when collapsed, and has a reset for that section alone. The
  panel's own Reset still resets everything.
* Each header also has a switch that bypasses its section while keeping its values, to see
  whether that part helps. A bypassed section is left out of the preview and of what Apply
  applies. Unlike reset, it loses nothing.
* Sections are stacked top to bottom in the order they apply, so reading order is processing
  order. No tabs: they would hide the order.
* Which sections are collapsed is remembered, as tool state, not a preference.
* Each section starts at the identity, so a section left alone changes nothing.
* Sections, in order: White balance (step 2), Levels, Oklab (brightness, contrast, midtones,
  saturation, fitting strong colours by chroma), Look (the colour table; step 3 adds to it).
* Adjust stays a tool for the colour of each pixel, applied as one `ColorEdit`, so it keeps one
  preview, one before and after, and one Apply. Geometry and changes that look at neighbouring
  pixels stay tools of their own.

## 2. White balance

Corrects the colour of the light a picture was taken in, or changes it on purpose. For a picture
already rendered (JPEG, PNG, TIFF), not raw sensor data.

### What the controls mean

A rendered picture has already been white balanced by the camera or whoever made it, and the
light it was taken in cannot be recovered from its pixels. So the controls do not read or
restore the original white balance: they say what white the picture is to be taken as having,
relative to how it is rendered, and correct that to neutral.

* **Temperature**, in kelvins: the assumed colour of the light. 6500 K means the picture is taken
  as neutral already: no change. Setting 3200 K says it is to be read as lit by tungsten, so it
  becomes bluer to compensate; 9000 K makes it warmer. The direction is the one cameras and raw
  editors use for the number, so it reads as people expect.
* **Tint**, from -100 (green) to +100 (magenta): how far the light was off the line of
  temperatures, towards green or magenta, as fluorescent and LED light often is. 0 is no change.
  As in other programs, the sign says which way the picture moves: +100 makes it magenta,
  which corrects green light.
* **Pick gray**: click something that should be neutral, or drag a rectangle over it, and both
  controls are set so that it becomes neutral.
  * A click averages a square patch centred on it, of a size chosen beside the button: 1, 5, 11
    or 21 pixels across, 5 by default. One pixel is rarely enough: noise and JPEG blocks shift
    its colour. A dragged rectangle averages all of it, for a gray card, a wall or a cloud.
  * While picking, the patch is outlined on the picture, by an overlay derived from
    `ImageOverlay` like the probe's; the probe itself reads one pixel and stays as it is.
  * The colour is averaged in linear light, from the picture as it enters Adjust: before white
    balance, levels and the colour table, so that neither the current settings nor a look sway
    it, and picking the same place again gives the same values.
  * Patches that are mostly transparent, nearly black or clipped are refused, with a message
    saying why, as their colour says nothing reliable about the light.
* **Auto**, later: a suggestion that sets the controls, never applied unasked. Simple estimates
  all fail on some scenes (gray world on a forest, the brightest pixels on a coloured lamp); the
  one to try first is a power mean ("shades of gray") over the pixels that are already nearly
  neutral in Oklab, as it fails least. Learnt estimates are out of scope.
* Off for gray pictures, which have no colour to correct.

The two numbers together describe one white point: a temperature says where on the line of
temperatures it lies, and the tint how far off that line. That is why picking one gray sets both.

### Kelvins and mireds

A mired is a million divided by the temperature in kelvins: 6500 K is 154 mireds, 3200 K is 313.
Both are absolute; the difference is how evenly they step. The eye sees about the same change for
each mired anywhere on the scale, but not for each kelvin: from 3000 K to 3500 K is a large change,
from 12000 K to 12500 K a barely visible one.

So the control shows kelvins, and its slider moves evenly in mireds: the slider's position is
linear in mireds, and the kelvins shown are a million divided by that, so they are not linear in
the position. Dragging it by the same distance changes the colour by about the same amount
everywhere, and the warm end, where most corrections are, is not squeezed into a few pixels.
Kelvins grow to the right, as in other programs: the left end is 2000 K (500 mireds), the right
15000 K (67 mireds), and 6500 K sits about a fifth of the way from the right. The number can also
be typed, in kelvins. The range is 2000 K to 15000 K, where the
approximation below holds; beyond it the colour changes little anyway.

### How it is computed

* The temperature and tint give a source white; the target is D65, the white of sRGB. The source
  is placed relative to D65, so that 6500 K and tint 0 give exactly D65 and the edit is exactly
  the identity: D65 lies slightly off the line of temperatures, so a plain point on the line
  would not. Points on the line come from a published approximation of the Planckian locus
  (Krystek), with tint as a distance perpendicular to it in CIE 1960 uv; tint 100 is 0.02 there.
* The correction was to be chromatic adaptation by the Bradford matrix; it scales R, G and B
  instead (see above and `decisions.md`). Either way it is one 3 by 3 matrix, applied in linear
  light.
* It is the first stage of `ColorEdit`, before the curves, so tone is set on a picture with the
  right white. In the preview it is one `mat3` in the shader, right after decoding to linear; on
  the CPU, one matrix per pixel in the existing pass. 16-bit and float pictures go through it
  with no 8-bit step in between. At no change the stage is skipped, so the picture is untouched.
* The working space is the picture's own RGB, decoded to linear. Its RGB to XYZ matrix comes
  from the picture's profile when that is a matrix-shaper profile, as most are, and from sRGB
  otherwise, which is also what a picture without a profile is taken as. A wide-gamut picture
  is so corrected in its own primaries, not as if it were sRGB. The Oklab stages still assume
  sRGB; making them follow the profile too is a separate change.
* Colours pushed beyond what an integer format holds are fitted by chroma, as the Oklab stages
  fit theirs, when that option is on; floats keep them.
* Pick gray inverts the construction: from the picked colour in linear light to its white point,
  then the temperature and tint that place the source white there, found numerically. It sets
  absolute values, not a change from the current ones.

### Tests

* 6500 K and tint 0 change nothing, to the last bit, as the stage is skipped.
* A gray tinted by a known adaptation, from a known temperature and tint, comes back neutral when
  those are set, and Pick gray on it finds them.
* The correction from a white, followed by its inverse, gives back float samples
  that were not clipped, within float precision.
* Pick gray finds the same values when clicked twice, with white balance and a colour table
  already set, and refuses transparent, black and clipped patches.
* A patch averages what it covers: on noise around a gray, a 5-pixel patch lands nearer the
  gray than single pixels do, and a rectangle gives its mean.
* A picture with a wide-gamut matrix profile is corrected in its own primaries: a gray in it
  comes back neutral.
* The preview matches what applying gives, as the existing colour preview tests check.
* A gray picture is left alone.

## 3. Colour table: strength and export

* **Strength**, from 0 to 100 %: the table's result mixed with the colour before it, in the space
  the table works in, the values as stored, as other programs mix a look. This is exact, as it
  mixes two results for the same colour, and is how a look is usually toned down.
* **Export as .cube**: the whole Adjust edit, white balance included, written as a 33-point
  table, so that a look made here can be used in other programs. The table's points go through
  the edit as floats, so no clipping of an integer format is baked in; the table still holds
  only what its range and points can, and is interpolated between them.
* A .cube carries the colour transform only, not a profile: the exported table expects colours
  in the RGB of the picture it was made on, encoded as stored, and says so in its TITLE.

## Batch

Like the other edits, a `ColorEdit` holds values and no reference to the picture, so the same
adjustment can be applied to many pictures by the batch operations planned later. Pick gray is
the exception: it reads one picture, so in a batch the values it found are what is repeated.
