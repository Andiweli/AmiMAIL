# AmiMAIL 2.1.0 - Einspielen und Testbericht

## Ausgangspunkt und Umfang

Basis ist der zuletzt getestete AmiMAIL-2.0.6-Stabilitätsstand. Die ursprüngliche
2.0.6-Codebasis entspricht dem Repository-Stand
`5b76637130f1c35b39c53a01a79ce8aee4338c0e`; darauf bleiben die separat gelieferten
Stabilitätskorrekturen erhalten. Dieses ZIP enthält nur gegenüber diesem
Stabilitätsstand geänderte oder neue Dateien, jeweils vollständig.

Alle drei besprochenen Verbesserungsbereiche sind jetzt enthalten:

1. Automatischer Build und Konsistenzprüfung des deutschen Catalogs;
   erweitert auf V8 mit 542 IDs und den neuen Dialog-/Übertragungstexten.
2. Eigener Anhang-Auswahldialog mit Mehrfachauswahl, Alles/Keine, nur regulären
   Anhängen oder nur eingebetteten Grafiken. Vorbelegt sind reguläre Anhänge.
3. Dateibasierter Empfang, MIME-Index, blockweiser Export, lokale ausgehende
   MIME-Kopie, Streaming für SMTP sowie IMAP-Drafts/Sent, Fortschritt und Abbruch.

**Nicht enthalten:** Konversationsansicht oder erneute Experimente mit
Textmarkierung. Die ursprüngliche TextEditor-Instanz, ihr Schreibschutz,
Layout, Scrollbar und Link-Doppelklick-Hook bleiben unverändert. Änderungen
in `gui_preview.c` betreffen Datenhaltung und Textbereitstellung, nicht den
Gadget-Aufbau oder dessen Eingabeverhalten.

## Auf dem Build-PC

Das ZIP in den vorhandenen Projektordner entpacken und dabei auch die neuen
Dateien unter `include/`, `src/` und `tests/` übernehmen. Dann vollständig
neu bauen, weil gemeinsame Netzwerk-/GUI-Strukturen erweitert wurden:

```sh
make clean
make
```

Das Ergebnis heißt jetzt **`bin/AmiMAIL`**. Die Version ist **2.1.0**.
`make` baut/prüft auch den deutschen Binary-Catalog. Python 3.8+ wird nur auf
dem Build-PC benötigt, ohne Zusatzpakete. Falls erforderlich:

```sh
make PYTHON=python
```

Der Paket-Build legt die Programmdatei und ihr Icon als `AmiMAIL` bzw.
`AmiMAIL.info` ab. Die Original-Iconquelle bleibt aus Kompatibilitätsgründen
`assets/Icons/AmiMail.info`. Release-Pakete enthalten nur den installierbaren
Catalog, nicht dessen CD/CT-Bauquellen, und beide Guides im Ordner `Guides`.

## Auf dem Amiga ersetzen

AmiMAIL beenden, die bisherige Installation sichern und ersetzen:

- die neu gebaute Programmdatei `AmiMAIL`;
- `Catalogs/deutsch/AmiMAIL.catalog` durch die enthaltene bzw. neu gebaute V8;
- die vorhandenen beiden Guides durch `_Guides/AmiMAIL_EN.guide` und
  `_Guides/AmiMAIL_DE.guide` aus diesem Paket.

Eine alte zusätzliche Catalog-Kopie im zentralen
`LOCALE:Catalogs/deutsch/` nicht versehentlich weiterverwenden. Danach AmiMAIL
neu starten. Keine Kontodateien löschen, keine Konten neu einrichten.
**`ENVARC:AmiMail` und bestehende Kontodateinamen werden nicht umbenannt.**
Quellcode, Tests und Python werden auf dem Amiga nicht zur Laufzeit gebraucht.
Dieses ZIP enthält keinen hier kompilierten m68k-Programm-Build.

## Disk-Zwischenspeicher und Abbruch

Standard ist **`PROGDIR:Spool`**, ein beschreibbarer Ordner auf dem
Programmvolume. Alternativ vor Programmstart den globalen ENV-Wert
`AmiMAIL_SpoolDir` auf einen anderen Disk-Ordner setzen. Der Pfad wird im
GUI-Prozess aufgelöst, bevor ein Worker startet. `RAM:` und `T:` sind für die
beabsichtigte RAM-Ersparnis ungeeignet. Der gewählte Datenträger muss genug
Platz für Originalmail, gegebenenfalls extrahierte Entwurfsanhänge und die
neu erzeugte ausgehende MIME-Kopie haben.

Die Grenzen bleiben getrennt: ausgehende Anhangsdaten zusammen höchstens
20 MiB, vollständige MIME-Nachricht höchstens 32 MiB, begrenzter Textauszug.
Es gibt keinen festen Grenzwert für die Anzahl der Anhänge. MIME-Metadaten
benötigen weiterhin Speicher proportional zur Zahl der MIME-Teile; auch die
begrenzte Textkonvertierung und das GUI benötigen RAM. Das ist kein
vollständiger Offline-Mailcache oder allgemeiner Recovery-Postausgang.

Die geteilte Statuszeile zeigt links Meldungen und rechts Prozentwert und
Balken. Fortschritts-Popups entfallen. Die Anzeige gehört nur zum aktuellen
Konto, Ordner und zur ausgewählten Mail; fremde Hintergrundaufträge bleiben
unsichtbar. Der Prozentwert gilt für den aktuellen Arbeitsschritt bzw. Anhang.
Ohne bekannte Gesamtmenge bleibt er leer. Das kleine X bricht nur den
angezeigten, noch abbrechbaren Vorgang ab. Beim lokalen Anhangsexport bleibt
die Navigation vorübergehend gesperrt; X oder Escape bricht ab.

Der Balken verwendet das native fuelgauge.gadget. Falls diese optionale Klasse
fehlt, bleibt eine Prozentanzeige als Ersatz verfügbar. Empfang, MIME-Analyse
und laufender Upload sind abbrechbar. Die lokale Vorbereitung wird zuerst
abgeschlossen, damit nach dem Schließen des Verfassen-Fensters eine komplette
Sicherung verfügbar ist. Beim abschließenden SMTP-/IMAP-Protokollschritt ist
Abbrechen gesperrt; eine bereits bestätigte Zustellung kann nicht rückgängig
gemacht werden. Beim Beenden werden Netzwerkzugriffe abgebrochen und Worker
vor der Freigabe ihrer Daten sauber beendet.

Wichtige Fehlerfälle:

- Abbruch eines Anhangsexports veröffentlicht keine unvollständige Datei;
  fertig gespeicherte Dateien bleiben erhalten. Namenskonflikte erhalten
  Suffixe, vorhandene Dateien werden nicht überschrieben.
- Eine bestätigte SMTP-Zustellung bleibt ein Erfolg, auch wenn die spätere
  Gesendet-Kopie scheitert. Dann bleibt die lokale .eml-Kopie erhalten.
- Nach fehlender/ungültiger Serverbestätigung im endgültigen Protokollschritt
  ist das Ergebnis **unbekannt**, nicht sicher fehlgeschlagen. Vor erneutem
  Versand Gesendet/Entwürfe und tatsächliche Zustellung kontrollieren.
  AmiMAIL wiederholt eine solche Operation nicht automatisch.
- Bei fehlgeschlagener oder unklarer ausgehender Übertragung bleibt die
  lokale Kopie erhalten. Der Pfad wird in der Statusmeldung mitgegeben und
  liegt im konfigurierten Spool-Ordner. Sehr lange Pfade können in der
  einzeiligen Statusanzeige abgeschnitten erscheinen.
- Scheitert bereits die Vorbereitung, etwa wegen voller Disk, ist eine
  hinterlassene Kopie ausdrücklich unvollständig. Nicht als vollständige
  Mail erneut senden. Bereits vorhandene temporäre Anhangsdateien bleiben
  in diesem Fall erhalten. Ein solcher Fehler garantiert keine vollständige
  Wiederherstellung des zuvor nur im Verfassen-Fenster vorhandenen Textes.
- Ausgehende Spooldateien sind .eml-Kopien. Empfangsspooldateien enthalten
  zusätzlich IMAP-Rahmendaten; sie sind kein allgemeiner .eml-Export.
- Erfolgreiche temporäre Arbeit wird entfernt. Nach Abstürzen übrig gebliebene
  Job-Ordner werden nicht blind gelöscht. Sicherungen manuell erst nach der
  Wiederherstellung entfernen; sie können private Texte und Bcc enthalten.

## Tatsächlich ausgeführte Prüfungen

Die folgenden Prüfungen wurden auf dem Linux-Buildhost ausgeführt, ohne
Zugangsdaten und ohne echte Mailzustellung:

- GCC, C99, `-Wall -Wextra -Wshadow -Wpointer-arith -Wstrict-prototypes
  -Wmissing-prototypes -Wformat=2 -Werror` für die Testprogramme.
- Bisherige Host-Regressionstests: 506 Prüfungen, keine Fehler.
- Stabilitätsregressionen: 5.689 Prüfungen, keine Fehler.
- Sichere Dateiersetzung/Fehlerinjektion: 80 Prüfungen, keine Fehler.
- Bisherige SMTP-Streamingregressionen: 43 Prüfungen, keine Fehler.
- Neue Datei-MIME-/Exporttests: 87.669 Prüfungen, keine Fehler.
  Enthalten sind unterschiedliche Transferkodierungen, Blockgrenzen,
  Binärdaten/NULs, verschachtelte MIME-Strukturen, Zeichenkodierung,
  reguläre/Inline-Anhänge, Dateinamenskonflikte, Abbruch und Bereinigung.
  Ein real erzeugter Anhang von genau 20 MiB wurde als MIME-Datei geschrieben,
  indiziert, extrahiert und byteweise mit dem Original verglichen.
  20 MiB + 1 Byte werden abgewiesen.
- Neue IMAP-Dateitests: 14.507 Prüfungen, keine Fehler. Die Produktionslogik
  verarbeitet TLS-Testantworten in 1/2/3/7/17/511/8.191/8.192-Byte-Blöcken,
  UID/Flags hinter dem Literal, zusätzliche FETCH-Antworten, Abbruch,
  Serverablehnung und verlorene Bestätigungen. Ein 28-MiB-Literal wurde mit
  begrenzten Parserpuffern verarbeitet.
- Neue SMTP-Dateitests: 173 Prüfungen, keine Fehler. Bcc plus gefaltete
  Fortsetzungen erscheinen nicht in SMTP DATA, Bcc bleibt aber Empfänger
  im Envelope. Dot-Stuffing funktioniert über Blockgrenzen. SMTP-Zustellung,
  QUIT-Fehler nach Zustellung, ausdrückliche Ablehnung, Abbruch, fehlende oder
  unlesbare Bestätigung und unveränderliche lokale Kopien werden geprüft.
- Zehn vorhandene MIME-Text-Fixtures liefern über die RAM- und Disk-Wege
  dieselben Ergebnisse; zusätzlich läuft die vorhandene 506-Prüfungen-Suite.
- Catalog: 542 IDs/Übersetzungen/Binary-Einträge geprüft; 19 Python-Tests
  bestanden. Neue Texte sind Englisch als Fallback und Deutsch im Catalog.
- Dieselben C-Testprogramme liefen zusätzlich mit Clang,
  AddressSanitizer/UndefinedBehaviorSanitizer und Leak-Erkennung fehlerfrei.
  Nur für diesen Clang-Testlauf wurde `-Wno-format-nonliteral` ergänzt:
  der bestehende Locale-Formatter verwendet absichtlich eine zur Laufzeit
  geprüfte Übersetzungs-Formatzeichenfolge. GCC-Tests benötigen das nicht.
- Fünf relevante native C-Zweige wurden mit GCC und Clang anhand expliziter
  API-Test-Doubles auf Syntax/Projektdeklarationen geprüft. Diese Prüfung
  definiert unbekannte OS-Symbole nicht automatisch passend zum Quelltext.

Die großen Zahlen enthalten byteweise und wiederholte Schleifenprüfungen;
**sie sind keine Anzahl voneinander unabhängiger Testszenarien.**

### Nicht hier geprüft

**Kein vollständiger m68k-Build gegen dein NDK/AmiSSL-SDK, kein Linktest unter
AmigaOS und kein realer ReAction-/Mailserver-Laufzeittest.** Die API-Doubles
sind kein Amiga-SDK und beweisen keine ABI- oder Gadget-Kompatibilität.
Native Requester, ListBrowser-Auswahl, Ereignisverarbeitung, Prozessende,
Dateisystemverhalten und echte Serverabbruchfälle müssen am Amiga geprüft
werden. Ebenso wurde der Windows/UCRT-Hosttestpfad hier nicht ausgeführt.

## Empfohlener erster Amiga-Test

1. Mit gesicherter Konfiguration oder Testkonto starten. Deutsche neue
   Dialogtexte, Version 2.1.0 und unveränderten Vorschau-Schreibschutz prüfen.
2. Gemischte Mail mit PDF, normalem Bildanhang und eingebetteten Grafiken:
   einzelne Dateien sowie beide Kategorien auswählen und speichern.
3. In einen Ordner mit gleichnamigen Dateien exportieren. Die bisherigen
   Dateien müssen unverändert bleiben; neue bekommen Suffixe.
4. Eine größere Mail laden/exportieren und mitten im Vorgang abbrechen.
   Fortschritt, Reaktionsfähigkeit, temporäre Dateien und erneuten Abruf prüfen.
5. Mehr als zehn kleine Anhänge sowie zusammen genau 20 MiB versenden.
   Einen größeren Entwurf speichern, wieder öffnen und weiterbearbeiten.
6. Versand/Entwurf mit absichtlichem Verbindungsabbruch prüfen. Bei unklarem
   Ergebnis zuerst serverseitig nachsehen; keine doppelten Zustellversuche.
7. AmiMAIL während einer Übertragung beenden und neu starten. Keine
   Beschädigung vorhandener Konfigurationen/Mails; verbleibende Sicherungen
   nur nach Prüfung manuell entfernen.

## Technische Referenzen

- RFC 3501, insbesondere Literale und APPEND:
  https://www.rfc-editor.org/rfc/rfc3501.html
- RFC 5321, DATA und Zustellbestätigung:
  https://www.rfc-editor.org/rfc/rfc5321.html
- Klassische window.class-Header (WINDOW_Layout/ParentGroup):
  https://d0.se/include/classes/window.h
- ReAction layout.gadget:
  https://developer.amigaos3.net/autodocs/layout.gadget/
- Native Request()/EndRequest()-Modalität:
  https://d0.se/autodocs/intuition.library/Request
