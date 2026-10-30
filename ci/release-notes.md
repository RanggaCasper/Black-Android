<p align="center">
  <img src="https://raw.githubusercontent.com/UnknuwProjects/Black-Android/main/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png" width="96" alt="Black">
</p>

<h1 align="center">Black for Android {{VERSION}}</h1>

<p align="center">
  Test build of an unofficial native Android port of the original Xbox version of <b>Black</b>.<br>
  Based on the work of <a href="https://github.com/RanggaCasper/Black-Android">RanggaCasper</a>, statically recompiled with
  <a href="https://github.com/TGP482/XboxXBE-Recomp">XboxXBE-Recomp</a> and rendered with Vulkan.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Android-10%2B-3DDC84?logo=android&logoColor=white" alt="Android 10+">
  <img src="https://img.shields.io/badge/arch-arm64--v8a-blue" alt="arm64-v8a">
  <img src="https://img.shields.io/badge/Vulkan-1.1-AC162C?logo=vulkan&logoColor=white" alt="Vulkan 1.1">
  <img src="https://img.shields.io/badge/SDL-3-1D4F91" alt="SDL3">
</p>

> [!NOTE]
> This is a test build. It is functionally the same as the upstream project and adds no new features. Its purpose is to verify the build and release process.

> [!IMPORTANT]
> This release does not include any game files. You need the files from your own legally acquired copy of *Black* (USA or Europe release).

## Download

| File | Size | SHA-256 |
| --- | --- | --- |
| [`{{APK}}`](https://github.com/UnknuwProjects/Black-Android/releases/download/{{TAG}}/{{APK}}) | {{SIZE}} | `{{SHA256}}` |

## Installation

1. Install `{{APK}}` on your phone.
2. Open the app and allow **all files access** when prompted.
3. Copy the game to `/storage/emulated/0/Black`, or to a `Black` folder on your SD card. Use either the disc image (`.iso`) or `default.xbe` together with its data folders.
4. The game starts automatically once it finds the files.

> [!TIP]
> If you prefer not to grant all files access, place the game in `Android/data/com.black.unofficial/files/assets` instead. Settings, saves, logs and the shader cache are stored in `Android/data/com.black.unofficial/files`.

## Features

| Category | Details |
| --- | --- |
| Graphics | Render resolution 20-200%, FOV 45-140°, viewmodel FOV, FMV scale, unlocked frame rate, V-Sync, FPS counter, anisotropic filtering, 2x / 4x / 8x draw distance, optional reload blur |
| Touch controls | Presets and a layout editor to move and resize each control, touch sensitivity, gyroscope aiming using the phone or a DualShock 4 / DualSense |
| Other input | SDL3 controller support with rumble, keyboard, and captured mouse with rebindable controls |
| General | Language selection (including Indonesian), skip intros, audio output, update check, pause in background |
| Cheats | Infinite ammo and grenades, rapid fire, no recoil, god mode, one-hit kills, game speed |

Settings are available in the game's options menu, organized into one page per group. The Android back button opens the pause menu.

## Requirements

- Android 10 or newer, arm64
- GPU with a Vulkan 1.1 driver (Adreno 6xx or newer, Mali-G7x or newer, Xclipse)
- 4 GB RAM and 2 GB of free storage

## Notes

- This build uses a different application ID from the upstream release, so it installs separately and does not share settings or saves with it.
- Please report problems through the [issue tracker](https://github.com/UnknuwProjects/Black-Android/issues) and include your device model and Android version.

## Credits

Original Android port by [RanggaCasper](https://github.com/RanggaCasper/Black-Android). Built on [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by TGP482, with [SDL](https://github.com/libsdl-org/SDL), [glslang](https://github.com/KhronosGroup/glslang), [toml++](https://github.com/marzer/tomlplusplus) and [Dear ImGui](https://github.com/ocornut/imgui).

---
