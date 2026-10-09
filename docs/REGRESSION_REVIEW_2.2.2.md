# AmiMAIL 2.2.2 – Nachprüfung und Korrekturen

Stand: 10.10.2026. Vergleichsbasis ist `main` bei
`b8bfa79dc207066e6703bcfee220bf93eb9413f5`; der zuvor ausgelieferte
Audit-Stand ist `402ec11281bea8cd3eafa24c51a12d228bce75ce`.
Die Änderungen werden ausschließlich auf `audit` abgelegt.

Die Codekorrekturen wurden einzeln committed, auf `audit` gepusht und jeweils
mit dem Remote-SHA abgeglichen:

- `1a10bd1bd1e2e5a11da1ee68dbb8a3b7cadbf7c8`: Gmail-Filter und Regressionen.
- `3419814d3b2defb838fb531c9d156c679bd70fe6`: Requester-Abstände und Regressionen.

## Ergebnis der Rückmeldung

Die letzte Größenkorrektur der Ja/Nein-Requester machte deren Innenhöhe vom
tatsächlichen Layoutminimum abhängig. Im Textlayout fehlte ein ausdrücklich
reservierter Abstand vor den Schaltflächen; zusätzlicher freier Platz konnte
dadurch verschwinden. Das war eine sichtbare Verschlechterung. Der bisherige
Test verwendete eine feste Layout-Mindesthöhe und erkannte den verlorenen
Abstand deshalb nicht.

Die Bestätigungen erhalten nun vier zusätzliche Pixel für die Frage, zwei für
den Hinweis und eine leere Zeile in Höhe des Screenfonts vor Ja/Nein. Die
Leerzeile verwendet dieselbe transparente, schreibgeschützte Klasse wie die
Textzeilen. Es wird keine weitere Gadgetbibliothek benötigt. Die Innenmaße,
Zentrierung und Größenberechnung durch ReAction bleiben erhalten; die frühere
unsichere Übernahme der Hauptfensterrahmen wird nicht wieder eingeführt.
Ja, Nein, Escape, Fensterschließen und ein Öffnungsfehler behalten ihre
getrennten Ergebnisse. Ein nicht bestätigter Löschvorgang sendet keinen Auftrag.

Der Gmail-Filter war weiterhin von der Host-Erkennung und dem Selectable-Flag
abhängig. Er erkennt jetzt zusätzlich Google-OAuth-Konten und `[GoogleMail]`.
Bei einem erkannten Google-Konto werden `[Gmail]`, `[Google Mail]` und
`[GoogleMail]` auch dann ausgeblendet, wenn LIST sie als auswählbar meldet.
Bekannte nicht auswählbare Namespace-Wurzeln werden auch ohne Google-Hostnamen
ausgeblendet. Auswählbare gleichnamige Mailboxen anderer Provider bleiben
erhalten. Unterordner bleiben sichtbar und behalten ihre vollständigen
Serverpfade; Baumaufbau und Scroller werden nicht geändert.

Die [Google-IMAP-Dokumentation](https://developers.google.com/workspace/gmail/imap/imap-extensions)
zeigt `[Gmail]` als Namespace mit `Noselect` und beschreibt auch `[GoogleMail]`.
Die neuen Tests decken die zusätzlichen Filterbedingungen ab. Ein aktueller
LIST-Mitschnitt vom Gerät liegt nicht vor; welche dieser Bedingungen dort zum
sichtbaren Container führte, ist deshalb nicht abschließend nachgewiesen.

## Nachprüfung der bisherigen Audit-Änderungen

Alle geänderten produktiven C-Dateien wurden erneut gegen die Vergleichsbasis
gelesen. Neben den beiden GUI-Korrekturen wurden insbesondere folgende
Verbindungen zwischen den Änderungen geprüft:

| Bereich | Prüfung und Ergebnis |
| --- | --- |
| Kontakte und Dateiersetzung | Ladefehler gegenüber fehlender Datei, alte `.old`-Sicherung, `.bak`-Wiederherstellung und fehlgeschlagenes Speichern geprüft. Keine weitere bestätigte Regression. |
| Kontokonfiguration | Vorhandener Schlüssel bleibt bei fehlgeschlagenem Save erhalten; eine bereits gespeicherte Konfiguration kann mit passendem `.key.pending` wiederhergestellt werden. Konfigurationsformate und automatische Anmeldung bleiben erhalten. |
| IMAP | LIST/XLIST-Literale, Flags, Mailboxquotierung und UID/Größe vor oder nach FETCH-Literalen geprüft. Unterordnerpfade werden durch den GUI-Filter nicht verkürzt. |
| SMTP | Quotes, Kommata in Anzeigenamen, Kommentare, Gruppen und bisherige Semikolon-Listen geprüft. Vorhandene Versand- und Streamtests bestehen. |
| TLS und Worker | Taskbezogene Fehlernummern, nichtblockierender Verbindungsaufbau, WANT-Retries, Deadline-Verschachtelung, Teilwrites, Abbruchsignal und Ressourcenabbau geprüft. Hosttests ersetzen keinen Lauf mit dem realen TCP/IP-Stack und AmiSSL. |
| HTTPS/OAuth/Update | Fehler bei Buffer-Allokationen, HTTP-Längen und Chunks, gemeinsamer Decoder, Tokenwerte und bestehende Erfolgspfade geprüft. Keine weitere bestätigte Regression. |
| Anhangsexport | Anzahl aus der vorbereiteten Anhangsliste, Pfadgrenzen, Verzeichniswiederherstellung und Fehler der Präferenzspeicherung geprüft. Der bestehende Auswahldialog ist unverändert. |
| Build und Paketlayout | Getrennte Release-/Debug-Objekte, vollständige Installer-Stage, vorhandenes Icon, Sprachdateien und Einschränkung des Quellarchivs auf versionierte Dateien geprüft. |

Zusätzlich wurde der Quelltext direkt verglichen: Alle übrigen GUI-Module sind
unverändert. In `gui_dialogs.c` ist der gesamte Bereich vor den
Bestätigungsrequestern unverändert. Die Anhangsauswahl sowie Baumaufbau,
Expansion und Ordnerscroller sind ebenfalls unverändert. Damit wurden keine
Layoutänderungen an Kontoeinstellungen, Kontakten, Composer, Anhangauswahl,
Hauptfenster, Vorschau oder vorhandener Statusleiste vorgenommen.

Es wurde keine weitere bestätigte Funktionsregression gefunden. Die vorhandene
statische Warnung in `src/mailto.c:178` bleibt ein unbestätigter Analyzer-Hinweis,
wie im [ursprünglichen Auditbericht](AUDIT_2.2.1.md) beschrieben. Sie wurde nicht
unterdrückt. Ein vollständiger Nachweis korrekter Darstellung und Bedienung auf
echter Hardware folgt aus dieser Quellprüfung nicht.

## Validierung

- `make review-test host-check static-analysis`: erfolgreich; statische Analyse
  über 25 Host-Module mit dem oben genannten einzelnen Hinweis.
- Ordnerauswertung: 89 Prüfungen mit dem tatsächlichen nativen Mapping und
  UI-Doubles. Requester: 30256 Prüfungen mit dem tatsächlichen Requester-,
  Lösch- und Entwurfscode sowie API-Doubles.
- Der Requestertest berechnet die Mindesthöhe nun aus den tatsächlich gesetzten
  Kind-Tags und prüft Textabstände, Leerzeile und Fonts von 8, 12, 16 und 24
  Pixeln, mit und ohne Hinweis. Unterschiedliche Fensterrahmen, Antworten,
  Öffnungsfehler und alle acht Objektallokationsfehler sind abgedeckt.
- Die neuen Filter- und Abstandsanforderungen schlagen mit dem unveränderten
  vorherigen Code aus `402ec112` fehl. Das belegt die Testunterschiede; es ist
  kein Nachweis der konkreten LIST-Antwort oder ReAction-Darstellung des Geräts.
- Ordner- und Requestertests zusätzlich mit ASan und UBSan erfolgreich.
- Release und Debug vollständig mit `make -B` neu gebaut und m68k-gelinkt:
  jeweils 50 Compilation Units, keine Compiler- oder Linkerwarnungen.
  Beide Programme haben das Amiga-HUNK-Format und die Kennung
  `$VER: AmiMAIL 2.2.2 (10.10.2026)`.
- Beide Pakettests bestanden mit dem neu gebauten Programm. Die Guide-Dateien
  behalten ihre Latin-1-Inhalte; gegenüber dem vorherigen Audit-Stand wurden
  dort ausschließlich Versionsangaben und Datum geändert.

## Version und Gerätetest

Build, Programmkopf, `$VER`, README-Versionsbadge und beide Guides tragen jetzt
2.2.2. Katalogquellen, IDs und Binärkatalog bleiben unverändert und kompatibel
mit V13. Alte Versionsangaben in historischen Fix-Berichten bleiben erhalten.
Es wird kein GitHub-Release erstellt und kein bestehender Tag geändert.

Auf dem Amiga bitte das neu gebaute Programm ersetzen und die Version 2.2.2
prüfen. Die aktualisierten Guides können mitkopiert werden; ein neues Catalog
ist für diese Änderungen nicht erforderlich. Gmail neu verbinden, die
Namespace-Wurzeln und erhaltene Unterordner prüfen. Die Ja/Nein-Bestätigungen
für Löschen, Papierkorb, Spam und Entwurfsschließen mit der tatsächlich
verwendeten Schrift auf Abstände sowie Ja, Nein und Abbruch prüfen.

## Vorschlag für die Meldungen in der Mailliste

Die bisherige Platzhalterzeile schreibt ihre Meldung in die Betreffspalte.
Als Ersatz bietet sich ein zentrierter Hinweis über die gesamte freie Fläche
der leeren Mailliste an: „Noch nicht verbunden“, „Ordner wird geladen“ oder
„Ordner ist leer“. Bei Bedarf kann dort eine vorhandene Aktion wie Abrufen oder
Erneut versuchen angeboten werden. Allgemeine Informationen und Fortschritt
bleiben in der vorhandenen Statusleiste unten. Dieser Vorschlag wurde auf
ausdrücklichen Wunsch noch nicht implementiert.
