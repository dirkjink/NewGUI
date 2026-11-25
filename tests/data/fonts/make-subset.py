# Builds tests/data/fonts/NotoSansCJK-Subset.ttc (see README.md).
# Builds a two-face collection from Noto Sans CJK: face 0 the JP face with
# Han characters, face 1 the KR face with Hangul.
from fontTools.ttLib import TTCollection
from fontTools.ttLib.ttCollection import TTCollection as Out
from fontTools import subset
src = "/fonts/NotoSansCJK-Regular.ttc"
faces = []
for index, text in ((0, "日本語一"), (1, "한글")):
    font = TTCollection(src).fonts[index]
    options = subset.Options()
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.layout_features = ["*"]
    options.notdef_outline = True
    options.desubroutinize = True
    s = subset.Subsetter(options)
    s.populate(text=text)
    s.subset(font)
    faces.append(font)
out = Out()
out.fonts = faces
out.save("/out/NotoSansCJK-Subset.ttc")
for f in faces:
    print(f["name"].getDebugName(1), sorted(hex(c) for c in f.getBestCmap()))
