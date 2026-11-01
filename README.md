# Vora

Lightweight cross-platform video player **and** image viewer.
C++17 · Qt 6 Widgets · libmpv · OpenSubtitles.com · exiv2 (optional).

- PotPlayer-style hotkeys, folder playlist, resume position, hardware decoding
- BSPlayer-style **automatic subtitle download** (OpenSubtitles hash match, then title match)
- Modern image viewer: zoom-to-cursor, fit modes, animated GIF/WebP, EXIF panel, histogram,
  filmstrip + folder grid, slideshow with fade, crop/rotate/flip/resize, lossless JPEG rotation,
  rename / recycle-bin delete / copy / set wallpaper

## Build

### Windows (MSYS2 UCRT64)

```bash
pacman -Syu
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,pkgconf,qt6-base,qt6-svg,qt6-imageformats,mpv,exiv2}
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/vora.exe
```

Portable folder (copies Qt plugins + all DLLs): `./deploy/windows-deploy.sh` → `dist/Vora/`.

### macOS

```bash
brew install qt mpv exiv2 pkg-config cmake ninja
cmake -S . -B build -G Ninja && cmake --build build
macdeployqt build/vora.app
```

### Linux (Debian/Ubuntu)

```bash
sudo apt install qt6-base-dev qt6-svg-dev libqt6opengl6-dev libmpv-dev libexiv2-dev pkg-config cmake ninja-build
cmake -S . -B build -G Ninja && cmake --build build
```

exiv2 is optional: without it EXIF panel / RAW preview / lossless rotation are disabled.
AVIF/HEIC/JXL need the KDE `kimageformats` Qt plugin installed.

## Subtitles

Works out of the box, like BSPlayer — no account or key:

1. Local first: subtitles next to the video and in `Subs/`, `Subtitles/` (incl. `Subs/<episode>/`) are
   loaded; the best one in your languages (Settings → Subtitles, e.g. `en,mn`) is selected.
   Ad files (`Downloaded from YTS.MX`), forced-only and SDH variants are ranked down.
2. If none match, the BSPlayer subtitle service is queried by file hash (all 8 servers in
   parallel) and the best result is saved as `Movie.en.srt` next to the video.
3. OpenSubtitles.com is used too when a key is available: either the user's own (Settings), or one
   baked into the build: `cmake -B build -DVORA_OS_KEY=<key>` (free at opensubtitles.com/consumers).

Manual search: Ctrl+J. Re-fetch best match: Ctrl+D.

## YouTube, Twitch, Kick

- **Ctrl+U** (or right-click ▸ Open stream): paste a YouTube / Twitch / Kick / any yt-dlp-supported link;
  pick quality (best, 1080p, 720p, 480p, audio only). Live streams show a LIVE badge.
- **Ctrl+Y** (or the YouTube button on the start screen): search YouTube without an account.
  Each result has **Play**, **Video** (MP4, H.264 + AAC, up to the chosen quality) and **Audio** (MP3).
  Downloads run in the background (two at a time) with progress in the Downloads panel;
  the save folder is changeable there (default: your Downloads folder). Esc / Back returns to the player.
- While watching an online video: right-click ▸ Download ▸ Video (MP4) / Audio (MP3).
- **Quality while watching**: the badge in the control bar (e.g. `1080p60`) or right-click ▸ Quality lists the
  qualities this video really has (plus Auto and Audio only); switching reloads at the same position.
- **Playlists** (Ctrl+L, or the Playlists tab): "Watch later" plus your own lists, saved in
  `%APPDATA%\Vora\Vora\playlists.json`. Add with **Save** on any result, right-click ▸ Save to playlist
  while watching, or "Add link or YouTube playlist…" (imports a whole YouTube playlist). Drag to reorder.
  **Play all** / **Shuffle** play in order; PgUp/PgDn move through the list; right-click ▸ Playlist jumps.
  With **Loop** on, the whole playlist repeats; otherwise the end screen offers "Play list again".
- Command line: `vora --youtube "search words"`, `vora --download <url> [video|mp3|m4a]`,
  `vora --playlists`, `vora --playlist "name"`.
- Uses [yt-dlp](https://github.com/yt-dlp/yt-dlp): found next to the exe, in the app data folder, or on PATH
  (PATH copies older than 90 days are ignored). If missing, Vora offers to download it and updates its
  own copy weekly. YouTube needs a JavaScript runtime for yt-dlp: Deno, or Node.js / Bun if installed.
  `ffmpeg.exe` (bundled in the portable build) merges video+audio and converts to MP3.
- Download only what you have the right to keep (your own uploads, Creative Commons, etc.).

## Troubleshooting

- `VORA_MPV_LOG=<file>` writes mpv's log (decoder choice, subtitle search status `[subs] …`).
- `VORA_HWDEC=<mode>` overrides hardware decoding (`no`, `d3d11va-copy`, `nvdec-copy`…).
- `VORA_MPV_OPTS="opt=value;opt2=value"` passes extra mpv options.
- Hardware decoding uses copy-back; NV12/P010 frames are converted to planar YUV because
  semi-planar frames render with colour-stripe corruption through libmpv's OpenGL path inside
  Qt on NVIDIA (mpv 0.41).

## Hotkeys

| Video | | Image | |
|---|---|---|---|
| Space | play/pause | ← → / PgUp PgDn | prev / next |
| ← → | seek 5 s (Shift 1 s, Ctrl 30 s, Alt 60 s) | Home / End | first / last |
| click | play/pause | 0 / 1 | fit / 100% |
| ↑ ↓ / wheel | volume | wheel, + − | zoom to cursor |
| M | mute | 0 / 1 / 2 / W | fit / 100% / fill / fit width |
| PgUp PgDn | prev / next video | double-click | fit ↔ 100% |
| S / A | cycle subtitle / audio | Ctrl+L / Ctrl+R | rotate |
| V | subtitle on/off | H / V | flip |
| [ ] | subtitle delay | C → Enter | crop |
| Z X C | speed reset / − / + | Ctrl+E | resize |
| , . | frame step | Ctrl+Z / Ctrl+S / Ctrl+Shift+S | undo / save / save as |
| Ctrl+J / Ctrl+D | search subs / auto-fetch | S, F5 | slideshow |
| Ctrl+E | screenshot | T / G / I / Ctrl+H / O | strip / grid / info / histogram / overlay |
| I | media info | Ctrl+C / Ctrl+Shift+C | copy image / path |
| | | F2 / Del | rename / recycle bin |

Common (PotPlayer style): click video = play/pause, double-click = fullscreen, Esc = exit fullscreen / minimize,
Enter/F fullscreen, Ctrl+O open, Ctrl+Shift+O folder, Ctrl+U URL, Ctrl+P settings, right-click menu.
