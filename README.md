# ProjectChronos

External CS2 cheat with overlay-based ESP, aimbot, grenade helper, and exploit framework. Built with C++20, DX11 overlay, and ImGui.

> **Educational project only.** For learning about game internals, memory reading, overlay rendering, and Windows API techniques.

## Features

### ESP (Wallhacks)
- Player boxes (corner/full), health/armor bars, skeleton, name, weapon
- Weapon icons (`[AK]`, `[AWP]`, etc.) with clip/reserve ammo
- Glow, chams (through-wall player silhouettes), head dot
- Snaplines, aimline, out-of-view indicators
- Bomb timer with progress bar
- Spectator list (view-angle-based detection)
- Radar overlay with rotation and teammate toggle
- Money, defuse kit, flags display

### Aimbot
- Mouse-event-based external aim (no CUserCmd writes)
- Smooth aim with configurable speed and curve
- Recoil Control System (RCS) with absolute punch compensation
- Backtrack (lag compensation via tick records)
- Per-weapon profiles (rifle, pistol, sniper, SMG, shotgun)
- Auto-fire modes: hold-to-fire, burst with fire rate limiting
- Configurable aim key (Always On, RMB, LMB, ALT, SHIFT, etc.)
- Deadzone (0.5°) to prevent jitter
- Hitchance calculation per target

### Grenade Helper
- Pre-made spots for Mirage, Dust2, Inferno, Nuke, Overpass, Anubis, Ancient
- Smooth auto-aim to throw angle
- Throw sequences: Stand, Jump, Crouch, Walk, Run
- Visual indicators: diamond markers, info panel, trajectory preview
- Configurable keybinds (Show Spots / Throw / Auto-Trick)

### Auto-Tricks (Movement)
- Pre-defined movement sequences per map
- Short→Window (Mirage), Tunnels→Xbox (Dust2), etc.
- Configurable trigger key and radius

### Misc Features
- **Anti-Aim**: Pitch/Yaw manipulation, Edge AA
- **Fake Duck**: Rapid crouch toggle
- **Quick Stop**: Releases movement keys when shooting
- **Quick Switch**: Press Q after shot
- **Knife Bot**: Auto-stab within range (400ms debounce)
- **Auto Defuse**: Press USE near planted bomb
- **Fake Latency** (visual flag)
- **Clan Tag** (animated)

### Exploit Layer
- Input history rewrite for silent aim
- Subtick rewind / packet manipulation
- Spread cancel, lag comp hijack
- Packet engine with WinDivert integration

### Config System
- Save on DELETE key, load on startup
- 80+ settings synced bidirectionally
- Per-feature keybinds configurable in menu

## Build

### Requirements
- Visual Studio 2022 (MSVC v143, x64)
- Windows SDK 10.0+
- WinDivert (included in `external/WinDivert/`)

### Build
```cmd
cd ProjectChronos
build.bat
```

Output: `build/chronos.exe`

### Run
1. Start CS2
2. Run `chronos.exe` as Administrator
3. INSERT — toggle menu
4. DELETE — save config
5. END — exit

## Project Structure

```
ProjectChronos/
├── src/
│   ├── aimbot/          # AimController, Resolver, Autowall, QuantumAim
│   ├── core/            # Memory reader, State engine, Offsets, Config
│   ├── exploits/        # Executor, Input history, Packet engine
│   ├── nade/            # Grenade helper, throw sequences, tricks
│   ├── overlay/         # DX11 overlay, ImGui menu, ESP rendering
│   ├── safety/          # VAC shield, HWID spoofer, failure response
│   └── utils/           # Logging, math
├── external/
│   └── WinDivert/       # Packet capture library
├── build.bat            # Build script
└── CMakeLists.txt
```

## Technical Details

- **Overlay**: Transparent DX11 window with `WS_EX_TRANSPARENT` for click-through
- **Memory**: External process memory read via `ReadProcessMemory` (driver-assisted for writes)
- **Aim**: `mouse_event(MOUSEEVENTF_MOVE)` — standard external approach
- **ESP**: ImGui `AddRect`/`AddLine` on background draw list
- **Offsets**: Auto-downloaded from cs2-dumper, with fallback defaults
- **Backtrack**: Stores 64 tick records per player, returns best lag-compensated position

## Disclaimer

This project is for **educational purposes only**. Using cheats in online games violates Terms of Service and may result in account bans. The author is not responsible for any misuse.
