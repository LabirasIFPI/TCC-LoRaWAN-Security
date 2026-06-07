# TCC-LoRaWAN-Security

> **Analysis of Availability and Energy Efficiency in LoRaWAN Networks under Reactive Jamming Attack: A Simulation-Based Approach Using NS-3**  
---

## Abstract

This work quantitatively analyzes the impact of **reactive jamming** on the availability and energy efficiency of LoRaWAN networks, focusing on the Brazilian regulatory scenario (AU915 band). Through simulations in NS-3, PDR and energy consumption metrics were evaluated under different Spreading Factors (SF), in both fixed-SF and ADR-enabled experiments. The attack severely reduced network availability, with PDR drops of up to 24.7 percentage points in SF7. An "energy degradation spiral" was identified, in which ADR increases the SF in response to collisions, raising exposure time and battery drain — an effect worsened by the absence of Duty Cycle restrictions in Anatel's AU915 regulations.

---

## Key Results

- PDR drop of up to **24.7 pp at SF7** (fixed-SF experiment)
- PDR drop of up to **22.6 pp at SF8** in the rural environment with active ADR
- Identification of the **"energy degradation spiral"** phenomenon
- Battery lifetime reduction of up to **33% at SF7** in the rural scenario
- The jammer is **energetically harmless without ADR** — isolating ADR as the sole vector of energy exhaustion
- At 5,000 m, jammer impact on PDR becomes marginal due to natural channel attenuation, though energy drain via ADR persists
- In urban environments, PDR drops of only **3–7 pp** at short distances, becoming indistinguishable from channel degradation at long distances

---

## Repository Structure

```
TCC-LoRaWAN-Security/
├── README.md
├── artigo/
│   └── artigo_somma.pdf
├── simulacao/
│   ├── scratch/
│   ├── results/
│   └── plots/
└── docs/
    └── results.md
```

---
## How to Reproduce the Simulation

### Requirements

- NS-3 version 3.45
- LoRaWAN module (Van den Abeele et al., 2017)
- Linux (Ubuntu recommended)

### Running

```bash
cd simulacao/
./waf configure
./waf build
./waf --run <script-name>
```

---

## Simulation Parameters

| Parameter | Value |
|---|---|
| Simulator | NS-3 v3.45 |
| LoRaWAN Module | Abeele et al. (2017) |
| Coverage Radius | 1,000 m to 5,000 m |
| Number of End-devices | 30 |
| Payload Size | 20 bytes |
| Traffic Interval | 1 packet every 3 minutes |
| Frequency Plan | AU915 (Anatel Act nº 14.448/2017) |
| Spreading Factors | SF7 to SF12 |
| Jammer Distance to Gateway | 50 m |
| Simulation Duration | 24 hours per scenario |
| Seeds | 5 (statistical validation) |

---

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.