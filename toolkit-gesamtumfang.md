# Abschätzung: Gesamtumfang des Toolkits

| | |
|---|---|
| **Status** | Entwurf 0.1 |
| **Datum** | 2026-11-22 |
| **Bezug** | [`canvas-core-profil-spezifikation.md`](canvas-core-profil-spezifikation.md) (Entwurf 0.6). Die dort spezifizierten Teile (Canvas, Backends, Looks, Export, Test-Kit) werden hier nur in der Gesamtsumme aufgeführt. |
| **Grundlage** | Messung des Haiku-Quellstands (Commit `b1d497bb69`) mit `tokei`, reine Codezeilen ohne Leer- und Kommentarzeilen |
| **Herkunft** | Mit KI-Unterstützung erstellt. Nicht als Beitrag zum Haiku-Projekt geeignet. |

## 1. Gegenstand

Abgeschätzt werden die Teile des Toolkits, die die Spezifikation nicht abdeckt:

1. **Grundgerüst:** Nachrichtensystem, Looper, Application, View, Window, Layout-Engine
2. **Controls und Widgets:** Container, Controls, Menüs, Listen, TextView, Dialoge
3. **Text-Subsystem:** Fonts, Shaping, BiDi, Zeilenumbruch, Absatzlayout
4. **Plattformschicht:** auf Basis von SDL3, mit nativen Ergänzungen

## 2. Annahmen

- **Modernisierte API nach Grundsatz K1 der Spezifikation:** keine Quell- oder Binärkompatibilität zu BeAPI/Haiku, halboffene Rechtecke, eigener Namensraum, `std::expected`, Smart Pointer, über Fenstergrenzen hinweg nur Nachrichten.
- **Konzeptionelle Nähe zu Haiku:** gleiche Aufteilung der Klassen, gleiche Hooks und, wo sinnvoll, gleiche Methodennamen. Haiku-Code wird **Methode für Methode übertragen** und dabei angepasst, nicht wörtlich kopiert. Mechanische Änderungen (Typen, Namen) laufen per Skript oder clang-tidy.
- **Entfällt gegenüber Haiku:** Protokoll zum app_server, Binärkompatibilität zu BeOS (`_Reserved*`, `Perform`), Archivierung und Replicants, `CopyBits`. Gemessen machen Archivierung, Scripting und ABI-Funktionen in View, Window, Controls, Containern, Listen, Menüs und TextView **rund 3.400 von 47.122 Zeilen (≈ 7 %)** aus. Mit den Archiv-Konstruktoren und den Umwegen über `BString`, `BList` und `status_t` sind es grob 8–10 %. Scripting kommt gebündelt als Modul M25 der Spezifikation wieder.
- **Die Zeichen-API ähnlich `BView`** steckt in der internen Kompatibilitätsschicht (M12 der Spezifikation) und wird hier nicht noch einmal gezählt.
- **Die Plattformschicht baut auf SDL3 auf** (eigenständiges Projekt, zlib-Lizenz, festgeschriebene Version 3.2.x). SDL3 wird ausschließlich in der Plattformschicht verwendet.
- **Gezählt** werden Netto-Codezeilen einschließlich Header, mit einer Unsicherheit von **±50 %**. Externe Bibliotheken (SDL3, Skia, FreeType, HarfBuzz, ICU bzw. Alternativen) sind nicht enthalten.

## 3. Ausgangsbasis in Haiku

| Bereich | Haiku-Code (tokei) |
|---|---|
| Looper, Handler, Message, Messenger, MessageRunner, Filter, Queue, Invoker | 6.995 |
| Application, Clipboard, Cursor, PropertyInfo | 2.348 |
| Window | 3.266 |
| View (mit `ViewPrivate.h`) | 6.002 |
| Layout-Klassen, Builder, `layouter/` | 9.904 |
| Container-Views (Box, ScrollView/-Bar, TabView, SplitView …) | 4.710 |
| Controls (18 Dateien, ohne Header) | 10.017 |
| Menüs | 6.494 |
| ListView, OutlineListView, Items | 2.740 |
| ColumnListView | 4.592 |
| TextView mit Hilfsklassen | 6.454 |
| Alert, AboutWindow, ToolTip, InterfaceDefs | 2.991 |
| Geometrie und Grafiktypen (Rect, Font, Bitmap, Screen, Shape …) | 4.388 |
| *wird nicht portiert:* Picture, Shelf, Dragger, Replicants, WindowStack, Deskbar, PrintJob, DecorInfo, Input, Icon | *6.989* |
| **Summe** | **77.890** |

## 4. Grundgerüst

| Modul | Inhalt | Bezug in Haiku | Vorgehen | Zeilen |
|---|---|---|---|---|
| G1 Locker, Autolock | rekursiver Lock mit Besitzer-Thread | Support Kit | neu, klein | 150 – 300 |
| G2 **Message** | typisierte Felder, Antworten, Serialisierung (für Fernsteuerung, Drag & Drop, Zwischenablage) | `Message.cpp` (mit Area-Transfer) | neu, einfacher | 1.500 – 2.500 |
| G3 **Looper, Handler, Messenger, MessageRunner, Filter, Queue, Invoker** | Thread pro Looper, Kette von Handlern, Token, Timer über `Clock` | 6.995 (mit Message) | neu, angelehnt an Haiku | 1.800 – 2.800 |
| G4 Application | Integration mit dem Hauptthread (Pflicht unter macOS/SDL), Ablauf beim Beenden (`QuitRequested` über alle Fenster), Pulse | `Application.cpp` | neu | 600 – 1.000 |
| G5 Clock und Timer | echte Implementierung, Gegenstück zur Test-Uhr (M22) | — | neu | 150 – 300 |
| G6 Palette, Einstellungen | Gegenstück zu `ui_color`, Benachrichtigung bei Änderungen | `InterfaceDefs.cpp` (Teil) | angepasst | 300 – 600 |
| G7 Adapter für Übersetzungen | Gegenstück zu `B_TRANSLATE`, Plural, Formatierung von Zahlen und Datum | Locale Kit | Adapter für gettext oder ICU | 300 – 600 |
| G8 **View** | Baum, Koordinaten, Hooks für Ereignisse, Fokus, Scrollen, Invalidierung → DisplayList, Einbindung ins Layout | 6.002, davon großer Teil Serverprotokoll (414 `fLink`-Aufrufe) | weitgehend neu | 2.500 – 4.000 |
| G9 **Window** | Verteilung der Ereignisse (`_DetermineTarget`, `_UnpackMessage`), Fokus, Tastaturnavigation, Shortcuts, Default-Button, Frame-Zyklus, Abbildung von Look und Feel auf die Plattform | 3.266 | stark angepasst | 2.500 – 3.500 |
| G10 **Layout-Engine** | BLayout-Baum, Group, Grid, Split, Card, `layouter/`, Builder | 9.904 | weitgehend mechanischer Port | 7.000 – 9.000 |
| **Summe Grundgerüst** | | | | **16.800 – 24.600** |

## 5. Controls und Widgets

| Modul | Bezug in Haiku | Faktor | Zeilen |
|---|---|---|---|
| W1 Container-Views (ohne Scrollen über `CopyBits`) | 4.710 | 0,6 – 0,8 | 2.800 – 3.800 |
| W2 Controls (zeichnen bereits über ControlLook; ColorControl zeichnet selbst) | 10.017 | 0,6 – 0,8 | 6.000 – 8.000 |
| W3 **Menüs** (Tracking, Untermenüs, Tastatur, scrollende Menüs, Popups über `SDL_CreatePopupWindow`, auch unter Wayland relativ zum Elternfenster) | 6.494 | 0,7 – 0,9 | 4.500 – 6.000 |
| W4 Listen (ListView, OutlineListView) | 2.740 | 0,65 – 0,9 | 1.800 – 2.500 |
| W5 **TextView** (Gap-Buffer, Styles, Undo, Auswahl, Eingabemethoden; Zeilenlayout, Trefferprüfung und Cursorbewegung neu auf Basis des Text-Subsystems, damit BiDi möglich wird) | 6.454 | 0,85 – 1,3 | 5.500 – 8.500 |
| W6 Alert, About, ToolTips | 2.991 (mit InterfaceDefs) | — | 1.500 – 2.500 |
| W7 Öffentliche Grafiktypen (Bitmap ohne Server, Font als Fassade zum Text-Subsystem, Shape, Polygon) | 4.388 | — | 1.000 – 1.900 |
| **Summe Controls und Widgets** | | | **23.100 – 33.200** |
| *W8 optional:* ColumnListView | 4.592 | 0,7 – 0,9 | *3.200 – 4.200* |

## 6. Text-Subsystem

Ersetzt die grobe Schätzung T1 aus der Spezifikation (Entwurf 0.5: 2.000 – 3.500).

| Modul | Inhalt | Zeilen |
|---|---|---|
| X1 Font-Verwaltung | FreeType, Suche von Schriften (fontconfig, DirectWrite, CoreText), Fallback-Ketten | 800 – 1.500 |
| X2 Shaping | HarfBuzz-Wrapper, erzeugt `GlyphRun` mit `text`/`clusters` | 300 – 600 |
| X3 BiDi | über ICU, SheenBidi oder fribidi | 200 – 400 |
| X4 Zeilenumbruch | UAX #14 über ICU oder libunibreak | 200 – 400 |
| X5 Metriken und Kürzung | Port von Haikus `truncate_string`, Meldung für Layout-Lint | 300 – 500 |
| X6 Absatzlayout | Zeilen, Trefferprüfung, Cursorpositionen auch bei BiDi; Grundlage für die TextView | 1.000 – 2.000 |
| **Summe Text-Subsystem** | | **2.800 – 5.400** |

## 7. Plattformschicht

| Modul | Inhalt | Zeilen |
|---|---|---|
| P1 Plattform-Abstraktion | `Platform`, `NativeWindow`, `Display`, `Cursor`, `Clipboard`, Einbindung in den Event-Loop | 400 – 700 |
| P2 **SDL3-Implementierung** | Fenster mit Abbildung von Look und Feel (modal, floating, popup); Übersetzung der Ereignisse in Messages mit eigenen Tastencodes und Modifiern; Eingabemethoden (`SDL_EVENT_TEXT_EDITING` → InlineInput, Kandidatenfenster über `SDL_SetTextInputArea`); Zwischenablage mit MIME-Typen; Dateien per Drag & Drop; Cursor; Monitore und Wechsel der Pixeldichte; Weiterleiten an den Hauptthread; Datei- und Meldungsdialoge von SDL3 | 2.500 – 4.000 |
| **Basis zusammen** | | **2.900 – 4.700** |
| P3 *nativ:* globale Menüleiste unter macOS | Brücke zu `NSMenu` in Objective-C++ | 500 – 900 |
| P4 *nativ:* Drag & Drop beliebiger Daten zwischen Anwendungen | pro Betriebssystem (OLE, NSPasteboard, Wayland/X11) | 600 – 1.200 |
| P5 *nativ:* Benachrichtigungen | pro Betriebssystem | 300 – 600 |
| **Plattformschicht gesamt** | | **4.300 – 7.400** |

## 8. Gesamtbild

| Block | Produktionscode |
|---|---|
| Grundgerüst | 16.800 – 24.600 |
| Controls und Widgets | 23.100 – 33.200 |
| Text-Subsystem | 2.800 – 5.400 |
| Plattformschicht | 4.300 – 7.400 |
| **Zwischensumme dieses Dokuments** | **47.000 – 70.600** |
| Canvas, Looks, Export, Test-Kit (Spezifikation, Phasen 1–3) | 16.650 – 23.500 |
| **Toolkit gesamt** | **etwa 64.000 – 94.000** |
| Tests (Erfahrungswert 30–50 % des Produktionscodes) | etwa 25.000 – 45.000 |

**Plausibilitätsprüfung:** Die Teile dieses Dokuments entsprechen 60–90 % des vergleichbaren Haiku-Codes (rund 71.000 Zeilen ohne die nicht portierten Teile). Das passt dazu, dass vieles wegfällt (Serverprotokoll, Kompatibilität, Archivierung) und anderes neu hinzukommt (Plattformschicht, BiDi, Integration mit dem Hauptthread).

**Grobe Größenordnung des Aufwands:** Bei üblichen 10.000–20.000 Zeilen getestetem C++-Produktionscode pro Personenjahr, wobei Ports schneller gehen als Neuentwicklung, ergeben sich **etwa 4–9 Personenjahre**. Das ist eine Faustregel, kein Plan.

## 9. Erster Meilenstein (MVP)

**Ziel:** eine Demo-App unter Linux mit Fenstern, Layouts, Buttons, Checkboxen, Radio-Buttons, Slider, einzeiligem Textfeld, Menüs und Scrollbars, im BeOS-Look, testbar.

| Teil | Zeilen |
|---|---|
| Grundgerüst ohne Serialisierung, Layout nur Group/Grid | 15.300 – 23.000 |
| Controls (Teilmenge), Container (Teilmenge), Menüs, TextView nur LTR | 11.800 – 16.000 |
| Text-Subsystem ohne BiDi | 1.500 – 2.500 |
| Plattform: nur SDL3-Basis (P1, P2) | 2.500 – 4.000 |
| Canvas Phase 1 der Spezifikation (Skia CPU, BeOS-Look, Grundausstattung Test-Kit) | 11.550 – 17.050 |
| **MVP gesamt** | **etwa 42.000 – 62.000** |

Das MVP umfasst also schon rund zwei Drittel des Gesamtumfangs. Grundgerüst, Layout, Menüs und Text braucht man von Anfang an fast vollständig.

## 10. Was die Schätzung verschieben kann

| Faktor | Wirkung auf den Codeumfang | Wirkung auf den Aufwand |
|---|---|---|
| ~~Quellkompatibilität zur BeAPI~~ (**verworfen**, Grundsatz K1) | etwa gleich, eher höher | wäre um etwa 10–20 % geringer, bezogen auf das Ganze nur etwa 6–12 % |
| Konzeptionelle Nähe zu Haiku (Abschnitt 2, **gewählt**) | in den Zahlen bereits enthalten | holt nach Einschätzung etwa die Hälfte der Ersparnis einer Quellkompatibilität, ohne deren Nachteile |
| Text zunächst nur LTR, BiDi später | Text-Subsystem und TextView zunächst etwa −2.000 bis −3.500 | entsprechend |
| Nur Linux im ersten Schritt | native Module vorerst etwa −1.400 bis −2.700 | entsprechend |
| Barrierefreiheit (noch nicht enthalten) | etwa +2.000 bis +4.000 über AccessKit, aufbauend auf dem Scripting-Baum | entsprechend |
| ColumnListView | +3.200 bis +4.200 | entsprechend |

## 11. Offene Punkte

1. Wird zuerst nur Linux unterstützt?
2. Kommt BiDi gleich ins MVP oder später?
3. Wann folgt die Barrierefreiheit? Sie ist für viele Produkte Pflicht, in der EU z. B. durch den European Accessibility Act für bestimmte Produkte und Dienste.
4. Welche externen Bibliotheken werden für BiDi und Zeilenumbruch verwendet: ICU (vollständig, groß) oder SheenBidi/fribidi und libunibreak (klein)?
