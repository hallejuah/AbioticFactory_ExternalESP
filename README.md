# Abiotic External ESP — C++ architecture (v0.1)

**Windows x64 · C++20 · Win32/GDI · CMake · external, read-only.**

A complete, extendable C++ source project porting the validated foundations of Python `abiotic_esp_loot_v35.py`. No DLL injection, `WriteProcessMemory`, remote threads, or hooks. This is an **initial native port**: code is not yet compiled/tested on Windows in this environment, so use Python v0.35 as the fallback until you validate the C++ binary.

## Build

Requires **Visual Studio 2022** with **Desktop development with C++**, Windows SDK, and CMake (Visual Studio installs CMake optionally).

```bat
cd /d "C:\Users\Dword666\Documents\Abiotic\AbioticExternalCPP"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build\Release\AbioticESP.exe
```

For developer builds use `--config Debug`. You can quit with End, rebuild and relaunch without restarting the game.

## Architecture

```text
AbioticESP.exe
 ├─ Process [process.hpp/.cpp]
 │   ├─ find one exact game process + main module
 │   ├─ hash game EXE with Windows BCrypt SHA256
 │   └─ OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION)
 ├─ UE5 [ue5.hpp/.cpp]
 │   ├─ FNamePool index → class name
 │   ├─ UWorld → GameInstance → CameraCache
 │   ├─ UObject, TArray, RootComponent, FVector3d
 │   ├─ World-to-Screen (matching Python v0.18)
 │   └─ life: +0x8F8 HP, +0x8FC armor, +0xA48 death
 ├─ Scanner [scanner.hpp/.cpp]
 │   ├─ Enumerate actor pointers and classify
 │   ├─ check Level arrays before/after
 │   └─ atomically commit snapshot only on stable scan
 ├─ Settings [settings.hpp/.cpp]
 │   └─ persistent, extensible INI
 └─ Overlay [overlay.hpp/.cpp]
     ├─ independent click-through transparent Win32/GDI overlay
     ├─ render labels and HP bars
     ├─ F3..F10, End
     └─ compact 4-tab sidebar (Objects / Visual / Distance / System)
```

**Extension points**:
- `UE5::...` is for validated field readers.
- `categorize()` for new actor classes.
- `Scanner::refresh()` for new snapshot streams (dropped inventory, containers).
- `Config` + `settings.cpp` for persisted settings.
- `Overlay::updatePanel()` for control panels and future feature tabs.
- `Overlay::drawRow()` for new visual elements.

## First-run verification

1. Run game in borderless/windowed mode. Compile and launch native ESP.
2. Game EXE SHA256 must match `b5fac7c4d5b8a678c58731e4e44bc96f7f598748a92906740b2cfab743b177c1`.
3. Verify world/level/FName pool and camera projection. If nothing appears, check native debugger Output window (`OutputDebugStringA`).
4. Verify Pest/Peccary/Carbuncle HP, robot HP + numeric armor, `F7` death filter.
5. Verify `F10` settings tabs, that labels remain visible with panel focused, and persistence.
6. Compare against Python v0.35 in the same scene. **Do not run both overlays at once during comparison** to avoid confusion.

## Hotkeys

| Key | Action |
|---|---|
| F3 | resource nodes |
| F4 | micro loot |
| F5 | dropped items |
| F6 | HP text / bars |
| F7 | dead filter |
| F8 | overlay visible |
| F9 | manual scan |
| F10 | tabbed settings |
| End | quit |

## Data integrity and future milestones

- Dropped items are currently `Dropped Item (?)`; contents unknown.
- `Armor` is a numeric value from actor `+0x8FC`; do **not** divide it by a guessed maximum. Per-robot `MaxArmor` **TODO**.
- HP baseline is observational (Pest 25, Peccary 80, Carbuncle 25, Robot 180) — not proven universal across variants. Bars are relative to these known baselines; we should eventually discover per-instance maximum HP.
- Class classification follows Python v0.35, but complex cases (actor ancestry and level streaming races) need native integration tests. This C++ port deliberately avoids claiming parity before a real Windows test.
- Full snapshots refresh every 12 seconds, on F9, and on world/level identity changes. Actor *count-only* change debounce optimization from Python is not yet ported. It can be added as a scanner scheduler later.
- Do not remove exact SHA256 and FName anchor checks merely to make unsupported game builds work.
- Dynamic game values must be read, not inferred from in-game progression.

## Roadmap

1. Test native compile, camera projection, loot filtering, focus, overlay, settings and streaming transitions.
2. Implement actor-count debounce, profiling and robust streaming recovery (v0.35 parity).
3. Investigate read-only `Dropped Item` payload: item ID, stack and condition.
4. Identify per-instance maximum HP and **MaxArmor**; add adaptive percentage bars only when validated.
5. Add minimap/radar via an independent `RadarRenderer`, optionally switching Win32 GDI to Direct2D or ImGui/DX11 behind a drawing interface.
6. Separate scanner worker + double-buffer snapshots if stutters appear (never dereference pointers without re-validation).

## v0.2 MSVC 2026 compilation fixes

- Fixed the duplicate `sy` declaration in `src/ue5.cpp` by naming the projected coordinates `screenX`, `screenY`.
- Made the rectangle width explicitly `int` before `std::max` / `std::clamp` in `src/overlay.cpp`.
- Use `cmake -S . -B build -G "Visual Studio 18 2026" -A x64` and `cmake --build build --config Release --parallel 8`.
- Windows compilation and in-game runtime have not been verified in this environment.


## v0.3 MSVC 2026 fix
Fixed WinAPI LONG/int template deduction at overlay.cpp lines ~218/230 by converting width values to int before std::clamp/std::max. This build has not been compiled on Windows in this environment.
