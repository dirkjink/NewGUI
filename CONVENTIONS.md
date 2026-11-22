# Konventionen für NewGUI

| | |
|---|---|
| **Status** | Entwurf 0.1 |
| **Datum** | 2026-11-22 |
| **Bezug** | [`canvas-core-profil-spezifikation.md`](canvas-core-profil-spezifikation.md), Grundsätze K1, K4 und K5 |

Die Schlüsselwörter **MUSS**, **DARF NICHT**, **SOLL** und **KANN** sind im Sinne von RFC 2119 zu lesen.

---

## 1. Grundlagen

| Thema | Festlegung |
|---|---|
| Sprache | **C++23**, ohne Module |
| Compiler | GCC 13 oder neuer, Clang 17 oder neuer, MSVC 2022 (17.x) |
| Lizenz | **MIT** |
| Namensraum | **`ngui`**, Untermodule als verschachtelte Namensräume (`ngui::gfx`, `ngui::test` …) |
| Header-Schutz | `#pragma once` |
| Sprache im Code | US-Englisch für Namen und Kommentare (wie in Haiku). Projektdokumente sind auf Deutsch. |
| Warnungen | Der Build ist warnungsfrei: `-Wall -Wextra -Wpedantic` (GCC/Clang) bzw. `/W4` (MSVC). Warnungen werden behoben, nicht unterdrückt. Ein unvermeidbarer Workaround wird kommentiert und entfernt, sobald er nicht mehr nötig ist. |

## 2. Zwei Phasen

| Phase | Zeitraum | Ziel |
|---|---|---|
| **Port-Phase** | bis zur ersten öffentlichen Freigabe | Code aus Haiku mit möglichst **wenigen unnötigen Änderungen** übertragen. So bleiben Diffs gegenüber Haiku lesbar, und Fehlerbehebungen aus Haiku lassen sich übernehmen. |
| **Ziel-Phase** | ab der ersten öffentlichen Freigabe | endgültige Namen und Konventionen (Abschnitt 6) |

Die Umstellung (Abschnitt 7) MUSS **vor der ersten öffentlichen Freigabe** abgeschlossen sein. Ab Version 1.0 gilt Semantic Versioning (Spezifikation, Grundsatz K1), danach wäre jede Umbenennung ein Bruch der API.

## 3. Regeln für beide Phasen (Semantik und API-Gestalt)

Diese Regeln betreffen Bedeutung und Signaturen. Sie lassen sich später nicht mechanisch ändern und gelten deshalb **von Anfang an**, auch für portierten Code:

1. **Geometrie und Farbe** kommen aus `ngui::gfx`: `Point`, `Size`, `Rect` (halboffen, Spezifikation 4.1), `Color`. Haikus `BPoint`, `BSize`, `BRect` und `rgb_color` werden **nicht** portiert. Portierter Code wird an den betreffenden Stellen umgestellt, und jede Rechnung mit inklusiven Grenzen (`right - left + 1`, `right--` …) ist dabei ausdrücklich zu prüfen.
2. **Fehlerbehandlung** mit `std::expected<T, ngui::Error>` statt `status_t`. Über die API-Grenze werden keine Exceptions geworfen.
3. **Besitz** ist ausdrücklich geregelt: `std::unique_ptr` für exklusiven Besitz, `std::shared_ptr` nur bei echt geteiltem Besitz (z. B. Look-Objekte, Spezifikation L4/L10). Übergibt eine Funktion Besitz, ist das in der Signatur sichtbar.
4. **Nebenläufigkeit:** Über Fenstergrenzen hinweg wird ausschließlich mit Nachrichten gearbeitet. Kein Zugriff auf Views fremder Fenster, kein Locking von außen.
5. **Aufzählungen** sind `enum class` mit Werten in UpperCamelCase. Bitmengen gibt es über eine `Flags<E>`-Vorlage (Spezifikation L5). Haikus `B_`-Konstanten werden entsprechend abgebildet, z. B. `B_CONTROL_ON` → `BControlValue::On` in der Port-Phase bzw. `ngui::ControlValue::On` in der Ziel-Phase.
6. **Typen in Signaturen** kommen aus der Standardbibliothek: `std::string`/`std::string_view` statt `BString`, `std::vector`/`std::span` statt `BList`/`BObjectList`, `std::function` für Rückrufe.
7. **Entfällt vollständig:** Archivierung (`Archive`, `Instantiate`, Konstruktoren aus Archiv-Messages), Replicants, `_Reserved*`-Slots, `Perform()`-Hooks und andere Mittel zur Binärkompatibilität mit BeOS.
8. **K5: Gleicher Name nur bei gleicher Bedeutung.** Ein Haiku-Name darf nur für eine Klasse, Funktion oder Konstante verwendet werden, deren Verhalten dem Haiku-Original entspricht. Ändert sich die Semantik, bekommt das Element einen neuen Namen (siehe Tabelle in 4.2).

## 4. Port-Phase

### 4.1 Grundsatz K4

In der Port-Phase gelten die **Coding Guidelines von Haiku** (<https://www.haiku-os.org/development/coding-guidelines/>) vollständig, mit den Abweichungen aus Abschnitt 3 und 4.3. Der gesamte Code liegt im Namensraum `ngui`. Wie in Haiku wird innerhalb von Namensräumen nicht eingerückt:

```cpp
namespace ngui {


BButton::BButton(const char* name, const char* label, BMessage* message)
	:
	BControl(name, label, message, B_WILL_DRAW | B_NAVIGABLE),
	fDrawAsDefault(false)
{
}


} // namespace ngui
```

### 4.2 Namen in der Port-Phase

| Element | Konvention |
|---|---|
| Portierte Klassen mit gleicher Bedeutung | Haiku-Name mit `B`-Präfix, im Namensraum `ngui` (`ngui::BWindow`, `ngui::BView`, `ngui::BButton`, `ngui::BLooper`, `ngui::BMessage` …) |
| Neue Klassen ohne Haiku-Vorbild oder mit geänderter Bedeutung | **ohne** `B`-Präfix, schon in der Port-Phase (`ngui::gfx::Canvas`, `ngui::gfx::Rect`, `ngui::ControlLook`, `ngui::DrawContext`) |
| Member-Variablen, statische Variablen, globale Variablen, Konstanten | Präfixe `f`, `s`, `g`, `k` |
| private Methoden | `_PrivateMethod()` |
| Ausgabeparameter | Präfix `_` (`_width`, `_height`) |
| lokale Variablen, Parameter | lowerCamelCase, ausgeschriebene Namen |

Beispiele für **neue Namen wegen geänderter Bedeutung** (Regel K5):

| Haiku | NewGUI | Grund |
|---|---|---|
| `BRect` | `ngui::gfx::Rect` | halboffen statt inklusiv |
| `BPoint`, `BSize`, `rgb_color` | `ngui::gfx::Point`, `Size`, `Color` | ein einheitlicher Satz Geometrie- und Farbtypen (Abschnitt 3, Regel 1) |
| `BControlLook` | `ngui::ControlLook` | andere Schnittstelle (`DrawContext`, Canvas), Spezifikation 18.3 |
| `status_t` | `std::expected<T, ngui::Error>` | anderes Fehlermodell |
| anonyme `B_`-Konstanten | `enum class` mit `B`-Präfix im Typnamen, z. B. `BControlValue` | typsicher (Abschnitt 3, Regel 5) |

### 4.3 Abweichungen von den Haiku-Guidelines

| Haiku-Guideline | Port-Phase | Grund |
|---|---|---|
| `#ifndef KLASSE_H` … `#endif` | `#pragma once` | Entscheidung für das Projekt |
| `BString`, `BObjectList`, `status_t`, `AutoLock` bevorzugen | Standardbibliothek, `std::expected`, `std::scoped_lock` | Abschnitt 3 |
| C-Header (`string.h`) bevorzugen | in portierten Dateien wie im Original, in neuen Dateien C++-Header (`<cstring>`) | geringe Diffs bzw. Zielkonvention |
| `NULL` statt `0` | in portierten Dateien `NULL` wie im Original, in neuen Dateien `nullptr` | geringe Diffs bzw. Zielkonvention |
| `int32`, `uint32` … aus `SupportDefs.h` | als Aliase in `ngui/SupportDefs.h` (`using int32 = std::int32_t;` …) für portierten Code, in neuen Dateien `<cstdint>`-Typen | geringe Diffs bzw. Zielkonvention |
| Kopf mit „Haiku, Inc.“ | Kopf nach 4.4 | eigenes Projekt |

**Faustregel:** In portierten Dateien gilt wie in Haiku „konsistent mit dem umgebenden Code“. Neue Dateien folgen in den Punkten aus Abschnitt 4.3 bereits den Zielkonventionen. So entsteht später weniger Arbeit beim Umstellen.

### 4.4 Datei-Köpfe

**Neue Datei:**

```cpp
/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
```

**Aus Haiku portierte Datei.** Der Haiku-Hinweis und die Autorenliste MÜSSEN erhalten bleiben. Herkunft und Ausgangs-Commit werden angegeben:

```cpp
/*
 * Copyright 2001-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Button.cpp (commit b1d497bb69)
 *
 * Authors (Haiku):
 *		Marc Flerackers (mflerackers@androme.be)
 *		...
 */
```

Bis zum ersten `#include` folgen, wie in Haiku, zwei Leerzeilen.

### 4.5 Formatierung (Kurzfassung der Haiku-Guidelines)

- Einrückung mit Tabs, Tabbreite 4, höchstens 100 Spalten
- Fortsetzungszeilen: mindestens ein zusätzlicher Tab, ein weiterer pro Klammerebene. `&&` und `||` stehen am Anfang der Fortsetzungszeile.
- Der Rückgabetyp steht auf einer eigenen Zeile, darunter `Klasse::Methode(...)`, dann `{` auf eigener Zeile.
- Initialisierungsliste: eine Zeile nur mit `:`, dann eine Initialisierung pro Zeile, jeweils mit einem Tab eingerückt
- Bei `if`, `for`, `while` und `switch` steht `{` auf derselben Zeile, ebenso `} else {`. Einzeilige Rümpfe stehen ohne Klammern auf der nächsten Zeile.
- `case` wird um einen Tab eingerückt. Deklariert ein `case` Variablen, bekommt es eigene Klammern.
- Genau zwei Leerzeilen zwischen Funktionen und zwischen den Blöcken mit Includes, Definitionen und Funktionen
- `*` und `&` stehen am Typ: `BView* view`
- Klassendeklarationen tabellarisch ausgerichtet, `public:`/`private:` in Spalte 0, eigenes `private:` vor den Feldern
- Include-Reihenfolge: eigener Header, dann Systemheader, Standardbibliothek, Abhängigkeiten (Skia, SDL3 …), Header aus `ngui`, lokale Header. Jede Gruppe ist mit einer Kommentarzeile versehen und alphabetisch sortiert.
- Kommentare mit `//`, API-Dokumentation als Doxygen `/*! */`. Kein auskommentierter Code, kein `#if 0`, keine Namen oder Wertungen in Kommentaren.
- Casts im C++-Stil, keine Zuweisung in Bedingungen, kein `else` nach `return`, keine Klammern um Rückgabewerte, explizite Vergleiche (`pointer != NULL` bzw. `!= nullptr`)

## 5. Werkzeuge

| Werkzeug | Zweck | Stand |
|---|---|---|
| `.editorconfig` | Tabs, Tabbreite, Zeilenenden, Leerzeichen am Zeilenende | vorhanden |
| **haiku-format** (<https://github.com/owenca/haiku-format>, eine Abwandlung von clang-format) | Formatierung im Haiku-Stil einschließlich tabellarischer Klassendeklarationen | **einzurichten**; vorher prüfen, ob es mit C++23-Code zurechtkommt |
| `checkstyle.py` aus Haiku (`src/tools/checkstyle/`) | Prüfung der Haiku-Regeln in der Port-Phase | optional |
| clang-tidy | in der Port-Phase Prüfungen ohne Stilregeln (`bugprone-*`, `performance-*`); für die Umstellung in Abschnitt 7 die `modernize-*`-Prüfungen | einzurichten |

## 6. Ziel-Konventionen (ab Freigabe)

- Formatierung, Präfixe `f`/`s`/`g`/`k`, `_PrivateMethod` und die Regeln aus 4.5 **bleiben**.
- Klassen im Namensraum `ngui` **ohne** `B`-Präfix: `ngui::Window`, `ngui::View`, `ngui::Button`, `ngui::Looper`, `ngui::Message`.
- `enum class`-Typen ohne `B`-Präfix: `ngui::ControlValue`.
- Überall `nullptr`, `<cstdint>`-Typen und C++-Header. Die Aliase aus `ngui/SupportDefs.h` entfallen.
- Ein eventuell verbliebenes `B_`-Präfix an Konstanten entfällt.

## 7. Umstellung vor der Freigabe

| Schritt | Werkzeug |
|---|---|
| 1. Klassen ohne `B`-Präfix umbenennen, nach einer gepflegten Zuordnungstabelle | clang-rename bzw. clang-tidy auf Basis des Syntaxbaums, **nicht** `sed` |
| 2. `NULL` → `nullptr` | `clang-tidy modernize-use-nullptr` |
| 3. `int32` … → `std::int32_t` …, danach Aliase entfernen | clang-tidy bzw. skriptgesteuert mit anschließendem Übersetzen |
| 4. C-Header → C++-Header | `clang-tidy modernize-deprecated-headers` |
| 5. Kopf-Kommentare und Dokumentation an die neuen Namen anpassen | Skript und Durchsicht |
| 6. Nachweis | vollständiger Testlauf mit allen Snapshot-Arten (Spezifikation 20.5). Layout-, DisplayList- und SVG-Snapshots MÜSSEN unverändert bleiben, abgesehen von Namen im Layout-Snapshot. |

Grobe Größenordnung: Tage bis zwei Wochen, solange die Regeln aus Abschnitt 3 von Anfang an eingehalten wurden.

## 8. Verzeichnisstruktur und Build

### 8.1 Verzeichnisse

| Verzeichnis | Inhalt |
|---|---|
| `include/ngui/` | öffentliche Header, ein Unterverzeichnis je Modul (`include/ngui/gfx/` …). Eingebunden wird immer als `<ngui/…>`. |
| `src/` | Implementierung mit derselben Unterstruktur. Private Header liegen neben den Quelldateien. |
| `tests/` | Tests mit derselben Unterstruktur, eine Datei `<Klasse>Test.cpp` je getesteter Klasse |
| `cmake/` | CMake-Module (`Ngui*.cmake`) und Vorlagen (`cmake/templates/`) |

**Dateinamen** folgen wie in Haiku der wichtigsten Klasse der Datei, in UpperCamelCase und ohne `B`-Präfix: `Rect.h`/`Rect.cpp`, `Button.h`/`Button.cpp` für `BButton`. So bleiben die Dateinamen bei der Umstellung in Abschnitt 7 unverändert. Generierte Header (z. B. `ngui/Version.h`) entstehen im Build-Verzeichnis und werden nicht eingecheckt.

### 8.2 Build

- **CMake** 3.25 oder neuer mit **Presets** (`CMakePresets.json`), Generator **Ninja**. Lokale Abweichungen gehören in `CMakeUserPresets.json`, das nicht eingecheckt wird.
- **Abhängigkeiten** ausschließlich über das **vcpkg-Manifest** (`vcpkg.json`) mit festgeschriebener `builtin-baseline`. Eine Aktualisierung der Baseline ist eine eigene, nachvollziehbare Änderung. Abhängigkeiten, die nur für Tests nötig sind, stehen im Feature `tests`.
- **Jedes Ziel** bekommt seine Einstellungen über `ngui_configure_target()` (`cmake/NguiCompilerOptions.cmake`): C++23 ohne Compiler-Erweiterungen, Warnungsstufe, optional Warnungen als Fehler (`NGUI_WARNINGS_AS_ERRORS`, im Preset `ci` aktiv) und `-ffile-prefix-map` für reproduzierbare Builds.
- **Tests** mit GoogleTest. Sie werden über `gtest_discover_tests` bei CTest registriert. Das Preset `ci` bricht ab, wenn keine Tests gefunden werden.
- **Prüfung vor jedem Commit:** `cmake --workflow --preset ci` (konfigurieren, bauen, testen mit Warnungen als Fehler).
- **Reproduzierbare Builds:** Gleicher Commit und gleicher Compiler ergeben byte-identische Artefakte, unabhängig vom Ablageort. Dafür sorgen `-ffile-prefix-map` und `-Wdate-time` (kein `__DATE__`/`__TIME__`). Der Build-Workflow baut die Bibliothek an zwei Orten und vergleicht die Prüfsummen (Job `reproducible`, Teil des Pflicht-Checks).
- **SBOM und Schwachstellen-Scan:** Die SBOM (SPDX 2.3) fasst die SPDX-Dokumente zusammen, die vcpkg je Port erzeugt, ohne das Feature `tests` und ohne vcpkgs Hilfsports (`.github/scripts/vcpkg-sbom.py`). `.github/scripts/osv-scan.py` prüft die Upstream-Quellen gegen OSV, bei jedem PR, auf `main` und wöchentlich. Eine Ausnahme kommt nur mit Begründung und Ablaufdatum in `osv-scanner.toml`, das Gate wird nicht entfernt.
- **Preset `docs`** (`cmake --workflow --preset docs`, GCC oder Clang): instrumentierter Build (`NGUI_COVERAGE`), Testlauf mit JUnit-Ausgabe (`build/docs/test-results.xml`) und API-Referenz mit Doxygen (`NGUI_BUILD_API_DOCS`, Ziel `ngui_api_docs`). Der Docs-Workflow erzeugt daraus die Doku-Site (Abschnitt 8.3).

### 8.3 Tests, Nachweis und API-Dokumentation

- **Begründungsblock:** Ein Test beginnt mit drei Kommentaren in genau dieser Reihenfolge: `What is tested:`, `Success criteria:`, `Why it matters:`. Ein Label darf über mehrere Zeilen gehen, ein viertes Label gibt es nicht. Folgen Given/When/Then-Kommentare, steht davor eine Zeile, die nur `//` enthält. Für einen Test ohne getrennte Phasen genügt `// Given / When / Then` bzw. `// When / Then`, die Phasen-Kommentare sind optional.

  ```cpp
  TEST(RectTest, IntersectionIsHalfOpen)
  {
  	// What is tested: ...
  	// Success criteria: ...
  	// Why it matters: ...
  	//
  	// Given
  	...
  }
  ```

- Aus den Blöcken und dem Testlauf erzeugt `.github/scripts/generate-test-catalog.py` die Seite **Test-Nachweis** der Doku-Site. Der Build-Workflow prüft die Form der Blöcke bei jedem PR (`--check`). Die erzeugten Seiten (`docs/tests/`, `docs/project/`) werden **nicht** eingecheckt und nicht von Hand bearbeitet.
- **Fuzzing:** Code, der Daten von außen verarbeitet oder Grenzen berechnet (Region, Bilder, Schriften, serialisierte DisplayLists …), bekommt ein Fuzz-Ziel `tests/fuzz/<Name>Fuzz.cpp` mit `NGUI_FUZZ_TARGET(Name) { … }` aus `tests/fuzz/FuzzTarget.h`. Der Rumpf prüft die Invarianten mit `NGUI_FUZZ_CHECK` und trägt denselben Begründungsblock wie ein Test. Ein Ziel läuft auf zwei Arten:
  - **Regression, in jedem Testlauf** und mit jedem Compiler als Test `FuzzRegressionTest.<Name>`: die leere Eingabe und alle Dateien in `tests/fuzz/corpus/<Name>/`.
  - **Fuzzing, nachts** im Workflow `fuzz.yml`: libFuzzer mit AddressSanitizer und UndefinedBehaviorSanitizer (Preset `fuzz`, nur Clang, `CC=clang CXX=clang++`), je Ziel ein eigener Job. Lokal: `cmake --preset fuzz && cmake --build --preset fuzz`, dann `build/fuzz/tests/ngui_fuzz_<Name> <Arbeitsverzeichnis>`.

  Ein Fund (Artefakt `fuzz-finding-<Name>`) kommt nach `tests/fuzz/corpus/<Name>/` und ist damit dauerhaft eine Regressionseingabe. Danach wird der Code korrigiert. Die Ziele werden über den Dateinamen gefunden, eine Liste der Ziele gibt es nicht.
- **API-Dokumentation:** Jedes Element der öffentlichen Header bekommt einen Doxygen-Kommentar `/*! */` (bei Aufzählungen und Feldern auch nachgestellt als `/*!< */`). Doxygen bricht bei einem undokumentierten Element ab. Namensräume und die Hauptseite der Referenz sind in `docs/api/*.dox` dokumentiert.

### 8.4 Release

1. **Versions-Commit** (per PR): die Version in `CMakeLists.txt` (`project(... VERSION x.y.z)`) und in `vcpkg.json` (`version-semver`) setzen. Der Release-Workflow bricht ab, wenn beide nicht zur Version des Tags passen. Ein Pre-Release-Tag `vX.Y.Z-rc.N` gibt die Version `X.Y.Z` frei.
2. **Release** auf GitHub mit dem Tag `vX.Y.Z` auf dem gemergten Commit in `main` veröffentlichen. Der Workflow `release.yml` baut aus dem Tag das Quellarchiv `newgui-X.Y.Z.tar.gz` und die SBOM `newgui-X.Y.Z.spdx.json`, hängt beide an das Release und ergänzt eine signierte SLSA-Provenance (`newgui-X.Y.Z.intoto.jsonl`).
3. **Reproduzierbarkeit:** Alle Zeitstempel kommen aus dem getaggten Commit (`SOURCE_DATE_EPOCH`), ein manuell gepflegter Zeitstempel ist nicht nötig. Jede Kopie des Quellarchivs lässt sich nachbauen und gegen das Release-Asset prüfen:

   ```sh
   git archive --format=tar --prefix=newgui-X.Y.Z/ vX.Y.Z | gzip -9n | sha256sum
   gh release download vX.Y.Z --pattern '*.tar.gz' --output - | sha256sum
   ```

4. **Herkunft prüfen** mit [slsa-verifier](https://github.com/slsa-framework/slsa-verifier):

   ```sh
   slsa-verifier verify-artifact newgui-X.Y.Z.tar.gz \
   	--provenance-path newgui-X.Y.Z.intoto.jsonl \
   	--source-uri github.com/dirkjink/NewGUI --source-tag vX.Y.Z
   ```
