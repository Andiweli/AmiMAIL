# AmiMAIL 2.2.2 – Kontaktaktionen und GURU-Nachprüfung

Stand: 10.10.2026. Ausgangspunkt dieser Nachprüfung ist der ausgelieferte
Audit-Commit 64ad8b5d9556c01bd2b229be3611781812423e54. Die Änderungen bleiben
ausschließlich auf audit; Version, Kataloge und Guides werden nicht geändert.

Die Codekorrekturen wurden getrennt gepusht und mit dem Remote-SHA abgeglichen:

- 520dd13eec1b946f006db38cd0e8ec105105c179: C-Speicherpool und FILE-Verwaltung.
- 498b518d7bda0408ee191b3d9746a05a4eb9c7c7: letzte Worker-Bestätigung.

## Gerätedaten und Eingrenzung

Der Benutzer hat die korrekte Darstellung der Ja/Nein-Requester und das
Verschwinden der Gmail-Wurzel auf echter Hardware bestätigt. Anschließend
reagierten Kontaktaktionen zeitweise nicht; nach einem Neustart funktionierten
sie wieder. Wiederholtes Öffnen verschiedener Dialoge, Abbrechen der
Löschbestätigungen sowie Beenden und erneutes Starten führte zu einem Reset mit
GURU 8100 0005 0A7FEA98. AmigaOS: 3.2.3; Start über Workbench; Icon-Stack: 100000.

8100 0005 bezeichnet eine beschädigte Speicherliste beim Freigeben von Speicher.
Der Ort der Erkennung muss nicht der Ort der ursprünglichen Beschädigung sein.
Die zweite Zahl erlaubt hier keine Zuordnung zu einer Quellzeile. Siehe
[AmigaOS-Alert-Dokumentation](https://wiki.amigaos.net/wiki/Understanding_the_alert_error_numbers).

Der hier erzeugte m68k-Code hält im Hauptaufruf rund 23 KB Kontodaten auf dem
Stack; der Kontakteditor benötigt knapp 5 KB zusätzlich. Bei 100000 Byte ist
eine gewöhnliche zu kleine Icon-Stackgröße keine belegte Ursache. Das ist kein
vollständiger dynamischer Nachweis des maximalen Stackverbrauchs.

## Bestätigte Fehler und Korrekturen

### Gemeinsamer C-Speicherpool und FILE-Verwaltung

Die NP_Entry-Worker benutzen denselben Programm-Datenbereich wie die GUI.
Die tatsächlich lokal verlinkte libnix/newlib-Laufzeit verwendet einen globalen
MemMap-Allokator. Seine Allokations- und Freigabefunktionen enthalten keine
Task-Synchronisierung. Auch die FILE-Pool-Sperren in findfp.o sind leere
Routinen. Das wurde an den SDK-Headern und am tatsächlichen m68k-Code geprüft,
nicht aus dem Namen der Laufzeitbibliothek abgeleitet. GUI und Worker können
diese Strukturen gleichzeitig verändern und dadurch beschädigen.

src/runtime_lock.c schützt die öffentlichen malloc/calloc/realloc/free- und
fopen/fclose-Einstiege mit einer gemeinsamen rekursiven Exec-SignalSemaphore.
Bei newlib werden zusätzlich deren interne _r-Einstiege sowie __sfp und __sinit
über GNU-Linker-Wrapping geschützt. Die Makefile-Erkennung verwendet die
Header des gewählten Compilers. Ältere libnix-Builds erhalten die öffentlichen
Wrapper, ohne newlib-Symbole vorauszusetzen. Dieser ältere Bibliothekszweig
wurde mit API-Doubles geprüft, nicht mit einer zweiten nativen Toolchain.

Die gesamten calloc/realloc- und fopen/fclose-Aufrufe sind geschützt: Einzelne
interne Aufrufe umgehen Linker-Wrapping innerhalb derselben libc-Objektdatei.
Ein bloßes Ersetzen von __malloc_lock oder der leeren FILE-Sperrhaken wäre
für diese konkrete Laufzeit nicht ausreichend. Rekursion erlaubt innere
Allokationen beim Öffnen/Schließen einer Datei. Auch Fehlerpfade lösen die
Sperre. Netzwerk-Warteoperationen liegen außerhalb der Sperre; beim Öffnen und
Schließen einer Datei können andere C-Pool-Zugriffe kurz auf Datei-I/O warten.
Die rekursive Semantik ist in der
[Exec-Dokumentation](https://developer.amigaos3.net/autodocs/exec.library/ObtainSemaphore.html) beschrieben.

### Worker-Abschluss und Freigabe des Programmsegments

Die letzte STOP-Antwort erfolgte bislang vor der Rückkehr aus network_worker.
Die GUI konnte danach Ressourcen freigeben und das Programm beenden, während
der Worker noch seinen C-Funktionsabschluss im selben Programmsegment ausführte.
Auch im Vergleichsstand main ist diese Reihenfolge vorhanden.

Nach sämtlichen TLS-, IMAP- und Token-Freigaben setzt der Worker nun Forbid,
veröffentlicht den Endzustand, antwortet und kehrt unmittelbar zu DOS zurück.
Danach erfolgt im Worker kein wartender Aufruf und kein Permit mehr. Die
Beendigung des Prozesses liegt außerhalb des AmiMAIL-Programmsegments.
Dieser eng begrenzte Abschnitt verhindert das vorzeitige Ausführen der GUI;
die zuvor korrigierte Absicherung beim Versenden von STOP bleibt erhalten.
Siehe [Forbid](https://developer.amigaos3.net/autodocs/exec.library/Forbid.html)
und [RemTask](https://developer.amigaos3.net/autodocs/exec.library/RemTask.html).

### Diagnose der Kontaktaktionen

Der Kontaktmanager und -editor protokollieren mit aktiviertem
AmiMAIL_RequesterDebug zusätzlich Kontakte öffnen/schließen, Neu/Bearbeiten/
Löschen-Klicks, Editor öffnen/schließen sowie Aufbau- und Öffnungsfehler.
Ziel bleibt T:AmiMAIL-requesters.log. Die Einträge enthalten keine Kontaktnamen,
E-Mail-Adressen oder Feldinhalte. Dialoglayouts, Schaltflächen und
Speicher-/Löschverhalten der Kontakte wurden nicht umgebaut.

Die beiden bestätigten Laufzeitfehler sind plausible Ursachen für die
Instabilität. **Der konkrete GURU und der vorübergehende Ausfall der
Kontaktaktionen sind ohne Gerätetrace noch nicht diesen Fehlern zugeordnet.**
Die neuen Kontakt-Einträge dienen der Eingrenzung, falls der Ausfall verbleibt.

## Validierung und Grenzen

- Vollständige Host- und Regressionstests sowie host-check erfolgreich.
- Neuer Test führt den tatsächlichen Wrapper-Code mit sechs pthread-Tasks und
  Exec-Doubles aus: 22800 Backend-Aufrufe für den libnix-Zweig und 64800 für
  newlib. Er prüft verschachtelte Aufrufe, gleichzeitige Erstinitialisierung,
  fehlgeschlagene Allokationen/Dateiöffnungen/-schließungen sowie den Erhalt
  des alten Puffers nach fehlgeschlagenem realloc.
- Gegenprobe: Allein das Abschalten der Semaphore im temporären Code führt
  zur Verletzung der Prüfung auf gleichzeitige Pool-Benutzer.
- Queue-/Worker-Test: 302 Prüfungen einschließlich Endzustand vor ReplyMsg,
  gesperrter GUI-Ausführung bei der letzten Antwort und direkter Rückkehr.
  Die tatsächliche DOS-Taskentfernung wird nicht auf dem Host ausgeführt.
- Beide Tests zusätzlich mit AddressSanitizer und UndefinedBehaviorSanitizer
  erfolgreich. Kein POSIX-Code wird in das Amiga-Programm eingebaut.
- Release und Debug jeweils vollständig neu gebaut: 51 Compilation Units,
  keine Compiler-/Linkerwarnungen, Amiga-HUNK-Format und unveränderte Kennung
  $VER: AmiMAIL 2.2.2 (10.10.2026).
- tests/check_runtime_link.py prüft den echten, ungestrippten m68k-Debug-Build:
  12 Heap-/Open-/Close-Einstiege haben nur erwartete geschützte Referenzen;
  die beiden FILE-Pool-Wrapper sind verlinkt und tatsächlich referenziert.
  Die privaten Initialisierungsaufrufe in findfp.o wurden zusätzlich anhand
  der Bibliotheksobjekt-Disassemblierung geprüft, da HUNK deren lokale Namen
  im fertigen Programm nicht erhält.
- Statische Analyse über 25 Host-Module erfolgreich abgeschlossen; unverändert
  ein unbestätigter Hinweis in src/mailto.c:178. Native Semaphore- und
  DOS-Scheduler-Semantik sind damit nicht dynamisch validiert.
- Beide Pakettests mit dem neu gebauten Programm erfolgreich.

## Nächster Gerätetest

audit aktualisieren und vollständig neu bauen; auf dem Amiga nur das Programm
ersetzen. Kataloge und Guides sind gegenüber dem vorigen 2.2.2-Stand unverändert.
Die vorhandene Stackgröße 100000 beibehalten. Zunächst die gemeldete Folge aus
Dialogen, abgebrochenen Bestätigungen und wiederholtem Beenden/Starten prüfen,
auch während ein Konto im Hintergrund abruft. Für verbleibende Kontaktprobleme
vorher SetEnv AmiMAIL_RequesterDebug 1 ausführen und die neuen Logzeilen sichern.
Die vollständige Beseitigung des Geräteabsturzes wird erst nach diesem Test
bestätigt; es wurde kein Amiga-Gerätetest in dieser Umgebung ausgeführt.
