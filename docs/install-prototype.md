# Install the Milestone A prototype APK

## What you get

**Hoenn Forge** Android APK with:

- Legal onboarding + OR/AS dump picker (NCSD title check)
- **Play** boots the game via embedded **Azahar** core
- Thor defaults applied: **3× resolution**, **Vulkan**, **New 3DS mode**
- **Advanced emulator UI** button opens full Azahar settings if needed

## Built artifact (this PC)

```text
C:\hoenn-forge\dist\HoennForge-vanilla-relWithDebInfo.apk
```

(~53 MB, arm64-v8a + x86_64, package `dev.tzigdon.hoennforge`)

## Install on phone

1. Enable **Install unknown apps** for your file manager / browser.
2. Copy the APK to the phone (USB, Drive, etc.).
3. Open the APK and install.
4. First launch: accept legal → pick your **.3ds / .cci** dump → **Play**.
5. First Azahar run may also ask for a **user data folder** (normal for Azahar). Pick a folder on internal storage.

## Rebuild

```powershell
cd C:\hoenn-forge
.\scripts\build-android.ps1
```

Requires Android SDK + NDK, JDK 17+, and a prior (or auto) clone of Azahar under `emulator\azahar`.

## Not in this prototype yet

- Randomizer (Milestone B)
- Free camera (Milestone C)
- Single-screen toggle polish (Milestone D)
