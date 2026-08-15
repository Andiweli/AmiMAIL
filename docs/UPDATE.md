# AmiMail 1.0 RC2: GitHub-Updatepruefung

AmiMail prueft pro Programmstart hoechstens einmal den neuesten GitHub-Release
unter:

```text
https://github.com/Andiweli/AmiMAIL/releases
```

Der Check laeuft asynchron ueber den vorhandenen Netzwerk-Worker und blockiert
die ReAction-GUI nicht. Fehler des automatischen Checks bleiben still.

## Release-Schema

Empfohlenes Tag fuer RC2:

```text
v1.0-RC2
```

Das zugehoerige Release-Asset muss nach dem von AmiMail erwarteten Schema
benannt sein:

```text
AmiMAIL-v1.0-RC2.lha
```

AmiMail wertet RC-Versionen numerisch aus. Beispielsweise gilt:

```text
v1.0-RC2 > v1.0-RC1
v1.0      > v1.0-RC2
```

## Anzeige

Rechts oben im Hauptfenster steht die eingebaute Version. Darunter zeigt der
Statusbereich je nach Ergebnis:

```text
Aktuelle Version / Up to date
Neues Update     / new Update
```

Bei einem neueren Release ist der Update-Button anklickbar. Der Download wird
unveraendert nach `RAM:` geschrieben, beispielsweise:

```text
RAM:AmiMAIL-v1.0.lha
```

AmiMail entpackt oder installiert Updates absichtlich nicht automatisch.
Downloadfehler nach einem Benutzerklick werden in der Statuszeile angezeigt.

## Testschalter

Fuer einen Test, obwohl der aktuellste GitHub-Release nicht neuer als die
laufende Version ist:

```text
SetEnv AmiMAILUpdateTest 1
```

Danach AmiMail neu starten. Zum Deaktivieren:

```text
SetEnv AmiMAILUpdateTest 0
```

Der Schalter veraendert nur die lokale Bewertung des von GitHub gelieferten
Releases. Repo, Tag und Download-URL werden nicht gefaelscht.
