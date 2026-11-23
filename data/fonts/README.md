# Bundled fonts

The default fonts of NewGUI's text subsystem, embedded into the library
`ngui_text`: the counterparts of Haiku's be_plain_font, be_bold_font and
be_fixed_font. Haiku uses Noto Sans as its system font, so labels in the
ported looks match Haiku's screenshots.

| File | Role | Source |
|---|---|---|
| `NotoSans-Regular.ttf` | plain | `NotoSans-v2.015.zip`, `NotoSans/unhinted/ttf/` |
| `NotoSans-Bold.ttf` | bold | `NotoSans-v2.015.zip`, `NotoSans/unhinted/ttf/` |
| `NotoSansMono-Regular.ttf` | fixed | `NotoSansMono-v2.014.zip`, `NotoSansMono/unhinted/ttf/` |

The archives are the releases of <https://github.com/notofonts/latin-greek-cyrillic>.
The unhinted files are used, since the text subsystem does not hint
(specification 13.4).

## Checksums (SHA-256)

| File | SHA-256 |
|---|---|
| `NotoSans-v2.015.zip` | `0c34df072a3fa7efbb7cbf34950e1f971a4447cffe365d3a359e2d4089b958f5` |
| `NotoSansMono-v2.014.zip` | `090cf6c5e03f337a755630ca888b1fef463e64ae7b33ee134e9309c05f978732` |
| `NotoSans-Regular.ttf` | `f3961a9cde016d41a4879aecda1474d3a36d6bf54fa0e4643de029cc2248b0e8` |
| `NotoSans-Bold.ttf` | `87cb2d84472a7d66da659ee47b6cdb9552326e8c128245231f191b6ac72529d9` |
| `NotoSansMono-Regular.ttf` | `87f8ce0522a6c99b743ee5fc75b4073cfdd575639119672828b7b9944b65b4f4` |

## License

SIL Open Font License 1.1, see `OFL.txt` (identical in both archives). The
OFL allows bundling the fonts with software under any license; the fonts
themselves stay under the OFL, and `OFL.txt` must ship with them.
