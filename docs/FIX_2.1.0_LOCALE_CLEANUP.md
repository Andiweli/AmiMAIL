# AmiMAIL 2.1.0: Locale-Bereinigung

## Ausgangspunkt und Umfang

Dieser Patch setzt auf dem zuletzt getesteten 2.1.0-Stand mit gleicher
Statushoehe und Ordnerfortschritt auf. Die Basis wurde aus den hier vorhandenen
Quell-/Patcharchiven in ihrer Auslieferungsreihenfolge zusammengesetzt.
Es werden nur vollstaendige geaenderte oder neue Projektdateien geliefert.
Programmversion 2.1.0 und Ausgabename bin/AmiMAIL bleiben unveraendert.

Die Korrektur ist auf die zuletzt genannten Sprach-Reststellen beschraenkt.
Sie ist keine Behauptung, alle nicht lokalisierten Texte des Gesamtprojekts
bereinigt zu haben. Servertexte, Mailinhalte und Protokollbezeichner werden
nicht automatisch uebersetzt.

## Korrekturen

- src/gui_window.c: Der Fehler beim Erzeugen des ReAction-Hauptfensters
  verwendet einen englischen Fallback und eine deutsche Catalog-Uebersetzung.
- src/gui.c: Die Fehlermeldung fuer nicht erzeugbare GUI-Pfeilgrafiken
  wird ebenfalls ueber eine Message-ID lokalisiert.
- src/mime.c: Der HTML-Grafikplatzhalter heisst ohne Catalog [Graphic] und
  mit deutschem Catalog [Grafik]. Das gilt fuer echte und maskierte img-Tags.
  Der Vergleich fuer reine Bildlinks verwendet denselben uebersetzten Text
  und dessen tatsaechliche Byte-Laenge statt eines fest eingebauten Arrays.
- src/codec.c: Derselbe Fehler bestand bei Emoji-/Symbol-Platzhaltern.
  Auch dieser Ausgabepfad ist jetzt lokalisiert. Zusammengesetzte Emojis
  werden weiterhin nicht in mehrere Platzhalter zerlegt.
- src/gui_attachments.c: Englische Fallbacks fuer Dateispalte, Abwaehlen
  und Fenstertitel entsprechen nun exakt den vorhandenen CD-Eintraegen.
  Die deutschen Texte und die Anhangsauswahl-Logik sind unveraendert.

Die neuen IDs sind 2100000062 bis 2100000065. Vorhandene IDs wurden nicht
umnummeriert. Fuer Grafikmarker existieren bewusst zwei IDs: MIME erzeugt
UTF-8, der Codec erzeugt lokalen Anzeigetext. Fuer Deutsch sind beide Texte
ASCII-identisch, fuer spaetere Sprachen duerfen die Byte-Kodierungen abweichen.

## Catalog und Build

Der mitgelieferte deutsche Binary-Catalog wurde wirklich neu erzeugt:
V9 / 546 Eintraege. CD, CT, catalog_ids.h und Binary-Catalog passen zusammen.
AMIMAIL_CATALOG_VERSION in catalog_ids.h ist jetzt 9. Der vorhandene Loader
in i18n.c verwendet bereits dieses Makro; er musste nicht veraendert werden.
Der bestehende Fallback auf einen aelteren Catalog bleibt erhalten. Nur der
neue V9-Catalog enthaelt jedoch die vier neuen Uebersetzungen.

Der Build fuehrt jetzt auch eine begrenzte Quellenpruefung aus:
- 687 statische Lookup-Aufrufstellen werden auf vorhandene MSG-IDs geprueft.
- Sieben gezielt gepruefte IDs muessen englische Fallbacks wie in der CD haben.
- Ein erneutes direktes Einbauen der korrigierten Diagnose-/Grafiktexte in
  ihren Modulen wird als Fehler behandelt.
- Kommentare sind keine Ausgabetexte und werden nicht beanstandet.

Diese Pruefung ersetzt weder den C-Praeprozessor noch eine vollstaendige
Analyse aller indirekten und dynamischen UI-Ausgaben. Sie garantiert nicht,
alle noch denkbaren Lokalisierungsfehler zu erkennen.

## Einspielen

1. Die Dateien dieses ZIPs in den bisherigen Projektordner uebernehmen.
2. make ausfuehren. Der Catalog wird dabei automatisch gebaut/geprueft.
3. AmiMAIL auf dem Amiga beenden.
4. bin/AmiMAIL als AmiMAIL in den Installationsordner kopieren.
5. _Catalogs/deutsch/AmiMAIL.catalog aus diesem Patch nach
   Catalogs/deutsch/AmiMAIL.catalog im Installationsordner kopieren.
6. AmiMAIL neu starten.

Keine Kontodaten, Guides, Fonts oder Einstellungen muessen ersetzt werden.
Ein englischer Binary-Catalog ist nicht erforderlich. Es wurden noch keine
zusaetzlichen Sprach-Catalogs erstellt.

## Ausgefuehrte Tests

- Unveraenderte zusammengesetzte Basis: Catalog V8 / 542 IDs geprueft;
  Host-Regressionssuite: 506 Checks, kein Fehler.
- Nach dem Patch: make review-test mit GCC und -Werror bestanden.
  Enthalten: vorhandene MIME-, SMTP-, IMAP-, Datei-/Anhangsexport-, Dialog-
  und Fortschritts-/Kontexttests sowie die neuen Locale-Tests.
- Catalog-/Quellentests: 29 unittest-Testfaelle bestanden. Getestet werden
  unter anderem falsche IDs/Fallbacks, fehlende Module, alte Catalogs,
  direkte deutsche Texte und versehentlich vertauschte Marker-Kodierungen.
- Neuer locale-test: 104 Checks bestanden. Er verwendet die echten MIME-
  und Codec-Funktionen mit einem amg_tr-Test-Double. Die deutschen Werte
  werden aus dem ausgelieferten Binary-Catalog gelesen. Geprueft werden
  Englisch ohne Uebersetzung, Deutsch und eine synthetische, laengere
  Uebersetzung mit unterschiedlichen UTF-8-/Latin-1-Bytes. Letztere ist
  nur eine Testfixture, kein neuer Sprach-Catalog.
- Bild-Alternativtexte, Bildlinks ohne Text, normale Links, Trackingbilder,
  maskierte img-Tags, Umlaute und zusammengesetzte Emojis geprueft.
- Komplette Host-review-test-Suite auch mit Clang bestanden. Beim gemeinsamen
  Clang-Build wurde -Wno-format-nonliteral ausschliesslich als Testoption
  gesetzt: Die bereits vorhandene dynamische vsnprintf-Formatverwendung in
  src/i18n.c loest sonst Clangs entsprechende Warnung aus. Produktions-
  Warnflags wurden nicht geaendert oder abgeschaltet.
- Clang AddressSanitizer/UndefinedBehaviorSanitizer und Leak-Erkennung:
  host-test, locale-test und mailfile-parity-test bestanden. 506 Host-Checks,
  104 Locale-Checks und 10 MIME-Datei-Paritaetsfixtures ohne Unterschiede.
- Native Syntax-/Dialogtests benutzen vorhandene explizite API-Doubles.
  Das ist kein echter m68k-Build, kein Test der locale.library und keine
  visuelle ReAction-Pruefung auf einem Amiga. Ein solcher Laufzeittest
  konnte hier nicht durchgefuehrt werden.

## Unveraendertes Verhalten

Bytegleich zur Basis bleiben gui_transfer.c, gui_runtime.c, gui_dialogs.c,
gui_actions.c, gui_internal.h, imap.c, network_task.c und gui_preview.c.
In gui_window.c und gui.c aendern sich nur die genannten Fehlermeldungs-
Aufrufe. Fortschrittskontext, Hoehenberechnung, Vorschau-Schreibschutz,
Auf-/Zuklappen von Ordnern und Konfigurationsabstaende bleiben unangetastet.

Die englischen Anhangsdialog-Bezeichnungen sind nun etwas ausfuehrlicher;
die vorhandenen Layout-Tags/Gewichte wurden nicht geaendert.

## Voraussetzungen fuer weitere Sprachen

Der aktuelle Catalog-Build ist noch gezielt auf deutsch / codeset 0
zugeschnitten. Vor weiteren Sprachen muss das Build-/Release-Werkzeug
mehrere CT-/Catalog-Paare samt Sprachmetadaten erzeugen und pruefen koennen.
Die Message-IDs muessen fuer alle Sprachen identisch bleiben.

Die GUI-Konvertierung ist derzeit Latin-1-orientiert, nicht allgemein
Unicode-faehig. Franzoesisch, Italienisch, Spanisch, Niederlaendisch und
Schwedisch waeren deshalb pragmatische erste Kandidaten, mit Tests fuer
Akzente, Schriftabdeckung, Menuekuerzel und laengere Beschriftungen.
Polnische Sonderzeichen wie U+0142, U+0105 und U+015B sind durch den bisherigen
lokalen Konvertierungspfad nicht allgemein abgedeckt. Hier braucht es
zusaetzlich passende Konvertierung und Schrift-/Codeset-Tests, nicht bloss
eine anders benannte CT-Datei. Auch franzoesische Ligaturen sollten bei der
konkreten Uebersetzung gezielt beruecksichtigt werden.

Die Reihenfolge zusaetzlicher Catalogs ist eine Produktempfehlung, keine
Aussage ueber unbekannte AmiMAIL-Nutzerzahlen. Ein muttersprachlicher
Praxistest auf AmigaOS ist vor Freigabe jeder Uebersetzung sinnvoll.
