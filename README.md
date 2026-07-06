# ProjectChronos

External CS2 cheat with overlay-based ESP, aimbot, grenade helper, and exploit framework. Built with C++20, DX11 overlay, and ImGui.

> **Educational project only.** For learning about game internals, memory reading, overlay rendering, and Windows API techniques.

## Features

### ESP (Wallhacks)
- Player boxes (corner/full), health/armor bars, skeleton, name, weapon
- Weapon icons (`[AK]`, `[AWP]`, etc.) with clip/reserve ammo
- Glow (3-pass multi-layer), chams (through-wall silhouettes), head dot
- Snaplines, aimline, out-of-view indicators
- Bomb timer with progress bar
- Spectator list (view-angle-based detection)
- Radar overlay with rotation, teammate toggle, and **bomb marker**
- Money, defuse kit, flags, velocity display
- **Dropped Weapons ESP** — weapon icons + distance at dead player positions
- **Sound ESP** — off-screen directional indicators with pulsing rings

### Aimbot
- Mouse-event-based external aim (no CUserCmd writes)
- Smooth aim with configurable speed and curve
- Recoil Control System (RCS) with absolute punch compensation
- Backtrack (lag compensation via tick records)
- Per-weapon profiles with **full editor UI** (FOV, smooth, hitchance, fire rate, spread)
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
- **BunnyHop** — auto jump when space held, on-ground check, configurable hitchance
- **Anti-Aim** — pitch (up/down/random), yaw (left/right/back/random), edge AA
- **Anti-Flash** — yellow screen tint + "FLASHED" overlay when flashed
- **Fake Duck** — rapid crouch toggle
- **Quick Stop** — releases movement keys when shooting
- **Quick Switch** — press Q after shot
- **Knife Bot** — auto-stab within range (400ms debounce)
- **Auto Defuse** — press USE near planted bomb
- **Fake Latency** (visual flag)
- **Clan Tag** (animated styles: static, scroll, fade, rainbow)

### Exploit Layer
- Input history rewrite for silent aim
- Subtick rewind / packet manipulation
- Spread cancel, lag comp hijack
- Packet engine with WinDivert integration

### Config System
- Save on DELETE key, load on startup
- 100+ settings synced bidirectionally
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

## Menu Tabs

| Tab | Features |
|-----|----------|
| **ESP** | Boxes, name, health, weapon, skeleton, glow, chams, snaplines, radar, bomb timer, spectators, dropped weapons, sound ESP |
| **Glow** | Glow style, color, chams color, head dot, velocity, scope overlay |
| **Radar** | Radar style, scale, rotation, team toggle |
| **Aim** | Enable, aim key, smooth, RCS, backtrack, fire mode, weapon profiles editor |
| **Nade** | Nade helper, auto-trick, throw key, spot radius, aim speed |
| **Misc** | BunnyHop, anti-aim, fake duck, quick stop/switch, knife bot, auto defuse, clan tag |

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
- **Glow**: 3-pass multi-layer rendering (outer glow → outline → inner core)
- **Config**: Flat file save/load with ~100+ settings

## Disclaimer

This project is for **educational purposes only**. Using cheats in online games violates Terms of Service and may result in account bans. The author is not responsible for any misuse.
