# Building Hoenn Forge

## Prerequisites

- Android Studio (or SDK 35 + build-tools)
- JDK 17+ (Android Studio JBR is fine)
- Windows example JDK:

```text
C:\Program Files\Android\Android Studio\jbr
```

## Project path

```text
C:\hoenn-forge
```

## Build debug APK

```powershell
cd C:\hoenn-forge
$env:JAVA_HOME = "C:\Program Files\Android\Android Studio\jbr"
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
.\gradlew.bat assembleDebug
```

APK output:

```text
app\build\outputs\apk\debug\app-debug.apk
```

## Unit tests

```powershell
.\gradlew.bat test
```

## Dump for local PC testing (not in APK)

```text
C:\hoenn-forge\local\dumps\*.3ds
```

On device/emulator, pick that file via the in-app SAF picker after sideloading the APK.

## Milestone A status

| Piece | Status |
|-------|--------|
| Onboarding + legal | Implemented |
| Dump pick + OR/AS NCSD validation | Implemented |
| Thor profile JSON | `profiles/thor.json` |
| Embedded Azahar core boot | **Next** |
