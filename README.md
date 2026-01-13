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

## Offene Fragen
- Topologie: Stern, Mesh, Hybrid?
- Security minimal vs. realistisch?
- Wie wenig Daten reicht?

## Nächster Schritt
- 2 Nodes
- 1 Wert
- stabiler Roundtrip über Distanz
