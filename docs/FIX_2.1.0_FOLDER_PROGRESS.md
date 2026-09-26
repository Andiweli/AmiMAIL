# AmiMAIL 2.1.0: gleiche Statushoehe und Ordnerfortschritt

## Grundlage

Dieser Patch setzt auf dem zuletzt gelieferten und vom Benutzer getesteten
Paket `AmiMAIL-2.1.0-status-progress-changed-files.zip` auf. Die ZIP-Datei
enthaelt ausschliesslich vollstaendige geaenderte oder neue Projektdateien.
Version 2.1.0 und Ausgabedatei `bin/AmiMAIL` bleiben unveraendert.

## Statusbereich

`gui_transfer_create_status_row()` erstellt das urspruengliche native
String-Gadget fuer die linke Statusmeldung. Dessen Mindesthoehe wird mit
`LayoutLimits()` und der aktuellen Screen-Schrift ermittelt. Diese Hoehe
wird als `CHILD_MinHeight` und `CHILD_MaxHeight` auf Statusfeld, FuelGauge
und X-Button angewendet. Die horizontale Gruppe hat keinen Aussenabstand.
Die Gewichte 80/20 und die bisherigen Mindestbreiten bleiben unveraendert.
Der X-Button sowie der Prozenttext-Ersatz ohne FuelGauge verwenden kein
zusaetzliches Text-Padding. Es erfolgen weder spaetere Hoehenkorrekturen
noch Aenderungen an anderen Fenstern oder am Vorschaufeld.

Kinder werden waehrend der Konstruktion zunaechst nicht vom neuen Layout
freigegeben. Erst bei erfolgreichem Aufbau uebernimmt das Layout sie. Auch
Fehler vor oder nach der Tag-Verarbeitung wurden mit Test-Doubles geprueft.

## Ordnerladen

Der historisch `AMG_NET_FETCH_INBOX` benannte Auftrag laedt auch andere
Ordner. Er traegt nun eine private Kopie seines Ursprungsordners und UID 0.
Die GUI fragt unveraendert nur das aktuelle Konto ab. Fuer diesen Auftrag
wird die Mailbox verglichen, nicht die eventuell noch ausgewaehlte einzelne
Nachrichten-UID. Fortschritt einer anderen Mail oder eines anderen Ordners
wird nicht auf den neuen Kontext uebertragen. `AMG_NET_CHECK_INBOX` bleibt
als Hintergrundpruefung ohne Fortschrittsanzeige.

Ablauf:

1. Verbindung/SELECT/UID SEARCH: ohne bekannte Gesamtmenge kein Prozentwert.
2. Nach SEARCH: Gesamtzahl der gefundenen UIDs im eingestellten Abrufzeitraum.
3. FETCH: Zaehler erhoeht sich auch innerhalb der bisherigen 100er-Bloecke.
4. 100% erst nach der letzten positiven Serverantwort und erfolgreicher
   Uebernahme des Antwortblocks. Danach wird der Balken wie bisher geleert.

Es handelt sich um Fortschritt der IMAP-Nachrichtenlisten-Uebertragung,
nicht um eine geschaetzte Ladezeit. Das nachfolgende Erzeugen/Sortieren der
GUI-Zeilen ist weiterhin synchron und bekommt hier keinen eigenen Balken.

Der Zaehler untersucht nur FETCH-Metadaten, nie Inhalte von MIME-Literalen.
UIDs vor und nach dem Literal werden beruecksichtigt. Doppelte Antworten,
unangeforderte UIDs und reine Flag-Benachrichtigungen zaehlen nicht doppelt.
Wurde eine Nachricht zwischen SEARCH und FETCH entfernt, schliesst die
positive Serverantwort ihren Eintrag in der Arbeitsliste trotzdem ab; es
wird deswegen keine kuenstliche Nachricht zur angezeigten Liste hinzugefuegt.

Die Netzwerk-Signalisierung beruecksichtigt fuer Ordner Nachrichtenzaehler
statt des bisherigen 64-KiB-Schritts und weckt die GUI ungefaehr pro Prozent.
Bytebasierte Fortschritte einzelner Mails und Anhaenge bleiben unveraendert.
Die alten IMAP-Einstiegspunkte bleiben als Wrapper ohne Callback erhalten.

Abbrechen mit X prueft weiterhin Konto, Ordner und Auftragsnummer. Bei Abbruch
waehrend FETCH wird die Verbindung geschlossen, ohne LOGOUT in einen noch
laufenden Datenblock zu schreiben. Ein erneuter Ordnerabruf stellt bei Bedarf
die Verbindung wieder her. Der Abruf selbst aendert keine Mails auf dem Server.

## Unveraendert

- Vorschau-Schreibschutz, Link-Doppelklick und Scrollbar.
- Anhangsauswahl, Export und lokale Sicherungskopien.
- Konfigurationsfenster und dessen zuletzt korrigierter unterer Abstand.
- Maillistendarstellung, Sortierlogik und Einstellung des Abrufzeitraums.
- Deutscher Catalog V8; keine neuen UI-Strings oder Catalog-Version.
- Keine Konversationsansicht in diesem Patch.

Die beiden Guides wurden passend zur Ordner-Fortschrittsanzeige aktualisiert;
ihre bestehenden Kodierungen bleiben erhalten.

## Ausgefuehrte Pruefungen

- Vor der Aenderung: `make review-test host-check` erfolgreich auf der
  zusammengesetzten bisherigen Patch-Grundlage.
- Nach der Aenderung: `make review-test host-check` mit GCC erfolgreich.
  Darin sind die bestehenden MIME-, Datei-, SMTP-, IMAP-, Catalog-,
  Anhangsdialog- und Kontext-Regressionen enthalten.
- Neuer `imap-folder-progress-test`: 205 Nachrichten in drei Abrufbloecken,
  Leseportionen von 1 bis 65536 Byte, Befehls-/Antwortgrenzen, identische
  Ergebnisbytes mit und ohne Callback, vor/nach Literal stehende UID,
  fehlende und doppelte UIDs, leere Ordner, negative Antwort, Verbindungs-
  abbruch, Pufferlimit und Benutzerabbruch. 100747 Assertions, kein Fehler.
  Die hohe Anzahl entsteht ueberwiegend durch fragmentierte Ein-Byte-Lese-
  vorgaenge; sie ist keine Anzahl unterschiedlicher Testfaelle.
- GUI-Doubles: identische Hoehentags fuer 11 Statushoehen, native FuelGauge
  und Text-Ersatz, Konstruktorfehler und Freigabepfade, Konto-/Ordnerfilter,
  wechselnde Nachrichtenwahl und Abbruchzuordnung.
- Queue-/Kontexttests: 278 Assertions erfolgreich, einschliesslich privater
  Ordnerkopie, fehlendem Ordnerargument, zu langen Namen und Zaehler-Throttle.
- Native Syntax, Dialog- und Kontexttests mit GCC und Clang mit `-Werror`.
- Neue IMAP-, Dialog- und Kontexttests zusaetzlich mit GCC und Clang unter
  AddressSanitizer, UndefinedBehaviorSanitizer und Leak-Erkennung erfolgreich.

Beim vollstaendigen Clang-Host-Check bleibt die bereits vorhandene Warnung
`-Wformat-nonliteral` im unveraenderten `src/i18n.c` (dynamisches
Uebersetzungsformat). Nur fuer den mit vielen gemeinsamen Modulen gelinkten
Sanitizer-IMAP-Test wurde diese spezifische Warnung deaktiviert; die geaenderten
nativen Module bestehen ihre Syntaxpruefung ohne solche Ausnahme.

Diese Pruefungen sind Host-Tests: kein echter Amiga-Task-Scheduler, keine
m68k-ABI und keine Pixelkontrolle einer realen ReAction-Oberflaeche. Ein
vollstaendiger m68k-Build und ein Test mit echtem Amiga/Mailserver wurden
hier nicht durchgefuehrt.

## Installation und Amiga-Pruefung

Dateien im bisherigen 2.1.0-Quellbaum ersetzen/ergaenzen und `make` ausfuehren.
Die bestehenden Header-Abhaengigkeiten bauen die betroffenen Module neu.
Auf dem Amiga die neu gebaute Datei `bin/AmiMAIL` im Programmverzeichnis
ersetzen. Optional die aktualisierten Guides uebernehmen. Der installierte
V8-Catalog muss fuer diesen Patch nicht ersetzt werden.

Zuerst pruefen: Hoehen und obere/untere Kanten im Ruhe- und Ladezustand sowie
nach Fenstergroessenaenderung. Dann einen grossen Ordner wie "Alle Nachrichten"
abrufen, waehrend andere Konten im Hintergrund aktiv sind. Prozentfortschritt
soll nur fuer den ausgewaehlten Ordner erscheinen. Ordner wechseln, zurueck-
wechseln, mit X abbrechen und erneut abrufen. Kleine/leere Ordner duerfen den
Balken nicht aktiv stehenlassen. Abschliessend eine einzelne Mail laden und
einen Anhang exportieren, um die bisherigen Fortschrittswege mitzupruefen.

Reproduzierbare Host-Ziele:

```sh
make review-test host-check
make imap-folder-progress-test dialog-test progress-context-test
HOST_CC=clang make native-syntax-test dialog-test progress-context-test
```
