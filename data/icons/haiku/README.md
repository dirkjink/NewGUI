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
