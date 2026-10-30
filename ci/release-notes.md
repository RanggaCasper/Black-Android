# Black for Android {{VERSION}}

> **Test Build Release**  
> Rilis ini ditujukan khusus untuk memverifikasi alur otomatisasi *build* dan sistem rilis CI/CD. Secara fungsionalitas dan fitur, versi ini sepenuhnya identik dengan basis **v1.7.0** dari proyek upstream. Tidak ada fitur baru yang ditambahkan pada rilis ini.

---

### Informasi Penting
* **Kebutuhan Berkas Game:** Aplikasi ini **tidak menyertakan** berkas data game apa pun. Anda wajib menyediakan berkas game *Black* (versi Xbox Original USA/Europe) yang Anda miliki secara sah.
* **Isolasi Instalasi:** Memakai *Application ID* terpisah sehingga dapat diinstal berdampingan tanpa mengganggu atau menimpa data/pengaturan dari versi upstream.

---

## Unduh

| Berkas | Ukuran | SHA-256 |
| :--- | :--- | :--- |
| [`{{APK}}`](https://github.com/UnknuwProjects/Black-Android/releases/download/{{TAG}}/{{APK}}) | {{SIZE}} | `{{SHA256}}` |

---

## Petunjuk Instalasi

1. Unduh dan pasang berkas `{{APK}}` pada perangkat Anda.
2. Buka aplikasi dan berikan izin **Akses Semua File** (*All Files Access*) saat diminta.
3. Salin berkas game ke jalur `/storage/emulated/0/Black` atau folder `Black` di Kartu SD Anda. Anda dapat menggunakan format ISO (`.iso`) atau folder data beserta `default.xbe`.
4. Game akan langsung mendeteksi berkas dan siap dimainkan.

> **Opsi Jalur Alternatif:** Jika Anda tidak ingin memberikan izin akses semua file, tempatkan data game di direktori:  
> `Android/data/com.black.unofficial/files/assets/`

---

## Ringkasan Fitur (Basis v1.7.0)

* **Grafis & Performa:** Dukungan render Vulkan 1.1, pengaturan resolusi 20–200%, unlocked FPS, FOV kustom, V-Sync, serta penghitung FPS.
* **Kontrol Sentuh & Gyro:** Tata letak tombol yang dapat diubah ukurannya, sensitivitas kustom, dan pembidikan berbasis Gyroscope.
* **Dukungan Pengontrol:** Mendukung gamepad via SDL3 (dengan rumble), keyboard, serta *captured mouse*.
* **Lainnya:** Dukungan Bahasa Indonesia, opsi lewati intro, serta menu *cheats* bawaan.

---

## Persyaratan Sistem

* **OS:** Android 10 (API 29) atau yang lebih baru (arm64-v8a)
* **GPU:** Mendukung Vulkan 1.1 (Adreno 6xx+, Mali-G7x+, atau Xclipse)
* **Memori:** Minimal RAM 4 GB & ruang penyimpanan kosong 2 GB

---

## Kredit & Lisensi

Proyek ini dikembangkan berdasarkan *porting* Android asli oleh [RanggaCasper](https://github.com/RanggaCasper/Black-Android).  
Bekerja menggunakan [XboxXBE-Recomp](https://github.com/TGP482/XboxXBE-Recomp) oleh TGP482, serta pustaka pendukung [SDL](https://github.com/libsdl-org/SDL), [glslang](https://github.com/KhronosGroup/glslang), [toml++](https://github.com/marzer/tomlplusplus), dan [Dear ImGui](https://github.com/ocornut/imgui).
