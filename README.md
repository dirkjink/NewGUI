# NewGUI

Ein plattformübergreifendes UI-Toolkit in C++23. Seine Architektur orientiert sich am Interface Kit von [Haiku](https://www.haiku-os.org/): ein Looper pro Fenster, Nachrichten, ein dauerhaft bestehender View-Baum, Constraint-Layout und austauschbare Looks. Gezeichnet wird über eine austauschbare Canvas-Schicht, Ziel-Renderer ist Skia.

**Status:** frühe Entwurfs- und Port-Phase. Es gibt noch keine stabile API.

## Dokumente

| Dokument | Inhalt |
|---|---|
| [`canvas-core-profil-spezifikation.md`](canvas-core-profil-spezifikation.md) | Spezifikation der Canvas-Schicht, der Backends, Looks, Export, Screenshots und des Test-Kits |
| [`toolkit-gesamtumfang.md`](toolkit-gesamtumfang.md) | Abschätzung für Grundgerüst, Controls, Text-Subsystem und Plattformschicht |
| [`CONVENTIONS.md`](CONVENTIONS.md) | Konventionen für Code und Projekt (Port-Phase und Ziel-Phase) |

## Bauen

**Voraussetzungen:**

- CMake 3.25 oder neuer und Ninja
- ein C++23-Compiler: GCC 13+, Clang 17+ oder MSVC 2022
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

## Lizenz

MIT, siehe [`LICENSE`](LICENSE). Code, der aus Haiku übernommen wird, steht ebenfalls unter der MIT-Lizenz und behält seine ursprünglichen Copyright-Hinweise (siehe [`CONVENTIONS.md`](CONVENTIONS.md), Abschnitt 4.4).
