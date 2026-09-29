# AmiMAIL 2.1.0 – optionale Herald-Benachrichtigungen

Stand: 27.09.2026. Programmversion: **2.1.0**. Ausgabedatei: **bin/AmiMAIL**.
Deutscher Catalog: **V10, 558 Einträge**.

## Ausgangspunkt und Einspielen

Dieser Patch baut auf dem zuletzt gelieferten Update
`AmiMAIL-2.1.0-remember-attachment-directory-changed-files.zip` auf. Die
vorherigen 2.1.0-Korrekturen für Dialoge, Statushöhe, Ordnerfortschritt,
Lokalisierung, Sound und Kontosortierung sind in der Arbeitsbasis enthalten.
Es werden nur vollständige geänderte beziehungsweise neue Dateien geliefert.

1. Den aktuellen Quellordner und die installierte Konfiguration sichern.
2. Die Dateien des ZIP mit ihren relativen Pfaden im Quellordner übernehmen.
3. `make clean && make` ausführen. Ein sauberer Neuaufbau ist wichtig, da
   `AmgAccount` ein zusätzliches Konfigurationsfeld enthält.
4. AmiMAIL am Amiga beenden und die neu gebaute `bin/AmiMAIL` als `AmiMAIL`
   im Programmverzeichnis installieren.
5. `_Catalogs/deutsch/AmiMAIL.catalog` als
   `Catalogs/deutsch/AmiMAIL.catalog` in der Installation ersetzen.
6. Bei Bedarf auch beide aktualisierten Guides aus `_Guides` übernehmen.

Das ZIP enthält **keine vorgebaute AmiMAIL-Programmdatei** und keinen Herald-
Server. Der deutsche Binary-Catalog ist fertig erzeugt und überprüft. Ein
englischer Catalog ist nicht nötig; englische Fallbacks bleiben im Programm.
Bestehende Konfigurationen bleiben lesbar. Fehlt der neue Eintrag
`herald_notifications`, ist die Funktion ausgeschaltet. Speichern und
Abbrechen des Konfigurationsfensters behalten ihre bisherigen Aufgaben.

## Bedienung und Umfang

In den Konto-Einstellungen steht unter dem Benachrichtigungston eine neue
Zeile mit **Herald-Benachrichtigungen** und **Test**. Die Checkbox gilt pro
Konto, ist unabhängig vom Hinweiston und standardmäßig aus. Der Testbutton
funktioniert auch bei ausgeschalteter Checkbox und verwendet den gerade
angezeigten Kontonamen; er speichert oder aktiviert die Einstellung nicht.
Die Antwort erscheint im Statusfeld dieses Fensters. Nach einem Kontowechsel
wird kein verspätetes Testergebnis dem falschen Konto zugeordnet.

Herald muss bereits laufen. AmiMAIL startet oder installiert ihn nicht und
benötigt weder `C:HeraldSend`, RexxMast noch ein ARexx-Skript. Verwendet werden
`rexxsyslib.library` und native Exec-Nachrichten im dokumentierten RexxMsg-
Format. Die Library und die lokalen Port-/Timerressourcen werden erst bei
Bedarf angelegt. Ohne Herald bleibt AmiMAIL normal bedienbar. Automatische
Benachrichtigungsfehler erzeugen keine Popups und überschreiben nicht die
normalen Statusmeldungen; der manuell ausgelöste Test meldet seinen Status.

Automatische Karten betreffen neu hinzugekommene Nachrichten im Posteingang,
ermittelt aus der bestehenden UID-Vergleichsbasis. Das funktioniert für das
aktive Konto und für die Hintergrundkonten, auch bei iconifiziertem AmiMAIL.
Ein erstes Einlesen ohne Vergleichsbasis und ein UIDVALIDITY-Wechsel erzeugen
keine Karten für den alten Ungelesen-Bestand. Das Öffnen vorhandener Mails,
das Laden anderer Ordner und die interne Fortschrittsanzeige sind keine
Herald-Auslöser. Der Patch ändert keine Abrufintervalle.

Beispiel einer Meldung: `3 neue Nachrichten - Privat` beziehungsweise
`3 new messages - Private`. Singular und Plural sind separat lokalisiert.
Verwendet wird nur der Kontoname, kein Absender oder Betreff. Ist kein Name
eingetragen, steht dort `Konto` beziehungsweise `Account`; eine Mailadresse
wird nicht ersatzweise eingeblendet. Trägt der Nutzer selbst eine Mailadresse
als Kontonamen ein, ist sie entsprechend Bestandteil dieses Namens.

Die angezeigte Anzahl bezieht sich auf den **letzten Abruf mit neuen Mails**,
nicht auf die Gesamtzahl ungelesener Nachrichten. Für dasselbe Konto wird
nur die neueste noch nicht gesendete Meldung aufbewahrt, nicht unbegrenzt
aufsummiert. Meldungen, die bei nicht laufendem Herald entstehen, werden
nicht für einen späteren Start gesammelt.

Karten sind absichtlich **stumm** (`NOSOUND`), einschließlich des Tests. Der
bisherige AmiMAIL-Hinweiston arbeitet unabhängig weiter. Priorität bleibt
normal, ohne Sticky, erzwungenes Blinken oder erzwungene Bildschirmwahl.
Anzeigedauer, Gruppierung und „Nicht stören“ bleiben Herald-Einstellungen.
„Testbenachrichtigung angenommen“ bestätigt die positive Befehlsantwort;
daraus folgt nicht, dass Herald trotz solcher Einstellungen ein Popup zeigt.
Ein Doppelklick zum Aktivieren eines Kontos ist **nicht** Teil dieses Patches.

## Technische Umsetzung

- `src/herald.c`: optionaler Transport, Registrierung, Quoting, begrenzte
  Warteschlange, Reply-Verarbeitung, Timer und sichere Ressourcenverwaltung.
- `src/gui_herald.c`: englische/deutsche Meldungen, Kontooption, Teststatus.
- Konto-/Speicherstruktur: zusätzliches boolesches Feld; alte oder ungültige
  Werte aktivieren die neue Funktion nicht automatisch.
- Drei bestehende Neumail-Erkennungspfade sind angebunden: manueller Abruf
  des Posteingangs, aktive periodische Prüfung und Hintergrundkontoprüfung.
- Hauptschleife und Kontokonfiguration verarbeiten Herald-Reply-/Timersignale
  asynchron. Der Transport wartet im normalen Betrieb nicht auf den Server.
- Im Konfigurationsfenster werden dafür ausschließlich die Herald-Signale
  zusätzlich bedient, nicht beliebige Netzwerkaktionen im Hintergrundfenster.

Jede Serie registriert `APP=AmiMAIL` neu; „APP ALREADY REGISTERED“ gilt als
Erfolg. Damit wird auch ein späterer Herald-Neustart erkannt, ohne einen
Portzeiger dauerhaft als Lebenszeichen zu behandeln. Falls die Registrierung
zwischen REGISTERAPP und NOTIFY verloren geht, wird einmal erneut registriert.
Rexx-Rückgabecode **und** Antworttext werden geprüft: ein `ERROR:` kann laut
mitgelieferter Implementierung auch mit `RC_OK` eintreffen.

Die Benachrichtigung hat zum Beispiel diese Form:

```text
NOTIFY APP=AmiMAIL ID=mail0 GROUP=mail0 UPDATE PRI=0 NOSOUND TITLE="AmiMAIL" TEXT="3 neue Nachrichten - Privat"
```

ID und GROUP verwenden den stabilen Kontoslot, nicht die Tabposition. Tests
verwenden `test0` statt `mail0`, damit sie reguläre Karten nicht ersetzen.
`TEXT` steht zuletzt. Anführungszeichen und Sternchen werden für ReadArgs
korrekt escaped; Steuerzeichen werden neutralisiert. Text bleibt innerhalb
der in Heralds `engine.h` vorgegebenen 160 Byte. Kontonamen sind dafür auf
80 lokale Zeichen begrenzt. Pro abgeschlossenem Versuch folgt eine Pause
von zwei Sekunden, damit mehrere Hintergrundkonten nicht sofort Heralds
Flood-Schutz auslösen. Es gibt höchstens einen laufenden Protokollauftrag und
höchstens eine wartende Meldung je Kontoslot plus einen separaten Test.

Ein `UNREGISTERAPP` wird beim Beenden bewusst nicht gesendet: Laut API würde
es bereits angezeigte und wartende AmiMAIL-Karten entfernen.

## Zeitlimits und wichtige Ausnahme beim Beenden

Nach fünf Sekunden ohne Antwort wird der Auftrag als ungeklärt behandelt.
Eine noch nachweislich in Heralds Portwarteschlange liegende eigene Nachricht
kann zurückgenommen und freigegeben werden. Hat Herald sie bereits mit
`GetMsg()` übernommen, darf AmiMAIL sie nicht freigeben: der Server könnte
noch auf sie zugreifen oder später `ReplyMsg()` aufrufen. Dann wird kein
weiterer Auftrag gesendet, bis dieses eine Paket zurückkommt. Die GUI bleibt
bedienbar; spätere Antworten führen nicht mehr zum Senden alter Meldungen.
Eine ausbleibende Antwort beweist nicht, dass keine Karte angezeigt wurde.

Beim Beenden gibt AmiMAIL einem bereits übernommenen Paket eine letzte,
auf **250 ms** begrenzte Rückgabefrist. Ist es dann noch im Besitz des Servers,
bleiben **dieses eine Nachrichtenpaket, seine separat allokierten Strings,
der private Antwortport und eine Library-Referenz** aus Sicherheitsgründen
reserviert. Die Signalisierung ist deaktiviert, sodass eine späte Antwort
keinen bereits beendeten GUI-Task anspricht. Der restliche Herald-Client und
seine Timerressourcen werden freigegeben; der Programmcode wird nicht durch
das Paket referenziert.

Das ist eine bewusst dokumentierte Ausnahme, **keine vollständige Freigabe
aller Ressourcen in diesem Fehlerfall**. Sie kann auch bei einem nur sehr
langsamen Herald auftreten. Die reservierten Ressourcen werden erst durch
einen Neustart des Amigas zurückgewonnen; wiederholte Starts/Beendigungen
unter dieser Bedingung können weitere solche Einzelpakete zurücklassen.
Der dokumentierte Herald-Client hat keinen Abbruch-/Übernahme-Handshake, der
ein vorzeitiges Freigeben eines fremd genutzten Pakets sicher machen würde.
Normale Antworten und zurücknehmbare Aufträge werden vollständig freigegeben.

## Unveränderte Bereiche

Gegenüber der letzten Arbeitsbasis bytegleich geblieben sind unter anderem:
`src/gui_attachments.c` (gemerktes Speicherverzeichnis), `src/gui_window.c`
(Statushöhe/Anordnung), `src/gui_transfer.c` (Fortschritt), `src/gui_preview.c`
(schreibgeschützte Vorschau), `src/gui_messages.c` (Mailliste) sowie
`src/gui_notify.c` (nativer Hinweiston). Die bestehende Zentrierungsberechnung
und der Pfeil-/Reihenfolge-Schutz im Konfigurationsfenster wurden nicht
verändert. Die neue Herald-Zeile vergrößert dieses Fenster um den Platz für
die zusätzliche Einstellung; sie fügt keinen Spacer unter Speichern/Abbrechen
hinzu. Die Konversationsansicht bleibt außerhalb dieses Patches.

## Ausgeführte Prüfungen

Alle folgenden Prüfungen liefen auf dem Linux-Host. Eine große Anzahl von
Checks entsteht teilweise durch Schleifen und Quoting-/Permutationstests;
sie ist nicht gleichbedeutend mit ebenso vielen unabhängigen Integrationstests.

| Prüfung | Ergebnis |
|---|---|
| `make review-test host-check` | Gesamte vorhandene Host-Prüfung bestanden |
| Allgemeine Host-Regressionssuite | 506 Checks, 0 Fehler |
| MIME-/Maildatei-Pfade | 87.669 Checks, 0 Fehler |
| IMAP-Dateitransfer mit simuliertem Gegenüber | 14.507 Checks, 0 Fehler |
| SMTP-Dateitransfer | 173 Checks, 0 Fehler |
| MIME-/Maildatei-Parität | 10 Vergleichsdateien, keine Abweichung |
| Ordnerfortschritt mit skriptgesteuertem TLS-Gegenüber | 100.747 Checks, 0 Fehler |
| Weitere Review-/Dateifehler-/SMTP-Streaming-Tests | 5.689 / 80 / 43 Checks bestanden |
| Bestehende Konto-Umsortierung | 19.200 Permutations-/Richtungsfälle bestanden |
| Sound-, Dialog-, Export-, Zielordner- und Fortschrittstests | Bestanden, native Aufrufe simuliert |
| Catalog-Tests | 30 Tests bestanden |
| Catalog-Binary und Quell-ID-/Fallback-Audit | V10, 558 IDs; 702 statische Lookup-Stellen und 19 auditierte Fallback-IDs geprüft |
| Neue Herald-Transport-/GUI-Brückenprüfung | 82.652 Checks bestanden |
| Neue Kontooption: Speichern, Laden, Kopieren, Migration | 68 Checks bestanden |
| Neue Herald-Prüfung mit GCC und Clang | Beide bestanden |
| Neue Herald-Prüfung mit ASan/UBSan und Leak-Check | Unter GCC und Clang bestanden |

Die Herald-Tests kompilieren den tatsächlichen nativen Transportcode sowie
die GUI-Brücke gegen explizite Exec-/Rexx-/Timer-Test-Doubles. Geprüft werden
u. a. Registrierung und Fehlerantworten, Quoting und Längengrenzen, fehlender
Dienst, Allokationsfehler, Neustart, Konto-Warteschlangen, verspätete Antworten,
Testkarte, Singular/Plural, deutsche Latin-1-Texte und Shutdown-Rennen.
Die Settings-Tests verwenden die echten Konto-/Speicherfunktionen mit lokalen
unverschlüsselten Testdateien. Verschlüsselung/AmiSSL ist dabei nicht ausgeführt.
Die Call-Sites und Konfigurationsanbindung werden zusätzlich statisch geprüft;
das ist kein Ersatz für den tatsächlich laufenden ReAction-Dialog.

Bei der Clang-Prüfung ist ausschließlich für das unveränderte
`src/i18n.c` und den gleichartigen Test-Formatter die Warnung zu dynamischen
Formatstrings ausgenommen. Die übersetzten Formate prüft der Catalog-Validator.
Der neue Transport und die übrigen Herald-Tests behalten `-Wformat=2 -Werror`.
Die Testsimulation gibt das absichtlich reservierte Shutdown-Paket am Testende
selbst frei; der bestandene Leak-Check widerlegt daher **nicht** die oben
beschriebene Ressourcen-Ausnahme auf einem echten Amiga.

**Nicht durchgeführt:** vollständiger m68k-Build/Linktest, ABI-Prüfung mit dem
konkreten NDK, echter AmigaOS-/Herald-Laufzeittest, visuelle Dialogprüfung und
Benachrichtigung über einen echten Mailserver. Das Paket ist der geprüfte
Quellcode-Teststand für diese anschließende Erprobung, keine native Freigabe.

## Empfohlener Test am Amiga

1. Herald starten, RexxMast darf ausgeschaltet sein. Im Konto auf Test klicken:
   stille Testkarte und Statusbestätigung erwarten. Ohne Herald einen erneuten
   Test auslösen: verständliche Statusmeldung, kein blockierender Requester.
2. Herald nur für ein Konto aktivieren und speichern. Nach dem initialen Abruf
   eine neue Testmail zustellen: eine Karte mit Konto und Anzahl erwarten.
   Danach dieselbe Mail anklicken oder den Ordner erneut abrufen: keine neue
   Herald-Karte allein wegen dieser Aktion.
3. Ein zweites Konto aktivieren, einschließlich Hintergrundkonto. Karten müssen
   getrennt bleiben. Nach Umsortieren rechts/links muss die Zuordnung stimmen.
4. Sound aus, Herald an: Karte ohne Ton. Sound an, Herald an: Herald selbst bleibt
   stumm; der vorhandene AmiMAIL-Ton darf wie zuvor laufen.
5. Herald beenden und neu starten, während AmiMAIL läuft. Ein späterer Test
   beziehungsweise tatsächlich neuer Abruf muss erneut registrieren können.
6. Checkbox ausschalten, speichern und neu starten: Einstellung bleibt aus.
   Test ohne Speichern darf die gespeicherte Einstellung nicht verändern.
7. Weiterhin Anhang exportieren und erneut öffnen: das zuletzt erfolgreiche
   Zielverzeichnis bleibt vorbelegt. Statushöhe, Ordnerfortschritt, Vorschau
   und normale Konto-Umsortierung zusätzlich kurz gegenprüfen.

## Schnittstellengrundlage und Lizenz

Verwendet wurden das vom Nutzer bereitgestellte `Herald.lha`, darin
`Files/Developer/Herald-ARexx-API.md` und die Client-Dateien
`Files/Developer/HeraldSend/rxclient.c`, `rxclient.h`, `heraldsend.c`,
`strutil.c`, `strutil.h`, `engine.h` und `HowToBuild.readme`.
Die Anpassung ersetzt das synchrone Beispiel-WaitPort durch den oben
beschriebenen asynchronen Ablauf. Sie verwendet keine private Serverstruktur.

Copyright-Hinweis und Nutzungsbedingungen des HeraldSend-Clientteils von
Marcus Gerards stehen in `docs/HERALD_CLIENT_LICENSE.txt`. Dessen ausdrückliche
Ausnahme erlaubt Übernahme und Anpassung der Developer/HeraldSend-Dateien mit
Ausnahme von `heraldpipe.c`. Dieses Paket bündelt weder den Herald-Server noch
HeraldSend, HeraldPipe, deren Grafiken oder deren vollständige SDK-Quellen.
