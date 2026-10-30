<p align="center">
  <img src="https://raw.githubusercontent.com/UnknuwProjects/Black-Android/main/app/src/main/res/mipmap-xxxhdpi/ic_launcher.png" width="96" alt="Black Android Logo">
</p>

<p align="center">
  <code><b>[ B L A C K : A N D R O I D ]</b></code><br>
  <code>REL-{{VERSION}} // STABLE BUILD</code>
</p>

<p align="center">
  An unofficial native Android port of the classic Xbox title <b>Black</b>,<br>
  recompiled via <a href="https://github.com/TGP482/XboxXBE-Recomp">XboxXBE-Recomp</a> and rendered with Vulkan.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Android-10%2B-3DDC84?style=flat-square&logo=android&logoColor=white" alt="Android 10+">
  <img src="https://img.shields.io/badge/Architecture-arm64--v8a-blue?style=flat-square" alt="arm64-v8a">
  <img src="https://img.shields.io/badge/Graphics-Vulkan_1.1-AC162C?style=flat-square&logo=vulkan&logoColor=white" alt="Vulkan 1.1">
  <img src="https://img.shields.io/badge/Core-SDL_3-1D4F91?style=flat-square" alt="SDL3">
</p>

---

> [!IMPORTANT]
> **No Game Assets Included**  
> This release contains **no copyrighted game assets**. You must provide your own legally obtained copy of *Black* (Xbox Original USA or Europe release).

---

### ⚡ What's New in {{VERSION}}
* **Expanded Silenced Weapon Variants:** Added dedicated cheat options for suppressed weapon variants in the weapon swap menu—including **Pistol (Silenced)**, **Glock (Silenced)**, **MP5 (Suppressed)**, and **Walther 2000 (Silenced)**—allowing seamless switching to stealth variants with fully functional first-person banks and world models.
* **Refined Update Center & Startup Control:** Relocated update options into the general settings category with a new persistent **Startup Update Check** (On/Off) toggle managed via SharedPreferences and JNI, alongside manual update triggers.
* **Refined White ESP:** Updated ESP features with clean **white color** and **thinner line widths** for sharp visual clarity without cluttering the game screen.
* **Forced Public SaveData:** Save files, configuration settings, and logs are now redirected directly to `/storage/emulated/0/Black/SaveData/` complete with real-time **toast notifications** confirming storage operations.

---

## 📦 Download

| File | Size | SHA-256 |
| :--- | :--- | :--- |
| [`{{APK}}`](https://github.com/UnknuwProjects/Black-Android/releases/download/{{TAG}}/{{APK}}) | {{SIZE}} | `{{SHA256}}` |

---

## 🚀 Installation & Setup

1. Install **`{{APK}}`** on your device.
2. Launch the app and grant **All Files Access** when prompted.
3. Place your game files (either disc image `.iso` or `default.xbe` with its data folders) inside:
   * `/storage/emulated/0/Black/` **OR**
   * A `Black` folder located at the root of your SD Card.
4. The game will automatically detect and boot the files.

> [!TIP]
> **Fallback Path:** If broad storage permissions are bypassed, data will gracefully fallback to `Android/data/com.black.unofficial/files/assets/`.

---

## ✨ Features (Base v1.7.0)

| Category | Specifications & Options |
| :--- | :--- |
| 🖥️ **Graphics** | Render resolution 20–200%, FOV 45–140°, viewmodel FOV, FMV scale, unlocked framerate, V-Sync, FPS counter, anisotropic filtering, draw distance scaling, toggleable reload blur |
| 👆 **Touch Controls** | Fully customizable layout editor, touch sensitivity adjustment, and gyroscope aiming (device sensors, DualShock 4, or DualSense) |
| 🎮 **Input Support** | Native SDL3 controller integration with rumble feedback, physical keyboard, and captured mouse mapping |
| ⚙️ **General** | Multi-language support (including Indonesian), intro skipping, audio output routing, general settings update hub, startup update check control, and background auto-pause |
| 💀 **Cheats** | Infinite ammo/grenades, rapid fire, zero recoil, god mode, one-hit kills, game speed multipliers, and native silenced weapon variants (Pistol, Glock, MP5, Walther 2000) |

*Note: In-game settings are handled via the game's native options menu. Use Android's back button to trigger the pause menu.*

---

## 📱 System Requirements

- **Operating System:** Android 10 or newer (64-bit arm64-v8a)
- **Graphics GPU:** Vulkan 1.1 compatible (Adreno 6xx+, Mali-G7x+, Xclipse)
- **Memory & Storage:** 4 GB RAM minimum \| 2 GB free internal storage space

---

## 🙏 Credits & Acknowledgments

Port maintained by **UnknuwProjects**, based on the original Android adaptation by [RanggaCasper](https://github.com/RanggaCasper/Black-Android). Built on [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) by **TGP482**, with [SDL](https://github.com/libsdl-org/SDL), [glslang](https://github.com/KhronosGroup/glslang), [toml++](https://github.com/marzer/tomlplusplus), and [Dear ImGui](https://github.com/ocornut/imgui).

---
