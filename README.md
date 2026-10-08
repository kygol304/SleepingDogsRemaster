# Sleeping Dogs Remaster

A mod for **Sleeping Dogs: Definitive Edition** (Steam) that modernizes the game: wider camera,
GTA VI-style HUD, a GTA V / RDR2-style weapon wheel, an armory in the safehouse wardrobe, and new
weapons with their own 3D models.

Download and install: [Nexus Mods](https://www.nexusmods.com/sleepingdogsdefinitiveedition/mods/177).

## Features

- Wider, more readable camera (on foot, melee, vehicles), settings in `SleepingDogsRemaster.ini`
- GTA VI-style HUD: rounded rectangular minimap, health bar shown in combat
- Weapon wheel with slow motion: 3 weapon slots + bare hands, weapons drawn with the game's own animation
- Wardrobe **WEAPONS** entry to pick the weapon of each slot (firearms, melee, DLC weapons, finger gun)
- New weapons with 3D models: **AK-47** and **RPG-7 rocket launcher** (straight, fast rocket with a bigger explosion)
- Controller and keyboard/mouse (hold **R** for the wheel, mouse to pick, **1-4** for direct access)
- French / English texts, following the game language

## What the files do

| File | Purpose |
|---|---|
| `dinput8.dll` | The mod. Loaded by the game from its folder; forwards `DirectInput8Create` to the real `System32\dinput8.dll`, then patches functions of `sdhdship.exe` **in memory, inside the game process only**. No network access; it only reads/writes `SleepingDogsRemaster.ini` / `.log` and reads its own `SleepingDogsRemaster\` folder (models, icons). |
| `SleepingDogsRemaster-Installer.exe` | Copies the files to the game folder, backs up `UI.bix` and the size of `UI.big`, then appends the modified HUD / wardrobe screens to `UI.big` and updates their `UI.bix` entries. Choice 2 restores the original files and removes the mod. |

## Repository layout

| Folder | Content |
|---|---|
| `sd_remaster/` | The mod (`dinput8.dll`): `src/dllmain.cpp`, `src/newweapons.cpp` (runtime 3D models, textures, icons, rocket launcher) |
| `installer/` | The installer (`installer.cpp`) |
| `ui/` | ActionScript added to the game's HUD and wardrobe Flash screens |
| `tools_src/` | Build tools: UI screens (`build_hud.ps1`, `ui_patch.py`, `ui_wrap.py`), `weapons3d/` (Blender scripts converting the glTF models and rendering the icons) |
| `ghidra_scripts/` | Ghidra scripts used to study the game executable |

## Building

Requirements: Visual Studio 2022 Build Tools (MSVC 14.44, "Desktop development with C++") and CMake 3.15+.

The mod uses the data structures (not the function addresses) of the community
[SDmodding SDK](https://github.com/SDmodding/SDK), which must be cloned into `external/SD-SDK`:

```
git clone https://github.com/SDmodding/SDK external/SD-SDK
git -C external/SD-SDK checkout 38e0e375e22f2701e6f41316596f43e87a392e19
```

Then:

```
cmake -S sd_remaster -B build-mod -G "Visual Studio 17 2022" -A x64
cmake --build build-mod --config Release
cmake -S installer -B build-installer -G "Visual Studio 17 2022" -A x64
cmake --build build-installer --config Release
```

Output: `build-mod/Release/dinput8.dll` and `build-installer/Release/SleepingDogsRemaster-Installer.exe`.

The HUD / wardrobe screens (`Hud.bin`, `Wardrobe.bin`) are the game's own Flash screens with the
scripts from `ui/` added, rebuilt by `tools_src/build_hud.ps1` (needs JPEXS Free Flash Decompiler and
the screens extracted from the game). The weapon models (`.skm`) are produced by
`tools_src/weapons3d/import_models.py` (Blender) from the Sketchfab models credited below.

## Credits

- 3D models (CC-BY 4.0, https://creativecommons.org/licenses/by/4.0/), converted for Sleeping Dogs
  (scale, orientation, baked textures):
  - "AK 47" by MKoegler3D - https://sketchfab.com/3d-models/ak-47-565521a0e7424c259788e9795b2c93f5
  - "RPG_7" by Ranger_08 (AnilkumarG) - https://sketchfab.com/3d-models/rpg-7-625b550f52a04068b943837558b5a582
- Game structures: [SDmodding SDK](https://github.com/SDmodding/SDK); the game archives were studied with the SDmodding tools [BigFileSystem](https://github.com/SDmodding/BigFileSystem) and [PermToFBX](https://github.com/SDmodding/PermToFBX) (not included)
