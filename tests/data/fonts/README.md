# Test fonts

Fonts the tests load from files, for the fallback of the text subsystem
(module X1): characters that none of the bundled Noto fonts has.

| File | Faces | Characters |
|---|---|---|
| `NotoSansCJK-Subset.ttc` | 0: Noto Sans CJK JP, 1: Noto Sans CJK KR | 0: 一 日 本 語 (U+4E00, U+65E5, U+672C, U+8A9E); 1: 글 한 (U+AE00, U+D55C) |

A subset of `NotoSansCJK-Regular.ttc` (faces 0 and 1) from Noto CJK
(<https://github.com/notofonts/noto-cjk>), as packaged in Ubuntu 24.04
(`fonts-noto-cjk` 1:20230817+repack1-3), made with fontTools by
`make-subset.py`:

```sh
docker run --rm -v /usr/share/fonts/opentype/noto:/fonts:ro -v "$PWD":/out \
  python:3.12-slim bash -c "pip install -q fonttools && python /out/make-subset.py"
```

The collection has two faces so that the tests also load a face other than
the first.

## Checksums (SHA-256)

| File | SHA-256 |
|---|---|
| `NotoSansCJK-Regular.ttc` (source) | `b76b0433203017ca80401b2ee0dd69350349871c4b19d504c34dbdd80541690a` |
| `NotoSansCJK-Subset.ttc` | `9a3f1a1004d8b0d71612729fd1654be6924d09de61ff723b7615a2ed39bd3471` |

## License

SIL Open Font License 1.1, see `OFL.txt`. Noto CJK reserves the font name
"Source"; the subset keeps the names "Noto Sans CJK JP" and "Noto Sans CJK
KR", which are not reserved. The fonts are only used by the tests and are
not part of the library.
