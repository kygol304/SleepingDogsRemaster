# Sleeping Dogs Remaster

A mod for **Sleeping Dogs: Definitive Edition** (Steam) that modernizes the game: wider camera,
GTA VI-style HUD, a GTA V / RDR2-style weapon wheel, an armory in the safehouse wardrobe, and new
weapons with their own 3D models.


## Features

- Wider, more readable camera (on foot, melee, vehicles), settings in `SleepingDogsRemaster.ini`
- GTA VI-style HUD: rounded rectangular minimap, health bar shown in combat
- Weapon wheel with slow motion: 3 weapon slots + bare hands, weapons drawn with the game's own animation
- Wardrobe **WEAPONS** entry to pick the weapon of each slot (firearms, melee, DLC weapons, finger gun)
- New weapons with 3D models: **AK-47** and **RPG-7 rocket launcher** (straight, fast rocket with a bigger explosion)
- Controller and keyboard/mouse: hold **D-pad left** / **C** for the wheel (right stick or mouse to pick),
  **1-4** for direct access; **RB** / **R** stay the game's reload / pick up / swap
- Picking up a weapon fills a free wheel slot (the weapon in hand is kept); it only swaps when all slots are full.
  The game's pick-up prompt shows the mod weapons' names and icons
- Focus bar (blue, under the yellow combat meter): **D-pad right** / **X** slows time down; it fills up like the combat meter (`[Focus]` in the ini)
- Night-time look: brighter, more saturated neon glow (`[Graphics]` in the ini)
- French / English texts, following the game language

## Controls

| Action | Controller | Keyboard / mouse |
|---|---|---|
| Open the weapon wheel | hold **D-pad left** | hold **C** |
| Pick in the wheel | right stick | move the mouse |
| Confirm | release | release |
| Reload | tap **RB** | tap **R** |
| Pick up / swap a weapon | hold **RB** | hold **R** |
| Focus (slow motion) on / off | **D-pad right** | **X** |
| Armory (in a safehouse) | **A** in the wheel | left click in the wheel |
| Direct weapon access | - | **1** bare hands, **2** belt, **3** / **4** long guns |
| Change the bare-hands icon | **Y** in the wheel | - |

## Settings (`SleepingDogsRemaster.ini`)

Everything can be tuned in `SleepingDogsRemaster.ini`, in the game folder (created with default values
on the first launch, and kept when you update the mod). Open it with Notepad, change a value, save.
`[Camera]`, `[Combat]`, `[Vehicle]` and `[Graphics]` are applied while you play; the other sections at
the next game launch. A missing line simply uses its default value.

Scales are multipliers of the original game value: `1.0` = original, `1.2` = 20 % more, `0.5` = half.

### `[Camera]` - on foot

| Key | Default | Effect |
|---|---|---|
| `Enabled` | `1` | `1` = mod camera, `0` = original camera |
| `FovScale` | `1.2` | Field of view (wider view above 1) |
| `DistanceScale` | `1.0` | Distance behind the character |
| `Height` | `0` | Height offset in meters (negative = lower) |
| `Shoulder` | `0` | Over-the-shoulder offset in meters (negative = left shoulder) |
| `Smoothing` | `1.0` | Camera smoothness (higher = softer movements) |
| `CombatToo` | `1` | Also apply the camera in combat (`1` / `0`) |

### `[Combat]` - melee camera (targeted walk, focus, grapple)

| Key | Default | Effect |
|---|---|---|
| `FovScale` | `1.10` | Field of view |
| `DistanceScale` | `0.75` | Distance (below 1 = closer to the fight) |
| `Height` | `-0.40` | Height offset in meters |

### `[Vehicle]`

| Key | Default | Effect |
|---|---|---|
| `Enabled` | `1` | `1` = mod vehicle camera, `0` = original |
| `FovScale` | `1.15` | Field of view in vehicles |
| `DistanceScale` | `1.08` | Distance behind the vehicle |
| `Height` | `-0.15` | Height offset in meters |

### `[Minimap]`

| Key | Default | Effect |
|---|---|---|
| `Rounded` | `1` | `1` = GTA-style rounded rectangle, `0` = original round map |
| `OffsetX` / `OffsetY` | `-40` / `40` | Map position offset in pixels (negative = left / up) |
| `MinZoom` | `1.05` | Minimum zoom (higher = closer; avoids empty corners when driving fast) |

### `[Wheel]` - weapon wheel

| Key | Default | Effect |
|---|---|---|
| `Key` | `C` | Keyboard key that opens the wheel (a letter or a digit) |
| `SlowMo` | `0.25` | Game speed while the wheel is open (`0.25` = 4x slower, `1` = no slow motion) |
| `FistIcon` | `4` | Bare-hands icon (`0` to `13`, also cycled with **Y** in the wheel) |

### `[Focus]` - focus bar (slow motion)

The blue bar under the yellow combat meter. It fills up when the yellow meter fills up (hits, combos,
takedowns). Press **D-pad right** / **X** to slow time down; press again to stop.

| Key | Default | Effect |
|---|---|---|
| `Key` | `X` | Keyboard key for the focus (a letter or a digit) |
| `SlowMo` | `0.35` | Game speed during the focus (`0.35` = about 3x slower) |
| `Duration` | `15` | Seconds (real time) for a full bar to empty |
| `Gain` | `1.0` | Fill speed (`2.0` = fills twice as fast as the yellow meter) |
| `Start` | `1.0` | Bar level when the game starts (`1` = full, `0` = empty) |

The focus can be started from 10 % of the bar.

### `[Loadout]`

| Key | Default | Effect |
|---|---|---|
| `KeepBetweenSessions` | `0` | `1` = keep the wheel weapons from one game session to the next, `0` = empty wheel at launch |

### `[Rocket]` - RPG-7 rocket launcher

| Key | Default | Effect |
|---|---|---|
| `Speed` | `3.0` | Rocket speed (x the grenade launcher) |
| `Gravity` | `0.05` | Rocket drop (`0` = perfectly straight, `1` = falls like a grenade) |
| `Damage` | `2.0` | Damage multiplier |
| `Radius` | `1.6` | Explosion radius multiplier |
| `Clip` | `1` | Rockets per clip |

### `[Graphics]` - lights and colors

| Key | Default | Effect |
|---|---|---|
| `Enabled` | `1` | `1` = mod look, `0` = original rendering |
| `BloomBoost` | `1.15` | Glow intensity of lights during the day (`1` = original) |
| `NightBloomBoost` | `1.6` | Glow intensity of lights (neons) at night |
| `BloomThreshold` | `0.9` | Daytime glow threshold (lower = more lights glow) |
| `NightBloomThreshold` | `0.7` | Night-time glow threshold |
| `BloomSaturation` | `1.15` | Color saturation of the glow |
| `SkySaturation` | `1.1` | Sky color saturation |
| `SkyBoost` | `1.0` | Sky brightness |
| `Exposure` | `0.0` | Global brightness in EV (`0` = original, `0.3` = brighter, `-0.3` = darker) |

The night values are used between 8 pm and 5 am in game, with a smooth transition at dusk and dawn.

### `[Armory]` and `[Safehouses]`

The armory (choosing the weapon of each slot) is available in the wardrobe, and from the wheel in a
safehouse.

| Key | Default | Effect |
|---|---|---|
| `[Armory] SafehouseOnly` | `1` | `1` = the wheel armory only works in a safehouse, `0` = everywhere |
| `[Armory] Radius` | `25` | Safehouse radius in meters |
| `[Armory] Belt` / `Back` / `Hand` | .50 pistol / assault rifle / pump shotgun | Weapons given by the **F9** kit (game object names) |
| `[Safehouses] Count`, `P1`, `P2`... | 1 safehouse | Extra safehouse positions; **F10** adds your current position |

## What the files do

| File | Purpose |
|---|---|
| `dinput8.dll` | The mod. Loaded by the game from its folder; forwards `DirectInput8Create` to the real `System32\dinput8.dll`, then patches functions of `sdhdship.exe` **in memory, inside the game process only**. No network access; it only reads/writes `SleepingDogsRemaster.ini` / `.log` and reads its own `SleepingDogsRemaster\` folder (models, icons). |
| `SleepingDogsRemaster-Installer.exe` | Copies the files to the game folder, backs up `UI.bix` and the size of `UI.big`, then appends the modified HUD / wardrobe screens to `UI.big` and updates their `UI.bix` entries. Choice 2 restores the original files and removes the mod. |

## Repository layout

| Folder | Content |
|---|---|
| `sd_remaster/` | The mod (`dinput8.dll`): `src/dllmain.cpp`, `src/newweapons.cpp` (runtime 3D models, textures, icons, rocket launcher), `src/graphics.cpp` (bloom / sky tuning) |
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
