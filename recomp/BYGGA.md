# Bygga Mario Party 2: Recompiled

Den här guiden tar dig från en ren dator till ett spelbart `mp2-recomp`. Allt som
görs från ROM:en sker på din egen dator, och inget av det hamnar i git.

> **Du behöver en egen ROM** av *Mario Party 2 (USA)*. Formaten `.z64`, `.n64`
> och `.v64` fungerar alla. Bygget kontrollerar att det är rätt version
> (SHA-1 `166eda1c05670d337e2c3f15a5db528ae1e5d6e3`).

Själva bygget är **ett kommando** (eller, på Windows, att dra ROM-filen till en
fil). Det enda du gör för hand är att installera verktygen första gången.

---

## Windows 10/11

### 1. Installera verktygen (en gång)

1. **Git** – <https://git-scm.com/download/win>. Standardvalen räcker.
2. **Python 3** (3.10 eller nyare) – <https://www.python.org/downloads/>.
   Kryssa i **"Add python.exe to PATH"** i första rutan i installationen.
3. **Visual Studio 2022 Build Tools** (gratis) –
   <https://visualstudio.microsoft.com/downloads/> → längst ner under
   *Tools for Visual Studio* → *Build Tools for Visual Studio 2022*.
   (Har du redan Visual Studio 2022 Community fungerar det också.)
   - Välj arbetsbelastningen **"Desktop development with C++"**.
   - Gå till fliken **"Individual components"**, sök på *clang* och kryssa i
     **"C++ Clang Compiler for Windows"**. (CMake och Ninja följer med
     arbetsbelastningen ovan.)
   - Installera.

### 2. Hämta koden

Öppna *Git Bash* eller *PowerShell* i en mapp med kort sökväg (t.ex. `C:\dev`)
och kör:

```
git clone --recurse-submodules https://github.com/Nahoy93/MP2-Recomp.git
cd MP2-Recomp
git checkout pc-port/steg1-recomp-grund
git submodule update --init --recursive
```

> Använd en kort sökväg som `C:\dev`. Windows har problem med mycket långa
> sökvägar, och bygget skapar tusentals filer.

### 3. Bygg

**Enklast:** öppna mappen `MP2-Recomp\recomp` i Utforskaren och **dra din
ROM-fil och släpp den på `BYGG-WINDOWS.bat`**. Ett fönster öppnas och visar
stegen 1/6–6/6.

Eller från PowerShell:

```
powershell -ExecutionPolicy Bypass -File recomp\tools\build_windows.ps1 -Rom "C:\Spel\Mario Party 2 (USA).z64"
```

Första bygget tar ungefär 15–40 minuter beroende på dator. Senare byggen går
mycket fortare.

### 4. Spela

Spelet ligger i `recomp\build\mp2-recomp.exe`. Första gången du startar det
väljer du din ROM via **"Välj ROM"** i startmenyn. Därefter står det
**"Starta spelet"**.

---

## Linux

### 1. Installera verktygen (en gång)

Ubuntu/Debian:

```
sudo apt install git python3 python3-venv cmake ninja-build clang lld llvm libsdl2-dev libgtk-3-dev
```

Fedora:

```
sudo dnf install git python3 cmake ninja-build clang lld llvm SDL2-devel gtk3-devel
```

Arch:

```
sudo pacman -S git python cmake ninja clang lld llvm sdl2 gtk3
```

Du behöver också Vulkan-drivrutiner för ditt grafikkort (Mesa räcker för
AMD/Intel, NVIDIAs egna drivrutiner för NVIDIA).

### 2. Hämta koden

```
git clone --recurse-submodules https://github.com/Nahoy93/MP2-Recomp.git
cd MP2-Recomp
git checkout pc-port/steg1-recomp-grund
git submodule update --init --recursive
```

### 3. Bygg

```
recomp/tools/build_linux.sh "/sökväg/till/Mario Party 2 (USA).z64"
```

### 4. Spela

```
recomp/build/mp2-recomp
```

---

## Kontroller

**Escape** öppnar inställningsmenyn när som helst, både i spelet och i
startmenyn. Där finns flikarna *Allmänt*, *Kontroller*, *Grafik*, *Ljud* och
*Moddar*. Under *Kontroller* kan du byta alla tangenter och handkontrollknappar.

Standardlayout för tangentbordet:

| N64 | Tangent |
|---|---|
| Styrspak | Piltangenterna eller W A S D |
| A | X eller Mellanslag |
| B | Z eller Vänster Shift |
| Z | C |
| Start | Enter |
| L / R | Q / E |
| C-knappar | I J K L |
| Styrkors | T F G H |

Handkontroller (Xbox, PlayStation, Switch Pro m.fl.) fungerar direkt via SDL.

---

## Vad bygget gör

| Steg | Vad som händer |
|---|---|
| 1 | Hämtar N64ModernRuntime, RT64 och RecompFrontend (git-submoduler i `recomp/lib`). |
| 2 | Skapar en Python-miljö i `recomp/.venv` med splat och några paket. |
| 3 | `tools/gen_syms.py` kör dekompileringens egen splat-konfiguration på din ROM och läser ut var alla ~30 000 funktioner börjar och slutar. Dekompileringen ändras inte. |
| 4 | Bygger N64Recomp och RSPRecomp och översätter spelets MIPS-kod och ljudmikrokod till C/C++ (`recomp/generated`). |
| 5 | `tools/gen_decls.py` deklarerar funktioner som den genererade koden anropar. |
| 6 | Kompilerar allt med Clang till `mp2-recomp`. |

Inställningar, sparfiler och tangentbindningar sparas i
`%LOCALAPPDATA%\mp2-recomp` (Windows) eller `~/.local/share/mp2-recomp`
(Linux). Lägg en tom fil som heter `portable.txt` bredvid programmet om du vill
ha allt i samma mapp som programmet istället.

## Om något går fel

- **Byggskriptet säger "Fel ROM"**: din ROM är inte den amerikanska versionen
  eller är modifierad.
- **Spelet startar men kraschar eller visar svart skärm**: starta det från en
  terminal (`recomp\build\mp2-recomp.exe` i PowerShell) och skicka utskriften.
  Startar du med dubbelklick på Windows hamnar utskriften i
  `%LOCALAPPDATA%\mp2-recomp\mp2.log`.
- **Miljövariabeln `MP2_DEBUG_LOADS=1`** skriver ut varje gång spelet laddar
  ett minispel eller en spelplan. Det hjälper vid felsökning.
