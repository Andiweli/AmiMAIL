# AmiMAIL 2.1.0: Fortschritt in der Statuszeile

## Grundlage und Umfang

Dieser Patch setzt auf AmiMAIL 2.1.0 mit dem Build-Fix sowie dem zuletzt
getesteten Anhangsdialog-/Konfigurationsabstands-Fix auf. Die ZIP-Datei
enthält nur vollständige geänderte und neue Dateien, mit Projektpfaden.
Die Programmversion bleibt 2.1.0, das Build-Ergebnis bleibt `bin/AmiMAIL`.

Die Anhangsauswahl bleibt ein eigener Auswahldialog. Nur die bisherigen
Fortschrittsfenster entfallen: sowohl die Übersicht aller Konten als auch
das zusätzliche Fortschrittsfenster bei lokalen Dateivorgängen.

## Bedienung

Die untere Statuszeile besteht aus einer festen horizontalen Gruppe:

- Links stehen weiterhin die bisherigen Statusmeldungen.
- Rechts erscheinen ein kleiner nativer Balken und ein Prozentwert.
- Daneben steht ein kleines `X` zum Abbrechen des angezeigten Vorgangs.

Die Fortschrittsgruppe reserviert ihre Breite auch im Ruhezustand. Es werden
beim Start oder Ende eines Transfers keine Layout-Kinder hinzugefügt oder
entfernt. Im Ruhezustand ist der Balken leer, ohne Prozenttext, und `X` ist
gesperrt. Die Statusmeldungen links werden vom Netzwerkbalken nicht ersetzt.

Der Prozentwert bezieht sich auf den jeweiligen Arbeitsschritt bzw. die
gerade exportierte Datei, nicht auf eine geschätzte Gesamtzeit. Beim Wechsel
von Empfang zu MIME-Auswertung oder zum nächsten Anhang kann er neu beginnen.
Ohne bekannte Gesamtmenge bleibt die native Prozentanzeige leer. `100%`
bedeutet das Ende dieses Schritts, nicht bereits eine bestätigte Zustellung.

Für den Balken wird `fuelgauge.gadget` über seinen Klassenzeiger verwendet.
Ist diese optionale Klasse nicht verfügbar oder scheitert ihr Aufbau, bleibt
eine kleine Prozentanzeige ohne Balken als Ersatz erhalten. Es wird dafür
kein zusätzliches Fenster geöffnet.

## Zuordnung und Sicherheit

Der Netzwerkauftrag erhält beim Einreihen eine eigene Kopie seiner
Ursprungs-Mailbox und Nachrichten-UID. Der Fortschritt wird ausschließlich
vom aktuell ausgewählten Konto abgefragt und nur angezeigt, wenn auch
Mailbox und Nachrichten-UID zum aktuellen GUI-Kontext passen. Logische
Ordnernamen und ihre vorhandenen Server-Aliase werden berücksichtigt.
Gleiche UIDs in anderen Konten oder Ordnern passen nicht zum Kontext.

Ausgehende Mails und Entwürfe erhalten den GUI-Kontext ihrer Einreichung.
Eine neue Mail darf von einem leeren Ordner aus gesendet werden (UID 0),
bleibt aber an Konto und Ordner gebunden. Unzugeordnete Hintergrundaufträge
bleiben unsichtbar. Ein Ansichtswechsel beendet nicht den Auftrag, sondern
nimmt nur dessen Fortschritt aus der nun unpassenden Anzeige.

Vor einem Netzwerkabbruch werden Konto, Kontext und Auftragsnummer erneut
geprüft. Während der lokalen Sicherungsvorbereitung und der abschließenden
Serverbestätigung bleibt Abbrechen wie bisher gesperrt. Bestehende Regeln
für unklare Serverantworten und lokale Sicherungskopien bleiben erhalten.

Verspätete Antworten für zuvor ausgewählte Mails oder Ordner werden vor der
Übernahme in die GUI verworfen; ihre Ressourcen werden freigegeben. Sie
dürfen weder die neue Vorschau noch deren Status durch alte Daten ersetzen.

Der lokale Anhangsexport verwendet denselben rechten Statusbereich. Da diese
Dateioperation weiterhin synchron läuft, bleibt die Navigation währenddessen
vorübergehend gesperrt. `X`, Escape und das Schließen des Hauptfensters werden
verarbeitet. Der Callback ruft keine allgemeinen Netzwerk-/Menüaktionen auf,
die die gerade verwendete Mail freigeben könnten. Vorherige Gadget-Zustände
werden danach wiederhergestellt. Bereits vollständig gespeicherte Anhänge
bleiben bei einem Abbruch erhalten.

## Unverändert

`src/gui_preview.c`, `src/gui_messages.c`, `src/gui_attachments.c` und
`src/gui_dialogs.c` sind gegenüber der Patch-Grundlage bytegleich. Auch der
vollständige Vorschau-Gadget-Aufbau in `gui_window.c` ist unverändert:
Schreibschutz, DoubleClickHook und Scrollbar bleiben bestehen. In dieser
Datei wird nur die untere Statuszeile geteilt.

Keine Konversationsansicht, keine erneuten Markierungsversuche, keine
geänderten Größenlimits oder MIME-Formatierung. Der deutsche V8-Catalog
bleibt bytegleich; es gibt keine neuen UI-Strings. Beide Guides beschreiben
nun die neue Fortschrittsanzeige statt der bisherigen Popups.

## Einspielen

1. Dateien aus dem ZIP im Projekt ersetzen bzw. ergänzen.
2. Wegen geänderter gemeinsamer Strukturen vollständig neu bauen:

   ```sh
   make clean && make
   ```

3. AmiMAIL auf dem Amiga beenden und die neu gebaute Datei `bin/AmiMAIL`
   als `AmiMAIL` im Programmverzeichnis ersetzen.
4. Die Guides können durch die enthaltenen Fassungen ersetzt werden.
   Der installierte V8-Catalog muss für diesen Patch nicht ersetzt werden.

Quellcode, Tests und Python-Hilfen gehören nur auf den Build-Rechner.

## Ausgeführte Prüfungen

`make review-test host-check` erfolgreich:

- Bisherige Host-Suite: 506 Prüfungen, keine Fehler.
- Stabilitätsregressionen: 5.689 Prüfungen, keine Fehler.
- Datei-Fehlerpfade: 80 Prüfungen, keine Fehler.
- SMTP-Streaming: 43 Prüfungen, keine Fehler.
- Dateibasiertes MIME: 87.669 Prüfungen, keine Fehler; enthält den
  vollständigen 20-MiB-Anhang-Rundlauf.
- IMAP-Dateipfad: 14.507 Prüfungen, keine Fehler.
- SMTP-Dateipfad: 173 Prüfungen, keine Fehler.
- MIME-Parität: 10 Vergleichsfälle, keine Unterschiede.
- Catalog: V8, 542 IDs und Binary geprüft; 19 Python-Tests bestanden.

Erweiterte Dialog-/Fortschrittstests mit GCC und Clang sowie jeweils
AddressSanitizer, UndefinedBehaviorSanitizer und Leak-Prüfung bestanden.
Geprüft werden unter anderem fünf gleichzeitig aktive Konten, gleiche UIDs
in verschiedenen Mailboxen, fehlende Zuordnung, Konto-/Mailwechsel,
veraltete Auftragsnummern, unbekannte Gesamtmengen, Rundung/Überläufe,
Iconify, Größenänderung, lokaler Export und Abbruch. Die bestehenden Tests
für Anhangsauswahl, Dialog-Fehlerpfade und Konfigurationshöhe bleiben aktiv.

Ein zusätzlicher Test verwendet den tatsächlichen Netzwerk-Queue-Code:
219 Prüfungen bestanden. Er prüft die Kopie der Kontextdaten unabhängig vom
Speicher des Aufrufers, den Übergang zwischen Transferphasen, Auftragsnummern,
kontogetrennten Abbruch sowie ausgehende Mails und Entwürfe. Außerdem werden
die neuen GUI-Anbindungen und frühen Prüfungen veralteter Ergebnisse im
Quelltext kontrolliert.

Die native C-Syntax ausgewählter Module wurde mit GCC/Clang und `-Werror`
gegen explizite OS-API-Test-Doubles geprüft. Alle diese GUI-/Queue-Prüfungen
laufen auf dem Host: Sie sind kein Test des echten Task-Schedulers,
ReAction-Zeichnens oder der m68k-ABI. Ein vollständiger m68k-Build und ein
AmigaOS-/Mailserver-Laufzeittest wurden hier nicht durchgeführt.

Die beiden neuen Regressionstest-Ziele sind Teil von `review-test`:

```sh
make dialog-test progress-context-test
HOST_CC=clang make native-syntax-test dialog-test progress-context-test
DIALOG_TEST_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  python3 tests/test_dialog_regressions.py
PROGRESS_TEST_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  python3 tests/test_progress_context.py
```

## Besonders auf dem Amiga testen

Eine größere Mail laden, während andere Konten im Hintergrund arbeiten:
kein Fortschrittsfenster und keine Übernahme fremder Konten in den Balken.
Während des Ladens Konto, Ordner und Mail wechseln; die alte Anzeige muss
verschwinden. Ein verspätetes Ergebnis darf die neu gewählte Mail nicht
ersetzen. Mehrere Anhänge exportieren und mit `X`/Escape abbrechen; fertige
Dateien müssen erhalten bleiben. Fenster verkleinern/vergrößern sowie
iconifizieren und wiederherstellen; Statuszeile und Balken müssen sauber
gezeichnet bleiben. Abschließend Vorschau-Schreibschutz und Link-Doppelklick
kurz gegenprüfen.
