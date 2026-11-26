# Sicherheitsrichtlinie

## Unterstützte Versionen

NewGUI ist in der frühen Entwurfs- und Port-Phase, es gibt noch keine Freigabe. Sicherheitskorrekturen fließen in `main` ein. Ab der ersten Freigabe erhält nur die jeweils neueste Version Korrekturen.

## Eine Schwachstelle melden

Schwachstellen bitte **nicht** als öffentliches Issue melden, sondern vertraulich über
[GitHub Security Advisories](https://github.com/dirkjink/NewGUI/security/advisories/new).

Hilfreich sind:

- eine Beschreibung der Schwachstelle und ihrer Auswirkung
- die betroffene Version bzw. der Commit
- Schritte zur Reproduktion oder ein Proof of Concept, falls vorhanden

Eine Eingangsbestätigung kommt innerhalb weniger Tage. Bitte vor einer Veröffentlichung angemessen Zeit für eine Korrektur lassen.

## Hinweise zum Umfang

NewGUI verarbeitet Daten, die nicht von der Anwendung selbst stammen: Eingaben von Tastatur, Maus und Zwischenablage, Bilder, Schriften und serialisierte DisplayLists. Ausdrücklich willkommen sind Meldungen über **Speicherfehler** (Überläufe, Use-after-free, undefiniertes Verhalten) beim Verarbeiten solcher Daten und über die **Fernsteuerung** (Spezifikation, Abschnitt 20.7), sobald es sie gibt.

Bereits umgesetzte Maßnahmen:

- **Der Code wird statisch analysiert.** Der Workflow CodeQL untersucht die Bibliothek (C++) und die Workflow-Definitionen bei jeder Änderung und wöchentlich. Die Ergebnisse stehen unter Security → Code scanning.
- **Abhängigkeiten werden laufend geprüft.** Der Build-Workflow erzeugt eine SBOM (SPDX 2.3) der ausgelieferten vcpkg-Abhängigkeiten und schlägt fehl, wenn eine davon von einer in [OSV](https://osv.dev/) bekannten Schwachstelle betroffen ist, bei jeder Änderung und wöchentlich. Die SBOM liegt jedem Lauf als Artefakt bei. Dependabot schlägt Aktualisierungen der vcpkg-Baseline und der per SHA gepinnten Actions vor.
- **Der Build ist reproduzierbar.** Gleicher Commit und gleicher Compiler ergeben byte-identische Artefakte. Der Build-Workflow prüft das bei jeder Änderung.
- **Die Aufstellung des Repositorys wird öffentlich bewertet.** Das OpenSSF-Scorecard-Badge im README verweist auf die aktuelle Bewertung je Prüfung. Sie ist ein Hinweis auf die Aufstellung, keine Note: Einige Prüfungen setzen ein Projekt mit mehreren Maintainern und Pflicht-Reviews voraus und fallen bei einem einzelnen Maintainer zwangsläufig niedrig aus. Bewusst in Kauf genommene Abzüge sind an der Stelle der Entscheidung begründet, siehe den Kommentar zu `repo_token` in `.github/workflows/scorecard.yml`.

- **Release-Assets tragen eine SLSA-Provenance.** Der Release-Workflow baut Quellarchiv und SBOM aus dem Release-Tag, hängt sie an das GitHub-Release und ergänzt eine mit Sigstore signierte SLSA-Provenance (`*.intoto.jsonl`), prüfbar mit [slsa-verifier](https://github.com/slsa-framework/slsa-verifier). Das Quellarchiv ist reproduzierbar, die Provenance gilt also für jede nachgebaute Kopie (`CONVENTIONS.md`, Abschnitt 8.4).

- **Grenzflächen werden gefuzzt.** Code, der Daten von außen verarbeitet, bekommt ein libFuzzer-Ziel mit seinen Invarianten (`CONVENTIONS.md`, Abschnitt 8.3). Der Workflow Fuzz untersucht es jede Nacht mit AddressSanitizer und UndefinedBehaviorSanitizer, jeder Testlauf spielt die eingecheckten Funde erneut ab. Ziele sind `Rect` (Mengenoperationen der Rechtecke, Normalisierung abgerundeter Rechtecke) `Region` (die Clip-Region, gegen ein Bitmap-Modell geprüft) `Gradient` (Farbberechnung linearer Gradienten) `DisplayList` (Deserialisierung beliebiger Bytes, kanonischer Rundlauf) `Canvas` (beliebige Aufruffolgen; was die Canvas aufzeichnet, muss die Deserialisierung annehmen) und `Dump` (Text- und JSON-Ausgabe beliebiger Listen, JSON stets wohlgeformt); weitere folgen mit den nächsten Parsern (Bilder, Schriften). Die Scorecard-Prüfung „Fuzzing“ sucht libFuzzer-Ziele nur, wenn C++ einen großen Anteil am Repository hat. Ihr Wert folgt also dem Sprachanteil, nicht dem tatsächlichen Fuzzing. Maßgeblich ist der Verlauf des Workflows Fuzz.

- **Look-Plugins laden nur, wer es ausdrücklich will.** Ein Look-Plugin ist fremder Code, der mit den Rechten der Anwendung läuft. NewGUI lädt Plugins nur, wenn die Anwendung `LookRegistry::LoadPlugin()` oder `LoadPlugins()` aufruft oder der Start die Umgebungsvariable `NGUI_LOOK_PATH` vorfindet, nie aus einem Standardordner. Unter Windows sucht `LoadLibraryEx` die Abhängigkeiten eines Plugins nur in dessen Verzeichnis und den Systemverzeichnissen, nicht im aktuellen Verzeichnis oder in `PATH`. Ein Plugin, das für einen anderen Build oder eine andere Schnittstellenversion gebaut ist, wird abgelehnt.
