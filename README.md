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
| `docs` | Debug | Coverage, JUnit-Ausgabe der Tests, API-Referenz (braucht GCC oder Clang, Doxygen 1.9.8+) |
| `fuzz` | RelWithDebInfo | libFuzzer-Programme mit ASan und UBSan (braucht Clang: `CC=clang CXX=clang++`) |
| `skia` | RelWithDebInfo | mit Skia aus dem vcpkg-Port (Feature `skia`) und dem Skia-Backend (Bibliothek `ngui_skia`); der erste Build von Skia dauert lange, danach kommt es aus dem Binär-Cache von vcpkg |

Die CI (`.github/workflows/build.yml`) führt den Workflow `ci` mit GCC 13 und 14, Clang 19 und 20 unter Linux sowie MSVC 2022 unter Windows aus. vcpkg wird dort genau auf dem Commit der `builtin-baseline` ausgecheckt. Der Job `Build` fasst die Ergebnisse zusammen und ist Pflicht-Check für `main`.

Der Build-Workflow prüft außerdem, dass der Build reproduzierbar ist (die Bibliothek wird an zwei Orten gebaut, die Prüfsummen müssen übereinstimmen). Daneben erzeugt er eine SBOM (SPDX 2.3) der ausgelieferten vcpkg-Abhängigkeiten und prüft sie gegen die [OSV-Datenbank](https://osv.dev/), auch wöchentlich ohne neuen Commit. Einzelheiten stehen in [`CONVENTIONS.md`](CONVENTIONS.md), Abschnitt 8.2.

Releases (`release.yml`) tragen ein reproduzierbares Quellarchiv, die SBOM und eine signierte SLSA-Provenance. Der Ablauf und die Prüfung stehen in [`CONVENTIONS.md`](CONVENTIONS.md), Abschnitt 8.4.

Weitere Workflows: `docs.yml` (Doku-Site nach GitHub Pages, bei PRs nur gebaut), `codeql.yml` (statische Analyse von C++ und den Workflows), `scorecard.yml` (OpenSSF Scorecard), `release.yml` (Release-Assets mit Provenance), `fuzz.yml` (nächtliches Fuzzing mit libFuzzer). Dependabot hält die vcpkg-Baseline und die gepinnten Actions aktuell.

## Lizenz

MIT, siehe [`LICENSE`](LICENSE). Code, der aus Haiku übernommen wird, steht ebenfalls unter der MIT-Lizenz und behält seine ursprünglichen Copyright-Hinweise (siehe [`CONVENTIONS.md`](CONVENTIONS.md), Abschnitt 4.4).
