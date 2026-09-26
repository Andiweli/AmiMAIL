# AmiMAIL 2.0.6 - Stabilitaetspatch

## Umfang

Dieser Patch behebt die vier Punkte der Codeanalyse: sichere Dateiersetzung,
getrennte Groessenlimits, MIME-Zeichensatzkonvertierung und begrenzte Suche
nach MIME-Trennzeilen. Er ergaenzt den automatischen Catalog-Build und
verringert eine grosse Kopie beim IMAP-Empfang. Die Programmversion bleibt
2.0.6; der deutsche Catalog traegt Version 7.1.

Die schreibgeschuetzte TextEditor-Instanz, ihre Geometrie, Mausbehandlung,
Scrollbar und Link-Hooks wurden NICHT umgebaut. In gui_preview.c aendert sich
nur die Verarbeitung des anzuzeigenden Textes und seiner Groessenfehler.
Die bisherigen erfolglosen Markierungs-Patches sind nicht enthalten.

Noch nicht enthalten: eine Anhang-Auswahlliste, vollstaendiges Streaming von
Empfang/Entwurfsspeicherung auf Disk, eine neue Fortschritts-/Abbruchoberflaeche
oder die Konversationsansicht. Das sind separate GUI-/Architekturarbeiten,
keine Bestandteile dieses Stabilitaetspatches.

## Einspielen

1. AmiMAIL beenden und den aktuellen Quellstand sichern.
2. Alle Dateien aus diesem Archiv in den Projektordner uebernehmen.
   Neue Dateien unter src/, include/, tools/ und tests/ mitkopieren.
   Vorhandene Kontodateien NICHT loeschen.
3. Auf dem Build-PC wird Python 3.8 oder neuer benoetigt. Es werden keine
   Python-Zusatzpakete installiert. Python ist NICHT auf dem Amiga notwendig.
4. Im Projektordner ausfuehren:

       make clean
       make

   Falls der Python-Befehl unter MSYS nur "python" heisst:

       make PYTHON=python

   Der Standard-Build erzeugt/prueft den Catalog automatisch. Das komplette
   Neubauen ist wichtig, weil AmgBuffer zwei neue interne Felder enthaelt.
5. Die neue Binary bin/AmiMail installieren. Ebenfalls die enthaltene bzw.
   neu erzeugte Datei _Catalogs/deutsch/AmiMAIL.catalog nach
   PROGDIR:Catalogs/deutsch/AmiMAIL.catalog kopieren. Alternativ kann der
   vorhandene zentrale Catalog in LOCALE:Catalogs/deutsch/ ersetzt werden.
   Keine veraltete zweite Kopie im anderen Suchpfad liegen lassen.
6. AmiMAIL neu starten. Englische Fallbacks bleiben in der Binary; deutsche
   Uebersetzungen kommen ausschliesslich aus dem Catalog.

Dieses Archiv enthaelt nur geaenderte und neu benoetigte Dateien, jeweils
vollstaendig. Keine alten Vorschau-Patches oder Rollbacks darueberkopieren.

## Technische Aenderungen

### Einstellungen und Schluesseldateien

storage.c verwendet die neue gemeinsame Hilfsfunktion amg_file_replace().
Sie prueft die fertig geschriebene temporaere Datei, verschiebt die alte
Datei nach .bak und uebernimmt erst dann die neue Datei. Bei fehlgeschlagenem
Umbenennen wird die alte Datei wiederhergestellt. Schlaegt auch diese
Wiederherstellung fehl, bleibt .bak erhalten und wird beim naechsten Laden
oder Speichern wiederhergestellt, sofern die Zieldatei fehlt.

Ein bereits vorhandenes Original wird niemals vorab geloescht. Fehler beim
Entfernen einer Sicherung nach erfolgreichem Speichern gelten nicht als
fehlgeschlagener neuer Speichervorgang. Explizites Loeschen eines Kontos
entfernt auch die reservierten .bak-Dateien, damit geloeschte Konten nicht
allein durch den Wiederherstellungsmechanismus zurueckkehren.

Der Schutz betrifft die von storage.c verwalteten Kontoeinstellungen,
Kontoreihenfolge und Schluesseldateien. Er ist KEINE stromausfallsichere
Transaktion ueber mehrere Dateien. Konto und Schluessel werden weiterhin
separat gespeichert. Signatur-/Fensterstatusdateien anderer Module wurden
in diesem Patch nicht auf ein anderes Speicherformat umgestellt.

### Getrennte Groessenlimits

Die bisherigen UI-Texte nennen MB; die Grenzwerte sind binaere MiB:

- Anhangsdaten zusammen: weiterhin exakt 20 * 1024 * 1024 Bytes.
- Komplette rohe MIME-Nachricht: 32 * 1024 * 1024 Bytes.
- Dekodierter einzelner Textteil: 2 * 1024 * 1024 Bytes.
- Vorschau-Nachrichtentext: 512 * 1024 Bytes.
- Kompletter Vorschautext inklusive Header und Listen: maximal 1 MiB.

32 MiB lassen Platz fuer Base64, Zeilenumbrueche und uebliche MIME-Header
bei 20 MiB Anhaengen. Es gibt weiterhin keine feste Anhangsanzahl. Sehr
viele selbst winzige Anhaenge koennen allerdings das unabhaengige rohe
MIME-/Speicherlimit erreichen; das wird als Fehler behandelt.

Die Groesse der Dateien wird beim Erzeugen/Senden erneut geprueft. Auch
waehrend des Lesens wird die wirkliche Byte-Summe gezaehlt, damit ein nach
der Vorpruefung gewachsener Anhang die 20 MiB nicht umgehen kann. Der
SMTP-Stream und der Entwurfspuffer beachten beide das rohe MIME-Limit.
Nach einem Streamfehler wird keine abschliessende SMTP-DATA-Terminierung
mehr gesendet; der vorhandene Fehlerpfad schliesst die Verbindung.

Zu grosse Textteile werden fuer die Vorschau gekuerzt oder ausgelassen und
mit einem lokalisierten Hinweis versehen. Die Originalnachricht und ihre
Anhaenge werden dadurch nicht gekuerzt. Nicht unterstuetzte Nachrichtentexte
verhindern nicht mehr grundsaetzlich die Anzeige speicherbarer Anhaenge.
Weiterleiten/Bearbeiten verwendet weiterhin seine bestehenden kleineren
Editorgrenzen und darf uebergrosse Texte ablehnen statt still zu kuerzen.

Auch mit diesen Grenzen koennen grosse Nachrichten viel RAM brauchen.
Dieser Patch garantiert keinen Empfang einer 32-MiB-Mail auf jedem Amiga.
Eine grosse IMAP-Literal-Kopie entfaellt durch Eigentumsuebergabe des
Parserpuffers; Netzwerkantwort und Vorschau behalten weiterhin eigene Daten.
Es gibt noch keinen vollstaendigen Disk-Spool fuer Empfang oder Entwuerfe.

### MIME und Zeichensaetze

Alle vier rekursiven MIME-Durchlaeufe verwenden dieselbe neue
laengenbegrenzte Suche. Trennzeichen werden nur als vollstaendige
Trennzeilen erkannt. Aehnliche Zeichenfolgen mitten im Text, verschachtelte
Grenzen mit gemeinsamem Praefix und eingebettete NUL-Bytes verwechseln die
Suche nicht mehr. Genau ein Zeilenende gehoert zur Trennzeile; echte
abschliessende CR/LF-Bytes eines Binaeranhangs bleiben erhalten.

Transfer-Dekodierung und Zeichensatzkonvertierung sind getrennt. Unterstuetzt
werden UTF-8, US-ASCII, ISO-8859-1, ISO-8859-15, Windows-1252 sowie UTF-16,
UTF-16LE und UTF-16BE, einschliesslich gaengiger Aliasnamen. Auch kodierte
RFC-2047-Header und einfache erweiterte Dateinamenparameter nutzen diese
Konvertierung. Fehlende charset-Angaben werden als UTF-8 interpretiert,
wenn die Bytes gueltig sind, ansonsten als Windows-1252. Das ist eine
Kompatibilitaetsheuristik, keine sichere Erkennung jedes Zeichensatzes.

Unbekannte deklarierte Zeichensaetze werden nicht faelschlich als UTF-8
behandelt. Der Nachrichtentext kann dann als nicht unterstuetzt erscheinen;
die Originaldaten bleiben erhalten. Dieser Patch ist keine universelle
Zeichensatzbibliothek und implementiert keine neuen MIME-Containerarten.

### Catalogs

tools/catalog_tool.py baut den deutschen IFF-CTLG-Catalog reproduzierbar.
Es prueft CD/CT-Namen, eindeutige numerische IDs, die Definitionen im Header,
englische/deutsche printf-Argumente, Catalog-Version und den fertigen
Binaerinhalt. Byte-Escapes bleiben erhalten: lokale Latin-1-Texte werden
nicht mit den absichtlich internen UTF-8-Texten vermischt.

AMIMAIL_CATALOG_VERSION im Header ist die gemeinsame Versionskonstante
fuer die Pruefung und den Loader. Der vorhandene Fallback auf aeltere
Catalogs bleibt bestehen. Neue IDs sind nur angehaengt; alte IDs sind
unveraendert. Der Compiler unterstuetzt bewusst den von AmiMAIL genutzten
CD/CT-Teilumfang, nicht die gesamte FlexCat-Makrosprache.

Separat ausfuehrbar:

    make catalogs
    make catalogs-check
    make catalog-test

## Durchgefuehrte Pruefungen

Referenz fuer die bearbeiteten Dateien: Andiweli/AmiMAIL, Commit
5b76637130f1c35b39c53a01a79ce8aee4338c0e (2.0.6).
Die zentralen Ausgangsdateien wurden ueber Git-Blob-Pruefsummen abgeglichen.

Auf Linux mit GCC 14.2, C99, den bestehenden Warnoptionen und -Werror:

- Bestehende Tests: 506 Pruefungen, 0 Fehler.
- Neue Kern-/MIME-/Groessentests: 5689 Pruefungen, 0 Fehler.
- Dateiersetzung mit injizierten Fehlern: 80 Pruefungen, 0 Fehler.
- SMTP-Streaming gegen einen Test-TLS-Sink: 43 Pruefungen, 0 Fehler.
- Catalog-Tool: 18 Python-Tests, alle bestanden.

Die vier C-Tests wurden zusaetzlich mit Clang 17, AddressSanitizer,
UndefinedBehaviorSanitizer und Leak-Erkennung ausgefuehrt. Es wurden keine
Sanitizer-Fehler gemeldet. Clang verwendet dabei -Wno-format-nonliteral
fuer den bestehenden absichtlich dynamischen Catalog-vsnprintf-Aufruf.
Alle anderen genannten Warnoptionen bleiben aktiv.

Die neuen Pruefungen umfassen einen wirklichen 20-MiB-Dateianhang mit
MIME-Build, Einlesen, Anhangserkennung und Dekodierung; 20 MiB plus ein Byte;
veraltete Dateigroessenangaben; Grenzpruefung beim Streamen; Binardaten mit
NUL und abschliessenden Zeilenumbruechen; verschachtelte und irrefuehrende
Boundaries; leere Textteile; UTF-16/Windows-1252/Latin-1/Latin-9; abgeschnittenes
UTF-8; zu lange IMAP-Zeilen; sowie 3000 kleine begrenzte Zufallseingaben.
Die numerische Pruefungsanzahl ist keine Aussage ueber vollstaendige
Fehlerfreiheit oder eine umfassende Sicherheitspruefung.

Der Catalog wurde nach dem Schreiben vollstaendig zurueckgelesen und gegen
alle 514 Quelltexte verglichen. Zusaetzlich wurde der Reader gegen 509
Uebersetzungen des frueheren unabhaengigen Catalogs geprueft: keine
Byte-Abweichungen. Der jetzige Catalog enthaelt die aktuellen 20-MiB-Texte,
Eingebettete Grafiken, den Entwurfshinweis und die drei neuen Hinweise.

WICHTIG: Hier wurde KEIN vollstaendiger m68k-AmigaOS-Build ausgefuehrt.
Ein nativer Compiler/SDK und ein echter AmigaOS-3.2-Oberflaechentest standen
nicht zur Verfuegung. Der Host-Build ersetzt weder den Test der nativen
DOS-Rename-Aufrufe noch einen echten SMTP/IMAP-Server- und GUI-Laufzeittest.
Es wurden keine Nachrichten verschickt und keine echten Konten geaendert.

Reproduzierbar auf dem Build-PC:

    make catalogs
    make review-test
    make host-check

Sanitizer-Durchlauf mit Clang (Linux):

    make clean
    export HOST_TEST_FLAGS="-std=c99 -O1 -g -Wall -Wextra -Wshadow"
    export HOST_TEST_FLAGS="$HOST_TEST_FLAGS -Wpointer-arith"
    export HOST_TEST_FLAGS="$HOST_TEST_FLAGS -Wstrict-prototypes"
    export HOST_TEST_FLAGS="$HOST_TEST_FLAGS -Wmissing-prototypes -Wformat=2"
    export HOST_TEST_FLAGS="$HOST_TEST_FLAGS -Werror -Wno-format-nonliteral"
    export HOST_TEST_FLAGS="$HOST_TEST_FLAGS -fsanitize=address,undefined"
    export HOST_TEST_FLAGS="$HOST_TEST_FLAGS -fno-omit-frame-pointer"
    ASAN_OPTIONS=detect_leaks=1 make review-test HOST_CC=clang
    unset HOST_TEST_FLAGS

Normale Host-Tests benoetigen keine Sanitizer.

Vor dem Einsatz am Amiga zuerst ein Testkonto bzw. eine gesicherte
Konfiguration verwenden und pruefen: Speichern/Neustart, Umlaute in alten
Mails, HTML-Mails mit eingebetteten Grafiken, eine Mail groesser als 8 MiB,
Anhangsexport und unveraenderte schreibgeschuetzte Vorschau.

## English summary

This is a foundational stability patch, not the conversation-view or
attachment-selection UI feature. Program version remains 2.0.6. It adds
recoverable replacement for storage-managed files, separate MIME/text/
attachment limits, bounded MIME delimiter parsing, charset conversion,
automated catalog generation/checking and reduced IMAP literal copying.
Preview gadget geometry, read-only mode and input handling are unchanged.

Copy every supplied full file into the existing source tree, then run
`make clean` and `make`. Python 3.8+ is required on the build host only;
`make PYTHON=python` selects a differently named Python executable.
Install both the rebuilt program and the supplied/rebuilt German catalog.
Run `make review-test` for the host regression suite.

GCC/Clang host tests, ASan/UBSan and catalog round-trip checks passed.
No native m68k build, Amiga GUI test or live SMTP/IMAP delivery test was run.
File replacement is recoverable, not a power-loss-atomic multi-file
transaction. Full disk streaming and the optional UI extensions remain
separate work, explicitly not included in this archive.

## Technical references

Primary references consulted for the implementation:

- RFC 2046, section 5.1.1 (multipart delimiter lines), section 4.1.2 (charset):
  https://www.rfc-editor.org/rfc/rfc2046
- AmigaOS 3 dos.library Rename documentation:
  https://developer.amigaos3.net/autodocs/dos.library/Rename.html
- FlexCat's catalog writer, for independent IFF CTLG layout comparison:
  https://github.com/adtools/flexcat/blob/master/src/createcat.c

No FlexCat source code or external runtime library is included.
