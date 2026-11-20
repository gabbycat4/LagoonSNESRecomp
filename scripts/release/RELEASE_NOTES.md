**Unofficial eye-comfort fork** of [rages4calm/LagoonSNESRecomp](https://github.com/rages4calm/LagoonSNESRecomp) 0.5.2: smooth motion at 120/180/240 pictures per second, half-pixel motion, steady camera, no slowdown, screen shake off. Everything is optional, in **Options → Comfort**.

**Bring your own clean Lagoon (USA) ROM** (unheadered, 1 MiB, CRC32 `d2554270`). No ROM is included; the game asks for it on first start.

### Linux — `Lagoon-EyeComfort-x86_64.AppImage`
1. Download it, then make it executable: right-click → Properties → *Allow executing as program* (or `chmod +x Lagoon-EyeComfort-x86_64.AppImage`).
2. Double-click it and choose your ROM.
- For Linux desktops from 2024 on (Ubuntu 24.04+, Fedora 40+, Arch/CachyOS and similar). If it does not start, install `fuse2` / `libfuse2`.
- Settings and saves: `~/.local/share/LagoonEyeComfort`.

### Windows 10/11 — `Lagoon-EyeComfort-win-x64.zip`
1. Extract the whole folder somewhere writable (not Program Files).
2. Run `LagoonSNESRecomp.exe` (SmartScreen: *More info → Run anyway*) and choose your ROM.
- Built on Linux and lightly tested on Windows; reports welcome in Issues.

### Starting settings
The game starts with the eye-comfort setup on: 16:9 widescreen, borderless full screen, **120 pictures per second**, half pixels, steady camera, no slowdown and no screen shake. With a 180 or 240 Hz monitor, raise **Pictures per second** in Options → Comfort and restart the game. **Alt+Enter** switches between full screen and a window. See the README for every setting.

Credits: the original port is by rages4calm and contributors (CREDITS.md), built with SNESRecomp. License: PolyForm Noncommercial 1.0.0. `SHA256SUMS.txt` lists the file checksums.
