# Haiku's icons as SVG

Haiku's vector icons (`data/artwork/icons` of the Haiku repository, commit
`b1d497bb69`), converted from Icon-O-Matic's native files to SVG. NewGUI
draws every icon from SVG (lunasvg, module W7); Haiku's HVIF format is not
ported.

- 708 icons, with `flags/` and `Pulse/`.
- The icons NewGUI shows itself are embedded into the library
  (`src/CMakeLists.txt`); `BIconUtils::GetSystemIcon()` finds the others by
  name in this directory, wherever an application installs it
  (`BIconUtils::SetSystemIconDirectory()`, `NGUI_ICON_DIR`).
- `SHA256SUMS` lists the checksum of each file (`sha256sum -c SHA256SUMS`).

## Names

The file names are those of Haiku's artwork directory. That directory
collects every Haiku vector icon ever made, also drafts and designs no
longer used ("I still want to keep all Haiku vector icons ever made in this
folder", Stephan Aßmus, 2009). So several files often show the same thing.

### Category

The part before the first `_` says what an icon stands for:

| Prefix | Icons for |
|---|---|
| `App_` | applications |
| `File_`, `Folder_` | file types and folders, as Tracker shows them |
| `Device_` | drives, media and hardware |
| `Prefs_` | preference applications |
| `Server_` | system servers |
| `Action_`, `Document_`, `List_` | buttons and menus (back, forward, home, search, stop, save, add) |
| `Alert_` | the alert types of `BAlert` |
| `Overlay_` | small emblems drawn over another icon (a Be or Zeta logo, say) |
| `Color_` | a color palette, see below |
| `Misc_`, `Website_`, `Mail_`, `Notify_`, `Tray_`, … | everything else |

`flags/` holds the flags of the locale preferences: an ISO 3166 country code
and the name, plus a few languages without a country (`BR Breton`,
`CA Catalan`, `EO Esperanto`). `Pulse/` holds the processor makers' logos of
the Pulse application.

### Variants

- **`_2`, `_3`, `_4`:** another design of the same icon, by another artist
  or from another time, not another size. For example, `App_Generic_4` came
  from zuMi in 2009, `Action_GoBack_3` from zuMi's collection in 2012. An
  unnumbered file is the oldest design.
- **`_1`:** a numbered first design beside an unnumbered one. The
  `Action_Go*_1` and `_2` files were `Navigation_Left_1` etc. before 2010.
- **A letter after the number (`_3b`):** a revision of that design;
  `Action_GoHome_3b` has a better perspective than `Action_GoHome_3`.
- **`_Large`:** the same drawing scaled up (by about 1.32) to fill more of
  the canvas, for icons drawn very small. Tracker's navigator buttons use
  `Action_GoBack_3_Large` at 16 pixels.
- **`_Small`:** a part for building other icons; `Misc_Box_Small` is a box
  from the generic application icon.
- **A state:** `App_Beam_2_NewMail` and `App_Beam_2_NoMail` are the mail
  client with and without new mail; `App_Old` marks old (BeOS) applications.
- **`Color_NN_Name`:** 23 color swatches, numbered in palette order. They
  are the suggested colors for Haiku icons, not icons for an application.

A name does not say which variant Haiku ships: Haiku's resource files
contain the icon data itself, not these names. NewGUI picks the variant
where it uses an icon, for example `Alert_Warning` for `BAlert`.

## Conversion

The converter followed Haiku's icon library (`src/libs/icon`), not
Icon-O-Matic's SVG exporter:

| Feature | SVG mapping |
|---|---|
| Paths, shape matrix, non-zero fill, painter's order | native, exact |
| Stroke transformer (last in chain) | native stroke (width, cap, join, miter limit) |
| Contour transformer | computed outline polygon; offset = width / 2 as in AGG's `math_stroke` |
| Stroke followed by further transformers | computed outline polygon |
| Perspective transformer | geometry projected exactly (flattened); its effect on gradients approximated by a least-squares affine fit |
| Linear / radial gradients | native; "smooth" interpolation sampled with intermediate stops |
| Diamond gradients | four triangles with linear ramps (exact) |
| Conic gradients | 2 × 96 angular wedges (approximation) |
| Level of detail (min/max visibility scale) | CSS media queries on the SVG's own width |

lunasvg does not evaluate media queries; NewGUI resolves them for the size
it draws an icon at before handing it over (`svg::ResolveMediaQueries()`),
so small icons show Haiku's simpler level of detail.

## Known differences to Haiku's renderer

- Haiku blends in linear light (gamma 2.2). SVG renderers blend in sRGB, so
  semi-transparent shadows and anti-aliased edges look slightly darker.
  `color-interpolation="linearRGB"` is set as Icon-O-Matic does, but
  lunasvg ignores it.
- Pixel hinting (6 shapes) is not representable in SVG and is ignored.

## License

MIT, as the Haiku project's own work in its repository (`License.md` there;
the icon files carry no header of their own, so the holders are the Haiku
project and the icons' authors):

```
Copyright (c) Haiku, Inc. and the authors of the icons

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
