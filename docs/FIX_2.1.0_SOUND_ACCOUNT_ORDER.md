# AmiMAIL 2.1.0 — Sound und Kontoreihenfolge

## Basis und Umfang

Basis ist der zuletzt ausgelieferte 2.1.0-Stand einschließlich
Stabilitätskorrekturen, Anhangsdialog, Konfigurationsabstand, geteilter
Statuszeile, Ordnerfortschritt und Locale-Bereinigung (deutscher Catalog V9).
Die vollständigen Ersatzdateien enthalten diese bisherigen Änderungen.

Programmversion 2.1.0, Ausgabename `bin/AmiMAIL` und Catalog V9 bleiben bestehen.
Vorschaufeld, Anhangsauswahl, Fortschrittslogik und Fenstergeometrie werden
nicht umgebaut. Es werden keine zusätzlichen Sprachtexte benötigt.

## 1. Hinweiston und Tonvorschau

### Im bisherigen Quellcode festgestellt

`src/gui_notify.c` verlangte sowohl eine lesbare Tondatei als auch fest
`C:SoundPlayer`. Der externe Befehl wurde über eine Shell-Zeichenkette
asynchron gestartet. Fehlte eine dieser Voraussetzungen, kam dieselbe
pauschale Fehlermeldung. Die Erkennung einer bereits laufenden eigenen
Wiedergabe gab dagegen Erfolg zurück und unterdrückte nur den weiteren Ton.
Dass auf dem betroffenen Amiga tatsächlich der Befehl fehlt, ist damit nicht
nachgewiesen; die installierte Maschine wurde nicht untersucht.

### Änderung

Vorschau und neue Mails verwenden jetzt denselben nativen Sound-Worker:

- Kein externes `C:SoundPlayer`, kein Shell-Aufruf und keine Shell-Auswertung
  von Dateinamen. Der Dateipfad wird vor dem Start absolut aufgelöst.
- `datatypes.library`, das Sound-Objekt, Fertigsignal und Watchdog-Timer
  gehören ausschließlich dem Sound-Prozess. Er lädt, startet, stoppt und
  entsorgt sein Objekt selbst.
- Der DataType muss zur Sound-Gruppe gehören und `STM_PLAY` anbieten.
  Der Rückgabewert von `DTM_TRIGGER` wird nicht als portables Boolean
  interpretiert. Das Objekt bleibt bis Fertigsignal, Abbruch oder Timeout
  erhalten; weder Null noch Nichtnull beendet die Wiedergabe sofort.
- Einmalige Wiedergabe, keine überlappenden eigenen Hinweistöne.
- Nach dem Start begrenzt ein 120-Sekunden-Watchdog das Warten auf ein
  ausbleibendes Fertigsignal.
- Beim Beenden wird der Prozess zum Stoppen aufgefordert und vor dem
  Entladen des Programms abgewartet. Die GUI entsorgt kein fremdes
  noch spielendes Audioobjekt.

Die GUI wartet auf die Lade-/Startbestätigung, nicht auf das Ende der
Wiedergabe. Dateizugriff und Decodierung können diesen kurzen Startweg
verlängern. Die Bestätigung bedeutet, dass der Wiedergabeauftrag erteilt
wurde, nicht dass Audioausgabe physisch gemessen wurde. Ein in einer
DataType-Methode blockierender Treiber kann vom Watchdog nicht gewaltsam
beendet werden. Installierte Audio-DataTypes und funktionierende Ausgabe
bleiben erforderlich. Die mitgelieferten 8SVX-Samples sind für den ersten
Praxistest geeigneter als beliebig kodierte WAV-Dateien.

## 2. Konten nach links/rechts verschieben

Der ursprüngliche Modellhelfer führte bereits genau einen Nachbartausch aus.
Die genaue Folge der Ereignisse auf dem betroffenen Amiga ist nicht
aufgezeichnet worden. Der Patch sichert beide relevanten Ebenen ab:

- Ein Druck-/Loslass-Paar darf nur einen Positionswechsel auslösen.
  Weitere Loslassereignisse ohne neue Aktivierung werden ignoriert.
- Die Aktivierung wird über `WINDOW_IDCMPHook` und `IDCMP_GADGETDOWN`
  erfasst. Ein nicht vorhandenes `WMHI_GADGETDOWN` wird nicht verwendet.
  Falls die Nachricht vom übergeordneten Layout statt vom Pfeil kommt,
  dienen die gespeicherte Klickposition und die tatsächlichen
  Pfeil-Gadget-Grenzen zur Zuordnung.
- Kontoslot und visuelle Tabposition bleiben getrennt. Der ausgewählte
  Kontoslot wird durch das Verschieben nicht geändert.
- Beim Sortieren bleiben vorhandene ClickTab-Nodes und die Adressen ihrer
  kontoslotbezogenen Beschriftungen erhalten. Es wird nicht länger die
  gesamte Node-Liste freigegeben und neu angelegt. Die Liste wird zum
  Umsortieren abgehängt, neu geordnet und wieder angebunden.
- Pro Schritt wird nur mit dem nächsten eingerichteten Konto getauscht.
  Noch nicht eingerichtete Slots werden übersprungen.
- Speichern, Abbrechen, Hinzufügen und Entfernen behalten ihre bisherigen
  Aufgaben. Die letzten Abstandskorrekturen werden nicht verändert.

## Ausgeführte Prüfungen

Die Tests für den neuen Code verwenden explizite Amiga-API-Test-Doubles auf
dem Host. Der echte Produktionscode des Workers wird auf einem Host-Thread
ausgeführt; die privaten Sortierhelfer werden aus `gui_dialogs.c` extrahiert.
Dies ist keine Prüfung mit einem Amiga-SDK und keine m68k-ABI-Freigabe.

- GCC und Clang mit `-Wall -Wextra -Wshadow -Wpointer-arith`,
  `-Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Werror`.
- Zusätzliche GCC-/Clang-Läufe mit AddressSanitizer und UndefinedBehaviorSanitizer.
- 19.200 Kombinationen aus Kontopermutation, eingerichteten Slots und
  Verschieberichtung: Nachbartausch, Rückweg und Node-Identität.
- Doppelte Loslassereignisse, falscher Pfeil, geänderter aktiver Slot,
  Fensterdeaktivierung und Klick-Grenzen des Layout-Fallbacks.
- Soundstart, Fertigsignal, Überlappung, erneute Wiedergabe, Beenden während
  Wiedergabe und Watchdog. Fehler beim Dateipfad, Signal, Prozess,
  Bibliothekszugriff, Timer und DataType sowie fehlende Play-Fähigkeit.
- Beide Trigger-Rückgabekonventionen halten das Objekt bis zur Freigabe am Leben.
- Ressourcenfreigaben und nur worker-seitige Sound-Objektverwaltung.
- Bestehendes `make review-test` einschließlich MIME-, Datei-/Protokoll-,
  Anhangs-, Fortschritts-, Dialog- und Catalog-Prüfungen.
- `make host-check` für den Nicht-Amiga-Build.

Der deutsche Binary-Catalog bleibt unverändert: V9, 546 Einträge.
Ein vollständiger `m68k-amigaos-gcc`-Build, echte ReAction-Ereignisfolgen,
Audiohardware und installierte Sound-DataTypes wurden hier nicht ausgeführt.
Diese Praxistests sind deshalb noch erforderlich.

## Einspielen

Die Dateien aus dem ZIP in den bestehenden 2.1.0-Quellbaum übernehmen,
anschließend `make` ausführen. Das Ergebnis heißt weiterhin `bin/AmiMAIL`.
Am Amiga das vollständig beendete Programm durch die neue `AmiMAIL` ersetzen.
Die aktualisierten Guides können ebenfalls übernommen werden.
Catalog, Kontodateien, Tondateien und sonstige Systemdateien nicht ersetzen.
Die Tests und der Testbericht dienen der Entwicklung und müssen nicht auf
den Amiga kopiert werden.

## Gezielter Praxistest

1. Einen mitgelieferten kurzen 8SVX-Ton in den Kontoeinstellungen auswählen.
   Vorschau abwarten, erneut auswählen und anschließend den Hinweiston bei
   einer wirklich neu eintreffenden Mail prüfen. Zusätzlich einmal während
   laufender Wiedergabe AmiMAIL beenden und erneut starten.
2. Das erste Konto mit einzelnen Klicks auf den Rechtspfeil schrittweise
   verschieben (linke Maustaste auf ">").
   Pro Klick genau eine Position; das verschobene Konto bleibt ausgewählt.
   Danach zurück nach links, speichern und die Reihenfolge nach Neustart
   prüfen. Einen weiteren Versuch mit Abbrechen verwerfen.

## API-Grundlagen

- AmigaOS DataTypes Library, insbesondere Lebenszyklus, Trigger und
  Fertigsignal; der Beispieltext bezeichnet den klassischen V39-
  Trigger-Rückgabewert ausdrücklich als undefiniert:
  https://wiki.amigaos.net/wiki/Datatypes_Library
- Native ClickTab-Dokumentation, Listenbesitz, dynamische Tabs und Nummerierung:
  https://developer.amigaos3.net/autodocs/clicktab.gadget/
- Klassische NDK-Header: `soundclass.h` (SignalBit ist eine Maske),
  `classes/window.h`, `proto/datatypes.h`:
  https://github.com/BartmanAbyss/vscode-amiga-debug/tree/055097bba74dd1b2f764dcb90781d2017bd1d499/bin/linux/opt/m68k-amiga-elf/sys-include
