# PC-Anwendung fuer den Mikrocontroller-Rechner

Diese C++-Konsolenanwendung bildet die PC-Seite der Portfolio-Aufgabe ab.
Sie baut eine serielle Verbindung zum Arduino auf, sendet Rechenausdruecke wie
`34 * 72` an den Mikrocontroller, zeigt die Rueckmeldung an und kann den gesamten
Nachrichtenaustausch als Textdatei speichern.

## Funktionen

- serielle Kommunikation zwischen PC und Mikrocontroller
- Versand eines Ausdrucks im Format `<a> <op> <b>`
- Anzeige der Rueckmeldung des Arduino im Terminal
- Protokollierung aller gesendeten und empfangenen Nachrichten
- Export des Kommunikationsprotokolls in eine Textdatei
- lauffaehig unter Windows sowie unter macOS/Linux

## Dateien

- `microcontroller_pc_client.cpp` - Quellcode der PC-Anwendung
- `microcontroller_pc_client` - unter Linux bereits kompiliertes Beispiel-Binary

## Kompilierung

### Windows (Visual Studio Developer Command Prompt)

```bat
cl /EHsc /std:c++17 microcontroller_pc_client.cpp
```

### macOS / Linux (g++ oder clang++)

```bash
g++ -std=c++17 -Wall -Wextra -pedantic microcontroller_pc_client.cpp -o microcontroller_pc_client
```

## Ausführung

### Variante 1: Port beim Start uebergeben

Windows:

```bat
microcontroller_pc_client.exe COM3 115200
```

macOS / Linux:

```bash
./microcontroller_pc_client /dev/cu.usbmodemXXXX 115200
```

### Variante 2: Port beim Start eingeben

```bash
./microcontroller_pc_client
```

Dann die serielle Schnittstelle eingeben.

## Bedienung im laufenden Programm

- `34 * 72` -> Ausdruck senden
- `10 / 4` -> Ausdruck senden
- `:save protokoll.txt` -> gesamtes Protokoll speichern
- `:help` -> Hilfe anzeigen
- `:quit` -> Programm beenden

## Erwartetes Verhalten mit deinem Arduino-Sketch

Nach dem Oeffnen der seriellen Schnittstelle sendet der Arduino einmal:

```text
OK ready
```

Danach liefert der Arduino fuer gueltige Eingaben z. B.:

```text
OK 2448
```

oder bei Fehlern z. B.:

```text
ERR parse
ERR div0
```

## Beispielablauf

```text
[uC] OK ready
> 34 * 72
[PC] 34 * 72
[uC] OK 2448
> 10 / 4
[PC] 10 / 4
[uC] OK 2.500000
> :save protokoll.txt
Protokoll gespeichert: protokoll.txt
```

## Hinweis für Phase 2

Fuer die schriftliche Phase-2-Dokumentation kannst du diese Struktur verwenden:

1. kurze Ergaenzung zum Feedback aus Phase 1
2. Aufbau des Gesamtsystems (PC-Anwendung + Mikrocontroller-Anwendung)
3. Beschreibung der Module
4. Kommunikationsablauf ueber UART
5. Testfaelle und Ergebnisse
6. Build- und Startanleitung
7. GitHub-Link

## Mögliche naechste Schritte

- GitHub-Repository sauber anlegen
- Arduino-Code und PC-Code gemeinsam strukturieren
- UML-Diagramm als Platzhalter im Bericht vorsehen
- Phase-2-Bericht als LaTex-Datei aufbauen
