# Overlay port: lunasvg

The `lunasvg` port of the vcpkg baseline (`builtin-baseline` in
`vcpkg.json`; lunasvg 3.5.0), unchanged except for the patches below.
`vcpkg-configuration.json` puts `ports/` before the baseline, so every
preset, the CI and the fuzz workflow build lunasvg from this port. The port
version is 1, so the build can tell it apart from the baseline's.

`portfile.cmake` is the baseline's, plus the `PATCHES` list of
`vcpkg_from_github()`. Drop the overlay once lunasvg releases fixes for all
patches and the baseline has that release; then delete this directory.

## Patches

### `unclosed-style-comment.patch`

**Problem:** `Document::loadFromData()` never returns for an SVG with an
unclosed CSS comment at the second character of a style. The comment can be
in a `<style>` element or in a `style` attribute:

```svg
<svg xmlns="http://www.w3.org/2000/svg"><style>x/*</style></svg>
<svg xmlns="http://www.w3.org/2000/svg"><rect style="x/*"/></svg>
```

An icon file of 30 bytes hangs the application's thread that draws it.

**Cause:** `removeStyleComments()` in `source/svgparser.cpp` erases every
`/* ... */` from a style before parsing it. For an unclosed comment,
`value.find("*/", start + 2)` returns `npos`, and
`value.erase(start, end - start + 2)` erases `npos - start + 2` characters:

- For `start == 1` that length wraps around to 0. Nothing is erased, and the
  loop finds the same `/*` again forever.
- For `start == 0` it erases one character; for any later start it erases to
  the end, which is what the patch does explicitly.

**Fix:** An unclosed comment runs to the end of the text, as CSS reads it:
`value.erase(start)` and the loop ends.

**Found by:** NewGUI's fuzz target `VectorIcon` (`tests/fuzz/VectorIconFuzz.cpp`),
seeded with Haiku's icons (`data/icons/haiku/`).

**Regression tests:** The two SVGs above are in the fuzz corpus
(`tests/fuzz/corpus/VectorIcon/unclosed-style-comment.bin`,
`unclosed-inline-comment.bin`); `FuzzRegressionTest.VectorIcon` draws them
in every test run. Without the patch the test hangs until its timeout.

**Upstream:** Not fixed in lunasvg 3.5.0 nor in `master` as of 2026-11-28
(`removeStyleComments()` unchanged); not reported yet.
