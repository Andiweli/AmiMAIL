# AmiMAIL 2.1.0: Anhangsdialog und Konfigurationsabstand

## Grundlage

AmiMAIL-2.1.0-improvements-changed-files.zip plus
AmiMAIL-2.1.0-build-fix-changed-files.zip. Die drei geaenderten
Produktionsdateien sind vollstaendig enthalten. Version 2.1.0 und der
Build-Ausgabename bin/AmiMAIL bleiben unveraendert.

## Anhangsauswahl und Fortschrittsfenster

Die neuen Fenster verwendeten bei ihrer Objekterzeugung ausschliesslich
NewObject(NULL, Klassenname, ...). Anders als bei einem expliziten
Klassenzeiger haengt dieser Weg von der Registrierung des oeffentlichen
Klassennamens ab. Nun verwenden alle Konstruktoren in diesen beiden Modulen
NewObjectA() mit den bereits geoeffneten Klassen aus WINDOW_GetClass(),
LAYOUT_GetClass(), LISTBROWSER_GetClass(), STRING_GetClass() und
BUTTON_GetClass(). Die Taglisten sind explizite struct-TagItem-Arrays.

Die bisherige Meldung "Nicht genug Speicher" war nicht eindeutig: Auch ein
Fehler beim Anlegen/Oeffnen des Fensters oder beim Sperren des Hauptfensters
fuehrte dazu. Ohne einen Laufzeittest auf dem betroffenen Amiga ist nicht
bewiesen, welche dieser Stellen dort abbrach. Die Klassen-Namensabhaengigkeit
ist jetzt entfernt; verbleibende Aufbaufehler werden mit einem eigenen
Schrittcode und vorhandenen lokalisierten Meldungen angezeigt.

Codes bei "Anhaenge auswaehlen ... fehlgeschlagen":

| Code | Fehlgeschlagener Schritt |
| --- | --- |
| 1 | Spalteninformationen anlegen |
| 2 | Anhangsmetadaten pruefen oder Listenzeile anlegen |
| 3 | ListBrowser erzeugen |
| 4 | Statusfeld erzeugen |
| 5 | Schaltflaechen erzeugen |
| 6 | Erste Schaltflaechenzeile erzeugen |
| 7 | Zweite Schaltflaechenzeile erzeugen |
| 8 | Hauptlayout erzeugen |
| 9 | WindowObject erzeugen |
| 10 | Intuition-Fenster oeffnen |
| 11 | Hauptfenster fuer den modalen Dialog sperren |

Code 12 bei "Auswahl speichern ... fehlgeschlagen" bezeichnet einen Fehler
beim Aufbau/Oeffnen des anschliessenden Fortschrittsfensters.

Die Eigentumsverhaeltnisse der Gadgets sind jetzt explizit. Layoutgruppen
leihen sich ihre Kinder mit CHILD_NoDispose; die Anwendung gibt sie nach
Schliessen des Fensters genau einmal frei. Das gilt auch, wenn eine
Objekterzeugung nach teilweiser Verarbeitung der Kinder scheitert.
Das WindowObject erhaelt sein Layout erst nach erfolgreichem OM_NEW.
Die gleichen Korrekturen gelten fuer das Netzwerk-Fortschrittsfenster.

Einzel-/Mehrfachauswahl, Kategorien, Abbruch, Dateiexport und der Schutz
bereits vorhandener Dateien bleiben erhalten. MIME-/Disk-Verarbeitung und
Mailvorschau wurden in diesem Patch nicht veraendert.

## Konfiguration: keine zusaetzliche Leerzeile unten

Die berechnete Hoehe des Kontofensters verwendete den unteren Rand des
Hauptfensters. Dieser ist wegen dessen Groessenaenderungsgadget breiter.
Das Kontofenster hat kein Groessenaenderungsgadget. Deshalb wird hier jetzt
Screen.WBorBottom statt Window.BorderBottom des Hauptfensters verwendet.
Bei einem 10-Pixel-Hauptfensterrand und einem normalen Rand von 2 Pixeln
entfallen damit genau 8 ueberfluessige Pixel, ohne fest kodierten Zeilenabzug.

Die Messung mit LayoutLimits(), die normale Layout-Aussenluft und die
Zentrierung vor dem Oeffnen bleiben erhalten. Keine nachtraegliche
Fensterverschiebung, keine Aenderung an den Ja/Nein-Requestern.

## Einspielen

Dateien in den bestehenden 2.1.0-Quellordner kopieren und make ausfuehren.
Auf dem Amiga AmiMAIL beenden und nur die neu gebaute Programmdatei
bin/AmiMAIL uebernehmen. Der deutsche V8-Catalog, Guides, Kontodateien und
Spool-Einstellungen sind unveraendert und muessen nicht ersetzt werden.

## Ausgefuehrte Tests

- GCC und Clang: native Zweige mit expliziten API-Doubles, -Werror.
- Neue Dialog-Kontrollflusstests mit GCC und Clang; zusaetzlich jeweils
  AddressSanitizer, UndefinedBehaviorSanitizer und Leak-Erkennung.
- Auswahl regulaerer Anhaenge, nur Grafiken, alles, manuelle Auswahl,
  leere Auswahl, Abbrechen, Schliessknopf und Ctrl-C.
- Fehler vor und nach Verarbeitung von Konstruktor-Taglisten an jeder
  Objektposition beider Dialoge und des Netzwerk-Fortschrittsfensters.
- Fehler beim Oeffnen/Sperren eines Fensters, Listen-/Spaltenfehler und
  Pruefung der neuen Fehlermeldungen. Kein doppeltes Freigeben und keine
  verbliebenen Objekte in diesen Host-Fehlerszenarien.
- Simulierter Auswahl-/Exportablauf sowie Abbruch im Fortschrittsfenster.
- make -j2 review-test host-check: bestanden. Vorhandene Host-, Datei-MIME-,
  IMAP-, SMTP-, Dateiersetzungs-, Catalog- und Paritaetstests bestanden.
- Catalog unveraendert: Version 8, 542 IDs, 19 Python-Tests bestanden.
- Byte-/Diff-Pruefung: Mailvorschau, Hauptfenster, Mailliste, Makefile,
  Versionsnummer und Transfer-/MIME-Datenpfade unveraendert. In gui_dialogs.c
  wurde nur die untere Rahmenhoehe einschliesslich Kommentar geaendert.

Die Dialogtests verwenden API-Doubles, keinen Amiga-Emulator. Sie pruefen
unsere Kontrollfluesse und Eigentumsregeln, nicht echte ReAction-Darstellung,
SDK-/m68k-ABI oder die konkrete Ursache auf dem Amiga. Ein vollstaendiger
m68k-Build, ein echter AmigaOS-GUI-Test und ein Mailserver-Test wurden hier
nicht durchgefuehrt.

Host-Dialogtests separat:

    python3 tests/test_dialog_regressions.py

Mit Sanitizern (GCC oder HOST_CC=clang):

    DIALOG_TEST_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
      ASAN_OPTIONS=detect_leaks=1 python3 tests/test_dialog_regressions.py

Am Amiga zuerst eine Mail mit kleinem PDF und eingebetteter Grafik verwenden:
Auswahldialog oeffnen, eine Datei speichern, Kategorien wechseln und Export
abbrechen. Danach im Kontofenster den Abstand unter Speichern/Abbrechen und
unveraendert zentriertes Oeffnen kontrollieren.

## Referenzen

- ReAction und direkte Klassenzeiger:
  https://wiki.amigaos.net/wiki/ReAction
- Klassische WINDOW_GetClass()-Schnittstelle:
  https://d0.se/autodocs/window_cl/WINDOW_GetClass
- CHILD_NoDispose und LayoutLimits:
  https://developer.amigaos3.net/autodocs/layout.gadget/
- Berechnung normaler Fensterraender vor dem Oeffnen:
  https://wiki.amigaos.net/wiki/Window_Structures_and_Functions
