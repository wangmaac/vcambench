# VCamBench

English | [한국어](README.ko.md)

**Create as many fake cameras on Windows 11 as you need.**

Software that touches cameras takes a different path when there are zero devices,
one device, or several. The awkward part is that testing those paths means
actually producing those situations: unplugging a webcam, finding a laptop that
has none, or scraping together a machine with three.

This produces them in software. Start the app and cameras appear; close it and
they are gone.

![Test pattern](media/vcam-output.gif)

---

## Requirements

- **Windows 11** (build 22000 or newer) — `MFCreateVirtualCamera` exists only there
- Administrator rights to install — the media source is registered as a COM
  server under `HKLM`

Windows 10 will not work, and the installer refuses to run on it.

## Install

**Microsoft Store** *(pending — link added once it is listed)*
Installs and updates handled for you.

**Download** — the latest `setup.exe` from [Releases](../../releases)

> **Not signed yet** — SmartScreen will warn. Choose `More info` → `Run anyway`.
> A free code signing certificate has been applied for.

**Build it yourself** — the source is MIT. Follow [Build](#build) below; it is
the same code the Store version is built from.

## Usage

1. Run the app and press **Add** as many times as you like
2. The cameras show up in the Camera app, Chrome, Zoom, Teams, and anything else
3. **Closing the window removes them** — they exist only while the process does

The window speaks English and Korean. It follows Windows' own language on first
run; **Language** in the menu bar overrides that, and the choice is remembered.

Each camera draws its own name, a frame counter, elapsed time and a sweeping
dial. A glance tells you whether you are looking at a live stream or a frozen
frame.

### Diagnostics, installed alongside

```powershell
# What Windows actually enumerates - separately for MF, DirectShow and WinRT
camlist.exe --all

# Pull frames the way an ordinary app does and save them as BMP
camcapture.exe --name "VCamBench 1" --frames 5 --out .\frames

# Drive the cameras from a console, for scripts
vcamctl.exe --count 3 --seconds 60
```

---

## How it works

Every camera access on Windows 11 goes through the **Windows Camera Frame
Server**. This app does not push frames anywhere. It registers a COM class that
knows *how* to make them. When a consuming app opens the camera, the Frame
Server loads that class into its own process and pulls frames from it.

```
app  "make me a camera with this CLSID"  ──▶  Frame Server (svchost.exe)
      (not in the video path at all)            │ loads our DLL
                                                ▼ renders a frame every 33ms
                                      Camera app / Chrome / Zoom / Teams …
```

This is also **why it cannot ship as MSIX**. MSIX registers per user, and the
Frame Server runs as LOCAL SERVICE, which never sees that registration. The
experiment and the evidence are in
[`docs/msix-limitation.md`](docs/msix-limitation.md).

## Verified behaviour (Windows 11 26200)

| | |
|---|---|
| Appears in device lists | Media Foundation, DirectShow and WinRT — all three |
| Video delivery | 1280×720 NV12 30fps, no conversion |
| Consumers checked | Windows Camera app, Chrome, WebRTC-based apps |
| Several at once | 1 real webcam + 3 virtual = 4 devices |
| When the host exits | Removed from the list, nothing left behind, kill included |

---

## Build

- Visual Studio 2022 or newer (Desktop development with C++)
- Windows SDK 10.0.22621 or newer
- Inno Setup 6, to build the installer

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
.\build\Release\vcamcore_tests.exe

& "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe" installer\vcambench.iss
```

While iterating on the DLL, the Frame Server caches the image it loaded first.
The scripts under `tools\` clear that state; each one explains why in its own
comments.

## Code signing policy

Free code signing provided by [SignPath.io](https://about.signpath.io/),
certificate by [SignPath Foundation](https://signpath.org/).

Release installers are built by GitHub Actions
([`build.yml`](.github/workflows/build.yml)) and only those build outputs are
signed. Nothing built on a maintainer's machine is.

**Roles**

| | |
|---|---|
| Committers and reviewers | [wangmaac](https://github.com/wangmaac) |
| Approvers | [wangmaac](https://github.com/wangmaac) |

**Privacy policy**

This program will not transfer any information to other networked systems unless
specifically requested by the user or the person installing or operating it.

There is no network traffic and no account. The only thing written down is a
local diagnostic log at `%ProgramData%\VCamBench\vcamsource.log`, which never
leaves the machine.

> **Signing approval is pending.** Releases are unsigned for now and will
> trigger SmartScreen. Signed releases follow once it is granted.

## License

MIT — [`LICENSE`](LICENSE)

## Sponsor

This is MIT licensed and anyone can build it and use it for nothing. What the
Microsoft Store listing sells is convenience: a signed installer, no build
toolchain, automatic updates.

If you build it yourself and it saved you some time,
[GitHub Sponsors](https://github.com/sponsors/wangmaac) is appreciated.

[![Sponsor](https://img.shields.io/badge/Sponsor-wangmaac-ea4aaa?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/wangmaac)
