# Build Hoenn Forge Android APK (Azahar core + Hoenn onboarding)
# Prerequisites: Android SDK, NDK 30+, JDK 17+, git clone of azahar under emulator/azahar
$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot -Parent
$Azahar = Join-Path $Root "emulator\azahar"
$Android = Join-Path $Azahar "src\android"
$Overlay = Join-Path $Root "overlay\azahar"

if (-not (Test-Path $Android)) {
    Write-Host "Cloning Azahar (shallow)..."
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "emulator") | Out-Null
    git clone --depth 1 https://github.com/azahar-emu/azahar.git $Azahar
    Push-Location $Azahar
    git submodule update --init --depth 1
    git -C externals/dynarmic submodule update --init --depth 1
    git -C externals/sirit/sirit submodule update --init --depth 1
    git -C externals/cubeb submodule update --init --depth 1
    git -C externals/libadrenotools submodule update --init --depth 1
    Pop-Location
}

Write-Host "Applying overlay..."
$Main = Join-Path $Android "app\src\main"
Copy-Item (Join-Path $Overlay "java\org\citra\citra_emu\hoennforge\*") `
    (Join-Path $Main "java\org\citra\citra_emu\hoennforge\") -Force -Recurse
New-Item -ItemType Directory -Force -Path (Join-Path $Main "java\org\citra\citra_emu\hoennforge") | Out-Null
Copy-Item (Join-Path $Overlay "java\org\citra\citra_emu\hoennforge\*") `
    (Join-Path $Main "java\org\citra\citra_emu\hoennforge\") -Force
Copy-Item (Join-Path $Overlay "res\layout\*") (Join-Path $Main "res\layout\") -Force
Copy-Item (Join-Path $Overlay "AndroidManifest.xml") (Join-Path $Main "AndroidManifest.xml") -Force
Copy-Item (Join-Path $Overlay "jni\android_common\android_common.h") `
    (Join-Path $Main "jni\android_common\android_common.h") -Force
Copy-Item (Join-Path $Overlay "app-build.gradle.kts") (Join-Path $Android "app\build.gradle.kts") -Force

# Ensure app_name + hoenn strings present (idempotent-ish: only if missing)
$strings = Join-Path $Main "res\values\strings.xml"
$s = Get-Content $strings -Raw
if ($s -notmatch 'hoenn_welcome_title') {
    $snippet = Get-Content (Join-Path $Overlay "res\values\hoenn_strings_snippet.xml") -Raw
    $s = $s -replace '(<string name="app_name"[^>]*>)[^<]*(</string>)', '${1}Hoenn Forge${2}'
    $s = $s -replace '(<string name="app_name"[^>]*>Hoenn Forge</string>)', "`$1`n    $snippet"
    # If app_name replace failed pattern, force simple replace of Azahar name
    $s = $s -replace '>Azahar</string>', '>Hoenn Forge</string>', 1
    Set-Content $strings $s -NoNewline
}

$env:JAVA_HOME = if (Test-Path "C:\Program Files\Android\Android Studio\jbr") {
    "C:\Program Files\Android\Android Studio\jbr"
} else { $env:JAVA_HOME }
$env:ANDROID_HOME = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { "$env:LOCALAPPDATA\Android\Sdk" }
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME

Push-Location $Android
.\gradlew.bat :app:assembleVanillaRelWithDebInfo -x ktlintCheck
Pop-Location

$apk = Get-ChildItem (Join-Path $Android "app\build\outputs\apk") -Recurse -Filter *.apk |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
$dist = Join-Path $Root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$out = Join-Path $dist "HoennForge-vanilla-relWithDebInfo.apk"
Copy-Item $apk.FullName $out -Force
Write-Host "APK ready: $out ($([math]::Round((Get-Item $out).Length/1MB,1)) MB)"
