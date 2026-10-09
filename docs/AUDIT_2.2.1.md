# AmiMAIL Audit und Fehlerbereinigung

Stand: 9. Oktober 2026. Ausgangspunkt ist AmiMAIL 2.2.1 auf `main`, Commit
`b8bfa79dc207066e6703bcfee220bf93eb9413f5`. Alle Änderungen liegen auf
[audit](https://github.com/Andiweli/AmiMAIL/tree/audit). Die Versionsnummer bleibt
2.2.1. `main`, andere Branches, Tags und GitHub-Releases wurden nicht verändert.

24 bestätigte Befundgruppen wurden bewertet: 6 mit hoher, 16 mit mittlerer und
2 mit niedriger Priorität. 23 sind im erlaubten Umfang abgeschlossen, davon
20 technische Bereinigungen und 3 Korrekturen der Dokumentation beziehungsweise
Lizenz. Der bestehende Release-Tag bleibt wegen der ausdrücklichen Vorgabe,
Tags nicht zu ändern, offen. Diese Zahlen zählen Befundgruppen, keine einzelnen
Testfälle oder jede Änderung an einer Codezeile.

Release und Debug wurden mit der m68k-Amiga-Toolchain vollständig gebaut und
gelinkt. Die Host-Suite und gezielte Speicherprüfungen bestehen. Ein fehlerfreier
Betrieb auf echter Amiga-Hardware ist damit noch nicht nachgewiesen. Der Nutzer
übernimmt die Tests vor Ort.

## Bestätigte Befunde und Änderungen

| ID | Priorität | Befund und abschließende Behandlung | Betroffene Dateien |
| --- | --- | --- | --- |
| F01 | Hoch | Unterbrochene Kontaktspeicherung konnte die alte `.old`-Datei übersehen und Daten verlieren. Gemeinsame sichere Ersetzung und Wiederherstellung von `.bak` sowie historischen `.old`-Dateien implementiert. | `src/contacts.c`, `src/fileio.c`, `include/fileio.h` |
| F02 | Mittel | Fehler beim Öffnen des Kontaktbuchs erschienen als leeres Buch. Nur eine tatsächlich fehlende Datei gilt jetzt als leer; andere Lesefehler erhalten vorhandene Daten und liefern einen Fehler. | `src/contacts.c` |
| F03 | Hoch | Das Quell-ZIP konnte Git-Metadaten und ignorierte private OAuth-Dateien enthalten. Es archiviert jetzt ausschließlich versionierte Arbeitsdateien, schließt private Header und Ausgaben aus und lehnt Symlinks beziehungsweise Pfade außerhalb des Repositories ab. | `Makefile`, `tools/source_dist.py` |
| F04 | Mittel | SMTP zerlegte Kommata und Semikolons in zitierten Namen wie `"Doe, Jane" <jane@example.com>`. Der Parser berücksichtigt Quotes, Escapes, Kommentare, Winkelklammern und Gruppen und erhält bestehende Semikolon-Listen. | `src/smtp.c` |
| F05 | Hoch | IMAP FETCH verlor UID oder Größe, wenn diese erst nach einem Nachrichtenliteral kamen. Literale werden vollständig übersprungen und Metadaten vor und nach ihnen ausgewertet. Mailinhalt wird dabei nicht als Serverantwort interpretiert. | `src/imap.c` |
| F06 | Mittel | LIST/XLIST unterstützte Mailboxnamen als Literale unvollständig. Länge, Abschluss, Grenzen, eingebettete NUL-Bytes und Speicherfehler werden jetzt geprüft. | `src/imap.c` |
| F07 | Mittel | Teilzeichenfolgen konnten IMAP-Flags falsch zuordnen, etwa `\SeenOther` als `\Seen`. Exakte Token-Grenzen eingeführt, auch für Special-Use-Flags. | `src/imap.c`, `src/imap_parser.c` |
| F08 | Mittel | IMAP-Mailboxquotierung konnte Speicherfehler übergehen und Zeilenumbrüche in Befehle übernehmen. Append-Fehler werden weitergereicht; CR/LF wird abgewiesen. | `src/imap.c` |
| F09 | Mittel | Die dokumentierte Speicherung des letzten Anhangsverzeichnisses fehlte; der bestehende Dialogtest konnte deshalb nicht kompilieren. Persistenz, Rückfall bei fehlenden Laufwerken, begrenzte Pfade und sichere Dateiersetzung hergestellt. Export verwendet die tatsächliche Anhangsliste nach der Vorbereitung. | `src/gui_attachments.c` |
| F10 | Niedrig | Der Kontakt-Icon-Test erwartete eine alte Pixelposition und wurde von `review-test` nicht ausgeführt. Erwartung an den vorhandenen nativen Header-Inset angepasst; beide Kontaktprüfungen integriert. Keine visuelle Produktionsänderung zur Anpassung an den Test. | `tests/test_contact_sort_icons.py`, `Makefile` |
| F11 | Hoch | TCP-Verbindungsaufbau und TLS-Handshake konnten blockieren; TLS-Retries starteten den Timeout erneut. Sockets bleiben nichtblockierend. Timer-Deadlines umfassen sämtliche Retries und vollständige Pufferwrites. Abbruch weckt den Worker; Stop-Signal und Nachricht werden vor seiner möglichen Beendigung gemeinsam geschützt. | `src/tls.c`, `src/network_task.c` |
| F12 | Mittel | Gleichzeitig laufende Worker nutzten gemeinsame Fehlernummern; bei teilweise erfolgreicher AmiSSL-Initialisierung fehlte ein Cleanup-Pfad. Fehlerstorage liegt jetzt pro bsdsocket-Opener vor; erfolgreiche Teilinitialisierung wird auf Fehlern bereinigt. Deskriptorgrenzen werden vor FD_SET geprüft. | `src/tls.c` |
| F13 | Mittel | README beschrieb ein obligatorisches Masterpasswort und ausschließlich PBKDF2-basierte Speicherung. Tatsächlich erstellt die aktuelle GUI zufällige lokale Schlüssel. Automatische Anmeldung bleibt auf Nutzerwunsch erhalten; die Schutzgrenzen sind korrekt dokumentiert. | `README.md`, `docs/ARCHITECTURE.md`, beide AmigaGuides |
| F14 | Mittel | Die Aussage ohne lokalen Mailcache erklärte die Klartext-Spool- und Wiederherstellungsdateien nicht. Speicherorte und Aufbewahrung bei fehlgeschlagenen Transfers werden beschrieben. Wiederherstellungsdateien werden nicht blind gelöscht. | `README.md`, `docs/ARCHITECTURE.md`, beide AmigaGuides |
| F15 | Mittel | Native und Host-Ziele konnten dieselben Abhängigkeitsdateien einlesen; Debug benutzte ein paralleles Clean und gemeinsame Objekte. Release und Debug haben getrennte Objekte; Host-Prüfungen lesen keine nativen `.d`-Dateien. | `Makefile` |
| F16 | Mittel | Release-Icon-Pfad und Ignore-Muster verwendeten den falschen Dateinamen beziehungsweise die falsche Großschreibung. An vorhandene Assets angepasst; generierte Verzeichnisse werden vollständig ignoriert. | `Makefile`, `.gitignore` |
| F17 | Mittel | Pakete enthielten keine konsistenten Installer-Quelldateien und keine Benachrichtigungssounds. Beide Archivformate verwenden nun dieselbe vollständige Stage. Der Installer nutzt das tatsächlich vorhandene Icon; die alte Auswahl bleibt erhalten, wenn beide separaten Varianten bereitgestellt werden. Zielpfade nutzen Amiga-konformes `tackon`. | `tools/release_stage.py`, `_Installer/Install-AmiMAIL`, `Makefile` |
| F18 | Mittel | README und LICENSE widersprachen sich. Auf ausdrückliche Nutzerentscheidung wurde die ursprüngliche Non-Commercial Source License aus Commit `e360ea9` wiederhergestellt. Frühere Lizenzgewährungen bleiben durch deren Abschnitt 7 berücksichtigt. | `LICENSE` |
| F19 | Niedrig | Zwölf überholte Quelltextkopien waren als `.bak` versioniert. Aus dem aktuellen Baum entfernt; die Git-Historie bleibt erhalten. | zwölf frühere `.bak`-Dateien unter `src/` und `include/` |
| F20 | Hoch | Neue Kontoschlüssel wurden vor der Konfiguration ersetzt und bei anschließendem Fehler gelöscht. Neue Schlüssel werden jetzt zuerst als `.key.pending` gespeichert. Nach dem Konfigurationscommit werden sie übernommen; der Loader erkennt einen dazu passenden Zwischenschlüssel nach einem Abbruch. Alte Schlüssel bleiben bei fehlgeschlagenem Save erhalten. | `src/storage.c` |
| F21 | Hoch | HTTPS/OAuth überging fehlgeschlagene Buffer-Allokationen, wodurch NULL-Puffer an Stringparser gelangen konnten. Alle betroffenen Append- und Terminierungsschritte werden geprüft. Fehlerhaftes HTTP liefert keinen teilweise übernommenen Antwortinhalt. | `src/tls.c`, `src/oauth.c` |
| F22 | Mittel | HTTP akzeptierte abgeschnittene beziehungsweise fehlerhaft begrenzte Antworten. Chunk-Überläufe, Endmarkierung, Trailer, gemischte Header-Großschreibung, doppelte Längenfelder und Content-Length werden geprüft. OAuth und Update verwenden denselben Chunk-Decoder. Ordnungsgemäßes Verbindungsende wird von einem I/O-Fehler unterschieden. | `src/tls.c`, `src/update.c`, `include/tls.h` |
| F23 | Mittel | OAuth-Laufzeiten akzeptierten negative, übergroße oder nachfolgend ungültige Werte. Begrenzte Dezimalauswertung eingeführt; rohe Steuerzeichen in JSON-Strings werden abgewiesen. Vorhandene Tokens bleiben bei Parse- und Speicherfehlern erhalten. | `src/oauth.c` |
| F24 | Mittel | `v2.2.1` zeigt weiter auf `acb7dffba69442d3166269ef4e0f90ddd0ef5d21`, dessen Makefile 2.2.0 enthält. Der maßgebliche geprüfte Quellstand ist `main` bei `b8bfa79…` und der darauf aufbauende Branch `audit`. Tag bleibt wegen der Workflow-Vorgabe unverändert. | externe Release-Metadaten; keine Änderung |

POP3 ist im Repository nicht implementiert. Die Protokollkorrekturen betreffen
IMAP, SMTP, HTTPS/OAuth und TLS. Es wurden keine neuen Laufzeitabhängigkeiten
eingeführt; die ergänzten Build-Werkzeuge verwenden die bereits benötigte
Python-Standardbibliothek. Die Konfigurationsformate und Legacy-APIs bleiben
erhalten. `.key.pending` ergänzt die Wiederherstellung, ohne vorhandene
Kontodateien voraussetzungslos zu konvertieren.

## Prüfergebnisse

| Prüfung | Ergebnis und Aussagegrenze |
| --- | --- |
| m68k Release und Debug | Vollständiger Neuaufbau und Link mit `m68k-amigaos-gcc`, 68020/Soft-Float, NDK und AmiSSL 5.27 SDK erfolgreich. Nach der letzten Worker-Änderung beide Executables erneut kompiliert und gelinkt. Keine Compiler- oder Linkerwarnungen in diesen Builds. Die geänderten nativen Module wurden zusätzlich mit `-Werror` gegen die SDK-Header geprüft. |
| `make review-test host-check` | Erfolgreich einschließlich aller bestehenden und neu integrierten Host-Ziele. Core: 506 Prüfungen; Review: 5689; Datei-Fehlerpfade: 80; SMTP-Stream: 43. |
| Protokoll und Dateien | Mailfile: 87669; IMAP-Dateitransfer: 14507; SMTP-Dateitransfer: 173; Ordnerfortschritt: 100747; IMAP-Mutationen: 11450; MIME-Parität: 10 Fixtures ohne Unterschiede. |
| Neue Regressionen | Kontakte: 35; SMTP-Empfänger: 60; IMAP-Kompatibilität: 257. Weitere Tests prüfen native Timer-/Socket-Retries, endgültige Teilwrites, Schlüsselpaare und HTTP-/OAuth-Allokationsfehler. |
| GUI und Nebenläufigkeit | Bestehende Dialog-, Kontakte-, Composer-, Herald-, Sound-, Checkbox- und Requester-Prüfungen bestanden. Worker-Queue nach abschließender Ergänzung: 296 Prüfungen. Native Syntaxprüfungen für die GUI-Module und den Worker bestanden. Dies sind API-Doubles, keine tatsächlichen ReAction- oder Exec-Laufzeitprüfungen. |
| Kataloge | 571 IDs, Quellen und Binärkatalog konsistent; 32 Katalogtests bestanden. Keine Versionsänderung. |
| ASan und UBSan | Core/Review/Fileio/SMTP-Stream sowie die neuen Kontakt-, SMTP-, IMAP-, HTTP/OAuth-, Timer-, Schlüssel- und relevanten Dialog-/Worker-Tests bestanden. Keine gemeldeten Speicher- oder Undefined-Behavior-Fehler in diesen Läufen. Dies ist kein Beweis für alle Speicherpfade auf einem 32-Bit-Amiga. |
| Statische Analyse | GCC 13 Analyzer über 25 Host-Module abgeschlossen. Eine Warnung in `src/mailto.c:178` blieb als nicht bestätigter Analyzer-Alarm erhalten; siehe unten. Native-only Pfade wurden zusätzlich durch Quellprüfung, API-Doubles und m68k-Kompilation abgedeckt. |
| Quellarchiv | Erstellung und Regressionen bestanden. Das ZIP enthält versionierte Arbeitsdateien, keine `.git`-Daten, private OAuth-Header, `.bak`-Quellen oder Build-Verzeichnisse. Fehler ersetzen ein vorhandenes ZIP nicht. |
| Paketlayout | Zwei reale Datei-/Archivtests bestanden; Tar-Paket lokal erzeugt. Programm, Icon, Installer-Setup, Guides, CD/CT/Katalog und Sounds sind vorhanden. Installer wurde strukturell geprüft, aber nicht durch Installer V43 auf einem Amiga ausgeführt. |
| LHA | Lokale Erstellung mit dem vorhandenen SDK-Archivierer scheiterte an dessen fest verwendetem `/tmp/lhXXXXXX`-Pfad. Die gemeinsame Stage ist geprüft; ein erfolgreich erzeugtes LHA und dessen CRC-Test werden nicht behauptet. Kein GitHub-Release wurde angelegt. |

Die Analyzer-Warnung führt über `decode_component` zu `duplicate_text`. Der
gemeldete Pfad überspringt die Eingabeschleife trotz eines zuvor nichtleeren
Adressanteils. Tatsächlich schreibt die Schleife jedes ausgegebene Byte;
`decoded[out_pos] = 0` terminiert auch den leeren Fall, bevor `strlen` und
`memcpy` die Zeichenkette verwenden. Deshalb ist daraus kein bestätigter Zugriff
auf uninitialisierte Daten abgeleitet worden. Die Warnung wurde weder per Flag
noch per Pragma unterdrückt. Die Host-Compiler und der Crosscompiler melden
mit ihren normalen Warnungen keinen entsprechenden Fehler.

## Grenzen und Tests auf dem echten Amiga

Die m68k-Binaries wurden nicht auf echter Hardware oder in einem AmigaOS-Emulator
ausgeführt. Besonders zu prüfen sind:

1. Konten mit automatischer Anmeldung laden und ändern; Sicherung der bisherigen
   `ENVARC:AmiMail`-Dateien vor Tests. Unterbrochenes Speichern und Wiederaufnahme
   eines passenden `.key.pending` prüfen.
2. Netzwerk während Connect, TLS-Handshake, Empfang und Versand trennen; Abbruch
   und Programmende kontrollieren. DNS reagiert nur dann auf den Timer-/Abbruch-
   BREAKMASK, wenn der installierte Stack diese Unterbrechung unterstützt.
3. TLS-Zertifikatsprüfung, STARTTLS und erneute Verbindungen mit dem verwendeten
   Stack/AmiSSL testen. Die Host-TLS-Doubles prüfen die Retry-Logik, kein reales
   Zertifikat oder echte SSL-Records.
4. Nachrichten mit UID nach dem FETCH-Literal, ungewöhnlichen Ordnernamen und
   Empfängern wie `"Doe, Jane" <jane@example.com>` abrufen beziehungsweise senden.
5. Anhangsauswahl, große Anhänge, zuletzt verwendetes Verzeichnis, fehlende
   Laufwerke und nicht beschreibbare Ziele prüfen. Kontakte speichern/laden und
   die Sortierpfeile mit unterschiedlichen Fonts, Fenstern und Screens ansehen.
6. Optional Installer mit Sprachwahl und Sounds aus dem `Install`-Drawer testen;
   die tatsächliche grafische Bedienung bleibt unbestätigt.

Automatische Anmeldung bleibt erhalten. Wer Konfiguration und zugehörige
Schlüssel lesen kann, kann Zugangsdaten entschlüsseln. Mail- und Recovery-Spools
sind Klartext. Diese vom Nutzer bestätigten Betriebsentscheidungen wurden
dokumentiert; es wurde kein Masterpasswort-Modus eingeführt und kein
unbestätigter Versand als erfolgreich behandelt.

## Commits und geänderte Dateien

Jeder der folgenden Commits wurde auf `audit` gepusht und danach mit dem
Remote-Branch-SHA abgeglichen. Der Abschlussbericht wird separat versioniert.

| Commit | Inhalt |
| --- | --- |
| `f4d60e3cd9181ad2eb95ffc98508c7728618ec98` | Kontaktbuch und Wiederherstellung |
| `49270f3f7875b2e5388b6677f9f43057d78f660d` | SMTP-Empfänger und IMAP-Literale |
| `540bfd44c76dfaac3f9d3398d8c050a8c3f45c12` | Anhangsverzeichnis und Kontakttests |
| `c0068ce77af7ae469ac7d88a8aebbe59037fdd2d` | Nichtblockierende Netzwerkoperationen und getrennte Fehlernummern |
| `51beeff2ee2a12db2de7811613679dd619fefd2d` | Wiederherstellbare Kontoschlüssel |
| `e4af230844868aef7bbff5f970153f0c4485eb49` | HTTP-/OAuth-Fehlerbehandlung |
| `975a246b4e937164205eb79fbb2e8924d06b2299` | Builds, Pakete und Quelltext-Backups |
| `1f69cb5b2cc116aa6669ec981f0cc1c2932269a0` | Lizenz und Schutzgrenzen |
| `1786147ff4ea727e241dbb81e220fffdb3b079a8` | Reihenfolge von Abbruchsignal und Stop-Bestätigung |

Produktionscode: `src/contacts.c`, `src/fileio.c`, `src/gui_attachments.c`,
`src/imap.c`, `src/imap_parser.c`, `src/network_task.c`, `src/oauth.c`,
`src/smtp.c`, `src/storage.c`, `src/tls.c`, `src/update.c`;
Header: `include/fileio.h`, `include/tls.h`.

Build/Pakete: `.gitignore`, `Makefile`, `_Installer/Install-AmiMAIL`,
`tools/release_stage.py`, `tools/source_dist.py`, `tools/static_analysis.py`.
Dokumentation: `LICENSE`, `README.md`, `docs/ARCHITECTURE.md`,
`_Guides/AmiMAIL_DE.guide`, `_Guides/AmiMAIL_EN.guide`, dieser Bericht.

Tests: `tests/test_contact_sort_icons.py`, `tests/test_contacts_recovery.c`,
`tests/test_https_oauth.c`, `tests/test_https_oauth.py`, `tests/test_imap_compat.c`,
`tests/test_packaging.py`, `tests/test_progress_context.c`,
`tests/test_smtp_recipients.c`, `tests/test_storage_keypair.c`,
`tests/test_storage_keypair.py`, `tests/test_tls_deadlines.c`,
`tests/test_tls_deadlines.py`.

Entfernte, weiterhin in der Git-Historie verfügbare Backups:
`include/amigmail.h.version-repair-1.2.bak`,
`include/storage.h.20260820-ui-master.bak`,
`src/app.c.20260820-combined-fix.bak`,
`src/gui_dialogs.c.20260820-combined-fix.bak`,
`src/gui_dialogs.c.20260820-ui-master.bak`,
`src/gui_dialogs.c.888-grey-final.bak`, `src/gui_dialogs.c.bak`,
`src/gui_internal.h.20260820-combined-fix.bak`,
`src/main.c.version-repair-1.2.bak`, `src/splash.c.20260820-ui-master.bak`,
`src/splash.c.888-grey-final.bak`, `src/storage.c.20260820-ui-master.bak`.

Der Vergleich mit `main` umfasst diese Änderungen und keine Verschiebung von
`main`. Es gab keinen Merge, Rebase, Force-Push, PR oder neuen Release-Tag.
