<p align="center">
  <img src="docs/media/logo.png" width="88" alt="">
</p>

<h1 align="center">Pearl</h1>

<p align="center">
  A Spotify player for Windows 10 and 11, written from scratch in C++ with its own Direct3D renderer.<br>
  One executable under 1&nbsp;MB. No runtime, no installer.
</p>

<p align="center">
  <a href="https://github.com/nn-nymph/pearl/releases/latest"><b>Download</b></a>
  &nbsp;·&nbsp;
  <a href="#connect-spotify">Connect Spotify</a>
  &nbsp;·&nbsp;
  <a href="#building-from-source">Build from source</a>
</p>

<p align="center">
  <img src="docs/media/hero.png" alt="Pearl's home page with a mix built around Arctic Monkeys, Franz Ferdinand and Kaiser Chiefs, and 505 playing in the player bar">
</p>

Pearl controls Spotify through Spotify Connect, so the audio still comes from the Spotify app, your phone or a speaker. Pearl is the part you look at. It builds a home page from what you actually listen to, shows synced lyrics, gives you a player bar you can park anywhere, and has a mini player that sits on the edge of your screen.

## In motion

<p align="center">
  <img src="docs/media/demo-play.webp" width="100%" alt="Pressing play on Your Mix: the player switches from Take Me Out to 505 and the background shifts color, then the page scrolls through On repeat and Artists for you">
</p>
<p align="center"><sub>Press play on Your Mix. The player swaps tracks and the background takes its color from the cover.</sub></p>

<p align="center">
  <img src="docs/media/demo-dock.webp" width="100%" alt="Dragging the player bar to the top-right corner, where it shrinks and snaps into place, then flinging it back to the bottom">
</p>
<p align="center"><sub>Drag the player bar and let go. It springs to the nearest of five spots and shrinks to a compact pill away from the bottom edge.</sub></p>

## What it does

- **A home page built from your listening.** Pearl reads your top artists, top tracks and recent plays, then lays out a 30-song mix, albums and artists close to what you play, an On repeat shelf, and new releases from artists you play. The page is cached, so it shows up instantly and refreshes in the background.
- **Spotify Connect remote.** Play, pause, skip, seek, volume, shuffle and repeat. If no Spotify device is running, Pearl starts the desktop app minimized and hands playback to it.
- **Now Playing with synced lyrics.** A full-window view with the cover, controls and lyrics from [LRCLIB](https://lrclib.net). The current line stays in focus as the song plays, and clicking any line jumps there.
- **Search** across Spotify for songs, albums and artists. Album pages and artist pages come with top tracks, discography and a short bio from Wikipedia. Paste any Spotify link with <kbd>Ctrl</kbd>+<kbd>V</kbd> to open it.
- **Liked Songs and playlists.** Heart anything, drag a song onto a playlist in the sidebar (or onto **+** to start a new one), or right-click for Play next, Add to playlist and Go to album. These are Pearl's own and never modify your Spotify library.
- **Mini player.** Press <kbd>Insert</kbd> from anywhere to fold Pearl into a floating pill. Throw it toward an edge and it snaps to the nearest of eight spots. Hover it for a seek bar and a button that brings the full window back.
- **Behaves like a Windows app.** Per-monitor DPI, rounded corners and the acrylic backdrop on Windows 11, media keys, a single instance, and it remembers its window, volume and where you left off. If the graphics driver resets or updates while Pearl is open, it rebuilds its renderer and keeps going.

<table>
  <tr>
    <td width="50%"><img src="docs/media/liked-songs.png" alt="Liked Songs"></td>
    <td width="50%"><img src="docs/media/playlist.png" alt="A playlist with a four-cover mosaic"></td>
  </tr>
  <tr>
    <td align="center"><sub>Liked Songs</sub></td>
    <td align="center"><sub>Playlists get a mosaic of their first four covers</sub></td>
  </tr>
  <tr>
    <td width="50%"><img src="docs/media/context-menu.png" alt="Right-click menu on a song"></td>
    <td width="50%"><img src="docs/media/mini-player.png" alt="The mini player at rest and on hover"></td>
  </tr>
  <tr>
    <td align="center"><sub>Right-click any song</sub></td>
    <td align="center"><sub>Mini player, at rest and on hover</sub></td>
  </tr>
</table>

## Getting started

### Requirements

Pearl runs on pretty much any PC with Windows 10 or 11.

- **Windows 10 or 11, any version.** Everything from the first Windows 10 release (1507) to the latest Windows 11, including Home, Pro, Education, Enterprise, LTSC and the N editions.
- **Any 64-bit processor.** Intel and AMD use `Pearl.exe`. Snapdragon and other Windows on ARM PCs get a native `Pearl-arm64.exe`.
- **Any graphics.** Intel, AMD, NVIDIA and Qualcomm graphics all work through Direct3D 11. Without a usable GPU driver, as in virtual machines, Remote Desktop sessions or very old DirectX 10 cards, Pearl draws in software instead.
- **Spotify Premium.** Spotify only allows playback control through its Web API for Premium accounts.

Windows 11 adds rounded corners, and 22H2 or later adds the translucent acrylic backdrop. Windows 10 gets a solid background. Windows in S mode only runs apps from the Microsoft Store, so it has to be switched out of S mode first.

### Install

Download `Pearl.exe` from [Releases](https://github.com/nn-nymph/pearl/releases/latest) (or `Pearl-arm64.exe` on an ARM PC) and run it. Nothing gets installed and there's no runtime to set up. Settings and caches live in `%APPDATA%\Pearl`, so uninstalling means deleting the exe and that folder.

The exe isn't code-signed, so SmartScreen may ask you to confirm the first time you run it.

### If something looks wrong

- **Glitches, flicker or a blank window.** Some older or buggy graphics drivers draw incorrectly. Start Pearl with `--software`, for example by adding it to the end of a shortcut's **Target**, and it renders on the CPU instead.
- **Small screens.** The window never asks for more room than the screen has, so Pearl fits on 1366×768 laptops even at 125% scaling.

### Connect Spotify

Pearl doesn't ship with a Spotify client ID. You register your own, which is free and takes about a minute.

1. Open the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard) and create an app.
2. Add `http://127.0.0.1:43821/callback` as a redirect URI and tick **Web API**.
3. Copy the app's **Client ID**.
4. In Pearl, click the link button at the bottom of the sidebar, press **Paste ID**, then **Connect** and approve Pearl in your browser.

<p align="center">
  <img src="docs/media/spotify-setup.png" width="720" alt="Pearl's Spotify panel walking through the three setup steps">
</p>

A few things worth knowing:

- New Spotify apps run in development mode. Your own account works right away; anyone else needs to be added under **User Management** in the dashboard.
- Sign-in uses OAuth with PKCE, so there is no client secret.
- The Client ID and refresh token are stored in `%APPDATA%\Pearl\spotify.dat`, encrypted with Windows DPAPI for your user account. They never leave your machine except to talk to Spotify.

### Keyboard shortcuts

| Keys | Action |
| --- | --- |
| <kbd>Space</kbd> | Play / pause |
| <kbd>←</kbd> <kbd>→</kbd> | Seek 5 seconds |
| <kbd>Ctrl</kbd>+<kbd>←</kbd> <kbd>Ctrl</kbd>+<kbd>→</kbd> | Previous / next track |
| <kbd>↑</kbd> <kbd>↓</kbd> | Volume |
| <kbd>M</kbd> | Mute |
| <kbd>L</kbd> | Like the current song |
| <kbd>Ctrl</kbd>+<kbd>K</kbd>, <kbd>Ctrl</kbd>+<kbd>F</kbd> or <kbd>/</kbd> | Search |
| <kbd>Ctrl</kbd>+<kbd>V</kbd> | Open a Spotify link from the clipboard |
| <kbd>Ctrl</kbd>+<kbd>N</kbd> | New playlist |
| <kbd>Esc</kbd> | Close a menu, panel or detail page; leave Now Playing |
| <kbd>Insert</kbd> | Fold Pearl into the mini player; press again to show or hide the pill. Works system-wide. |
| Media keys | Play / pause, next, previous |

### What Pearl connects to

- `accounts.spotify.com` and `api.spotify.com` for sign-in, playback and everything else Spotify
- Spotify's image CDN for cover art
- `lrclib.net` for lyrics
- `en.wikipedia.org` for artist bios

That's the whole list. There are no analytics, no telemetry and no update checks. The listening history Pearl uses for recommendations stays in `%APPDATA%\Pearl`.

## Building from source

You need Visual Studio 2022 (any edition, or the standalone Build Tools) with the **Desktop development with C++** workload and the **C++ Clang tools for Windows** component, since the project builds with the ClangCL toolset. You also need a Windows 10 or 11 SDK, which provides `fxc.exe` for the shaders.

```powershell
git clone https://github.com/nn-nymph/pearl.git
cd pearl
.\tools\build.ps1                                     # build\release\Pearl.exe
.\tools\build.ps1 -configuration Debug                # build\debug\Pearl.exe
.\tools\build.ps1 -platform ARM64                     # build\release-arm64\Pearl.exe
.\tools\build.ps1 -rebuild
```

`build.ps1` finds Visual Studio through `vswhere`, so the edition and install location don't matter. MSBuild only offers the ARM64 target once the **MSVC ARM64/ARM64EC build tools** component is installed, even though Pearl links nothing from it. Shaders are compiled in a pre-build step (`tools\shaders.ps1`) and embedded in the exe as resources.

Before sending a pull request, run `.\tools\style.ps1`. It checks the house style: tabs, spaces inside parentheses and template brackets, LF line endings, and no comments.

### How it's put together

Pearl doesn't link the C runtime. It has its own entry point, allocator, containers, JSON parser and WinHTTP wrapper, and it builds with exceptions and RTTI turned off.

The UI is immediate-mode. Each frame the app walks its state and records draw lists, and the renderer batches those into Direct3D 11 draws presented through DirectComposition. Text goes through DirectWrite in Segoe UI Variable, and icons come from Segoe Fluent Icons. The glass panels sample a blurred copy of the frame with a slightly different refraction offset per color channel, which is where the soft prismatic edge comes from.

Animation is spring-based throughout. The renderer only draws while something is moving and schedules frames against the display's vblank. When nothing is happening it falls back to a slow background drift, and when the window is hidden it stops drawing entirely.

One exe covers every Windows 10 and 11 build because everything it imports already exists in the first Windows 10 release. Newer APIs like per-monitor DPI v2 are looked up at runtime and fall back gracefully when they're missing. Media Foundation is loaded on demand, so the N editions start fine without it. The code is compiled for baseline x64 with no AVX, and the few rounding functions the compiler needs live in `src/runtime`. The renderer asks for a Direct3D 11 GPU and falls back to WARP, Windows' software rasterizer, if any part of the hardware path fails. While running, it watches for a lost device and rebuilds itself when the driver comes back.

| Path | What's there |
| --- | --- |
| `src/runtime` | Entry point, allocator and the handful of C functions the compiler expects (no CRT) |
| `src/base` | Containers, text, math, JSON, file I/O |
| `src/render` | D3D11 device, draw lists, text layout |
| `src/shaders` | UI and glass shader, blur passes, procedural covers for the demo library |
| `src/ui` | App state, pages, widgets, player bar, mini player |
| `src/spotify` | Sign-in, Connect playback, search, the home page's taste engine |
| `src/lyrics` | LRCLIB client and LRC parser |
| `src/audio` | WASAPI output, Media Foundation decoder, synthesizer |
| `src/library` | Local library scan, tags, cover art, demo library |
| `src/net` | HTTP over WinHTTP |
| `src/debug.cc` | Headless capture and benchmark harness (Debug builds only) |

### Headless capture

Debug builds include a scripted renderer that runs without a window. Every image in this README came out of it.

```powershell
.\build\debug\Pearl.exe --capture shot --size 1240x800 --scale 2 --script "page albums; wait 1; shot albums"
```

That writes `shot_albums.png`. Scripts can click, drag, scroll, type and navigate, `bench 600` writes the average frame time over 600 frames, and `rebuild` throws away the renderer and builds a new one the way a driver reset would. Add `--software` to render through WARP. Without `--folder <path>`, the harness loads a built-in demo library of procedurally painted albums with synthesized songs.

## Status

- **Local files.** The folder scanner, tag reader, Media Foundation decoding and WASAPI output are all in the tree, but there's no folder picker in the UI yet. Debug builds accept `--folder <path>`.
- **Likes and playlists** live in Pearl and don't sync to your Spotify library.
- **Windows only.** Pearl targets Windows 10 and 11 on x64 and ARM64. There are no macOS or Linux builds.

## Acknowledgements

Lyrics come from [LRCLIB](https://lrclib.net) and artist bios from the [Wikipedia REST API](https://en.wikipedia.org/api/rest_v1/). Pearl is an independent project and isn't affiliated with or endorsed by Spotify.
