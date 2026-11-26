# NewGUI

[![Build](https://github.com/dirkjink/NewGUI/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/dirkjink/NewGUI/actions/workflows/build.yml)
[![CodeQL](https://github.com/dirkjink/NewGUI/actions/workflows/codeql.yml/badge.svg?branch=main)](https://github.com/dirkjink/NewGUI/actions/workflows/codeql.yml)
[![Docs](https://github.com/dirkjink/NewGUI/actions/workflows/docs.yml/badge.svg?branch=main)](https://dirkjink.github.io/NewGUI/)
[![Coverage](https://dirkjink.github.io/NewGUI/coverage/badge.svg)](https://dirkjink.github.io/NewGUI/coverage/)
[![OpenSSF Scorecard](https://api.scorecard.dev/projects/github.com/dirkjink/NewGUI/badge)](https://scorecard.dev/viewer/?uri=github.com/dirkjink/NewGUI)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/23)
[![Last commit](https://img.shields.io/github/last-commit/dirkjink/NewGUI)](https://github.com/dirkjink/NewGUI/commits/main)
[![Issues](https://img.shields.io/github/issues/dirkjink/NewGUI)](https://github.com/dirkjink/NewGUI/issues)

Ein plattformübergreifendes UI-Toolkit in C++23. Seine Architektur orientiert sich am Interface Kit von [Haiku](https://www.haiku-os.org/): ein Looper pro Fenster, Nachrichten, ein dauerhaft bestehender View-Baum, Constraint-Layout und austauschbare Looks. Gezeichnet wird über eine austauschbare Canvas-Schicht, Ziel-Renderer ist Skia.

**Status:** frühe Entwurfs- und Port-Phase. Es gibt noch keine stabile API.

## Dokumente

Die Doku-Site **<https://dirkjink.github.io/NewGUI/>** enthält diese Dokumente, den Test-Nachweis (jeder Test des letzten Laufs auf `main` mit Begründung), den Coverage-Bericht und die API-Referenz. Alles außer den Dokumenten selbst wird bei jedem Lauf neu erzeugt.

| Dokument | Inhalt |
|---|---|
| [`canvas-core-profil-spezifikation.md`](canvas-core-profil-spezifikation.md) | Spezifikation der Canvas-Schicht, der Backends, Looks, Export, Screenshots und des Test-Kits |
| [`toolkit-gesamtumfang.md`](toolkit-gesamtumfang.md) | Abschätzung für Grundgerüst, Controls, Text-Subsystem und Plattformschicht |
| [`CONVENTIONS.md`](CONVENTIONS.md) | Konventionen für Code und Projekt (Port-Phase und Ziel-Phase) |

## Bauen

**Voraussetzungen:**

- CMake 3.25 oder neuer und Ninja
- ein C++23-Compiler: GCC 13+, Clang 19+ oder MSVC 2022
- [vcpkg](https://github.com/microsoft/vcpkg), die Umgebungsvariable `VCPKG_ROOT` zeigt auf das vcpkg-Verzeichnis

Die Abhängigkeiten stehen in `vcpkg.json` (Manifest-Modus mit festgeschriebener Baseline). vcpkg installiert sie beim Konfigurieren automatisch.

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Alles in einem Schritt, mit Warnungen als Fehler, wie in der CI:

```sh
cmake --workflow --preset ci
```

| Preset | Build-Typ | Besonderheit |
|---|---|---|
| `debug` | Debug | für die Entwicklung |
| `release` | Release | optimiert |
| `ci` | RelWithDebInfo | Warnungen als Fehler |
| `docs` | Debug | Coverage mit Skia und SDL3, JUnit-Ausgabe der Tests, API-Referenz (braucht GCC oder Clang, Doxygen 1.9.8+, unter Linux die Header für X11 und Wayland) |
| `fuzz` | RelWithDebInfo | libFuzzer-Programme mit ASan und UBSan (braucht Clang: `CC=clang CXX=clang++`) |
| `skia` | RelWithDebInfo | mit Skia und SDL3 aus den vcpkg-Ports (Features `skia`, `sdl3`): Skia-Backend (`ngui_skia`), SDL3-Plattform (`ngui_sdl3`) und die Beispiele in `examples/`; der erste Build von Skia dauert lange, danach kommt es aus dem Binär-Cache von vcpkg. Unter Linux braucht SDL3 die Entwicklungs-Header des Fenstersystems: `sudo apt install libx11-dev libxext-dev libwayland-dev wayland-protocols libxkbcommon-dev libegl-dev` |

Alle Presets bauen das Text-Subsystem (`ngui_text`, Feature `text`: HarfBuzz, libunibreak und die mitgelieferten Schriften) und die Vektor-Icons (`ngui_svg`, Feature `svg`: lunasvg). Eine Anwendung meldet beide an, mit `TextSystem::SetDefault()` und `VectorIconRenderer::SetDefault()`; ohne Icon-Renderer zeigt `BAlert` kein Typsymbol. `FontTextSystem::Create(text::SystemFontSource())` nimmt für Zeichen, die den mitgelieferten Schriften fehlen, die Schriften des Systems und bietet sie über `BFont` an. Unter Windows kommen sie von DirectWrite, unter Linux von der fontconfig des Systems, die erst zur Laufzeit geladen wird und daher zum Bauen nicht gebraucht wird; `CreateBundled()` bleibt bei den mitgelieferten Schriften.

Gezeichnet wird mit Skia, auf der CPU oder über OpenGL auf der GPU (Phase G: `SurfaceKind::Gpu` über den GL-Kontext eines Fensters, `NativeWindow::glProcAddress`). Die SDL-Plattform zeichnet Fenster auf Wunsch auf der GPU (`NGUI_RENDERER=gpu` oder `SdlPlatform::SetGpuRendering(true)`): Die Fenster zeichnen ihre DisplayLists auf, der Hauptthread rendert sie über den OpenGL-Kontext des Fensters, weil SDL3 OpenGL nur dort erlaubt; ohne OpenGL zeichnen sie auf der CPU, Popups immer; Blend2D ist ein zweites, unabhängiges Backend (vcpkg-Feature `blend2d`, `NGUI_WITH_BLEND2D`), mit dem `NGUI_BACKEND=blend2d` jede Anwendung startet. Achsparallele Primitive zeichnen beide Pixel für Pixel gleich (Konformitätsstufe C1×), geglättete Kanten weichen nur im Rahmen ihrer Rasterer ab.

Anwendungen lassen sich wie unter Haiku per Scripting-Nachrichten steuern (`kGetProperty`, `kSetProperty`, `kCountProperties`, `kExecuteProperty` mit Specifiern, z. B. „`Value` of View `check` of Window `Settings`“): Anwendung, Fenster, Views, Controls, Textfelder und Listen haben Haikus Standardeigenschaften, eigene Klassen ergänzen ihre mit `BPropertyInfo` und `ResolveSpecifier()`. Aus einem anderen Prozess desselben Benutzers geht das über einen lokalen Socket (`<ngui/RemoteControl.h>`), wenn die Anwendung es erlaubt: `BApplication::EnableRemoteControl()` oder `NGUI_REMOTE_CONTROL=1`; sonst ist die Fernsteuerung aus. Das Werkzeug `ngui-hey` spricht dabei die Sprache von Haikus `hey`, etwa `ngui-hey example-button set Value of View slider of Window NewGUI to 30` für den Slider der Demo; `ngui-hey -l` listet die Anwendungen, die es erlauben.

Eingaben lassen sich aufzeichnen und in einem Test wiedergeben (`<ngui/Recording.h>`). Aufgezeichnet wird nur auf ausdrücklichen Wunsch: `BApplication::StartRecording(datei)`, nachdem der Benutzer zugestimmt hat, oder der Start mit `NGUI_RECORD=datei`. Tippen in Passwortfelder und in Views, die mit `BView::SetInputPrivate()` als privat markiert sind, steht nur maskiert in der Datei. `ngui-replay --dump datei` zeigt, was aufgezeichnet wurde. `test::Driver::Replay()` spielt die Aufzeichnung headless mit virtueller Uhr ab; so wird ein Fehler aus der Praxis zum Regressionstest.

Für Screenreader sagt jede View, was sie ist (`<ngui/Accessibility.h>`): Rolle, Name, Wert, Zustände und Aktionen. Ein Fenster bildet daraus einen Baum (`BWindow::GetAccessibleTree()`), Passwörter erscheinen darin nie. Die Einträge von Listen, Bäumen, Tabs und Menüs sind eigene Knoten, offene Menüs gehören zum Baum ihres Fensters. Eine `BColumnListView` ist eine Tabelle: eine Zeile mit den Spaltenköpfen, dann je sichtbarer Zeile eine Zeile mit ihren Zellen, mit Zeilen- und Spaltenindex; der Fokus liegt auf der Fokuszeile. Texte kommen als Zeilen mit Zeichen und Wörtern, mit Schreibmarke und Auswahl, die ein Screenreader auch setzen kann. `BWindow::Announce()` lässt einen Screenreader einen Text ansagen; eine View mit `SetLiveRegion()` (etwa eine Statuszeile) wird bei jeder Änderung angesagt. Views ohne eigene Beschriftung bekommen ihren Namen mit `BView::SetAccessibleName()`. Im Test-Kit prüft `EXPECT_ACCESSIBILITY_SNAPSHOT` den Baum, `Driver::PerformAccessibleAction()` löst Aktionen aus wie ein Screenreader, und das Layout-Lint warnt vor Controls ohne Namen. Mit dem vcpkg-Feature `accessibility` (Option `NGUI_WITH_ACCESSKIT`, in den Presets `skia` und `docs` an) reicht die SDL-Plattform den Baum über [AccessKit](https://github.com/AccessKit/accesskit-c) an Screenreader weiter: unter Linux über AT-SPI, unter Windows über UI Automation; macOS folgt. Screenreader können Controls auch bedienen. AccessKit kommt als Overlay-Port der fertigen Bibliotheken von Release 0.23.2 (`ports/accesskit-c`); Dependabot aktualisiert ihn nicht.

Tabellen zeigt `BColumnListView` (`<ngui/ColumnListView.h>`, Feldtypen in `<ngui/ColumnTypes.h>`): Zeilen in Spalten mit Titeln, Unterzeilen zum Auf- und Zuklappen, Sortieren per Klick auf einen Titel (erneut in die andere Richtung, mit Shift nach einer weiteren Spalte), Auswahl mit Maus und Tastatur, Bildlaufleisten und Platz für eine Statusanzeige. Spalten lassen sich mit der Maus verschieben, in der Breite ändern (ein Doppelklick auf den Rand passt sie an), aus der Titelzeile ziehen und über ein Menü (Klick mit der rechten Maustaste) ein- und ausblenden; `SaveState()` und `LoadState()` bewahren die Anordnung. Feldtypen gibt es für Text, Datum, Größe, Ganzzahl, Fortschrittsbalken und Bitmap. Die Demo zeigt sie auf der Seite „Files“. Die API folgt Haikus `BColumnListView`, der Code ist eine eigene Implementierung: Haikus Fassung steht unter der Open Tracker License, NewGUI bleibt reines MIT.

`gfx::ExportSvg()` schreibt eine DisplayList als SVG (`SvgExportBackend`): deterministisch, mit Verläufen, Clipping und eingebetteten Bildern, Text als Glyph-Umrisse oder als auswählbarer `<text>`; `EXPECT_SVG_SNAPSHOT` vergleicht das Ergebnis mit einer Referenzdatei.

Drei Looks sind eingebaut, alle 1:1 aus Haiku portiert: `be` (BeOS, die Voreinstellung), `haiku` (Haikus Standard-Look mit Verläufen) und `flat` (Haikus flacher Look, abgeleitet vom Haiku-Look und mit dessen Maßen). `NGUI_CONTROL_LOOK=haiku` startet eine Anwendung im Haiku-Look; ein Look, den das Backend nicht zeichnen kann, fällt auf `be` zurück. Zur Laufzeit wechselt `LookRegistry::Default().SetCurrent("haiku")` den Look aller Fenster (in der Demo das Menü „Look“); `BWindow::SetControlLook()` gibt einem einzelnen Fenster einen eigenen Look, etwa für eine Vorschau. Weitere Looks kommen als Plugins hinzu (`<ngui/LookPlugin.h>`): eine Shared Library, die `libngui` statisch einbindet und ihre Fabrik mit `NGUI_LOOK_PLUGIN()` exportiert. `LookRegistry::LoadPlugin()` bzw. `LoadPlugins(verzeichnis)` laden sie, beim Start auch die Verzeichnisse aus `NGUI_LOOK_PATH` (getrennt wie `PATH`), sodass `NGUI_CONTROL_LOOK` einen Plugin-Look wählen kann. Ein Plugin muss mit demselben Compiler, derselben Standardbibliothek und derselben Version des Toolkits gebaut sein (`LookPluginBuildKey()`) und zeichnet nur über seinen `DrawContext`.

Eine Anwendung wählt ihr Fenstersystem mit `BApplication::UsePlatform()`: nach Namen oder, ohne Namen, das bevorzugte der `PlatformRegistry` (SDL3, nachdem `SdlPlatform::Register()` es angemeldet hat). `NGUI_PLATFORM=headless` oder `UsePlatform("headless")` lässt sie ohne Fenstersystem laufen (`HeadlessPlatform`): Fenster zeichnen in CPU-Puffer auf einem virtuellen Bildschirm, mit virtueller Zwischenablage, und Dateidialoge beantwortet ein Platzhalter; so laufen Tests in der CI ohne Bildschirm. Das Test-Kit (`ngui::testkit`, `<ngui/test/Driver.h>`) treibt eine Anwendung darauf: `TestEnvironment` stellt eine steuerbare Uhr und Englisch ein, `Driver` lässt die Anwendung in einem eigenen Thread laufen, findet Fenster und Views (`Window("Titel").Find<BButton>("ok")`, `FindByLabel`, `FindByPath`), klickt, tippt und drückt Tasten über denselben Weg wie echte Eingaben und wartet mit `WaitForIdle()`, bis nichts mehr zu tun ist; läuft die Zeit ab, sagt der Bericht, welcher Looper woran arbeitet. `BWindow::Snapshot()` und `BView::Snapshot()` zeichnen ein Fenster oder eine View neu in ein `BBitmap` (aus jedem Thread, auf Wunsch mit Ausschnitt, Skalierung und offenen Menüs), bitgleich zu dem, was das Fenster zeigt; `CaptureFrame()` gibt das zuletzt gezeigte Bild, `EncodePng()` macht ein PNG daraus, und `EXPECT_IMAGE_SNAPSHOT(window, …)` vergleicht ein Fenster mit seinem Referenzbild. `EXPECT_LAYOUT_SNAPSHOT(window, …)` vergleicht den Viewbaum als Text. Das Layout-Lint (`<ngui/test/Lint.h>`) prüft Fenster auf überlappende Views, abgeschnittene Texte (gemeldet vom Textsystem), zu kleine und herausragende Views und eine Tab-Reihenfolge gegen die Lesereihenfolge; `LintMatrix()` wiederholt das für Skalierungen, Pseudo-Lokalisierung, größere Schrift und Looks.

Übersetzungen kommen wie bei Haiku über `B_TRANSLATE()` (`<ngui/Catalog.h>`), aus gettext-Katalogen (`.mo`), die NewGUI selbst liest: `BLocaleRoster::Default()->LoadCatalog("domain")` sucht `locale/<sprache>/LC_MESSAGES/domain.mo` neben dem Programm, für die Sprachen des Benutzers (`LANGUAGE`, `LANG` …, unter Windows die Anzeigesprachen). Die Texte sammelt `xgettext --keyword=B_TRANSLATE --keyword=B_TRANSLATE_CONTEXT:1,2c --keyword=B_TRANSLATE_PLURAL:1,2`; `NGUI_LANGUAGE=x-pseudo` zeigt eine Pseudo-Lokalisierung (längere, akzentuierte Texte), die zu enge Layouts und unübersetzte Texte sichtbar macht. Die Demo bringt eine deutsche Übersetzung mit (`examples/button/po/de.po`). Zahlen, Daten und Uhrzeiten schreiben `BNumberFormat`, `BDateFormat` und `BTimeFormat` so, wie das System es für die Locale des Benutzers vorsieht (unter Linux die Locale-Daten der C-Bibliothek, unter Windows dessen Formatfunktionen); `"C"` schreibt überall gleich.

Die CI (`.github/workflows/build.yml`) führt den Workflow `ci` mit GCC 13 und 14, Clang 19 und 20 unter Linux sowie MSVC 2022 unter Windows aus. vcpkg wird dort genau auf dem Commit der `builtin-baseline` ausgecheckt. Der Job `Build` fasst die Ergebnisse zusammen und ist Pflicht-Check für `main`.

Der Build-Workflow prüft außerdem, dass der Build reproduzierbar ist (die Bibliothek wird an zwei Orten gebaut, die Prüfsummen müssen übereinstimmen). Daneben erzeugt er eine SBOM (SPDX 2.3) der ausgelieferten vcpkg-Abhängigkeiten und prüft sie gegen die [OSV-Datenbank](https://osv.dev/), auch wöchentlich ohne neuen Commit. Einzelheiten stehen in [`CONVENTIONS.md`](CONVENTIONS.md), Abschnitt 8.2.

Releases (`release.yml`) tragen ein reproduzierbares Quellarchiv, die SBOM und eine signierte SLSA-Provenance. Der Ablauf und die Prüfung stehen in [`CONVENTIONS.md`](CONVENTIONS.md), Abschnitt 8.4.

Weitere Workflows: `docs.yml` (Doku-Site nach GitHub Pages, bei PRs nur gebaut), `codeql.yml` (statische Analyse von C++ und den Workflows), `scorecard.yml` (OpenSSF Scorecard), `release.yml` (Release-Assets mit Provenance), `fuzz.yml` (nächtliches Fuzzing mit libFuzzer). Dependabot hält die vcpkg-Baseline und die gepinnten Actions aktuell.

## Lizenz

MIT, siehe [`LICENSE`](LICENSE). Code, der aus Haiku übernommen wird, steht ebenfalls unter der MIT-Lizenz und behält seine ursprünglichen Copyright-Hinweise (siehe [`CONVENTIONS.md`](CONVENTIONS.md), Abschnitt 4.4).
