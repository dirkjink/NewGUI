# Test fonts

Fonts the tests load from files, for the fallback of the text subsystem
(module X1) and for right-to-left text (module X3): characters that none
of the bundled Noto fonts has.

| File | Faces | Characters |
|---|---|---|
| `NotoSansCJK-Subset.ttc` | 0: Noto Sans CJK JP, 1: Noto Sans CJK KR | 0: 一 日 本 語 (U+4E00, U+65E5, U+672C, U+8A9E); 1: 글 한 (U+AE00, U+D55C) |
| `NotoSansHebrew-Subset.ttf` | Noto Sans Hebrew | א ב ג ו ל ם ע ש (U+05D0–05D2, U+05D5, U+05DC, U+05DD, U+05E2, U+05E9) |
| `NotoSansArabic-Subset.ttf` | Noto Sans Arabic | ا ب ح ر ع ل م (U+0627, U+0628, U+062D, U+0631, U+0639, U+0644, U+0645), with their joining forms |

Subsets made with fontTools by `make-subset.py`, of fonts as packaged in
Ubuntu 24.04:

- `NotoSansCJK-Regular.ttc` (faces 0 and 1) from Noto CJK
  (<https://github.com/notofonts/noto-cjk>, `fonts-noto-cjk`
  1:20230817+repack1-3);
- `NotoSansHebrew-Regular.ttf` and `NotoSansArabic-Regular.ttf` from Noto
  (<https://github.com/notofonts>, `fonts-noto-core` 20201225-2), with all
  layout features, so that Arabic letters join.

```sh
docker run --rm -v /usr/share/fonts/opentype/noto:/fonts:ro \
  -v /usr/share/fonts/truetype/noto:/noto:ro -v "$PWD":/out \
  python:3.12-slim bash -c "pip install -q fonttools && python /out/make-subset.py"
```

The collection has two faces so that the tests also load a face other than
the first. fontTools writes the time into the files, so a new run gives
other checksums; those below are the files in the repository.

## Checksums (SHA-256)

| File | SHA-256 |
|---|---|
| `NotoSansCJK-Regular.ttc` (source) | `b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a` |
| `NotoSansHebrew-Regular.ttf` (source) | `436900d5ad77d33e4234247f3076eaecd25b92b9bea0519514f684140e3566a7` |
| `NotoSansArabic-Regular.ttf` (source) | `504d7407d86875acf7d04dfaa0fd7524d0b8797723bc4aa18022f29db25b0b6e` |
| `NotoSansCJK-Subset.ttc` | `9a3f1a1004d8b0d71612729fd1654be6924d09de61ff723b7615a2ed39bd3471` |
| `NotoSansHebrew-Subset.ttf` | `f00d619b6efd97a46ed4f201ea892948a1a9dd2036fc7287730e6cc08bf0950e` |
| `NotoSansArabic-Subset.ttf` | `7e490d8c3c25a288075afb14cff59169a7621b77024971be45a64a355383557a` |

## License

SIL Open Font License 1.1, see `OFL.txt`, which names the copyright of
each font. Noto CJK reserves the font name
"Source"; the subset keeps the names "Noto Sans CJK JP" and "Noto Sans CJK
KR", which are not reserved. The fonts are only used by the tests and are
not part of the library.
