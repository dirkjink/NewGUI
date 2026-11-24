# NewGUI

Ein plattformübergreifendes UI-Toolkit in C++23. Seine Architektur orientiert sich am Interface Kit von [Haiku](https://www.haiku-os.org/): ein Looper pro Fenster, Nachrichten, ein dauerhaft bestehender View-Baum, Constraint-Layout und austauschbare Looks. Gezeichnet wird über eine austauschbare Canvas-Schicht, Ziel-Renderer ist Skia.

**Status:** frühe Entwurfs- und Port-Phase. Es gibt noch keine stabile API.

## Inhalt dieser Site

| Seite | Inhalt |
|---|---|
| [Spezifikation der Canvas-Schicht](project/canvas-core-profil-spezifikation.md) | Canvas-Schicht, Backends, Looks, Export, Screenshots und Test-Kit |
| [Abschätzung des Gesamtumfangs](project/toolkit-gesamtumfang.md) | Grundgerüst, Controls, Text-Subsystem und Plattformschicht |
| [Konventionen](project/CONVENTIONS.md) | Code und Projekt, Port-Phase und Ziel-Phase |
| [Test-Nachweis](tests/test-evidence.md) | jeder Test des letzten Laufs auf `main`, mit Begründung |
| [Coverage-Bericht](https://dirkjink.github.io/NewGUI/coverage/) | Zeilen- und Zweig-Coverage der Bibliothek, mit Skia-Backend und SDL3-Plattform |
| [API-Referenz](https://dirkjink.github.io/NewGUI/api/) | mit Doxygen aus den öffentlichen Headern erzeugt |

Test-Nachweis, Coverage-Bericht und API-Referenz erzeugt der Docs-Workflow bei jeder Änderung auf `main` aus einem echten Lauf. Keine dieser Zahlen ist von Hand eingetragen.

Quellcode, Build-Anleitung und Lizenz: [github.com/dirkjink/NewGUI](https://github.com/dirkjink/NewGUI)
