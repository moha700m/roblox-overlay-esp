# rbx-esp — External Overlay ESP for Roblox

Transparent, click-through Windows overlay showing **boxes, names, distance and health bars** over other players in Roblox — built completely **externally** (reads process memory, draws a separate window; no DLL injection, no executor).

> ⚠️ **Educational purpose only.** Third-party software violates the Roblox ToS and can get your account banned. Use on throwaway accounts at your own risk.

## ✨ Features
- Color-coded ESP boxes per player
- Player name above the box
- Live distance in studs
- Gradient-red health bar on the side
- Fully external (handles `RobloxPlayerBeta.exe`, MS-Store variants too)
- Auto-follows the Roblox window position/size
- Every volatile offset lives in `rbx.ini` next to the EXE — patchable without recompiling

## 📦 Download prebuilt EXE
1. Go to **Actions → Build Windows EXE → latest run → Artifacts → `rbx-esp-win64`**, **or**
2. Open **Releases → nightly** and grab `rbx-esp-win64.zip`
3. Extract anywhere, right-click `rbx-esp.exe` → *Run as administrator*

## 🔧 Building locally
```
cmake -B build -A x64
cmake --build build --config Release
```
Requires Visual Studio 2022 with “Desktop development with C++”.

## ⚙️ Configuration (`rbx.ini`)
All memory offsets & render toggles live in `rbx.ini`. Offsets below match a recent Roblox build (Nov 2026, will drift over time):

```ini
offset.children       = 0x60
offset.name           = 0x18
offset.classname      = 0x28
offset.primitive      = 0xD8
offset.prim_rot       = 0x120
offset.prim_pos       = 0x150
offset.players_local  = 0x130
offset.humanoid_hp    = 0x18C
offset.humanoid_maxhp = 0x190
show.names            = 1
show.distance         = 1
show.health           = 1
```

### Updating offsets after a Roblox update
Re-open IDA/Ghidra on the new `RobloxPlayerBeta.exe`, locate `Instance::GetChildren`, `RBX::Instance::name`, `Primitive::position/rotation`, and update the matching hex values above. Restart `rbx-esp.exe` — no rebuild needed.

## ❗ Troubleshooting
| Symptom | Fix |
|---|---|
| “Roblox process not found” | Launch Roblox first, then run `rbx-esp.exe` |
| Nothing drawn but no errors | Check `offset.*` values against the current Roblox build |
| Black/flickering overlay | Disable HDR, ensure hardware-accelerated GPU scheduling is on/off tested |
