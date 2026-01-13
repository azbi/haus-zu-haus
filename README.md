# haus-zu-haus

Status: frühe Idee / unfertig

## Idee
ESP32-Nodes kommunizieren über große Distanzen
(z. B. .ch ↔ .ee) und tauschen aggregierte Zustände aus.

Noch kein Produkt, sondern ein verteiltes Experiment.

## Kontext
- mehrere Nodes
- lange Latenzen
- Internet / NAT / Firewalls
- Robustheit wichtiger als Echtzeit

## Grobe Bausteine
- ESP32
- MQTT oder alternative Transportidee
- einfache Payloads
- kein Cloud-Zwang

## Protokoll / Topics
Alle Nodes senden **nur rohe Messwerte**.
Keine Heuristik, keine Interpretation auf dem ESP32.

### Topic-Namespace
- `h2h`  
  globaler Namespace für *haus-zu-haus*
- `<house_id>`  
  z. B. `haus1`, `haus2`
- `<room>`  
  z. B. `wc`, `stube`
- `<metric>`  
  frei benannter Messwert (Semantik!)

### Beispiele (aktuell)

**Haus 1**
- `h2h/haus1/wc/humid`  
  → Luftfeuchte WC (%)
- `h2h/haus1/wc/light`  
  → Lichtpegel oder digitaler Wert
- `h2h/haus1/stube/light_adc`  
  → ADC-Wert Lichtsensor
- `h2h/haus1/stube/light_state`  
  → digital (0/1), falls vorhanden

**Haus 2**
- `h2h/haus2/wc/humid`
- `h2h/haus2/stube/light`

### Payload
Payload ist **immer ein einzelner numerischer Wert**:
- integer oder float
- keine JSON-Struktur
- keine Einheiten im Payload

Beispiele:
42
58.3
0
1
3120

Interpretation (z. B. „jemand da“, „Dusche läuft“) erfolgt **nicht auf dem Node**, sondern downstream.

### Erweiterbarkeit
Neue Sensoren werden hinzugefügt durch:
- neues `<metric>`
- kein Protokollbruch
- keine Anpassung bestehender Nodes

Beispiel (zukünftig):
- `hzh/haus1/stube/sound_rms`
- `hzh/haus1/stube/sound_peak`


## Offene Fragen
- Topologie: Stern, Mesh, Hybrid?
- Security minimal vs. realistisch?
- Wie wenig Daten reicht?

## Nächster Schritt
- 2 Nodes
- 1 Wert
- stabiler Roundtrip über Distanz
