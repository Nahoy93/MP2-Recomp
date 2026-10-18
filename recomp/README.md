# Mario Party 2: Recompiled

En PC-port av *Mario Party 2* (N64) för **Windows och Linux**, gjord med statisk
rekompilering: spelets egen MIPS-kod översätts till C och kompileras för PC,
ovanpå [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) och
grafikmotorn [RT64](https://github.com/rt64/rt64). Det är samma teknik som
*Zelda 64: Recompiled* använder.

Repot innehåller **ingen speldata**. Du bygger spelet från din egen ROM;
se [BYGGA.md](BYGGA.md).

## Hur det hänger ihop

```
din ROM ─▶ tools/gen_syms.py (splat + decompens yaml/symboler) ─▶ generated/mp2.us.syms.toml
        ─▶ N64Recomp ─▶ generated/RecompiledFuncs/*.c   (≈30 000 funktioner)
        ─▶ RSPRecomp ─▶ generated/RecompiledFuncs/aspMain_rsp.cpp (ljudmikrokod)
        ─▶ CMake + Clang ─▶ mp2-recomp(.exe)
```

Dekompileringen i resten av repot används bara för att *läsa* var funktionerna
ligger och vad de heter. Den behöver inte vara färdig, och inget i den ändras.

| Mapp | Innehåll |
|---|---|
| `config/` | N64Recomp- och RSPRecomp-inställningar |
| `tools/` | byggskript och generatorer |
| `src/`, `include/` | portens egen kod: fönster, ljud, inmatning, menyer, overlay-laddning |
| `assets/` | menyns typsnitt och ikoner |
| `lib/` | submoduler: N64ModernRuntime, RT64, RecompFrontend |
| `generated/` | skapas från din ROM, aldrig i git |

## Plan

| Steg | Innehåll | Status |
|---|---|---|
| 1 | Recomp-grund: bygget, overlay-laddning, ljud, fönster, tangentbord/handkontroll, startmeny och ESC-meny (Allmänt, Kontroller, Grafik, Ljud, Moddar) | **den här grenen** |
| 2 | Egen ESC-meny för spelet: fortsätt, starta om, avsluta till startmenyn, snabbval | planerat |
| 3 | Kontroller och grafik: lokal multiplayer (P1–P4 till olika enheter), widescreen, högre bildfrekvens | planerat |
| 4 | Förbättringar och fusk: egna flikar *Förbättringar* och *Fusk* (mynt, stjärnor, tärning m.m.) | planerat |
| 5 | Netplay med direktanslutning: Host/Join via IP och port, synkad inmatning | planerat |

### Läget just nu

Steg 1 är skrivet och testat så långt det går utan ROM: verktygskedjan
(splat → N64Recomp → RSPRecomp → deklarationer) och hela C++-bygget med runtime,
RT64 och menyer har körts mot en syntetisk ROM på Linux. **Själva spelet har
ännu inte körts**, eftersom det kräver den riktiga ROM:en. Räkna med att det
första riktiga bygget kan stöta på saker som behöver justeras, t.ex. en
libultra-funktion som spelet använder på ett sätt runtime inte räknat med.
Kör då spelet från en terminal och skicka utskriften.

## Teknik i korthet

- **Overlays.** Alla minispel och spelplaner länkas till samma adress,
  `0x80102800`. Spelet laddar dem i bitar om 0x4000 byte via
  `HuRomDmaCodeRead`, vilket runtime inte kan se som en hel laddning. Porten
  ersätter den funktionen (`src/overlays.cpp`) och talar om för runtime vilken
  overlay som ligger där.
- **ESC-menyn** kommer från RecompFrontend. Escape öppnar och stänger den, och
  så länge den är öppen får spelet ingen inmatning.
- **Tangentbord och handkontroller** går via RecompFrontends `recompinput` med
  profiler som sparas mellan sessioner.

Se [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) för licenser. Det byggda
programmet omfattas av GPL-3.0 (via N64ModernRuntime).
