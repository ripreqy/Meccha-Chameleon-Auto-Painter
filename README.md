# CEMECCHA

Internal cheat for **MECCHA CHAMELEON** (UE 5.6, DX12)

> The game is just really boring, and I'm not really in the mood to do anything for it, tbh
> Educational / research code. Use responsibly — running third-party
> software against a live multiplayer service is against most games'

Discord : https://discord.gg/code-engine
Website : https://code-engine.dev

## Features

### Auto Painter
- chameleon camouflage — samples the environment behind the character
  via a spawned `ASceneCapture2D` at `SCS_BaseColor`, projects each runtime
  triangle onto that render target, and paints the sampled pixel via
  `URuntimePaintableComponent::PaintAtUVWithBrush`.
- **Back-first paint order** — sorts strokes so the character's back
  (mesh local `X < 0`) paints before the front. Great for reveal clips.
- **Progressive draw** — spreads N strokes per frame over multiple frames
  so the render thread never blocks; configurable from 1 → 4096
  strokes/frame (logarithmic slider).
- **Turbo / Fast / Balanced / Detailed / Extreme** presets tuning brush
  radius, sample density and pace.
- **Multiplayer sync** — patches `FUNC_NetReliable` onto `ServerPaintBatch`
  and flushes the internal `TArray<FPaintStroke>` at end of job so Hunters
  see the camo.

### ESP
- Skeleton, box, name (real player name via direct `PlayerNamePrivate`
  read at `APawn::PlayerState → 0x340` — no ProcessEvent), distance,
  bone-based W2S.
- Iterates `GWorld → Levels → Actors` (Meccha-style), not GObjects — 100×
  faster than the naive scan.
- Class-kind cache per `UClass*`, per-mesh bone-array offset probe,
  hardcoded paintman bone indices — zero stutter even under load.

### Stability
- SEH around every UE `ProcessEvent`, memory write and direct-read.
- Match-transition detection via GWorld pointer diff — invalidates cached
  actor / RP / mesh pointers before they're dereferenced dangling.
- Cursor flicker fix (`ImGuiConfigFlags_NoMouseCursorChange` +
  software cursor tied to menu state).

## Project layout

```
CEMECCHA/
├── CEMECCHA.sln
└── CEMECCHA/
    ├── CEMECCHA.vcxproj
    ├── src/
    │   ├── main.cpp                   # DllMain + loader thread
    │   ├── core/                      # logger, globals, pattern scan
    │   ├── sdk/                       # UE reflection + PenguinHotel offsets
    │   ├── hooks/                     # DX12 Present + ExecuteCmdLists +
    │   │                              # game-thread marshalling
    │   ├── features/                  # AutoPainter, ESP, CamoCapture,
    │   │                              # runtime_triangle_cache, Manager
    │   └── gui/                       # ImGui menu + dark-neon theme
    ├── third-party/
    │   ├── imgui/                     # docking branch
    │   └── minhook/
    └── build/                         # ignored — compiled artifacts
```

## Building

Requirements:
- Visual Studio 2022 (v143 toolset)
- Windows 10 SDK
- C++20

```
git clone https://github.com/<you>/CEMECCHA.git
cd CEMECCHA
msbuild CEMECCHA.sln -p:Configuration=Release -p:Platform=x64
```

Output DLL: `build/Release/CEMECCHA.dll`

## Running

Inject `CEMECCHA.dll` into `PenguinHotel-Win64-Shipping.exe`. A console
opens on inject with the log; wait until you see
`CEMECCHA fully initialized — press INSERT for menu, F1 to paint`, then:

| Key | Action |
| --- | --- |
| `INSERT` | Toggle the menu |
| `F1` | Trigger Auto Paint |
| `END` | Unload the DLL |

## Reference

- Dumper-7 for UE 5.6 property/class layouts

## Credits

- ImGui — Omar Cornut
- MinHook — Tsuda Kageyu

## License
MIT — do what you want, don't blame me.
