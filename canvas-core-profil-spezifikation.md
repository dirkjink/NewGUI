# Spezifikation: Austauschbare Canvas-Schicht – Core-Profil

| | |
|---|---|
| **Status** | Entwurf 0.7 (0.2: BeOS-Look als erster Port; 0.3: Austauschbarkeit der Looks; 0.4: Skia statt ImGui als erstes Backend, Blend2D als zweites; 0.5: Export, Screenshots und Test-Kit; 0.6: keine BeAPI-Kompatibilität, Kompatibilitätsschicht nur intern; 0.7: Namensraum `ngui`, C++23, MIT, Port-Phase nach CONVENTIONS.md) |
| **Datum** | 2026-11-22 |
| **Grundlage** | Analyse des Haiku-Quellstands (Commit `b1d497bb69`): `src/kits/interface/HaikuControlLook.cpp`, `src/add-ons/control_look/FlatControlLook`, `src/add-ons/control_look/BeControlLook` sowie Stichproben aus dem übrigen Interface Kit |
| **Herkunft** | Mit KI-Unterstützung erstellt. Nicht als Beitrag zum Haiku-Projekt geeignet (siehe `AGENTS.md` im Haiku-Repository). |

Die Schlüsselwörter **MUSS**, **DARF NICHT**, **SOLL** und **KANN** sind im Sinne von RFC 2119 zu lesen.

---

## Inhalt

1. Zweck und Geltungsbereich
2. Begriffe
3. Architekturüberblick
4. Grundkonventionen
5. Datentypen
6. Canvas-API (Core-Profil)
7. Clipping-Modell
8. Paint und Gradienten
9. Bilder
10. Text-Schnittstelle
11. DisplayList
12. Backend-Schnittstelle und Fähigkeiten
13. Skia-Backend
14. Blend2D-Backend
15. Dump-Backend
16. Extended-Profil
17. Kompatibilitätsschicht für Haiku-Code (intern)
18. ControlLook-Schnittstelle und Austauschbarkeit der Looks
19. Export und Screenshots
20. Test-Kit für Anwendungen
21. Teststrategie und Konformität
22. Datengrundlage
23. Umfangsabschätzung in Codezeilen
24. Offene Punkte

Anhang A: Verworfene Alternative – ImGui-Backend

---

## 1. Zweck und Geltungsbereich

Diese Spezifikation beschreibt eine Zeichenschicht („Canvas“) für ein UI-Toolkit, dessen Architektur sich an Haikus Interface Kit orientiert: Fenster als Looper, Views in einem Retained-Baum, Constraint-Layout und ein austauschbares ControlLook. Ziel ist, den **Renderer austauschbar** zu machen. Die erste Implementierung ist ein **Skia-Backend**, zunächst mit CPU-Raster-Surface, später mit GPU. Ein zweites, kleines Backend auf Basis von **Blend2D** dient als unabhängiger Nachweis der Austauschbarkeit. Ein Backend auf Basis von Dear ImGui wurde erwogen und verworfen (Anhang A). Views und ControlLook bleiben bei jedem Wechsel des Backends unverändert.

Das **Core-Profil** ist die Menge an Operationen, die *jedes* Backend vollständig unterstützen MUSS. Es ist so geschnitten, dass sich die drei vorhandenen Haiku-ControlLooks (Haiku, Flat, Be) darauf abbilden lassen, mit einer einzigen dokumentierten Ausnahme (`B_OP_INVERT`, Abschnitt 16).

**Reihenfolge der Looks:** Als erstes wird der **BeOS-Look** (`BeControlLook`) portiert, danach der Haiku-Look (`HaikuControlLook`). Die Gründe:

- `BeControlLook` leitet direkt von `BControlLook` ab und ist eigenständig. `FlatControlLook` setzt dagegen `HaikuControlLook` voraus.
- Er benutzt **keine Gradienten** und besteht überwiegend aus achsparallelen Linien und Rechtecken. Für diese Primitive verlangt Abschnitt 21 Pixelidentität. Damit eignet er sich am besten, um die Austauschbarkeit der Backends nachzuweisen.
- Seine AA-Geometrie beschränkt sich auf Radio-Buttons und Pfeildreiecke (Abschnitt 22). Abweichungen zwischen Backends lassen sich deshalb klar eingrenzen.

**Reihenfolge der Renderer:** Skia mit CPU-Raster (Phase 1), Blend2D als zweites Backend (Phase 2), Skia mit GPU-Surface (Phase G, ab Phase 1 jederzeit möglich).

Die Phasen und Konformitätsstufen sind in den Abschnitten 21.4 und 23.3 beschrieben.

**Grundsatz K1: Keine Kompatibilität zu BeAPI oder Haiku.** Weder Quell- noch Binärkompatibilität ist ein Ziel. Die öffentliche API ist eine moderne C++-API in **C++23** (ohne Module) unter **MIT-Lizenz**: halboffene Rechtecke, der Namensraum `ngui` (endgültig ohne `B`-Präfix, siehe K4), typsichere Aufzählungen, `std::expected` statt `status_t`, klarer Besitz über Smart Pointer, keine Archivierung, keine `_Reserved`-Slots oder `Perform`-Hooks. Über Fenstergrenzen hinweg wird ausschließlich mit Nachrichten gearbeitet.

- **Konzeptionelle Nähe:** Aufteilung der Klassen, Hooks (`AttachedToWindow`, `Draw`, `MouseDown`, `FrameResized` …), gut gewählte Methodennamen (`Frame()`, `Bounds()`, `Invalidate()`) und Algorithmen orientieren sich an Haiku. So lässt sich Haiku-Code Methode für Methode übertragen, ohne dass eine Kompatibilität zugesagt wird.
- **Kompatibilität nur intern und gezielt:** dort, wo sie Pixeltreue gegenüber Referenzen aus Haiku sichert, und zwar über die interne Kompatibilitätsschicht (Abschnitt 17).
- **Herkunft:** Wörtlich übernommener Haiku-Code (z. B. Layout-Engine, Region, ControlLooks) behält seine MIT-Copyright-Hinweise.
- **Stabilität:** Ab Version 1.0 gilt Semantic Versioning für die Quell-API. Eine stabile ABI wird nicht zugesagt (vgl. 18.6).
- **K4: Port-Phase.** Bis zur ersten öffentlichen Freigabe gelten für Namen und Formatierung die Coding Guidelines von Haiku, einschließlich des `B`-Präfixes für portierte Klassen. Der Code liegt dabei bereits im Namensraum `ngui` (z. B. `ngui::BWindow`). Regeln zu Semantik und API-Gestalt (halboffene Rechtecke, `std::expected`, Besitzregeln, Nachrichten statt Locking von außen, `enum class`) gelten von Anfang an. Vor der Freigabe werden Namen und Konventionen mechanisch auf die Zielform umgestellt. Einzelheiten stehen in [`CONVENTIONS.md`](CONVENTIONS.md).
- **K5: Gleicher Name nur bei gleicher Bedeutung.** Ein Haiku-Name wird nur für ein Element verwendet, dessen Verhalten dem Original entspricht. Bei geänderter Semantik gibt es einen neuen Namen, z. B. `ngui::gfx::Rect` statt `BRect` oder `ngui::ControlLook` statt `BControlLook`.
- **Namen in den Codebeispielen** dieser Spezifikation folgen der Port-Phase: portierte Klassen mit `B`-Präfix (`BWindow`, `BView` …), neue Klassen ohne (`Canvas`, `ControlLook`, `DrawContext` …).

**Nicht Gegenstand** dieser Spezifikation:
- das Text-Subsystem (Font-Verwaltung, Shaping, Metriken, Umbruch). Spezifiziert wird hier nur die Schnittstelle `GlyphRun`, Abschnitt 10.
- Fensterverwaltung, Ereignisverarbeitung, Layout
- Barrierefreiheit
- Screenshots auf Ebene des Betriebssystems (ganzer Bildschirm mit Fensterrahmen, siehe 19.1)

## 2. Begriffe

| Begriff | Bedeutung |
|---|---|
| **Canvas** | Die Aufzeichnungs-API, gegen die Views und ControlLook zeichnen. Sie führt nichts aus, sondern erzeugt eine DisplayList. |
| **DisplayList** | Eine unveränderliche, vom Renderer unabhängige Folge von Zeichenoperationen |
| **Backend** | Eine Komponente, die DisplayLists auf eine Oberfläche rendert (Skia, Blend2D, Dump) |
| **Export-Backend** | Eine Komponente, die eine DisplayList in ein Dateiformat übersetzt (z. B. SVG), ohne zu rastern (Abschnitt 19) |
| **Headless** | Betrieb ohne Bildschirm und Fenstersystem, Fenster existieren nur virtuell (Abschnitt 20.2) |
| **Surface** | Ein Renderziel, typischerweise der GPU-Puffer eines nativen Fensters oder eine Offscreen-Textur |
| **Core-Profil** | Pflichtumfang für alle Backends |
| **Extended-Profil** | Optionale Fähigkeiten, die über `Caps` abgefragt werden |
| **Logisches Pixel** | Die Koordinateneinheit der Canvas, unabhängig von der Pixeldichte des Bildschirms |
| **Geräte-Pixel** | Ein physisches Pixel der Surface: `device = logical × scale` |
| **Paint** | Die Stilparameter einer einzelnen Zeichenoperation |

## 3. Architekturüberblick

```
 ┌──────────────────────────────────────────────────────────────────┐
 │ View / ControlLook                                               │
 │   (optional über die Kompatibilitätsschicht, Abschnitt 17)       │
 └───────────────┬──────────────────────────────────────────────────┘
                 │ Canvas-API (Abschnitt 6)
 ┌───────────────▼──────────────┐     ┌──────────────────────────────┐
 │ Canvas-Frontend              │◄────│ Text-Subsystem → GlyphRun    │
 │  Zustandsstapel, Translation,│     └──────────────────────────────┘
 │  Clip-Region, Aufzeichnung   │
 └───────────────┬──────────────┘
                 │ DisplayList (unveränderlich, referenzgezählt)
 ┌───────────────▼──────────────────────────────────────────────────┐
 │ Backend-Schnittstelle (Abschnitt 12)                             │
 ├──────────────────────┬──────────────────┬────────────────────────┤
 │ Skia-Backend         │ Blend2D-Backend  │ Dump-Backend           │
 │  CPU-Raster (Ph. 1)  │  (Nachweis C1×)  │  (Tests, Debugging)    │
 │  GPU (Phase G)       │                  │                        │
 ├──────────────────────┴──────────────────┴────────────────────────┤
 │ CPU-Presenter: Pixel → natives Fenster (Teil von M11)            │
 │ Export: SVG-Backend; Offscreen-Surfaces für Screenshots (19)     │
 └──────────────────────────────────────────────────────────────────┘
```

**Verbindliche Regeln:**

- **R1:** Oberhalb der Backend-Schnittstelle DARF KEIN Typ, Header oder Makro einer Render-Bibliothek (Skia, Blend2D …) sichtbar sein. Das gilt auch für `SkPaint`, `SkRect`, `SkFont`, `BLContext`, `BLPath` usw.
- **R2:** Views zeichnen ausschließlich über die Canvas. Ein direkter Zugriff auf ein Backend ist nicht zulässig.
- **R3:** Fallbacks für nicht unterstützte Extended-Operationen gehören ins Canvas-Frontend oder in den Aufrufer, nicht ins Backend. Ein Backend MUSS das Core-Profil vollständig implementieren und DARF für Extended-Operationen, die es nicht unterstützt, `false` in `Caps` melden.

## 4. Grundkonventionen

### 4.1 Koordinaten

- Koordinaten sind `float` in logischen Pixeln. Der Ursprung liegt oben links, die y-Achse zeigt nach unten.
- Rechtecke sind **halboffen**: `Rect{x0, y0, x1, y1}` umfasst `[x0, x1) × [y0, y1)`. Ein Rechteck mit `x1 <= x0` oder `y1 <= y0` ist leer, und Operationen darauf haben keine Wirkung.
- Das Zentrum des Pixels `(i, j)` liegt bei `(i + 0.5, j + 0.5)`.
- **Abweichung von Haiku:** `BRect` ist inklusiv (`BRect(0,0,9,9)` bezeichnet 10 × 10 Pixel). Die Umrechnung findet ausschließlich in der Kompatibilitätsschicht statt (Abschnitt 17). Die Canvas selbst kennt keine inklusiven Rechtecke.

### 4.2 Pixeldichte

- Jede Surface hat einen Skalierungsfaktor `scale > 0`, z. B. 1.0, 1.5 oder 2.0. Er ist **kein** Teil der Transformation der DisplayList, sondern wird vom Backend beim Rendern angewendet.
- DisplayLists sind unabhängig vom Skalierungsfaktor. Dieselbe Liste DARF auf Surfaces mit unterschiedlichem `scale` gerendert werden.

### 4.3 Farbe und Blending

- Farben sind `Color{r, g, b, a}` als `float` im Bereich `[0, 1]`, **nicht vormultipliziert**, sRGB-kodiert.
- Intern und bei Bildern arbeiten Backends mit vormultipliziertem Alpha.
- Im Core-Profil gibt es genau einen Blend-Modus: **SrcOver** (Porter-Duff). Gemischt wird im sRGB-kodierten Raum, also gammanaiv. Das entspricht dem Verhalten von Haiku und dem von Skia ohne zugewiesenen Farbraum. Lineares Blending ist ein offener Punkt (Abschnitt 24).

### 4.4 Rasterung und Antialiasing

- **A1:** Achsparallele Rechtecke, Rechteckrahmen und achsparallele Linien werden **ohne Antialiasing** gerastert. Ihre Kanten werden nach der Skalierung auf ganze Geräte-Pixel gerundet (`floor(v + 0.5)`).
- **A2:** Alle anderen Geometrien (Ellipsen, Bögen, abgerundete Ecken, diagonale Linien, Polygone) werden mit Antialiasing gerastert, sofern `Paint::antialias` (Voreinstellung `true`) gesetzt ist.
- **A3:** Eine achsparallele Linie der Breite `w` zwischen `p0` und `p1` überdeckt das Rechteck, das entsteht, wenn man die Strecke senkrecht um `w/2` nach beiden Seiten verbreitert. In Längsrichtung bestimmt die Cap die Ausdehnung (`Butt`: keine, `Square`: `w/2`).

Hintergrund: Die Bevel-Rahmen der Haiku-Looks bestehen aus 1 px breiten Linien in wechselnden Farben. Mit Antialiasing würden sie unscharf.

## 5. Datentypen

```cpp
namespace ngui::gfx {

struct Point { float x, y; };
struct Size  { float w, h; };
struct Rect  { float x0, y0, x1, y1; };             // halboffen
struct Color { float r, g, b, a; };                 // sRGB, nicht vormultipliziert

struct Radii { float tl, tr, br, bl; };             // Radius pro Ecke, >= 0
struct RRect { Rect rect; Radii radii; };           // Radien werden auf min(w,h)/2 begrenzt

struct Line  { Point p0, p1; Color color; };        // für DrawLines

using ImageHandle = uint32_t;                       // 0 = ungültig
using FontId      = uint32_t;

} // namespace ngui::gfx
```

- `RRect`: Ist die Summe zweier benachbarter Radien größer als die betreffende Kantenlänge, werden alle Radien proportional verkleinert (wie in CSS bzw. Skia).
- Alle Typen sind POD und trivial kopierbar. Das ist Voraussetzung für die DisplayList (Abschnitt 11).

## 6. Canvas-API (Core-Profil)

```cpp
class Canvas {
public:
    explicit Canvas(Rect bounds);                    // Grenzen der aufzuzeichnenden View

    // Zustand
    void Save();
    void Restore();
    void Translate(float dx, float dy);

    // Clipping (Abschnitt 7)
    void ClipRect(const Rect& r);
    void ClipOutRect(const Rect& r);

    // Füllen
    void FillRect(const Rect& r, const Paint& p);
    void FillRRect(const RRect& rr, const Paint& p);
    void FillRing(const RRect& outer, const RRect& inner, const Paint& p);
    void FillEllipse(const Rect& r, const Paint& p);
    void FillConvexPolygon(std::span<const Point> pts, const Paint& p);

    // Umranden
    void StrokeRect(const Rect& r, const Paint& p);
    void StrokeRRect(const RRect& rr, const Paint& p);
    void StrokePolyline(std::span<const Point> pts, bool closed, const Paint& p);
    void StrokeArc(const Rect& oval, float startDeg, float sweepDeg, const Paint& p);
    void DrawLines(std::span<const Line> lines, float width, Paint::Cap cap);

    // Bilder und Text
    void DrawImage(ImageHandle img, const Rect& src, const Rect& dst, float alpha = 1.0f);
    void DrawGlyphRun(const GlyphRun& run, Point origin, const Paint& p);

    // Verschachtelung
    void DrawList(const DisplayListRef& child, Point offset);

    DisplayListRef Finish();                         // danach ist die Canvas unbrauchbar
};
```

### 6.1 Semantik der Operationen

| Operation | Semantik |
|---|---|
| `Save`/`Restore` | Sichert bzw. stellt wieder her: Translation und Clip-Region. Ein `Restore` ohne passendes `Save` ist ein Programmierfehler (Assertion). `Finish()` schließt offene `Save`s implizit. |
| `Translate` | Verschiebt alle folgenden Koordinaten. Die Wirkung ist kumulativ. Im Core-Profil gibt es **keine** Skalierung und keine Rotation. |
| `FillRect` | Füllt das halboffene Rechteck gemäß Regel A1 |
| `FillRRect` | Füllt das abgerundete Rechteck. Die Ecken sind Viertelellipsen mit den Halbachsen `radius × radius`. |
| `FillRing` | Füllt die Fläche zwischen `outer` und `inner`. `inner` MUSS vollständig in `outer` liegen, sonst ist das Ergebnis undefiniert. Typischer Fall: Rahmen mit 1–2 px Breite um abgerundete Controls. |
| `FillEllipse` | Füllt die in `r` einbeschriebene Ellipse |
| `FillConvexPolygon` | Füllt ein konvexes Polygon (≥ 3 Punkte, Reihenfolge beliebig). Bei nichtkonvexer Eingabe ist das Ergebnis undefiniert, eine Prüfung findet nur in Debug-Builds statt. |
| `StrokeRect` | Zeichnet den Rahmen **innerhalb** von `r` mit Breite `p.strokeWidth`, sodass die Außenkante bündig mit `r` ist. (Abweichung von Skia, die zentriert umrandet. Das Skia-Backend korrigiert das, indem es um `w/2` einrückt.) |
| `StrokeRRect` | wie `StrokeRect`, der Rahmen liegt innen |
| `StrokePolyline` | zentrierte Umrandung mit `strokeWidth`, `cap` und `join` aus dem Paint |
| `StrokeArc` | Bogen auf der in `oval` einbeschriebenen Ellipse. Winkel in Grad, 0° bei 3 Uhr, positiv **im Uhrzeigersinn** (y nach unten). |
| `DrawLines` | Zeichnet unabhängige Linien, jede in ihrer eigenen Farbe. Es gibt keine Joins zwischen den Linien. Achsparallele Linien folgen Regel A3. |
| `DrawImage` | Zeichnet den Ausschnitt `src` des Bildes (in Bildpixeln) skaliert nach `dst`. `alpha` wird multiplikativ angewendet. Filterung: bei Größe 1:1 *nearest*, sonst *linear*. |
| `DrawGlyphRun` | siehe Abschnitt 10 |
| `DrawList` | Fügt eine andere DisplayList an der Stelle `offset` ein, unter dem aktuellen Clip. Rekursion über Zyklen ist unzulässig. |

### 6.2 Fehlerverhalten

- Ungültige Eingaben (NaN, negative Breiten, leere Spans) führen dazu, dass die Operation **verworfen** wird. In Debug-Builds gibt es zusätzlich eine Warnung. Das Verhalten ist deterministisch und hängt nicht vom Backend ab.
- Die Canvas wirft keine Exceptions.

## 7. Clipping-Modell

- Der Clip-Zustand ist eine **Region aus achsparallelen Rechtecken in ganzen logischen Koordinaten**. Er wird als sortierte, disjunkte Rechteckliste geführt, nach dem Modell von Haikus `BRegion` (Banding).
- `ClipRect(r)`: `clip := clip ∩ round(r)`
- `ClipOutRect(r)`: `clip := clip \ round(r)`
- `round()` rundet jede Kante auf die nächste ganze Zahl (`floor(v + 0.5)`) **nach** Anwendung der aktuellen Translation.
- Der Anfangswert ist das Rechteck `bounds` der Canvas.
- Die Region wird **im Frontend berechnet** und zusammen mit jeder Zeichenoperation in die DisplayList geschrieben. Effizient geschieht das als Verweis auf eine Clip-Tabelle, siehe 11.2. Backends müssen keine Mengenoperationen auf Regionen beherrschen. Sie bekommen nur eine fertige Rechteckliste.
- Ist die Clip-Region leer, verwirft das Frontend alle folgenden Zeichenoperationen bis zum passenden `Restore`.

Clipping über Pfade oder abgerundete Rechtecke gehört **nicht** zum Core-Profil (Abschnitt 16).

## 8. Paint und Gradienten

```cpp
struct GradientStop { float offset; Color color; };  // offset in [0, 1], aufsteigend

struct LinearGradient {
    Point p0, p1;                                     // in Canvas-Koordinaten (vor Translation)
    std::array<GradientStop, 4> stops;
    uint8_t count;                                    // 2..4
};

struct Paint {
    Color color{0, 0, 0, 1};
    std::optional<LinearGradient> gradient;           // falls gesetzt, wird color ignoriert
    float strokeWidth = 1.0f;
    enum class Cap  : uint8_t { Butt, Round, Square } cap  = Cap::Butt;
    enum class Join : uint8_t { Miter, Round, Bevel } join = Join::Miter;
    bool antialias = true;                            // siehe 4.4, A2
};
```

- **Gradienten:** Im Core-Profil gibt es nur **lineare** Gradienten mit 2–4 Stopps. Die Farbe am Punkt `q` ergibt sich aus `t = clamp(dot(q − p0, p1 − p0) / |p1 − p0|², 0, 1)` und stückweise linearer Interpolation zwischen den Stopps. Interpoliert wird **nicht vormultipliziert** im sRGB-kodierten Raum.
- Ein Gradient DARF mit jeder Füll- und Umrandungsoperation kombiniert werden, außer mit `DrawLines` (das Farben pro Linie hat) und `DrawImage`.
- `p0 == p1` führt dazu, dass die Farbe des letzten Stopps verwendet wird.
- Umrechnung der Haiku-Offsets: `offset = haikuOffset / 255.0f`. Die beobachteten Muster sind `{0, 1}` und `{0, 0.518, 0.533, 1}` (Glossy).

## 9. Bilder

- Bilder werden über `Backend::CreateImage(const PixelData&)` erzeugt und mit `ReleaseImage` freigegeben. `PixelData` liefert RGBA8, nicht vormultipliziert und sRGB-kodiert; das Backend wandelt intern um.
- `ImageHandle`s sind **frontendweit** eindeutig. Ein Registry-Modul bildet sie auf die Ressourcen des jeweiligen Backends ab, sodass ein Wechsel des Backends keine Änderungen an Views verlangt.
- Bitmaps mit Farbschlüssel (Haikus `B_TRANSPARENT_COLOR` bei `B_OP_OVER`) MÜSSEN beim Import in Alpha umgewandelt werden. Eine Farbschlüssel-Semantik gibt es zur Laufzeit nicht.
- Eine DisplayList hält Referenzen auf die Bilder, die sie benutzt. Ein Bild wird erst freigegeben, wenn keine Liste mehr darauf verweist.

## 10. Text-Schnittstelle

```cpp
struct GlyphRun {
    FontId                     font;       // vom Text-Subsystem vergeben
    float                      sizePx;     // Schriftgröße in logischen Pixeln
    std::span<const uint32_t>  glyphs;     // Glyph-Indizes der Font-Datei (nicht Codepoints!)
    std::span<const Point>     positions;  // Grundlinienpositionen relativ zu origin

    // optional: Quelltext und Cluster-Zuordnung (für Export, Tests, später Barrierefreiheit)
    std::u8string_view         text;       // UTF-8, leer = nicht vorhanden
    std::span<const uint32_t>  clusters;   // pro Glyph der Byte-Offset in text; leer oder glyphs.size() Einträge
};
```

- Der Glyph-Run wird **fertig geformt** übergeben. Shaping, BiDi, Kerning, Zeilenumbruch und Kürzung (`TruncateString`) erledigt das Text-Subsystem.
- Alle Backends MÜSSEN dieselben Font-Dateien verwenden, die das Text-Subsystem über `FontId` bereitstellt. Ein Backend DARF KEINE eigenen Metriken berechnen.
- Im Core-Profil wird Text nur mit **Volltonfarbe** gezeichnet. Ein Gradient im Paint wird bei `DrawGlyphRun` ignoriert.
- Beim Kopieren in die DisplayList werden die Spans kopiert. Der Aufrufer muss sie also nicht länger am Leben halten.
- **`text` und `clusters` sind optional.** Render-Backends MÜSSEN sie ignorieren. Export-Backends (Abschnitt 19) und das Test-Kit (Abschnitt 20) DÜRFEN sie nutzen, z. B. für auswählbaren Text im SVG oder für Prüfungen auf Textinhalte.
- Das Text-Subsystem SOLL beide Felder standardmäßig füllen. Eine Canvas-Option (`Canvas::SetRecordGlyphText(false)`) erlaubt es, sie zum Sparen von Speicher wegzulassen.
- Sind `clusters` vorhanden, MÜSSEN sie monoton sein (bei Text von rechts nach links: monoton fallend) und auf gültige Grenzen von UTF-8-Zeichen zeigen.

## 11. DisplayList

### 11.1 Eigenschaften

- **Unveränderlich** nach `Canvas::Finish()`.
- **Referenzgezählt** (`DisplayListRef`, entspricht `std::shared_ptr<const DisplayList>`).
- **Thread-neutral:** Eine Liste wird in einem Thread aufgezeichnet (dem Fenster-Looper) und DARF in einem anderen gerendert werden (dem Render-Thread). Sie enthält keine Zeiger auf veränderliche externe Daten.
- **Selbstständig:** Arrays (Punkte, Linien, Glyphen) liegen inline in der Liste. Externe Ressourcen werden nur über Handles mit Referenzzählung angesprochen (Bilder, Unterlisten, Fonts).

### 11.2 Aufbau

```
DisplayList
 ├─ header      { magic, version, bounds, opCount, clipCount }
 ├─ clipTable   [ ClipRegion { rectCount, rects[] } … ]     // dedupliziert
 ├─ opStream    [ OpHeader { uint16 opcode, uint16 flags, uint32 size, uint32 clipIndex }
 │                payload (POD, 4-Byte-ausgerichtet) … ]
 └─ resources   [ ImageHandle…, DisplayListRef…, FontId… ]  // für die Referenzzählung
```

- Translationen werden im Frontend **auf die Koordinaten angewendet**. Der opStream enthält deshalb nur absolute Koordinaten im Raum der Liste, und `Save`, `Restore` und `Translate` erscheinen dort nicht.
- Jede Operation verweist auf einen Eintrag der Clip-Tabelle. Aufeinanderfolgende Operationen mit derselben Region teilen sich diesen Eintrag.
- Opcodes im Core-Profil: `FillRect`, `FillRRect`, `FillRing`, `FillEllipse`, `FillConvexPolygon`, `StrokeRect`, `StrokeRRect`, `StrokePolyline`, `StrokeArc`, `DrawLines`, `DrawImage`, `DrawGlyphRun`, `DrawList`.
- Unbekannte Opcodes (z. B. aus dem Extended-Profil auf einem Backend ohne Unterstützung) werden über `size` übersprungen.

### 11.3 Serialisierung

- Die binäre Form entspricht dem Speicherlayout (Little Endian, versioniert). Damit ist sie für Remote-Rendering und Snapshots in Tests geeignet.
- Das Dump-Backend (Abschnitt 15) erzeugt daraus eine deterministische Textform.

### 11.4 Caching

- Jede View hält ihre letzte DisplayList. Ein `Invalidate()` verwirft nur die Liste dieser View.
- Die Liste eines Fensters entsteht durch Verschachtelung (`DrawList`) der View-Listen entlang des View-Baums und wird für jedes Frame neu zusammengesetzt. Das ist billig, weil nur Verweise kopiert werden.

## 12. Backend-Schnittstelle und Fähigkeiten

```cpp
struct Caps {
    // Core: immer true, nur zur Dokumentation
    bool core = true;
    // Surface-Arten (13.3)
    bool gpuSurfaces      = false;
    // Extended (Abschnitt 16)
    bool affineTransform  = false;
    bool pathClip         = false;
    bool bezierPaths      = false;
    bool concavePolygons  = false;
    bool radialGradient   = false;
    bool conicGradient    = false;
    bool blendModes       = false;
    bool offscreenTargets = false;
    bool blurFilter       = false;
};

enum class SurfaceKind : uint8_t { CpuRaster, Gpu };

class Backend {
public:
    virtual ~Backend() = default;
    virtual Caps         Capabilities() const = 0;

    virtual ImageHandle  CreateImage(const PixelData&) = 0;        // thread-sicher
    virtual void         ReleaseImage(ImageHandle) = 0;            // thread-sicher

    virtual SurfaceRef   CreateWindowSurface(const NativeWindow&, SurfaceKind) = 0; // nur Render-Thread
    virtual SurfaceRef   CreateOffscreenSurface(Size device, float scale) = 0;      // CPU, für Tests
    virtual void         ResizeSurface(Surface&, Size device, float scale) = 0;

    virtual void         BeginFrame(Surface&, Color clear) = 0;    // nur Render-Thread
    virtual void         Render(const DisplayList&, Point offset) = 0;
    virtual void         EndFrame(Surface&) = 0;                   // präsentiert
};
```

- Ein Backend MUSS alle Operationen des Core-Profils implementieren und die Konformitätstests aus Abschnitt 21 bestehen.
- Ein Backend MUSS Operationen, die es nicht kennt, überspringen und DARF dabei nicht abstürzen.
- `NativeWindow` ist eine Plattformabstraktion (SDL3, GLFW oder native Handles) und wird außerhalb dieser Spezifikation definiert.
- CPU-Surfaces werden über den **CPU-Presenter** der Backend-Infrastruktur (M11) ins Fenster übertragen. Er kopiert nur die geänderten Bereiche, also die Vereinigung der invalidierten View-Grenzen.
- Ein Backend ohne GPU-Unterstützung meldet `gpuSurfaces = false` und DARF `SurfaceKind::Gpu` ablehnen.

## 13. Skia-Backend

Das Skia-Backend ist das **Ziel-Backend** und wird als erstes implementiert, zunächst mit einer CPU-Raster-Surface (13.3).

### 13.1 Abbildung der Operationen

| Operation | Umsetzung |
|---|---|
| Clip-Region | siehe 13.2 |
| `FillRect`/`FillRRect`/`FillEllipse` | `drawRect`/`drawRRect` (`SkRRect::setRectRadii`)/`drawOval` |
| `FillRing` | `drawDRRect` |
| `FillConvexPolygon`/`StrokePolyline` | `drawPath`, Caps und Joins aus `SkPaint` |
| `StrokeRect`/`StrokeRRect` | zuerst um `w/2` einrücken (der Rahmen liegt innen, siehe 6.1), dann `drawRect`/`drawRRect` mit Stroke |
| `StrokeArc` | `drawArc` (Winkelkonvention ist identisch) |
| `DrawLines` | achsparallel als `drawRect` (A3), sonst `drawLine` |
| Gradient | `SkGradientShader::MakeLinear`, Interpolation nicht vormultipliziert |
| `DrawImage` | `drawImageRect` mit `SkSamplingOptions` nach 6.1 |
| `DrawGlyphRun` | siehe 13.4 |
| Antialiasing | `SkPaint::setAntiAlias` nach den Regeln A1/A2. A1 wird zusätzlich durch Runden auf ganze Geräte-Pixel abgesichert. |

### 13.2 Clipping

- Eine Clip-Region aus einem Rechteck wird mit `clipRect` gesetzt, eine aus mehreren mit `SkRegion::setRects` und `clipRegion`.
- `clipRegion` erwartet **Geräte-Koordinaten**. Die Rechtecke der Region werden deshalb vorher mit `scale` multipliziert.

### 13.3 Surfaces

- **CPU-Raster (Phase 1):** Die Surface ist eine Raster-Surface im Format N32 mit vormultipliziertem Alpha (`SkSurfaces::Raster(SkImageInfo::MakeN32Premul(w, h))`, in älteren Milestones `SkSurface::MakeRaster…`). Nach `EndFrame` übernimmt der **CPU-Presenter** der Backend-Infrastruktur (M11) die Pixel und kopiert die geänderten Bereiche ins Fenster, z. B. über eine SDL-Textur oder einen Blit der Plattform.
- **Farbraum:** Die Surface bekommt keinen `SkColorSpace`. Damit wird im kodierten Raum gemischt, wie in 4.3 verlangt.
- **GPU (Phase G):** Ganesh (GL, Vulkan, Metal) oder Graphite, angefordert über `SurfaceKind::Gpu`. Player und Abbildung der Operationen bleiben gleich, nur das Anlegen der Surface, Swapchain und Flush unterscheiden sich. **Umgesetzt (2026-11-26):** Ganesh mit OpenGL über den Framebuffer 0 eines GL-Kontexts, der auf dem aufrufenden Thread aktuell ist (`NativeWindow::glProcAddress` lädt seine Funktionen); `EndFrame` übergibt die Befehle an die GPU, das Tauschen der Puffer übernimmt die Plattform. Da SDL3 OpenGL nur auf dem Hauptthread erlaubt, zeichnet die SDL-Plattform GPU-Fenster dort: Das Fenster übergibt seine DisplayList (`PlatformWindow::PresentList()`), der Hauptthread rendert nur die neueste und tauscht die Puffer; `ReadShownFrame()` liest das Gezeigte für `BWindow::CaptureFrame()` (19.1, Stufe b). Eingeschaltet wird das zunächst auf Wunsch (`NGUI_RENDERER=gpu`, `SdlPlatform::SetGpuRendering()`); ohne OpenGL, ohne Backend mit GPU-Surfaces oder ohne Kontext zeichnet das Fenster auf der CPU. Getestet gegen die CPU-Referenz unter Mesas llvmpipe (EGL-Pbuffer, `CompareRasterizers()`).
- **Offscreen-Surfaces** für Tests sind immer CPU-Raster-Surfaces und damit deterministisch.

### 13.4 Text

- Pro `FontId` wird ein `SkTypeface` aus **derselben Font-Datei** erzeugt, die das Text-Subsystem verwendet (`SkFontMgr::makeFromFile` bzw. `makeFromData`), und zwischengespeichert.
- `SkFont`: Größe `sizePx`, Subpixel-Positionierung an, Graustufen-AA (`Edging::kAntiAlias`).
- Glyph-Runs werden mit `SkTextBlobBuilder::allocRunPos` aus Glyph-IDs und Positionen gebaut.
- **Hinting** ist einstellbar. Für den Vergleich mit dem Blend2D-Backend (Stufe C1×) wird es in beiden Backends angeglichen, im Zweifel abgeschaltet (`SkFontHinting::kNone`).

### 13.5 Integration und Build

- Skia wird als **vorgebautes Paket** bezogen: der vcpkg-Port `skia`, die vorgebauten Bibliotheken von JetBrains (`skia-pack`) oder die Builds von Aseprite. Ein eigener Build mit `gn`/`ninja` ist nur nötig, wenn keines dieser Pakete passt.
- Der **Milestone** (`m1xx`) wird festgeschrieben. Aktualisiert wird nur bewusst und nur mit einem vollständigen Lauf der Golden-Image-Tests.
- Skia-Header erscheinen ausschließlich in Quelldateien des Backends (R1), das ein eigenes Bibliotheksziel bekommt (z. B. `ngui-gfx-skia`).
- Compiler-Einstellungen (C++-Standard, RTTI, Exceptions, Defines wie `SK_GL`/`SK_VULKAN`) MÜSSEN zur Konfiguration des Pakets passen.
- Die Binärgröße liegt bei mehreren MB. Wo das Paket es erlaubt, werden nicht benötigte Module weggelassen (z. B. PDF, SVG, Skottie).

### 13.6 Fähigkeiten

Skia kann alle Fähigkeiten des Extended-Profils bereitstellen. Das Backend meldet sie in `Caps`, sobald sie implementiert sind. `gpuSurfaces` wird ab Phase G gemeldet.

## 14. Blend2D-Backend

**Zweck:** ein zweites, unabhängiges Backend als **Nachweis der Austauschbarkeit** (Stufe C1×, 21.4) und als schlanke CPU-Referenz. Es ist kein Produktionsziel. Ob es nach Phase 2 erhalten bleibt, ist offener Punkt 10 (Abschnitt 24).

Die API-Namen in diesem Abschnitt beziehen sich auf die aktuelle Blend2D-Version und sind beim Einbinden zu prüfen.

### 14.1 Abbildung der Operationen

| Operation | Umsetzung |
|---|---|
| Surface | `BLImage` im Format `BL_FORMAT_PRGB32` mit einem `BLContext`. Die Übergabe ans Fenster erfolgt über den CPU-Presenter wie in 13.3. |
| Clip-Region | Blend2D kennt nur Clipping auf Rechtecke (`clipToRect`), deshalb Fan-out (14.2) |
| `FillRect` | `fillRect` mit auf Geräte-Pixel gerundeten Koordinaten (A1) |
| `FillRRect` | bei gleichen Radien `fillRoundRect`, sonst ein `BLPath` aus Linien und elliptischen Bögen |
| `FillRing` | ein `BLPath` mit äußerer und innerer Kontur, Füllregel `BL_FILL_RULE_EVEN_ODD` |
| `FillEllipse` | `fillEllipse` |
| `FillConvexPolygon` | `fillPolygon` |
| `StrokeRect`/`StrokeRRect` | um `w/2` einrücken, dann `strokeRect`, `strokeRoundRect` bzw. `strokePath` |
| `StrokePolyline` | `strokePolyline` bzw. `strokePath`. Breite, Caps und Joins werden über `setStrokeWidth`, `setStrokeCaps` und `setStrokeJoin` gesetzt. |
| `StrokeArc` | Pfad mit elliptischem Bogen, dann `strokePath`. Die Winkelkonvention aus 6.1 ist zu prüfen. |
| `DrawLines` | achsparallel als `fillRect` (A3), sonst `strokeLine` |
| Gradient | `BLGradient` mit `BLLinearGradientValues` und `addStop`. Ob vormultipliziert interpoliert wird, MUSS gegen Abschnitt 8 geprüft werden. |
| `DrawImage` | `blitImage(dstRect, image, srcArea)` mit `setGlobalAlpha(alpha)`, Filterung nach 6.1 |
| `DrawGlyphRun` | `BLFontFace` aus derselben Font-Datei (`createFromFile`), `BLFont` mit `sizePx`, `BLGlyphRun` aus Glyph-IDs und Positionen, dann `fillGlyphRun` |
| Antialiasing | Blend2D rastert immer analytisch mit AA. A1 wird durch Runden auf ganze Geräte-Pixel erfüllt, weil ganzzahlige Kanten volle Abdeckung ergeben. |

### 14.2 Clipping-Fan-out

- Bei einer Clip-Region aus *n* Rechtecken wird die Operation *n*-mal ausgeführt, jeweils mit `save()`, `clipToRect()`, Zeichnen und `restore()`.
- Bei den Mustern der ControlLooks ist *n* meist 1–4. Ab welcher Größe der Fan-out zu teuer wird, muss gemessen werden (offener Punkt 7).

### 14.3 Fähigkeiten

Gemeldet werden das Core-Profil sowie `affineTransform`, `bezierPaths`, `concavePolygons`, `radialGradient`, `conicGradient` und `offscreenTargets`. **Nicht** gemeldet werden `pathClip`, `blendModes`, `blurFilter` und `gpuSurfaces`.

### 14.4 Integration

- Blend2D wird als Quellpaket mit CMake eingebunden. Es bringt AsmJit für die JIT-Pipelines mit und steht unter der zlib-Lizenz.
- Die Version wird festgeschrieben. Vor dem Einsatz ist zu prüfen, wie stabil die API ist.

## 15. Dump-Backend

- Gibt eine DisplayList als deterministischen Text oder JSON aus. Koordinaten werden auf 3 Nachkommastellen gerundet, Opcodes erscheinen mit Namen, Clip-Regionen werden aufgelöst.
- Zweck: Snapshot-Tests ohne GPU und Fehlersuche.
- Rendert nicht und präsentiert nicht.

## 16. Extended-Profil (nicht Core)

| Fähigkeit | Bedarf im Haiku-Code | Empfohlener Ersatz, solange sie fehlt |
|---|---|---|
| Invert (`B_OP_INVERT`, `InvertRect`) | BeControlLook (1-mal, gedrückter Zustand, `BeControlLook.cpp:2737`), ColumnListView (4), TextView (1) | Hervorhebung mit Highlight-Farbe und Alpha. Im BeControlLook-Port wird das invertierte Rechteck durch `FillRect` mit `tint_color(base, B_DARKEN_2_TINT)` bei Alpha 0.5 ersetzt. Das ist eine bewusste und dokumentierte optische Abweichung. |
| Konstante Alpha-Überlagerung, False-Bold, Glow | HaikuControlLook, Leuchten bzw. Umriss von Labels | Glyph-Run mehrfach versetzt mit reduziertem Alpha zeichnen. Später `blurFilter`. |
| Affine Transformation | TabView (`RotateBy`, vertikale Tabs) | Rotation um 90° als Sonderfall über einen gedrehten Glyph-Run |
| Clipping über Pfade oder abgerundete Rechtecke | keiner der ControlLooks | — |
| Bézier-Pfade, konkave Polygone | keiner der ControlLooks | — |
| Radiale und konische Gradienten | keiner der ControlLooks | — |
| Offscreen-Ziele | `BBitmap` als Zeichenziel (Anwendungscode) | — |
| `CopyBits` | ColumnListView (Scrollen) | entfällt, die View wird neu gezeichnet |
| `DrawPicture` | 4 Stellen im Kit | entfällt, die DisplayList übernimmt diese Rolle |

## 17. Kompatibilitätsschicht für Haiku-Code (intern)

Die Schicht stellt eine BView-ähnliche Zeichen-API mit Zustand über der Canvas bereit. Sie dient dazu, die vorhandenen ControlLooks mechanisch und pixeltreu zu portieren.

- **K2: Nur intern.** Die Schicht ist **kein Teil der öffentlichen API**. Ihre Header liegen in einem internen Verzeichnis und werden nicht installiert. Anwendungen und neue Looks verwenden ausschließlich die Canvas (Abschnitt 6) bzw. `DrawContext` (18.3).
- **K3: Befristet.** Die Schicht bleibt bestehen, solange ein Look als 1:1-Port vorliegt, derzeit der BeOS-Look (23.3). Sind alle Looks gegen die Canvas neu gefasst, entfällt sie.
- Haikus Konstanten (`B_FOCUSED`, `B_LEFT_BORDER`, `B_OP_*` …) existieren nur innerhalb dieser Schicht und werden dort auf die Typen aus L5 (18.3) abgebildet.

### 17.1 Umrechnungen

```cpp
inline Rect  FromBRect(BRect r)   { return {r.left, r.top, r.right + 1, r.bottom + 1}; }
inline Point PixelCenter(BPoint p){ return {p.x + 0.5f, p.y + 0.5f}; }
```

### 17.2 Abbildung der BView-Aufrufe

| Haiku | Canvas |
|---|---|
| `PushState()`/`PopState()` | `Save()`/`Restore()`, dazu Sichern des Zustands der Schicht (High/Low-Farbe, Stiftbreite, Linienmodus) |
| `SetHighColor`/`SetLowColor` | Zustand der Schicht, wird in jedes `Paint` übernommen |
| `ClipToRect(r)` | `ClipRect(FromBRect(r))` |
| `ClipToInverseRect(r)` | `ClipOutRect(FromBRect(r))` |
| `ConstrainClippingRegion(&region)` | `ClipRect` je Rechteck der Region. Dafür hat die Schicht einen Hilfsweg, der die Clip-Region direkt setzt. |
| `FillRect(r[, gradient])` | `FillRect(FromBRect(r), paint)` |
| `FillRegion(&region)` | `FillRect` je Rechteck der Region |
| `StrokeRect(r)` | `StrokeRect(FromBRect(r), {strokeWidth = penSize})` |
| `FillEllipse(r[, gradient])` | `FillEllipse(FromBRect(r), paint)` |
| `StrokeEllipse(r)` | `StrokeArc(FromBRect(r), 0, 360, paint)` |
| `StrokeArc(r, start, span)` | `StrokeArc(FromBRect(r), −start, −span, paint)`. In Haiku zeigen positive Winkel gegen den Uhrzeigersinn. |
| `StrokeRoundRect(r, rx, ry)` | `StrokeRRect({FromBRect(r), {rx, rx, rx, rx}}, paint)` (im Code ist `rx == ry`) |
| `FillTriangle`/`StrokeTriangle` | `FillConvexPolygon`/`StrokePolyline(closed = true)` mit `PixelCenter` |
| `StrokeLine(a, b)` | `DrawLines({{PixelCenter(a), PixelCenter(b), high}}, penSize, Cap::Square)` |
| `BeginLineArray`/`AddLine(a, b, c)`/`EndLineArray` | wird gesammelt und als ein `DrawLines(…, 1, Cap::Square)` ausgegeben |
| `StrokeShape`/`FillShape` (nur Linien) | `StrokePolyline`/`FillConvexPolygon`. Shapes mit Kurven führen zu einem Fehler. |
| `MovePenTo(p)` vor Shape-Operationen | `Save(); Translate(p); … Restore()` |
| `SetLineMode(cap, join)` | Zustand der Schicht, wird ins `Paint` übernommen |
| `SetDrawingMode(B_OP_COPY/OVER/ALPHA)` | wird ignoriert (alles SrcOver) |
| `SetDrawingMode(B_OP_INVERT)` | nicht unterstützt (Abschnitt 16), Warnung in Debug-Builds |
| `DrawBitmap(bmp, where)` | `DrawImage(handle, bmpBounds, {where, size})` |
| `DrawString(s, where)` | Text-Subsystem formt `s` zu einem GlyphRun, dann `DrawGlyphRun(run, where)` |
| `B_SOLID_LOW` | `Paint.color = lowColor` |

## 18. ControlLook-Schnittstelle und Austauschbarkeit der Looks

### 18.1 Grundsatz

Ein **Look** ist eine Implementierung der Schnittstelle `ControlLook` (18.3). Er legt fest, wie die Standard-Controls gezeichnet werden und welche Metriken sie haben. Wie in Haiku (`be_control_look`) delegieren die Controls das Zeichnen vollständig an den Look und bringen selbst nur ihre Semantik ein: Zustand, Label, Icon.

Look und Renderer sind **orthogonal**:

- **L1:** Ein Look DARF ausschließlich über `DrawContext` (18.3) zeichnen und DARF KEIN Backend direkt ansprechen (vgl. R1/R2).
- **L2:** Ein Look, der nur das Core-Profil benutzt, MUSS auf jedem konformen Backend lauffähig sein.
- **L3:** Palette (Systemfarben) und Schrift sind weitere unabhängige Achsen. Ein Look DARF keine festen Farben voraussetzen, sondern leitet alle Farben aus der Basisfarbe ab, die ihm übergeben wird, sowie aus der Palette. Ausnahmen sind reine Schwarz-Weiß-Konstanten wie bei Haikus `kWhite`/`kBlack`.

Damit ergibt sich die folgende Matrix, die die Phasen aus 23.3 abdecken:

|  | Skia (CPU-Raster) | Blend2D | Skia (GPU) |
|---|---|---|---|
| BeOS-Look (`be`) | ab Phase 1 | ab Phase 2 | ab Phase G |
| Haiku-Look (`haiku`) | ab Phase 3 | ab Phase 3 | ab Phase 3 und G |
| Flat-Look (`flat`) | ab Phase 4b | ab Phase 4b | ab Phase 4b und G |

Die Spalte Blend2D gilt nur, solange das Backend erhalten bleibt (Abschnitt 24, Punkt 10).

### 18.2 Zuständigkeiten

| Ein Look bestimmt | Ein Look bestimmt nicht |
|---|---|
| wie Rahmen, Hintergründe, Buttons, Checkboxen, Radio-Buttons, Slider, Scrollbars, Tabs, Menüs, Pfeile und Labels gezeichnet werden | das Layout-Verfahren (Constraint-Engine) |
| **Metriken:** Abstände, Rahmen- und Hintergrund-Insets, Breite der Scrollbars, Icon-Größen | die Schrift (Text-Subsystem) |
| wie der Zustand eines Controls auf Statusflags abgebildet wird (`Flags`) | die Palette |
| abgeleitete Farben (z. B. `SliderBarColor`, Schattierungen) | Fensterrahmen (offener Punkt 9 in Abschnitt 24) |
| | Views, die selbst zeichnen, und Views der Anwendung (18.8) |

**Metriken haben Folgen für das Layout.** Im Haiku Interface Kit fragen 30 Quelldateien das ControlLook ab (Abschnitt 22). Be- und Haiku-Look haben identische Abstandsformeln (`be_plain_font->Size() / 2` bzw. `* 0.85`), aber eigene Rahmen-Insets (`BeControlLook::GetFrameInsets`). Ein Wechsel des Looks MUSS deshalb ein neues Layout auslösen (18.5).

### 18.3 Schnittstelle

```cpp
namespace ngui {

struct DrawContext {
    gfx::Canvas&      canvas;     // Core-Profil (Abschnitt 6)
    const gfx::Caps&  caps;       // Fähigkeiten des aktiven Backends
    const Palette&    palette;    // Systemfarben (Gegenstück zu ui_color)
    const TextSystem& text;       // Metriken, Shaping → GlyphRun
    float             scale;      // nur informativ (z. B. für die Wahl der Icon-Größe)
};

struct Insets { float left, top, right, bottom; };

using ControlStateFlags = Flags<ControlState>;  // Bitmenge über enum class (L5)
using BorderFlags       = Flags<Border>;

enum class ConformanceLevel : uint8_t { C1, C2, Extended };

struct LookInfo {
    std::string_view  id;           // stabiler Schlüssel, z. B. "be", "haiku", "flat"
    std::string_view  displayName;
    uint32_t          version;
    ConformanceLevel  requires;     // benötigte Konformitätsstufe (21.4)
    gfx::Caps         extendedCaps; // nur bei Extended: benötigte Fähigkeiten
};

class ControlLook {
public:
    virtual ~ControlLook() = default;

    virtual LookInfo Info() const = 0;

    // Metriken: wirken auf das Layout
    virtual Alignment DefaultLabelAlignment() const = 0;
    virtual float     DefaultLabelSpacing(const TextSystem&) const = 0;
    virtual float     DefaultItemSpacing(const TextSystem&) const = 0;
    virtual Insets    FrameInsets(FrameType, ControlStateFlags flags) const = 0;
    virtual Insets    BackgroundInsets(BackgroundType, ControlStateFlags flags) const = 0;
    virtual float     ScrollBarWidth(const TextSystem&, Orientation) const = 0;
    virtual Color     SliderBarColor(const Palette&, Color base) const = 0;
    virtual ControlStateFlags Flags(const BControl&) const = 0;

    // Zeichnen: wirkt nur auf die DisplayList
    virtual void DrawButtonFrame(DrawContext&, gfx::Rect& rect, Color base,
                                 Color background, ControlStateFlags flags,
                                 BorderFlags borders) const = 0;
    virtual void DrawButtonBackground(DrawContext&, gfx::Rect& rect, Color base,
                                      ControlStateFlags flags, BorderFlags borders,
                                      Orientation) const = 0;
    // … die übrigen Draw-Methoden aus Haikus BControlLook in derselben Form
};

} // namespace ngui
```

**Verbindliche Regeln:**

- **L4: Unveränderlichkeit und Thread-Sicherheit.** Alle Methoden sind `const`. Ein Look-Objekt DARF nach seiner Konstruktion keinen veränderlichen Zustand haben, da mehrere Fenster-Threads gleichzeitig damit zeichnen. Caches sind nur zulässig, wenn sie intern synchronisiert und frei von Seiteneffekten auf das Ergebnis sind.
  *Folge für die Ports:* `HaikuControlLook` und `BeControlLook` haben veränderliche Member (`fCachedOutline`, `fCachedWorkspace`, `fBackgroundInfo`). Sie dienen nur der Darstellung von Labels auf dem Tracker-Desktop. Diese Logik entfällt beim Port ersatzlos, denn es gibt keinen Haiku-Desktop.
- **L5: Gemeinsames Vokabular nach Haiku-Vorbild.** Übernommen wird die **Bedeutung** von Haikus Statusflags (fokussiert, angeklickt, aktiviert, Hover, deaktiviert, Default-Button, Umriss ignorieren, teilweise aktiviert, flach, ungültig, ist Control, Rahmen überblenden), Rand-Masken, Rahmen- und Hintergrundtypen, Pfeilrichtungen und Knob-Typen. **Namen und Werte sind frei** (Grundsatz K1). Vorgesehen sind typsichere Aufzählungen mit Bitmengen, z. B. `enum class ControlState : uint32_t { Focused = 1u << 0, Clicked = 1u << 1, … }` als `ControlStateFlags` und `enum class Border : uint8_t { Left, Right, Top, Bottom }` als `BorderFlags`. Dieses Vokabular ist allen Looks gemeinsam. Die interne Kompatibilitätsschicht bildet Haikus Konstanten darauf ab (Abschnitt 17).
- **L6: Rechteck per Referenz.** Wie in Haiku verkleinern `Draw…Frame`-Methoden das übergebene Rechteck um den verbrauchten Rand, sodass der folgende `Draw…Background`-Aufruf im Rest zeichnet. Rechtecke sind gemäß 4.1 halboffen.
- **L7: Zwei Implementierungsweisen, eine Außensicht.** 1:1-Ports erzeugen in jeder Methode einen Adapter der Kompatibilitätsschicht (`CompatView v(ctx.canvas, ctx.palette, ctx.text)`) und rechnen an der Schnittstelle mit `FromBRect` um (Abschnitt 17). Neue Looks zeichnen direkt auf `ctx.canvas`. Für die Controls ist der Unterschied nicht sichtbar.
- **L8: Neue Methoden** kommen nur am Ende der Klasse hinzu und erhalten eine Standardimplementierung in der Basisklasse, z. B. als Rückfall auf eine verwandte Methode. Die Schnittstellenversion (18.6) wird dabei erhöht.

### 18.4 Registry und Auswahl

```cpp
class LookRegistry {
public:
    using Factory = std::function<std::shared_ptr<const ControlLook>()>;

    void Register(std::string_view id, Factory);           // eingebaute Looks und Plugins
    std::vector<LookInfo> Available(const gfx::Caps&) const; // nur kompatible Looks

    std::shared_ptr<const ControlLook> Current() const;     // atomar lesbar
    Status SetCurrent(std::string_view id);                 // löst 18.5 aus
};
```

- Beim Start wird der Look aus der Einstellung `ngui.controlLook` gewählt (Voreinstellung: `be`).
- **L9: Prüfung der Kompatibilität.** Bevor ein Look aktiviert wird, MUSS die Registry `LookInfo::requires` und `extendedCaps` gegen die `Caps` des aktiven Backends prüfen. Bei Unverträglichkeit wird `SetCurrent` abgelehnt. Beim Start wird stattdessen der Rückfall-Look `be` (Stufe C1, auf jedem Backend lauffähig) gewählt und eine Warnung protokolliert.
- Ein Look mit Stufe `Extended` DARF alternativ selbst über `ctx.caps` auf Core-Operationen ausweichen. Er meldet dann `requires = C2` (bzw. C1) und gilt als kompatibel.

### 18.5 Wechsel zur Laufzeit und Look pro Fenster

Haiku kann den Look zur Laufzeit nicht wechseln. Der Grund steht als Kommentar in `_fini_interface_kit_()`: Andere Threads könnten noch Methoden des alten Objekts ausführen. Dieses Toolkit löst das über Referenzzählung:

1. Der aktuelle Look wird als `std::shared_ptr<const ControlLook>` gehalten und atomar ersetzt.
2. **L10:** Jeder Fenster-Looper übernimmt zu **Beginn jedes Layout- und Zeichenzyklus** einen eigenen Verweis (Snapshot) auf den für ihn gültigen Look. Während des Zyklus DARF der Look nicht neu gelesen werden.
3. `SetCurrent()` ersetzt den globalen Verweis und schickt allen Fenstern die Nachricht `LOOK_CHANGED`.
4. **L11:** Bei `LOOK_CHANGED` MUSS ein Fenster `InvalidateLayout(true)` aufrufen (wegen der Metriken, 18.2) und alle Views invalidieren. Die DisplayLists der Views werden dann mit dem neuen Look neu aufgezeichnet.
5. Das alte Objekt wird automatisch zerstört, sobald der letzte Zyklus seinen Verweis freigibt.

**Look pro Fenster:** Ein Fenster KANN einen eigenen Look setzen (`BWindow::SetControlLook(id)`), der den globalen überdeckt. `LOOK_CHANGED` betrifft dann nur Fenster ohne eigenen Look. Anwendungsfall: eine Live-Vorschau in einem Einstellungsdialog.

**Erlaubte Nebenwirkung:** Ändern sich die Mindestgrößen, darf sich ein Fenster nach dem Wechsel in der Größe anpassen. Dabei gelten dieselben Regeln wie bei jeder anderen Änderung des Layouts.

### 18.6 Auslieferung

**Eingebaute Looks** (Phasen 1–4b): `be`, `haiku` und `flat` werden in die Toolkit-Bibliothek einkompiliert und beim Start registriert.

**Looks als Plugins** (optional, Phase 4c): eine Shared Library mit einer C-Fabrikfunktion, analog zu Haikus `instantiate_control_look`:

```cpp
extern "C" ngui::ControlLook* ngui_create_control_look(uint32_t interfaceVersion);
extern "C" void               ngui_destroy_control_look(ngui::ControlLook*);
extern "C" const char*        ngui_look_plugin_build_key();   // LookPluginBuildKey()
```

- Die Plugins werden aus konfigurierten Verzeichnissen geladen (`dlopen` bzw. `LoadLibrary`).
- **L12:** Ein Plugin MUSS `nullptr` zurückgeben, wenn `interfaceVersion` nicht zu seiner Version passt. Der Lader verwirft es dann mit einer Warnung.
- Weil die Schnittstelle aus C++-vtables besteht, MÜSSEN Plugin und Toolkit mit demselben Compiler, derselben Standardbibliothek und derselben Hauptversion des Toolkits gebaut sein. Eine stabile C-ABI für Looks ist ausdrücklich **kein** Ziel dieser Spezifikation. Der Lader prüft das über einen Build-Schlüssel (Version des Toolkits, Compiler und Hauptversion, Standardbibliothek, bei MSVC Laufzeitbibliothek und Iterator-Debug-Stufe), den beide Seiten mit `LookPluginBuildKey()` bilden; das Makro `NGUI_LOOK_PLUGIN()` definiert die drei Funktionen.
- **Umgesetzt (2026-11-26):** `libngui` ist statisch und positionsunabhängig gebaut, ein Plugin bindet sie statisch ein und hat damit eine eigene Kopie des Codes. Es zeichnet deshalb nur über seinen `DrawContext` (L3), nicht über globale Objekte des Toolkits. Geladen wird nur über die API oder aus den Verzeichnissen in `NGUI_LOOK_PATH`, nicht aus einem Standardordner.
- Das Plugin wird erst entladen, wenn sein letztes Look-Objekt zerstört ist. Dafür sorgt ein Deleter im `shared_ptr`, der die Bibliothek referenziert.

### 18.7 Tests

- Golden-Image-Tests laufen als **Matrix aus Look und Backend**, jede Kombination hat ihren eigenen Referenzsatz (21.3).
- **Wechseltest:** Nach `SetCurrent(b)` in einem laufenden Fenster MUSS das gerenderte Bild identisch sein mit dem Bild nach einem Neustart mit Look `b`.
- **Nebenläufigkeit:** Ein Stresstest wechselt den Look in schneller Folge, während mehrere Fenster-Threads zeichnen. Erwartet werden keine Abstürze und keine Fehler im ThreadSanitizer.
- **Kompatibilität:** Ein Test-Backend mit absichtlich reduzierten `Caps` prüft die Regel L9.

### 18.8 Grenzen

- **Views, die selbst zeichnen** (`ListView`, `TextView`, Views der Anwendung), folgen dem Look nur über die Palette. Damit Hintergründe von Auswahl und Listeneinträgen einheitlich aussehen, SOLLEN solche Views entsprechende Look-Methoden nutzen, z. B. `DrawMenuItemBackground` oder eine noch zu ergänzende Methode `DrawListItemBackground`.
- **Fensterrahmen** wechseln nur mit, wenn clientseitige Decorations gebaut werden (Abschnitt 24, Punkt 9).
- **Pixeltreue gegenüber Haiku** gilt nur für die 1:1-Ports. Neue Looks haben keine Haiku-Referenz.

## 19. Export und Screenshots

### 19.1 Screenshots

Es gibt drei Ebenen:

| Ebene | Verfahren | Eigenschaften |
|---|---|---|
| **a) Neu rendern** (Standard) | Die aktuelle DisplayList eines Fensters oder einer View wird in eine Offscreen-CPU-Surface gerendert (`CreateOffscreenSurface`, Abschnitt 12). | auch bei verdeckten oder minimierten Fenstern, beliebige Skalierung, wählbares Backend, deterministisch, ohne GPU-Readback. Ohne die Fensterrahmen des Host-Systems. |
| **b) Angezeigtes Bild** (`FrameCapture`) | Das zuletzt präsentierte Bild wird übernommen: bei CPU-Raster direkt aus dem Speicher, bei GPU über Readback (`SkSurface::readPixels`). | genau das, was der Nutzer sieht, einschließlich GPU-Abweichungen. Nur in der aktuellen Größe, GPU-Readback ist langsam. |
| **c) Ganzer Bildschirm** | Mittel des Betriebssystems (Screenshot-Portal von xdg-desktop-portal, X11, Windows, macOS) | **nicht Gegenstand dieser Spezifikation.** Unter Wayland nur mit Zustimmung des Nutzers. |

```cpp
struct SnapshotOptions {
    std::optional<Rect> area;              // Ausschnitt in Fensterkoordinaten, leer = ganzes Fenster
    float               scale = 0.0f;      // 0 = Skalierung des Fensters
    std::string_view    backend = {};      // leer = aktives Render-Backend im CPU-Raster-Modus
    bool                includePopups = false; // Menüs und Tooltips des Fensters einblenden
};

std::future<Image> BWindow::Snapshot(const SnapshotOptions& = {});  // aus jedem Thread
Image              BView::Snapshot(const SnapshotOptions& = {});    // nur im Looper-Thread des Fensters
std::future<Image> BWindow::CaptureFrame();                         // Ebene b
Status             EncodePng(const Image&, OutputStream&);
```

**Regeln:**

- **S1:** Vor der Aufnahme MUSS der Looper des Fensters ausstehendes Layout und ausstehende Invalidierungen abarbeiten. Ein Snapshot zeigt also immer einen konsistenten Stand.
- **S2:** Ebene a rendert die DisplayList unverändert. Das Ergebnis MUSS bitgleich sein mit dem, was dasselbe Backend im CPU-Raster-Modus bei gleicher Skalierung ins Fenster zeichnen würde.
- **S3:** Bei `includePopups` werden die DisplayLists offener Popup-Fenster (Menüs, Tooltips) entsprechend ihrer Position auf dem Bildschirm darübergelegt. Der Mauszeiger ist nie enthalten.
- **S4:** PNG wird über den Encoder des Backends erzeugt (`SkPngEncoder` bzw. `BLImageCodec`). Ohne beide wird auf eine kleine eingebettete Bibliothek zurückgegriffen (z. B. `stb_image_write`).

### 19.2 Export-Backends

```cpp
struct ExportOptions {
    enum class TextMode : uint8_t { Paths, Text } text = TextMode::Paths;
    bool  embedImages = true;     // Bilder als data:-URI einbetten
    bool  pretty      = false;    // Einrückung und Zeilenumbrüche
    float scale       = 1.0f;     // Faktor für width/height, viewBox bleibt logisch
};

class ExportBackend {
public:
    virtual ~ExportBackend() = default;
    virtual Caps   Capabilities() const = 0;
    virtual Status Export(const DisplayList&, const Rect& bounds,
                          OutputStream&, const ExportOptions&) = 0;
};
```

- Export-Backends verwenden dieselbe DisplayList wie die Render-Backends. Es gelten die Regeln R1–R3.
- Ein Export-Backend MUSS das Core-Profil vollständig unterstützen.

### 19.3 SVG-Export-Backend

| DisplayList | SVG |
|---|---|
| `FillRect`, `StrokeRect`, achsparallele `DrawLines` | `<rect>` mit `shape-rendering="crispEdges"` (Regel A1) |
| `FillRRect` (Radius pro Ecke), `FillRing` | `<path>`, der Ring mit `fill-rule="evenodd"` |
| `FillEllipse`, `StrokeArc`, `FillConvexPolygon`, `StrokePolyline` | `<ellipse>`, `<path>` mit Bogen, `<polygon>`, `<polyline>` mit `stroke-linecap` und `stroke-linejoin` |
| `StrokeRRect` | `<path>`, um `w/2` eingerückt (der Rahmen liegt innen, 6.1) |
| linearer Gradient | `<linearGradient gradientUnits="userSpaceOnUse">` in `<defs>` |
| Clip-Region | `<clipPath>` aus Rechtecken in `<defs>`, dedupliziert entsprechend der Clip-Tabelle der DisplayList |
| `DrawImage` | `<image>` mit `data:image/png;base64,…`, mehrfach verwendete Bilder einmal in `<defs>` und dann über `<use>` |
| `DrawGlyphRun`, Modus `Paths` | Glyph-Umrisse als `<path>`. Die Umrisse liefert das Text-Subsystem (`TextSystem::GlyphOutline(FontId, glyph)`). |
| `DrawGlyphRun`, Modus `Text` | `<text>` mit `font-family`, `font-size` und x/y-Listen pro Zeichen. Nur möglich, wenn `GlyphRun::text` vorhanden ist und die Cluster 1:1 abbilden, sonst Rückfall auf `Paths`. |
| `DrawList` | `<g transform="translate(…)">` |

- **E1: Deterministische Ausgabe.** Gleiche DisplayList und gleiche Optionen MÜSSEN byte-gleiches SVG ergeben: stabile IDs (fortlaufend in der Reihenfolge des ersten Auftretens), Zahlen mit festem Format (höchstens 3 Nachkommastellen, ohne Nullen am Ende), feste Reihenfolge der Attribute.
- **E2:** `viewBox` entspricht `bounds` in logischen Pixeln, `width` und `height` sind `bounds × ExportOptions::scale`.
- **E3:** Farben werden als `#rrggbb` mit getrennter `fill-opacity` bzw. `stroke-opacity` ausgegeben. Damit bleiben sie im sRGB-kodierten Raum wie in 4.3.

### 19.4 Export über Skia (optional)

Das Skia-Backend kann eine DisplayList zusätzlich in `SkSVGCanvas` oder ein PDF-Dokument (`SkPDF::MakeDocument`) abspielen. Das ist die Grundlage für eine **Druckfunktion** nach dem Vorbild von Haikus `BPrintJob`. Für diesen Weg gelten E1–E3 **nicht**. Wie Text und Clipping im Ergebnis erscheinen, bestimmt Skia.

Umgesetzt ist der PDF-Weg (`ngui_skia`, vcpkg-Feature `pdf` von Skia), das eigene SVG-Backend aus 19.3 bleibt der Weg für SVG:

- **`gfx::PdfDocument`** schreibt Seiten beliebiger Größe in Punkten (1/72 Zoll, Ursprung oben links). `Draw(liste, bereich, position, punkteProPixel)` setzt einen Ausschnitt einer Liste an eine Stelle der Seite, mit `kPdfPointsPerPixel` = 0,75 (96 logische Pixel je Zoll) als Vorgabe.
- **`gfx::PdfExportBackend`** ist das `ExportBackend` für eine Seite der Größe `bounds × 0,75 × scale` Punkte, `gfx::ExportPdf()` der Kurzweg dazu.
- **Zeichnen:** Das Backend spielt die Liste als Vektoren ab. Die Kanten von Regel A1 liegen auf einem Raster von `PdfOptions::resolution` (576 dpi: 8 je Punkt, 6 je Pixel bei 96 dpi, damit Kanten auf ganzen Pixeln dort bleiben); Clip-Regionen werden als Pfade gesetzt, weil Regionen die Seitenmatrix nicht kennen. Glyph-Läufe werden zu Text in eingebetteten Schriften, Bilder werden verlustfrei (Deflate) eingebettet.
- **Determinismus:** Ohne Datum ergeben gleiche Listen gleiche Bytes. Seiten sind höchstens `kPdfMaxPageSize` = 14400 Punkte (200 Zoll) groß.
- **Aufzeichnen für den Export:** `BWindow::Record()` und `BView::Record()` zeichnen Fenster oder View wie `Snapshot()` neu auf (S1), auf dem Hintergrund des Panels, und geben die Liste zurück; ihre `Bounds()` sind der Bereich.

**Drucken.** `BPrintJob` folgt Haiku: `BeginJob()`, `DrawView(view, rechteck, wo)` bzw. `DrawList()`, `SpoolPage()`, `CommitJob()`, `CancelJob()`; `PaperRect()` und `PrintableRect()` in Punkten, `wo` relativ zum druckbaren Bereich; ein logisches Pixel ist `BPrintSettings::scale` Prozent von 0,75 Punkten. Die Einstellungen sind ein `BPrintSettings` (Drucker, Papier, Ausrichtung, Ränder, Maßstab, Kopien, Seitenbereich, Datei) statt einer `BMessage`; Fehler kommen als `std::expected` zurück. Die Seiten (DisplayLists mit Platzierung) gehen beim Commit an den `BPrintService` des Programms. Der Kern hat keinen; `SkiaPrintService` aus `ngui_skia` bietet:

- den **PDF-Drucker** `"PDF"`, der in `BPrintSettings::file` schreibt (überall, Vorgabe ohne andere Drucker);
- **CUPS** unter Linux, BSD und macOS: `libcups` wird erst bei Bedarf per `dlopen` geladen; Drucker sind die Ziele von `cupsGetDests` (Instanzen als `name/instanz`), ein Auftrag ist das PDF (`application/pdf`) mit der Option `copies`;
- **GDI** unter Windows: Drucker aus `EnumPrinters`, Seiten von Skia in der Druckerauflösung (höchstens 300 dpi) gerastert (`RenderPage()`) und mit `StretchDIBits` gedruckt; Papiergröße, Ausrichtung und Kopien im `DEVMODE`, eine Datei in `BPrintSettings::file` geht an `DOCINFO::lpszOutput`.

Die Dialoge sind eigene Fenster, modal zur Anwendung und asynchron wie `BAlert::Go()`: `ConfigPage(fertig)` zeigt „Page setup“ (Papier, Ausrichtung, Maßstab), `ConfigJob(fertig)` zeigt „Print“ (Drucker des Dienstes, Datei für einen Drucker, der Dateien schreibt, Kopien, alle Seiten oder ein Bereich). Beide kehren sofort mit dem Messenger des Fensters zurück; OK übernimmt die Einstellungen in den Job und ruft `fertig(true)` auf dem Thread des Fensters, Abbrechen oder Schließen ruft `fertig(false)`. `Settings()` gibt deshalb eine Kopie zurück.

### 19.5 Abgrenzung: SVG-Import

SVG-*Icons* in der Oberfläche anzeigen, also SVG lesen und ins Bild rendern, ist nicht Gegenstand dieses Abschnitts. Geeignet wären das SVG-Modul von Skia (`SkSVGDOM`) oder kleine Bibliotheken wie lunasvg bzw. ThorVG. Haikus HVIF-Icons wären ein eigenes Modul.

## 20. Test-Kit für Anwendungen

Das Test-Kit macht Anwendungen, die mit dem Toolkit gebaut sind, automatisiert testbar. Das Toolkit verwendet für seine eigenen Tests dieselben Bausteine (Abschnitt 21).

### 20.1 Ziele

- Tests laufen **ohne Bildschirm, Fenstersystem und GPU** (CI).
- Tests sind **deterministisch**. Wiederholte Läufe ergeben dieselben Ergebnisse.
- Eingaben laufen **über denselben Weg wie echte Eingaben**.
- Es gibt **keine Wartezeiten**. Synchronisiert wird über den Ruhezustand des Toolkits.

### 20.2 Headless-Plattform

- Eine Implementierung von `NativeWindow` ohne Fenstersystem. Fenster rendern in Offscreen-CPU-Surfaces. Ein virtueller Bildschirm hat konfigurierbare Größe und Skalierung.
- Eine **virtuelle Zwischenablage** und ein virtuelles Drag & Drop innerhalb des Prozesses
- Auswahl über die API (`BApplication::UsePlatform("headless")`) oder die Umgebungsvariable `NGUI_PLATFORM=headless`

### 20.3 Determinismus

`TestEnvironment` legt fest: Backend `skia` mit CPU-Raster, mitgelieferte Testschriften, Hinting aus, feste Skalierung, fester Look, feste Palette, feste Sprache.

- **T1: Zeitquelle.** Alles im Toolkit, was von der Zeit abhängt (Cursor-Blinken, Verzögerung von Tooltips, Wiederholrate von Tasten, Doppelklick-Erkennung, Animationen, Timer der Looper), MUSS die Schnittstelle `Clock` verwenden und DARF NICHT direkt die Systemuhr abfragen. Im Test-Modus steuert der Test die Uhr (`env.Clock().Advance(500ms)`).

### 20.4 Test-Treiber

```cpp
ngui::test::Driver d(app);
auto& w = d.Window("Einstellungen");
d.Click(w.Find<BButton>("ok"));
d.Type(w.Find<BTextControl>("name"), "Beispiel");
d.Key(Key::Tab);
d.WaitForIdle();
EXPECT_EQ(w.Find<BCheckBox>("autosave").Value(), BControlValue::On);
EXPECT_IMAGE_SNAPSHOT(w, "settings_after_input");
```

- **T2: Echter Eingabeweg.** Synthetische Eingaben (Maus, Tastatur, Mausrad, Drag & Drop) werden **unterhalb** der Verteilung der Ereignisse eingespeist, also so, wie die Plattformschicht echte Ereignisse einspeist. Bestimmung des Ziels, Fokus, Event-Masks und Mausverfolgung werden dadurch mitgetestet.
- **T3: Ruhezustand.** `WaitForIdle()` kehrt zurück, sobald *alle* folgenden Bedingungen erfüllt sind:
  1. Die Nachrichtenschlangen aller Looper sind leer.
  2. Es steht kein Layout aus.
  3. Jede invalidierte View hat eine neue DisplayList.
  4. Kein Timer ist zur aktuellen Zeit der `Clock` fällig.
  5. Der Render-Thread hat keine ausstehenden Frames.

  Bei einer Zeitüberschreitung schlägt der Test fehl, und es wird ein Diagnosebericht ausgegeben: welche Looper beschäftigt sind, welche Nachrichten anstehen, welche Views ungültig sind.
- **Abfragen:** `Find<T>(name)`, `FindByLabel`, `FindByPath("Fenster/Gruppe/ok")`, `FindAll<T>()`. Die Ergebnisse bieten `Frame()`, `IsVisible()`, `IsEnabled()`, `HasFocus()` und typabhängige Werte (`Value()`, `Text()`, `Selection()`).

### 20.5 Snapshots

| Art | Inhalt | Makro |
|---|---|---|
| **Layout** | Baum der Views mit Name, Typ, Frame, Sichtbarkeit, als Text | `EXPECT_LAYOUT_SNAPSHOT` |
| **DisplayList** | Ausgabe des Dump-Backends (Abschnitt 15) | `EXPECT_LIST_SNAPSHOT` |
| **SVG** | Ausgabe des SVG-Export-Backends (19.3) | `EXPECT_SVG_SNAPSHOT` |
| **Bild** | PNG über `BWindow::Snapshot` (19.1) mit den Toleranzen aus 21.2 | `EXPECT_IMAGE_SNAPSHOT` |

- Referenzdateien werden nach dem Muster `<testname>.<look>.<backend>.<scale>.<ext>` abgelegt.
- `--update-snapshots` (oder `NGUI_UPDATE_SNAPSHOTS=1`) übernimmt die aktuellen Ergebnisse als neue Referenzen.
- Schlägt ein Vergleich fehl, entsteht ein HTML-Bericht mit Referenz, Ergebnis und Differenzbild bzw. Text-Diff.

### 20.6 Layout-Lint

Eine Anwendung wird für eine konfigurierbare Matrix gerendert (Looks, Skalierungen 1.0/1.5/2.0, große Schrift, lange Übersetzungen über Pseudo-Lokalisierung) und automatisch geprüft auf:

- überlappende Geschwister-Views
- abgeschnittene Labels (Meldung des Text-Subsystems über Kürzung)
- Controls unterhalb ihrer Mindestgröße
- Inhalte außerhalb des Fensters oder ihrer Eltern-View
- Fokusreihenfolge, die nicht der Lesereihenfolge folgt (Warnung)

Befunde werden als Warnung oder Fehler gemeldet. Einzelne Views lassen sich davon ausnehmen.

### 20.7 Scripting und Fernsteuerung

Nach dem Vorbild von Haikus Scripting-Protokoll (`B_GET_PROPERTY`, `B_SET_PROPERTY`, `ResolveSpecifier`, `BPropertyInfo`, Werkzeug `hey`):

- **Nachrichten:** `GET`, `SET`, `EXECUTE`, `COUNT`, `CREATE`, `DELETE` auf Eigenschaften
- **Specifier:** direkt, Index, umgekehrter Index, Name, Bereich, verkettet (z. B. „`Value` of View `volume` of Window `Einstellungen`“)
- **Eigenschaftstabellen pro Klasse:** Das Toolkit liefert Standardeigenschaften. Window: `Title`, `Frame`, `Active`, `Hidden`. View: `Name`, `Frame`, `Hidden`, `Enabled`. Control: `Value`, `Label`. TextControl: `Text`. ListView: `Items`, `Selection`. Anwendungen können eigene ergänzen.
- **Fernsteuerung:** Über einen optionalen Transport per lokalem Socket (Unix-Domain-Socket bzw. Named Pipe) mit serialisierten Nachrichten lässt sich die App aus einem anderen Prozess steuern. Dazu gibt es ein Kommandozeilenwerkzeug analog zu `hey`.
- **T4: Sicherheit.** Der Transport ist standardmäßig **aus**. Er wird nur durch ausdrückliche Freischaltung aktiv (Build-Option oder Startparameter), lauscht nur lokal und DARF nie über das Netzwerk erreichbar sein. Der Socket ist nur für den Benutzer zugänglich, unter dem die App läuft.

### 20.8 Record & Replay

- **Aufzeichnung:** Eingabeereignisse auf Ebene der Plattform mit Zeitstempeln der `Clock`, dazu Anfangszustand (Fenstergrößen, Look, Skalierung, Einstellungen).
- **Wiedergabe:** im Headless-Betrieb mit virtueller Uhr. Ein Fehler aus der Praxis wird so zum Regressionstest.
- **T5: Datenschutz.** Aufzeichnungen können eingegebenen Text enthalten. Aufgezeichnet wird nur nach ausdrücklicher Zustimmung. Passwortfelder werden nie im Klartext aufgezeichnet.
  - **Zustimmung:** Die Anwendung ruft `BApplication::StartRecording(datei)` erst auf, nachdem der Benutzer zugestimmt hat. Wer die Anwendung mit `NGUI_RECORD=<datei>` startet, stimmt damit zu.
  - **Die Datei** ist nur für den Benutzer lesbar. `ngui-replay --dump <datei>` zeigt, was sie enthält.
- Wiedergaben sind nur so deterministisch wie die Anwendung selbst. Netzwerk, Dateisystem und Zufall muss die Anwendung bei Bedarf selbst abstrahieren.

### 20.9 Einbindung in Test-Frameworks

- Adapter für GoogleTest und Catch2 (Makros aus 20.5, Fixtures mit `TestEnvironment`)
- Labels für CTest, Berichte und Differenzbilder als CI-Artefakte

### 20.10 Grenzen

- Dialoge des Host-Systems (z. B. dessen Dateiauswahl) sind im Headless-Betrieb durch Platzhalter ersetzt und werden nicht getestet.
- Abweichungen auf echter GPU deckt nur Konformitätsstufe G ab (21.4), nicht das Test-Kit.
- Screenshots auf Ebene c (19.1) sind nicht enthalten.

## 21. Teststrategie und Konformität

### 21.1 Testebenen

1. **Einheitentests** für Typen, Region-Algebra und den Pfadaufbau in den Backends (z. B. Konturen von Ringen und von abgerundeten Rechtecken mit unterschiedlichen Radien).
2. **Snapshot-Tests der DisplayList:** Ein Control in einem definierten Zustand wird aufgezeichnet, mit dem Dump-Backend ausgegeben und mit einer eingecheckten Referenz verglichen. Diese Tests sind deterministisch und laufen ohne GPU.
3. **Golden-Image-Tests pro Backend:** Rendern in eine Offscreen-Surface und Vergleich mit Referenzbildern.
4. **Vergleich zwischen Backends:** Skia gegen Blend2D auf demselben Testkorpus, später zusätzlich Skia-CPU gegen Skia-GPU.
5. **Tests auf Ebene von Controls und Anwendungen** mit dem Test-Kit aus Abschnitt 20: Headless-Betrieb, Test-Treiber, Snapshots, Layout-Lint.

### 21.2 Konformitätskriterien (Startwerte, werden kalibriert)

| Primitivklasse | Kriterium |
|---|---|
| achsparallele Primitive ohne AA (A1/A3) | **pixelidentisch** |
| AA-Geometrie (Ellipsen, abgerundete Ecken, Bögen, Diagonalen) | ≤ 1 % abweichende Pixel, maximale Kanaldifferenz ≤ 32/255 |
| Gradienten | maximale Kanaldifferenz ≤ 3/255 |
| Text | ≤ 3 % abweichende Pixel im Bounding-Box-Bereich, Positionen der Grundlinie identisch |
| AA-Geometrie zweier Rasterer (Backend gegen Backend, CPU gegen GPU), auch viele Elemente in einem Bild | mittlere Kanaldifferenz ≤ 3/255, höchstens 0,5 % der Pixel mehr als 64/255 auseinander (`CompareRasterizers()`; kalibriert mit Skia gegen Blend2D und Skia auf Mesas llvmpipe gegen Skias CPU-Raster) |

### 21.3 Testkorpus

- Alle öffentlichen `Draw…`-Methoden des jeweiligen ControlLook in den Zuständen normal, hover, fokussiert, aktiviert, deaktiviert und Default-Button, jeweils bei `scale` 1.0 und 2.0.
- Referenzbilder für einen 1:1-Port: Bildschirmfotos aus einem echten Haiku mit **demselben aktiven ControlLook** (für Phase 1 also BeControlLook, auszuwählen in den Appearance-Einstellungen) und gleicher Font-Konfiguration. Bei AA-Geometrie gelten wegen der anderen Rasterung von Ellipsen die Toleranzen aus 21.2.
- **Matrix aus Look und Backend:** Jede Kombination aus registriertem Look und Backend hat einen eigenen Referenzsatz (Abschnitt 18.7).
- Ausnahme: der gedrückte Zustand im BeControlLook (Ersatz für Invert, Abschnitt 16) bekommt eine eigene, neu erzeugte Referenz.

### 21.4 Konformitätsstufen

Das Core-Profil wird schrittweise erfüllt. Ein Backend erreicht eine Stufe, wenn es alle Tests der Stufe besteht.

| Stufe | Umfang | Erreicht in | Abnahmekriterium |
|---|---|---|---|
| **C1** | alle Core-Operationen **ohne** Gradienten | Phase 1 (Skia, CPU-Raster) | Korpus BeControlLook. Achsparallele Primitive pixelidentisch zur Haiku-Referenz, Radio-Buttons (Ellipsen, Bögen) und Pfeildreiecke innerhalb der AA-Toleranz |
| **C1×** | C1 im Vergleich der Backends | Phase 2 | Skia- und Blend2D-Backend für **alle** achsparallelen Primitive des BeOS-Korpus pixelidentisch zueinander |
| **C2** | vollständiges Core-Profil einschließlich linearer Gradienten mit 2–4 Stopps | Phase 3 | Korpus HaikuControlLook auf allen vorhandenen Backends, Toleranzen aus 21.2 |
| **G** | die bis dahin erreichte Stufe auf einer GPU-Surface | Phase G | Abweichung gegenüber der CPU-Raster-Referenz desselben Backends innerhalb der Toleranzen aus 21.2 |

## 22. Datengrundlage

Gezählt wurden direkte `view->…`-Aufrufe:

| Aufruf | Haiku (3.950 Z.) | Flat (2.167 Z.) | Be (3.142 Z.) |
|---|---|---|---|
| `PushState`/`PopState` | 27/28 | 15/16 | 21/21 |
| `ClipToRect` | 23 | 15 | 13 |
| `ClipToInverseRect` | 15 | 15 | 0 |
| `FillRegion` | 2 | 2 | 2 |
| `SetHighColor`/`SetLowColor` | 69/1 | 36/1 | 75/5 |
| `FillRect` | 30 | 20 | 21 |
| `StrokeRect` | 1 | 1 | 8 |
| `BeginLineArray` + `AddLine` | 11 + 55 | 6 + 32 | 33 + 270 |
| `StrokeLine` | 21 | 9 | 19 |
| `FillEllipse` | 31 | 6 | 4 |
| `StrokeRoundRect` | 2 | 1 | 0 |
| `FillTriangle`/`StrokeTriangle` | 1/0 | 1/0 | 7/4 |
| `StrokeArc`/`StrokeEllipse` | 0/0 | 0/0 | 4/1 |
| `FillShape`/`StrokeShape` | 1/3 | 0/0 | 0/0 |
| `SetPenSize`/`SetLineMode` | 3/1 | 0/0 | 3/0 |
| `MovePenTo` | 3 | 0 | 1 |
| `DrawString` | 5 | 0 | 5 |
| `DrawBitmap` | 2 | 0 | 2 |
| `SetDrawingMode` | 25 | 13 | 17 |

Weitere Befunde:
- **Gradienten:** ausschließlich `BGradientLinear`, Stopps bei 0/255 oder 0/132/136/255. Pro Look: Haiku 27, Flat 8, **Be 0** (Zeilen mit `BGradientLinear`).
- **Vererbung:** `BeControlLook` leitet direkt von `BControlLook` ab und ist eigenständig. `FlatControlLook` leitet von `HaikuControlLook` ab.
- **AA-Geometrie im BeControlLook:** `FillEllipse` und `StrokeArc` nur bei Radio-Buttons (`BeControlLook.cpp:358–395`), `FillTriangle` bei Pfeilen und Markern (z. B. Zeilen 760–794, 1212, 1235). Alles andere ist achsparallel.
- **Drawing-Modes:** `B_OP_COPY` 12-mal, `B_OP_OVER` 15-mal, `B_OP_ALPHA` 15-mal, `B_OP_INVERT` 1-mal. `SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY)` 2-mal.
- **Transformationen:** keine in den ControlLooks.
- **Shapes:** nur Liniensegmente, keine Bézier-Kurven. Einmal runde Cap und runder Join.
- **Runde Ecken:** werden in `HaikuControlLook.cpp` (Zeilen 3283–3626, rund 345 Zeilen) über Clipping auf das Eckrechteck plus geschichtete `FillEllipse` emuliert. Weitere rund 200 Zeilen (3627–3825) entfallen auf `_DrawRoundBarCorner` und verwandte Helfer.
- **Übriges Interface Kit** (ohne ControlLooks, View, Picture*): `DrawString` 28, `ConstrainClippingRegion` 15, `DrawBitmap(Async)` 16, `InvertRect` 5, `CopyBits` 5, `DrawPicture` 4, `RotateBy` 1 (TabView).
- **ControlLook-Aufrufe außerhalb der Looks:** In 30 Quelldateien des Interface Kits wird `be_control_look` verwendet. Aufrufe, die nicht zeichnen: `DefaultLabelSpacing` 30, `Flags` 10, `DefaultItemSpacing` 10, `GetScrollBarWidth` 4, `ComposeIconSize` 3, `SliderBarColor` 2, `GetInsets` 2, `ComposeSpacing` 1.
- **Veränderlicher Zustand in Looks:** `HaikuControlLook` und `BeControlLook` haben die Member `fCachedOutline`, `fCachedWorkspace` und `fBackgroundInfo`. Sie werden nur für Labels auf dem Tracker-Desktop benutzt (`HaikuControlLook.cpp` ab Zeile 2250).
- **Scripting in Haiku:** In 29 Dateien des Interface Kits wird `ResolveSpecifier` implementiert. `src/kits/app/PropertyInfo.cpp` umfasst 564 Zeilen, das Kommandozeilenwerkzeug ist `src/bin/hey.cpp`. Mit `B_GET_PROPERTY`/`B_SET_PROPERTY` arbeiten u. a. `Window.cpp` (22 Stellen), `Control.cpp` (10) und `View.cpp` (6).
- **Screenshots in Haiku** entstehen auf der Server-Seite (`BScreen::GetBitmap` → `AS_READ_BITMAP`, `PrivateScreen.cpp`).

## 23. Umfangsabschätzung in Codezeilen

### 23.1 Vorbemerkungen

- Gezählt werden **Netto-Codezeilen** inklusive Headern, ohne Leer- und Kommentarzeilen.
- Unsicherheit **±50 %**. Die Werte sind Größenordnungen für die Planung, keine Zusagen.
- Externe Bibliotheken (Skia, Blend2D, FreeType, HarfBuzz) sind **nicht** mitgezählt.
- Wo Haiku-Code wiederverwendet werden kann, ist die Größe des Originals als Bezug angegeben. Haiku steht unter MIT-Lizenz, Copyright-Hinweise müssen erhalten bleiben.
- Alle Schätzungen setzen die modernisierte API nach Grundsatz K1 voraus.
- Grundgerüst, Controls, Text-Subsystem und Plattformschicht sind in einem eigenen Dokument abgeschätzt: [`toolkit-gesamtumfang.md`](toolkit-gesamtumfang.md).

### 23.2 Module

| # | Modul | Inhalt | Bezugsgröße | Schätzung (Zeilen) |
|---|---|---|---|---|
| M1 | **gfx-Grundtypen** | `Point`, `Size`, `Rect`, `RRect`, `Radii`, `Color`, `Line`, Hilfsfunktionen | Haiku `Rect.cpp`/`Point.cpp` + Header ≈ 830 | 500 – 800 |
| M2 | **Region** | Rechteck-Region mit ∩, ∪, \ und Banding | Haiku `RegionSupport.cpp` 1.641 + `Region.cpp` 557 + Header ≈ 450 → Port | 1.800 – 2.400 |
| M3 | **Canvas-Frontend** | API aus Abschnitt 6, Zustandsstapel, Translation, Clip-Berechnung, Validierung | — | 600 – 900 |
| M4 | **DisplayList** | Op-Stream, Clip-Tabelle, Ressourcen-Referenzen, Iterator, Serialisierung, Bounds | — | 900 – 1.400 |
| M5 | **Paint und Gradient** | `Paint`, `LinearGradient`, Farbberechnung, Umrechnung der Haiku-Offsets | Haiku `Gradient*.cpp` ≈ 720 (nur teilweise relevant) | 250 – 400 |
| M9 | **Skia-Backend (CPU-Raster)** | Player, Region → `SkRegion`, Paint-Konvertierung, Images, Text-Blobs, Raster-Surface | — | 900 – 1.500 |
| M9b | **Skia GPU-Surface** | Ganesh oder Graphite, Kontext- und Swapchain-Verwaltung, Flush | — | 200 – 400 |
| M18 | **Blend2D-Backend** | Player, Clip-Fan-out, Pfadaufbau für abgerundete Rechtecke und Ringe, Gradienten, Glyph-Runs | — | 700 – 1.100 |
| M10 | **Dump-Backend** | Text- und JSON-Ausgabe | — | 300 – 500 |
| M11 | **Backend-Infrastruktur** | `Backend`, `Caps`, `Surface`, CPU-Presenter, Registry für Handles, Auswahl des Backends, Pixeldichte | — | 400 – 700 |
| M12 | **Kompatibilitätsschicht (intern)** | BView-ähnliche API mit Zustand nach Abschnitt 17, Line-Arrays, Umrechnungen | — | 800 – 1.200 |
| M13-Be | **BeControlLook, 1:1-Port** | BeControlLook auf M12 übertragen, Ersatz für Invert | Original 3.142 | 2.900 – 3.200 |
| M13-Haiku | **HaikuControlLook, 1:1-Port** | HaikuControlLook auf M12 übertragen | Original 3.950 | 3.600 – 4.000 |
| M13-Haiku-V | **HaikuControlLook, vereinfacht** | Neufassung mit `FillRRect`/`FillRing`/`DrawLines`, ohne Eck-Emulation, ersetzt M13-Haiku | Original 3.950, davon ≈ 545 Eck-Helfer | 1.800 – 2.500 |
| M13-Flat | **FlatControlLook, Port** (optional) | überschreibt Teile des Haiku-Looks | Original 2.167 | 2.000 – 2.200 |
| M16 | **ControlLook-Schnittstelle** | `ControlLook`, `DrawContext`, `LookInfo`, `Insets`, Flags und Enums, Standardimplementierungen für L8 | Haiku `ControlLook.h` + `ControlLook.cpp` (rund 60 virtuelle Methoden) | 400 – 600 |
| M17a | **Look-Registry, Auswahl beim Start** | Registry, Einstellung, Prüfung nach L9, Rückfall | — | 150 – 250 |
| M17b | **Wechsel zur Laufzeit, Look pro Fenster** | atomarer Austausch, Snapshot pro Zyklus, `LOOK_CHANGED`, Relayout, Overlay pro Fenster | — | 200 – 350 |
| M17c | **Plugin-Lader** (optional) | `dlopen`/`LoadLibrary`, Versionsprüfung, Deleter mit Bindung an die Bibliothek | — | 150 – 300 |
| M19 | **Screenshot-API** | `BWindow::Snapshot`, `BView::Snapshot`, `CaptureFrame`, Popups einblenden, PNG-Kodierung | Haiku: serverseitig (`AS_READ_BITMAP`) | 200 – 400 |
| M20 | **SVG-Export-Backend** | `ExportBackend`, Abbildung nach 19.3, deterministische Ausgabe, Text als Pfade oder als Text | — | 600 – 1.000 |
| M21 | **Erweiterung von GlyphRun** | `text`, `clusters`, Aufzeichnung in der DisplayList, `GlyphOutline` im Text-Subsystem | — | 50 – 100 |
| M22 | **Headless-Plattform** | virtuelle Fenster und Bildschirm, virtuelle Zwischenablage, `Clock` mit Steuerung durch den Test | — | 300 – 600 |
| M23 | **Test-Treiber** | Einspeisung von Eingaben, `WaitForIdle` mit Diagnose, Abfragen, Snapshot-Makros, Adapter für Test-Frameworks | nutzt M14 | 800 – 1.500 |
| M24 | **Layout-Snapshot und Layout-Lint** | Textausgabe des View-Baums, Prüfungen nach 20.6, Matrix-Läufe | — | 300 – 600 |
| M25 | **Scripting-Protokoll** | Nachrichten, Specifier, Eigenschaftstabellen, Standardeigenschaften der Toolkit-Klassen | Haiku `PropertyInfo.cpp` 564 + `ResolveSpecifier` in 29 Dateien | 1.000 – 2.000 |
| M26 | **Fernsteuerung** | lokaler Transport, Freischaltung nach T4, Kommandozeilenwerkzeug | Haiku `hey.cpp` | 300 – 600 |
| M27 | **Record & Replay** | Aufzeichnung und Wiedergabe, Anfangszustand, Schutz von Passwortfeldern | — | 400 – 800 |
| M28 | **Export über Skia** (optional) | `SkSVGCanvas`, `SkPDF` nach 19.4 | — | 100 – 200 |
| M14 | **Test-Infrastruktur** | Offscreen-Rendering, Bildvergleich mit Toleranz, Berichte, Snapshot-Mechanik | — | 800 – 1.200 |
| M15 | **Tests** | Einheiten-, Snapshot- und Golden-Tests (Korpus nach 21.3) | — | 2.500 – 4.000 |

Nicht Teil dieser Spezifikation, aber zur Einordnung:

| # | Modul | Inhalt | Schätzung (Zeilen) |
|---|---|---|---|
| T1 | Text-Subsystem | Font-Verwaltung (FreeType), Shaping (HarfBuzz), BiDi, Zeilenumbruch nach UAX #14, Metriken, Kürzung (Port von Haikus `truncate_string`), Absatzlayout. Ausgearbeitet in `toolkit-gesamtumfang.md`. | 2.800 – 5.400 |
| B1 | Build-Integration von Skia und Blend2D | Paketbezug, Pinning, CMake-Ziele (Build-Skripte, kein Produktionscode) | 100 – 300 |

### 23.3 Summen nach Projektphase

| Phase | Ziel | Module | Zeilen (Produktionscode) | Tests (M14 + M15) | Konformität |
|---|---|---|---|---|---|
| **1** | Core-Profil mit **Skia (CPU-Raster)**, **BeOS-Look**, Grundausstattung des Test-Kits | M1–M5, M9, M10–M12, M13-Be, M16, M17a, M19, M21–M24 | **11.550 – 17.050** | 2.800 – 4.600 | C1 |
| **2** | Blend2D als zweites Backend (Nachweis), SVG-Export | + M18, M20 | **+ 1.300 – 2.100** | + 700 – 1.200 | C1× |
| **3** | **Haiku-Look**, Look zur Laufzeit wechseln | + M13-Haiku, M17b | **+ 3.800 – 4.350** | + 1.200 – 1.900 | C2 |
| **G** | Skia mit GPU-Surface (ab Phase 1 jederzeit) | + M9b | **+ 200 – 400** | + 200 – 400 | G |
| 4a (optional) | Haiku-Look vereinfachen | M13-Haiku-V ersetzt M13-Haiku | **netto − 1.100 bis − 2.200** | Referenzen neu erzeugen | C2 |
| 4b (optional) | Flat-Look | + M13-Flat (setzt Phase 3 voraus) | **+ 2.000 – 2.200** | + 500 – 800 | C2 |
| 4c (optional) | Looks als Plugins | + M17c | **+ 150 – 300** | + 100 – 200 | — |
| 4d (optional) | Scripting und Fernsteuerung | + M25, M26 | **+ 1.300 – 2.600** | + 300 – 600 | — |
| 4e (optional) | Record & Replay (setzt M22, M23 voraus) | + M27 | **+ 400 – 800** | + 200 – 400 | — |
| 4f (optional) | Export über Skia (SVG/PDF, Druck) | + M28 | **+ 100 – 200** | + 100 – 200 | — |

**Summe der Phasen 1–3:** etwa **16.650 – 23.500 Zeilen** Produktionscode und 4.700 – 7.700 Zeilen Tests, mit Phase G etwa 16.850 – 23.900 Zeilen.

**Gegenüber Entwurf 0.4** kommen in Phase 1 die Screenshot-API, die GlyphRun-Erweiterung, die Headless-Plattform, der Test-Treiber sowie Layout-Snapshot und Layout-Lint hinzu (M19, M21–M24, zusammen 1.650 – 3.200 Zeilen), in Phase 2 der SVG-Export (M20, 600 – 1.000). Scripting, Fernsteuerung, Record & Replay und der Export über Skia sind optional (4d–4f, zusammen 1.800 – 3.600).

**Gegenüber Entwurf 0.3** (ImGui zuerst) entfallen M6a, M6b, M7 und M8 mit zusammen 2.700 – 4.200 Zeilen (Anhang A).

Der Wechsel zur Laufzeit (M17b) liegt in Phase 3, weil es erst ab dort einen zweiten Look gibt. Die Schnittstelle (M16) und die Regel L4 (unveränderliche Look-Objekte) gelten aber **schon ab Phase 1**. Sonst müsste der BeOS-Port später noch einmal angefasst werden.

Die Kompatibilitätsschicht M12 bleibt erhalten, solange der BeOS-Look als 1:1-Port vorliegt (K3). Sie entfällt auch nach Phase 4a nicht.

Grob gesagt umfasst die austauschbare Canvas-Schicht ohne ControlLook, Text und Backends (M1–M5, M10–M12) etwa **5.550 – 8.300 Zeilen**, mit dem Skia-Backend (M9) etwa 6.450 – 9.800. Rund 1.800 – 2.400 davon (M2) sind ein weitgehend mechanischer Port aus Haiku.

### 23.4 Einordnung

- Es gibt **keinen Wegwerf-Code** mehr. Jedes Backend-Modul bleibt erhalten oder ist ausdrücklich als Prüfstein gekennzeichnet (M18).
- Die Austauschbarkeit selbst (M3, M4, M10, M11 und die Disziplin aus R1–R3) kostet etwa **2.200 – 3.500 Zeilen**, rund 19–21 % von Phase 1 (einschließlich der Grundausstattung des Test-Kits). Die Austauschbarkeit der Looks (M16, M17a–c) kommt mit 900 – 1.500 Zeilen dazu, davon 550 – 850 in Phase 1.
- **Phase 2 ist der eigentliche Nachweis der Architektur.** Wenn der BeOS-Look auf Skia und Blend2D für alle achsparallelen Primitive pixelidentisch ist (C1×), funktioniert die Austauschbarkeit. Ohne Blend2D fehlt dieser Nachweis. Dann bleiben nur die Regeln R1–R3 und die Snapshot-Tests mit dem Dump-Backend.
- **Das Test-Kit gehört bewusst schon in Phase 1.** Headless-Betrieb, `WaitForIdle`, Screenshots und Layout-Snapshots braucht bereits das Toolkit für seine eigenen Tests. Jede spätere Phase profitiert davon.
- Der größte einmalige Aufwand, der in keiner Zeilenzählung auftaucht, ist die **Integration von Skia** (Paket, Milestone, Compiler-Einstellungen, Binärgröße, siehe 13.5). Mit einem vorgebauten Paket ist das nach Einschätzung eine Sache von Tagen.
- Codezeilen sagen nur bedingt etwas über den Aufwand. Der Port des Region-Codes (M2) ist pro Zeile einfach. Aufwendiger sind der Pfadaufbau im Blend2D-Backend und die Pixelsemantik (A1/A3), die in allen Backends exakt stimmen muss.

## 24. Offene Punkte

1. **Lineares Blending:** gammanaiv wie Haiku (Voreinstellung) oder linear wie modernes Skia? Das beeinflusst die Optik der Gradienten.
2. **Fraktionale Skalierung:** Rundungsregel für Clips und A1-Primitive bei `scale = 1.25/1.5`. Mögliche Regeln: auf Geräte-Pixel runden oder logisch runden und dann skalieren.
3. **Subpixel-Positionierung von Glyphen:** Sind drei Varianten genug? Wie geht man mit Subpixel-AA (LCD) um? Vorschlag: im Core-Profil nur Graustufen-AA.
4. **Ersatz für Invert** in TextView und ColumnListView: Die Farbe für Auswahl-Hervorhebung muss im ControlLook definiert werden.
5. **GPU-Variante von Skia (Phase G):** Ganesh oder Graphite, GL, Vulkan oder Metal je nach Plattform.
6. **Bezug von Skia:** vcpkg, JetBrains `skia-pack` oder Aseprite-Builds? Welcher Milestone? Welche Module können entfallen (z. B. PDF, SVG, Skottie), um die Binärgröße zu senken?
7. **Grenze für den Clip-Fan-out** im Blend2D-Backend (14.2) muss gemessen werden.
8. **Glow und Umriss von Labels:** Gehört die Mehrfach-Emulation ins ControlLook, oder wird `blurFilter` früh ins Extended-Profil aufgenommen?
9. **Gesamteindruck jenseits des ControlLook:** Diese Punkte liegen außerhalb der Spezifikation, prägen aber, ob die Oberfläche „nach BeOS“ wirkt:
   - **Fensterrahmen:** Ohne eigenes Modul liefert sie das Host-System. Ein Titel-Tab im Be-Stil verlangt clientseitige Decorations. Grob geschätzt sind das 1.500 – 2.500 Zeilen, Bezug sind die Decorators in `src/add-ons/decorators/BeDecorator` bzw. `src/servers/app/decorator`.
   - **Systemfarben:** Haiku-Standardwerte aus `headers/private/interface/DefaultColors.h` übernehmen oder Farben vom Host lesen (z. B. Dark Mode)?
   - **Schrift:** Haiku verwendet standardmäßig Noto Sans. Ein Look nach BeOS R5 verlangt eine andere Schriftwahl im Text-Subsystem.
   - **Icons:** Für HVIF-Vektor-Icons ist ein eigener Renderer nötig. Bezug ist `src/libs/icon`, das Modul ist bisher nicht eingeplant.
   - **Views ohne ControlLook** (z. B. `BListView`) folgen dem Look nur über die Systemfarben.
10. **Blend2D nach Phase 2:** ~~behalten oder entfernen?~~ **Entschieden (2026-11-25): behalten.** Das Backend bleibt in der CI (Presets `skia` und `docs`) und sichert die Austauschbarkeit laufend ab (`CrossBackendTest`). Verwendet wird Blend2D 0.12 aus vcpkg, Lizenz zlib.
11. **Referenz-Backend aus Haikus `Painter`:** Der AGG-basierte `Painter` des app_server (`src/servers/app/drawing/Painter`) implementiert genau die Pixelsemantik von BView. Herausgelöst wäre er ein Test-Orakel, das bei 1:1-Ports pixelgleich zu Haiku ist. Er hängt allerdings stark an Klassen des app_server, Aufwand und Nutzen sind abzuwägen.
12. **Leistung des CPU-Raster-Pfads:** Ab welcher Fenstergröße bzw. welchem Anteil neu gezeichneter Fläche Phase G nötig wird, muss gemessen werden.

13. **Standard-Textmodus im SVG-Export:** `Paths` (originalgetreu) oder `Text` (auswählbar, kleiner, aber von den Schriften beim Betrachter abhängig)?
14. **Fernsteuerung:** Reicht die Freischaltung nach T4, oder soll zusätzlich jede Verbindung mit einem Token authentifiziert werden?
15. **Record & Replay:** ~~Welche weiteren Felder außer Passwortfeldern werden standardmäßig maskiert?~~ **Entschieden (2026-11-25):** keine weiteren. Maskiert werden Passwortfelder (`BTextView::HideTyping()`) und jede View, die die Anwendung mit `BView::SetInputPrivate()` als privat markiert, samt der Views darin. Alles andere wird im Klartext aufgezeichnet, damit eine Wiedergabe echten Text tippt. Maskiert heißt: Jedes getippte Zeichen wird zu „•“, der Tastencode zu 0. Tasten ohne Zeichen und Kürzel mit Command, Control oder Option bleiben erhalten.
16. **Ablage der Referenzdateien:** Die Matrix aus Look, Backend und Skalierung erzeugt viele Bilder. Git LFS, ein eigenes Referenz-Repository oder nur SVG- und Layout-Snapshots im Haupt-Repository?

---

## Anhang A: Verworfene Alternative – ImGui-Backend

In den Entwürfen 0.1 bis 0.3 war ein Backend auf Basis der Zeichenschicht von Dear ImGui (`ImDrawList`) als erstes Backend vorgesehen. Mit Entwurf 0.4 wurde es verworfen.

**Begründung:**

- Wegen der Anforderungen dieser Spezifikation hätte das Backend fast alles selbst bauen müssen: Glyph-Runs nach Glyph-ID, Clip-Regionen, abgerundete Rechtecke mit Radius pro Ecke, Ringe, Gradienten mit bis zu 4 Stopps, Caps und Joins. ImGui hätte nur die Tessellierung mit AA und die GPU-Anbindung beigetragen.
- Die dafür nötigen Module (A.6) umfassen 2.700 – 4.200 Zeilen, die nach dem Wechsel zu Skia weggefallen wären. Das Skia-Backend selbst hat 900 – 1.500 Zeilen.
- Die Vorteile von ImGui (einfacher Build, kleine Binärgröße, GPU vom ersten Tag an) wiegen diesen Mehraufwand nur auf, wenn ein Zielsystem ausdrücklich ohne Skia auskommen muss.

**Wann neu bewerten:** bei einem eingebetteten Zielsystem, einer geforderten sehr kleinen Binärgröße oder einer reinen GPU-Umgebung ohne Skia. Die Architektur (Abschnitt 3, Regeln R1–R3) erlaubt es, ein solches Backend jederzeit nachzurüsten.

Der folgende Entwurf bleibt zur Dokumentation erhalten und ist **nicht normativ**.


### A.1 Abbildung der Operationen

| Operation | Umsetzung |
|---|---|
| `FillRect` (ohne Gradient) | `ImDrawList::AddRectFilled`, Kanten auf Pixel gerundet, ohne AA |
| `FillRect` (mit Gradient) | Rechteck an den Stopps entlang der Gradientenachse in Bänder teilen, Farben pro Vertex. Bei achsparallelen Gradienten ist das exakt. |
| `FillRRect` | **eigene Tessellierung**, da ImGui nur einen einheitlichen Radius kennt: pro Ecke ein Viertelbogen mit adaptiver Segmentzahl, Fächer, AA-Saum |
| `FillRing` | eigene Tessellierung: Triangle-Strip zwischen äußerer und innerer Kontur mit gleicher Segmentierung |
| `FillEllipse` | `AddEllipseFilled` oder eigene Tessellierung, wenn ein Gradient gesetzt ist |
| `FillConvexPolygon` | `AddConvexPolyFilled` |
| `StrokeRect`/`StrokeRRect` | als `FillRing` mit eingerückter Innenkontur |
| `StrokePolyline` | `AddPolyline`. Für `Cap::Round` und `Join::Round` werden Kreisscheiben (`AddCircleFilled`, Radius `w/2`) an Enden und Ecken ergänzt. `Cap::Square` verlängert die Endsegmente um `w/2`. |
| `StrokeArc` | `PathArcTo` + `PathStroke` |
| `DrawLines` | achsparallel als `AddRectFilled` (exakt nach A3), sonst als `AddLine` |
| `DrawImage` | `AddImage` mit der Textur aus der Registry, `alpha` in der Vertexfarbe |
| `DrawGlyphRun` | Quads aus dem **eigenen Glyph-Atlas** (A.4) über `PrimReserve`/`PrimRectUV` |
| `DrawList` | rekursives Abspielen mit Offset |

### A.2 Clipping

- Für jede Operation mit einer Clip-Region aus *n* Rechtecken wird die Geometrie einmal erzeugt und *n*-mal mit unterschiedlichem `PushClipRect` ausgegeben. Die Vertex-Daten werden dabei wiederverwendet, indem nur die Index-Bereiche dupliziert werden.
- Ab einer konfigurierbaren Schwelle (Vorschlag: *n* > 8) SOLL stattdessen auf der CPU gegen die Region geschnitten werden.

### A.3 Gradienten auf beliebiger Geometrie

- Die Vertexfarbe ergibt sich aus der Projektion auf die Gradientenachse, analog zu `ImGui::ShadeVertsLinearColorGradientKeepAlpha`.
- Bei mehr als 2 Stopps MUSS die Geometrie an den Isolinien der Stopps geteilt werden, damit kein Dreieck einen Stopp überspannt. Andernfalls verschwimmt der Knick des Glossy-Gradienten.
- Alternativ KANN ein Fragment-Shader über `ImDrawCallback` eingesetzt werden. Das gilt dann als Erweiterung des GPU-Backends.

### A.4 Glyph-Atlas

- Er rastert Glyphen anhand von `(FontId, glyphIndex, sizePx × scale, subpixelX ∈ {0, ⅓, ⅔})` mit FreeType.
- Packing über `stb_rect_pack`, mehrere Atlasseiten, LRU-Verdrängung.
- Die Font-Engine von ImGui wird **nicht** verwendet.

### A.5 Was das Backend von ImGui nutzt

`ImDrawList`, `ImDrawData` und die Renderer-Backends (`imgui_impl_opengl3`, `imgui_impl_vulkan`, `imgui_impl_metal`). Nicht genutzt werden der Widget-Teil, das ID-System, `ImGuiIO`-Eingaben und `ImFontAtlas`. Ein `ImGuiContext` wird nur gebraucht, soweit `ImDrawListSharedData` ihn verlangt.

### A.6 Ehemals eingeplante Module

| ehem. Nr. | Modul | Schätzung (Zeilen) |
|---|---|---|
| M6a | Tessellierung: Geometrie | 600 – 900 |
| M6b | Tessellierung: Gradienten | 200 – 400 |
| M7 | ImGui-Backend | 1.200 – 1.800 |
| M8 | Glyph-Atlas | 700 – 1.100 |
| G1 | Anpassungen an den GPU-Backends von ImGui (nur Extended) | 300 – 600 |
