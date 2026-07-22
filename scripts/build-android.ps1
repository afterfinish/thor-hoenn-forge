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

# Hoenn fix: LoadState multiplayer null-check
$ssSrc = Join-Path $Overlay "core\savestate.cpp"
$ssDst = Join-Path $Azahar "src\core\savestate.cpp"
if ((Test-Path $ssSrc) -and (Test-Path $ssDst)) {
    Copy-Item $ssSrc $ssDst -Force
    Write-Host "Applied savestate.cpp LoadState null-check"
}
$Main = Join-Path $Android "app\src\main"
$JavaDst = Join-Path $Main "java"
$JavaSrc = Join-Path $Overlay "java"
if (Test-Path $JavaSrc) {
    Get-ChildItem $JavaSrc -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($JavaSrc.Length).TrimStart('\','/')
        $target = Join-Path $JavaDst $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        Copy-Item $_.FullName $target -Force
    }
}
if (Test-Path (Join-Path $Overlay "res\layout")) {
    Copy-Item (Join-Path $Overlay "res\layout\*") (Join-Path $Main "res\layout\") -Force
}
if (Test-Path (Join-Path $Overlay "AndroidManifest.xml")) {
    Copy-Item (Join-Path $Overlay "AndroidManifest.xml") (Join-Path $Main "AndroidManifest.xml") -Force
}
if (Test-Path (Join-Path $Overlay "jni\android_common\android_common.h")) {
    Copy-Item (Join-Path $Overlay "jni\android_common\android_common.h") (Join-Path $Main "jni\android_common\android_common.h") -Force
}
if (Test-Path (Join-Path $Overlay "app-build.gradle.kts")) {
    Copy-Item (Join-Path $Overlay "app-build.gradle.kts") (Join-Path $Android "app\build.gradle.kts") -Force
}

# Always re-inject Hoenn Forge strings (line-filter old hoenn_* then append snippet)
$strings = Join-Path $Main "res\values\strings.xml"
$snippetPath = Join-Path $Overlay "res\values\hoenn_strings_snippet.xml"
if ((Test-Path $strings) -and (Test-Path $snippetPath)) {
    $lines = Get-Content $strings -Encoding UTF8 | Where-Object {
        $_ -notmatch 'name="hoenn_' -and
        $_ -notmatch 'Hoenn Forge UI strings' -and
        $_ -notmatch '<!-- Prepare -->' -and
        $_ -notmatch '<!-- Play mode -->' -and
        $_ -notmatch '<!-- Randomizer -->'
    }
    $text = ($lines -join "`n")
    if ($text -match '>Azahar</string>') {
        $text = $text.Replace('>Azahar</string>', '>Hoenn Forge</string>')
    }
    $snippet = (Get-Content $snippetPath -Raw -Encoding UTF8).TrimEnd()
    if ($text -notmatch 'hoenn_welcome_title') {
        $text = $text -replace '</resources>', ($snippet + "`n`n</resources>")
    }
    [System.IO.File]::WriteAllText($strings, $text + "`n", [System.Text.UTF8Encoding]::new($false))
    Write-Host "Injected Hoenn Forge strings"
}

$env:JAVA_HOME = if (Test-Path "C:\Program Files\Android\Android Studio\jbr") {
    "C:\Program Files\Android\Android Studio\jbr"
} else { $env:JAVA_HOME }
$env:ANDROID_HOME = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { "$env:LOCALAPPDATA\Android\Sdk" }
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME

Push-Location $Android
.\gradlew.bat :app:assembleVanillaRelWithDebInfo -x ktlintCheck
$gradleExit = $LASTEXITCODE
Pop-Location
if ($gradleExit -ne 0) {
    Write-Error "Gradle build failed with exit code $gradleExit"
    exit $gradleExit
}

$apk = Get-ChildItem (Join-Path $Android "app\build\outputs\apk") -Recurse -Filter *.apk |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $apk) {
    Write-Error "No APK produced"
    exit 1
}
$dist = Join-Path $Root "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$out = Join-Path $dist "HoennForge-vanilla-relWithDebInfo.apk"
Copy-Item $apk.FullName $out -Force
Write-Host "APK ready: $out ($([math]::Round((Get-Item $out).Length/1MB,1)) MB)"