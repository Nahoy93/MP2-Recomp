# Tredjepartskomponenter / Third-party notices

Det byggda programmet innehåller komponenterna nedan. Eftersom N64ModernRuntime
är GPL-3.0 gäller GPL-3.0 för det färdiga programmet som helhet.

The built program contains the components below. N64ModernRuntime is GPL-3.0,
so the program as a whole is distributed under GPL-3.0.

| Komponent | Roll | Licens |
|---|---|---|
| [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime) (librecomp, ultramodern) | körtidsmiljön för den rekompilerade koden | GPL-3.0 |
| [N64Recomp](https://github.com/N64Recomp/N64Recomp), RSPRecomp | rekompilatorn | MIT |
| [RT64](https://github.com/rt64/rt64), plume | grafik | MIT |
| [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) (recompui, recompinput) | startmeny, ESC-meny, kontroller | se projektet |
| [RmlUi](https://github.com/mikke89/RmlUi) | UI under recompui | MIT |
| [SDL2](https://libsdl.org) | fönster, ljud, inmatning | Zlib |
| [Lato](https://www.latofonts.com) (från RmlUi) | menytypsnitt | SIL OFL 1.1 |
| [PromptFont](https://shinmera.com/promptfont) (`assets/promptfont`) | knappikoner | SIL OFL 1.1 (`assets/promptfont/LICENSE.txt`) |
| [splat](https://github.com/ethteck/splat), [spimdisasm](https://github.com/Decompollaborate/spimdisasm) | används vid bygget för att läsa ROM:en | MIT |

## Body Harvest: Recompiled

`assets/icons/*.svg`, `assets/promptfont/` and parts of the structure of
`CMakeLists.txt`, `src/main.cpp`, `src/platform.cpp`, `src/frontend.cpp` and
`src/overlays.cpp` are adapted from
[Body Harvest: Recompiled](https://github.com/danielgomesvieira2000/body-harvest-recomp):

```
MIT License

Copyright (c) 2026 Body Harvest: Recompiled contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The audio microcode branch-target table in `config/aspMain.us.toml` is the one
published by Snowboard Kids 2: Recompiled and Body Harvest: Recompiled for the
same stock libultra `aspMain`.

## Speldata / Game data

Det här repot innehåller ingen speldata. Du måste använda en egen, lagligt
dumpad ROM av Mario Party 2 (USA). / This repository contains no game data.
