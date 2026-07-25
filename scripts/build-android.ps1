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
# Hoenn free-look core + frame limiter reset (turbo after savestate)
foreach ($pair in @(
    @("core\hoenn_freecam.cpp", "src\core\hoenn_freecam.cpp"),
    @("core\hoenn_freecam.h", "src\core\hoenn_freecam.h"),
    @("core\cheats.cpp", "src\core\cheats\cheats.cpp"),
    @("core\perf_stats.h", "src\core\perf_stats.h"),
    @("core\perf_stats.cpp", "src\core\perf_stats.cpp")
)) {
    $src = Join-Path $Overlay $pair[0]
    $dst = Join-Path $Azahar $pair[1]
    if (Test-Path $src) {
        New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
        Copy-Item $src $dst -Force
        Write-Host "Applied $($pair[0])"
    }
}
# After LoadState success: reset frame limiter so turbo limit applies immediately
$coreCpp = Join-Path $Azahar "src\core\core.cpp"
if (Test-Path $coreCpp) {
    $cc = Get-Content $coreCpp -Raw
    if ($cc -notmatch "frame_limiter\.Reset\(\)") {
        $cc = $cc -replace `
            '(System::LoadState\(slot\);\s*\r?\n\s*LOG_INFO\(Core, "Load completed"\);\s*\r?\n\s*\} catch \(const std::exception& e\) \{[\s\S]*?return ResultStatus::ErrorSavestate;\s*\r?\n\s*\}\s*\r?\n\s*)frame_limiter\.WaitOnce\(\);', `
            "`$1frame_limiter.Reset();`r`n        frame_limiter.WaitOnce();"
        if ($cc -match "frame_limiter\.Reset\(\)") {
            [System.IO.File]::WriteAllText($coreCpp, $cc)
            Write-Host "Patched core.cpp FrameLimiter::Reset after LoadState"
        } else {
            Write-Host "WARNING: could not patch core.cpp FrameLimiter::Reset"
        }
    } else {
        Write-Host "core.cpp FrameLimiter::Reset already present"
    }
}
# Hook LDR CRO load/unload so freecam suspends in battle and re-applies on field return
$ldrRo = Join-Path $Azahar "src\core\hle\service\ldr_ro\ldr_ro.cpp"
if (Test-Path $ldrRo) {
    $ldr = Get-Content $ldrRo -Raw
    if ($ldr -notmatch "hoenn_freecam\.h") {
        $ldr = $ldr -replace '(#include "core/hle/service/ldr_ro/ldr_ro\.h")', "`$1`r`n#include `"core/hoenn_freecam.h`""
    }
    if ($ldr -notmatch "OnModuleLoaded") {
        $ldr = $ldr -replace `
            '(LOG_INFO\(Service_LDR, "CRO \\"\{\}\\" loaded at 0x\{:08X\}, fixed_end=0x\{:08X\}", cro\.ModuleName\(\),\s*\r?\n\s*cro_address, cro_address \+ fix_size\);)', `
            "`$1`r`n    Hoenn::FreeCam::GetInstance().OnModuleLoaded(cro.ModuleName());"
        $ldr = $ldr -replace `
            '(LOG_INFO\(Service_LDR, "Unloading CRO \\"\{\}\\"", cro\.ModuleName\(\)\);)', `
            "`$1`r`n    Hoenn::FreeCam::GetInstance().OnModuleUnloaded(cro.ModuleName());"
        [System.IO.File]::WriteAllText($ldrRo, $ldr)
        Write-Host "Patched ldr_ro.cpp freecam battle/field hooks"
    } else {
        Write-Host "ldr_ro.cpp freecam hooks already present"
    }
}
# Ensure citra_core CMakeLists lists hoenn_freecam (emulator tree is gitignored)
$coreCmake = Join-Path $Azahar "src\core\CMakeLists.txt"
if (Test-Path $coreCmake) {
    $cm = Get-Content $coreCmake -Raw
    if ($cm -notmatch "hoenn_freecam\.cpp") {
        $cm = $cm -replace "(cheats/gateway_cheat\.h\r?\n)", "`$1    hoenn_freecam.cpp`n    hoenn_freecam.h`n"
        [System.IO.File]::WriteAllText($coreCmake, $cm)
        Write-Host "Patched core CMakeLists for hoenn_freecam"
    }
}
# NativeLibrary freelook JNI declarations
$nlPath = Join-Path $Android "app\src\main\java\org\citra\citra_emu\NativeLibrary.kt"
if (Test-Path $nlPath) {
    $nl = Get-Content $nlPath -Raw
    if ($nl -notmatch "setHoennFreelook") {
        $nl = $nl -replace "(external fun isRunning\(\): Boolean)", @"
`$1

    /** Hoenn Forge: right-stick free-look (experimental overworld camera). */
    external fun setHoennFreelook(enabled: Boolean)
    external fun isHoennFreelookEnabled(): Boolean

    /** Hoenn Forge: L/R continuous zoom assist. */
    external fun setHoennZoomAssist(enabled: Boolean)
    external fun isHoennZoomAssistEnabled(): Boolean

    /** Hoenn Forge: cam-address RE probe (START menu numbered candidates). */
    external fun hoennScanCamCandidates(): Int
    external fun hoennGetCamCandidateLabels(): Array<String>
    external fun hoennSetCamProbeIndex(index: Int)
    external fun hoennGetCamProbeIndex(): Int
    external fun hoennDumpCamRE(tag: String): String
"@
        [System.IO.File]::WriteAllText($nlPath, $nl)
        Write-Host "Patched NativeLibrary.kt freelook/zoom/camProbe JNI"
    } elseif ($nl -notmatch "setHoennZoomAssist") {
        $nl = $nl -replace "(external fun isHoennFreelookEnabled\(\): Boolean)", @"
`$1

    external fun setHoennZoomAssist(enabled: Boolean)
    external fun isHoennZoomAssistEnabled(): Boolean
"@
        [System.IO.File]::WriteAllText($nlPath, $nl)
        Write-Host "Patched NativeLibrary.kt zoom JNI"
    }
    if ($nl -notmatch "hoennScanCamCandidates") {
        $nl = Get-Content $nlPath -Raw
        $nl = $nl -replace "(external fun isHoennZoomAssistEnabled\(\): Boolean)", @"
`$1

    /** Hoenn Forge: cam-address RE probe (START menu numbered candidates). */
    external fun hoennScanCamCandidates(): Int
    external fun hoennGetCamCandidateLabels(): Array<String>
    external fun hoennSetCamProbeIndex(index: Int)
    external fun hoennGetCamProbeIndex(): Int
"@
        [System.IO.File]::WriteAllText($nlPath, $nl)
        Write-Host "Patched NativeLibrary.kt camProbe JNI"
    }
    if ($nl -notmatch "hoennDumpCamRE") {
        $nl = Get-Content $nlPath -Raw
        $nl = $nl -replace "(external fun hoennGetCamProbeIndex\(\): Int)", @"
`$1
    external fun hoennDumpCamRE(tag: String): String
"@
        [System.IO.File]::WriteAllText($nlPath, $nl)
        Write-Host "Patched NativeLibrary.kt hoennDumpCamRE JNI"
    }
    if ($nl -notmatch "hoennCaptureTopScreen") {
        $nl = Get-Content $nlPath -Raw
        $nl = $nl -replace "(external fun hoennDumpCamRE\(tag: String\): String)", @"
`$1

    /**
     * Hoenn Forge Pokédex: capture top 3DS screen.
     * Returns IntArray: [width, height, ...ARGB pixels], or null on failure.
     * @param resScale 0 = use emulator resolution factor
     */
    external fun hoennCaptureTopScreen(resScale: Int): IntArray?
"@
        [System.IO.File]::WriteAllText($nlPath, $nl)
        Write-Host "Patched NativeLibrary.kt hoennCaptureTopScreen JNI"
    }
}
# native.cpp freelook implementation
$nativeCpp = Join-Path $Android "app\src\main\jni\native.cpp"
if (Test-Path $nativeCpp) {
    $nc = Get-Content $nativeCpp -Raw
    if ($nc -notmatch "hoenn_freecam\.h") {
        $nc = $nc -replace '(#include "core/core\.h")', "`$1`n#include `"core/hoenn_freecam.h`""
    }
    if ($nc -notmatch "setHoennFreelook") {
        $jni = @'

void Java_org_citra_citra_1emu_NativeLibrary_setHoennFreelook([[maybe_unused]] JNIEnv* env,
                                                             [[maybe_unused]] jobject obj,
                                                             jboolean enabled) {
    Hoenn::FreeCam::GetInstance().SetFreelookEnabled(static_cast<bool>(enabled));
}

jboolean Java_org_citra_citra_1emu_NativeLibrary_isHoennFreelookEnabled(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj) {
    return static_cast<jboolean>(Hoenn::FreeCam::GetInstance().IsFreelookEnabled());
}

void Java_org_citra_citra_1emu_NativeLibrary_setHoennZoomAssist([[maybe_unused]] JNIEnv* env,
                                                               [[maybe_unused]] jobject obj,
                                                               jboolean enabled) {
    Hoenn::FreeCam::GetInstance().SetZoomAssistEnabled(static_cast<bool>(enabled));
}

jboolean Java_org_citra_citra_1emu_NativeLibrary_isHoennZoomAssistEnabled(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj) {
    return static_cast<jboolean>(Hoenn::FreeCam::GetInstance().IsZoomAssistEnabled());
}

'@
        $nc = $nc -replace "\} // extern `"C`"", ($jni + "`n} // extern `"C`"")
        [System.IO.File]::WriteAllText($nativeCpp, $nc)
        Write-Host "Patched native.cpp freelook/zoom JNI"
    } elseif ($nc -notmatch "setHoennZoomAssist") {
        $jni = @'

void Java_org_citra_citra_1emu_NativeLibrary_setHoennZoomAssist([[maybe_unused]] JNIEnv* env,
                                                               [[maybe_unused]] jobject obj,
                                                               jboolean enabled) {
    Hoenn::FreeCam::GetInstance().SetZoomAssistEnabled(static_cast<bool>(enabled));
}

jboolean Java_org_citra_citra_1emu_NativeLibrary_isHoennZoomAssistEnabled(
    [[maybe_unused]] JNIEnv* env, [[maybe_unused]] jobject obj) {
    return static_cast<jboolean>(Hoenn::FreeCam::GetInstance().IsZoomAssistEnabled());
}

'@
        $nc = $nc -replace "\} // extern `"C`"", ($jni + "`n} // extern `"C`"")
        [System.IO.File]::WriteAllText($nativeCpp, $nc)
        Write-Host "Patched native.cpp zoom JNI"
    }
    if ($nc -notmatch "hoennScanCamCandidates") {
        $nc = Get-Content $nativeCpp -Raw
        $jni = @'

jint Java_org_citra_citra_1emu_NativeLibrary_hoennScanCamCandidates([[maybe_unused]] JNIEnv* env,
                                                                   [[maybe_unused]] jobject obj) {
    auto& system = Core::System::GetInstance();
    return static_cast<jint>(Hoenn::FreeCam::GetInstance().ScanCamCandidates(system));
}

jobjectArray Java_org_citra_citra_1emu_NativeLibrary_hoennGetCamCandidateLabels(JNIEnv* env,
                                                                               [[maybe_unused]] jobject obj) {
    auto& cam = Hoenn::FreeCam::GetInstance();
    const int n = cam.GetCamCandidateCount();
    jclass str_cls = env->FindClass("java/lang/String");
    jobjectArray arr = env->NewObjectArray(n, str_cls, nullptr);
    for (int i = 0; i < n; ++i) {
        const std::string label = cam.GetCamCandidateLabel(i);
        env->SetObjectArrayElement(arr, i, env->NewStringUTF(label.c_str()));
    }
    return arr;
}

void Java_org_citra_citra_1emu_NativeLibrary_hoennSetCamProbeIndex([[maybe_unused]] JNIEnv* env,
                                                                  [[maybe_unused]] jobject obj,
                                                                  jint index) {
    Hoenn::FreeCam::GetInstance().SetCamProbeIndex(static_cast<int>(index));
}

jint Java_org_citra_citra_1emu_NativeLibrary_hoennGetCamProbeIndex([[maybe_unused]] JNIEnv* env,
                                                                  [[maybe_unused]] jobject obj) {
    return static_cast<jint>(Hoenn::FreeCam::GetInstance().GetCamProbeIndex());
}

jstring Java_org_citra_citra_1emu_NativeLibrary_hoennDumpCamRE(JNIEnv* env,
                                                              [[maybe_unused]] jobject obj,
                                                              jstring tag) {
    const char* t = env->GetStringUTFChars(tag, nullptr);
    auto& system = Core::System::GetInstance();
    const std::string msg = Hoenn::FreeCam::GetInstance().DumpREState(system, t ? t : "jni");
    if (t) {
        env->ReleaseStringUTFChars(tag, t);
    }
    return env->NewStringUTF(msg.c_str());
}

'@
        $nc = $nc -replace "\} // extern `"C`"", ($jni + "`n} // extern `"C`"")
        [System.IO.File]::WriteAllText($nativeCpp, $nc)
        Write-Host "Patched native.cpp camProbe JNI"
    }
    if ($nc -notmatch "hoennDumpCamRE") {
        $nc = Get-Content $nativeCpp -Raw
        $jni = @'

jstring Java_org_citra_citra_1emu_NativeLibrary_hoennDumpCamRE(JNIEnv* env,
                                                              [[maybe_unused]] jobject obj,
                                                              jstring tag) {
    const char* t = env->GetStringUTFChars(tag, nullptr);
    auto& system = Core::System::GetInstance();
    const std::string msg = Hoenn::FreeCam::GetInstance().DumpREState(system, t ? t : "jni");
    if (t) {
        env->ReleaseStringUTFChars(tag, t);
    }
    return env->NewStringUTF(msg.c_str());
}

'@
        $nc = $nc -replace "\} // extern `"C`"", ($jni + "`n} // extern `"C`"")
        [System.IO.File]::WriteAllText($nativeCpp, $nc)
        Write-Host "Patched native.cpp hoennDumpCamRE JNI"
    }
    # L3 turbo: reset frame limiter when temporary limit is set (post-savestate lag)
    if ($nc -notmatch "setTemporaryFrameLimit[\s\S]*frame_limiter\.Reset") {
        $nc = Get-Content $nativeCpp -Raw
        $nc = $nc -replace `
            '(void Java_org_citra_citra_1emu_NativeLibrary_setTemporaryFrameLimit\(JNIEnv\* env, jobject obj,\s*\r?\n\s*jdouble speed\) \{\s*\r?\n\s*Settings::temporary_frame_limit = speed;\s*\r?\n\s*Settings::is_temporary_frame_limit = true;\s*\r?\n)(\})', `
            "`$1    Core::System::GetInstance().frame_limiter.Reset();`r`n`$2"
        $nc = $nc -replace `
            '(void Java_org_citra_citra_1emu_NativeLibrary_disableTemporaryFrameLimit\(JNIEnv\* env, jobject obj\) \{\s*\r?\n\s*Settings::is_temporary_frame_limit = false;\s*\r?\n)(\})', `
            "`$1    Core::System::GetInstance().frame_limiter.Reset();`r`n`$2"
        if ($nc -match "setTemporaryFrameLimit[\s\S]*frame_limiter\.Reset") {
            [System.IO.File]::WriteAllText($nativeCpp, $nc)
            Write-Host "Patched native.cpp temporary frame limit Reset"
        } else {
            Write-Host "WARNING: could not patch setTemporaryFrameLimit Reset"
        }
    } else {
        Write-Host "native.cpp temporary frame limit Reset already present"
    }
    # Pokédex: top-screen capture via RequestScreenshot + SingleFrameLayout
    $nc = Get-Content $nativeCpp -Raw
    if ($nc -notmatch "hoennCaptureTopScreen") {
        if ($nc -notmatch "framebuffer_layout\.h") {
            $nc = $nc -replace '(#include "core/core\.h")', @"
`$1
#include "core/3ds.h"
#include "core/frontend/framebuffer_layout.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>
"@
        }
        $jni = @'

jintArray Java_org_citra_citra_1emu_NativeLibrary_hoennCaptureTopScreen(
    JNIEnv* env, [[maybe_unused]] jobject obj, jint res_scale) {
    auto& system = Core::System::GetInstance();
    if (!system.IsPoweredOn()) {
        return nullptr;
    }
    auto& renderer = system.GPU().Renderer();
    u32 scale = res_scale > 0 ? static_cast<u32>(res_scale) : renderer.GetResolutionScaleFactor();
    if (scale == 0) {
        scale = 1;
    }
    if (scale > 8) {
        scale = 8;
    }
    const u32 width = static_cast<u32>(Core::kScreenTopWidth) * scale;
    const u32 height = static_cast<u32>(Core::kScreenTopHeight) * scale;
    // swapped=false → top screen only
    const Layout::FramebufferLayout layout =
        Layout::SingleFrameLayout(width, height, false, false);

    std::vector<u32> pixels(static_cast<size_t>(layout.width) * layout.height, 0);
    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    bool invert = false;

    renderer.RequestScreenshot(
        pixels.data(),
        [&](bool invert_y) {
            invert = invert_y;
            {
                std::lock_guard<std::mutex> lock(m);
                done = true;
            }
            cv.notify_one();
        },
        layout);

    {
        std::unique_lock<std::mutex> lock(m);
        if (!cv.wait_for(lock, std::chrono::seconds(3), [&] { return done; })) {
            LOG_ERROR(Frontend, "Hoenn Pokédex: top-screen capture timed out");
            return nullptr;
        }
    }

    if (invert) {
        for (u32 y = 0; y < height / 2; ++y) {
            for (u32 x = 0; x < width; ++x) {
                const size_t a = static_cast<size_t>(y) * width + x;
                const size_t b = static_cast<size_t>(height - 1 - y) * width + x;
                std::swap(pixels[a], pixels[b]);
            }
        }
    }

    const jsize out_len = static_cast<jsize>(2 + pixels.size());
    jintArray arr = env->NewIntArray(out_len);
    if (!arr) {
        return nullptr;
    }
    std::vector<jint> out(static_cast<size_t>(out_len));
    out[0] = static_cast<jint>(width);
    out[1] = static_cast<jint>(height);
    for (size_t i = 0; i < pixels.size(); ++i) {
        out[2 + i] = static_cast<jint>(pixels[i]);
    }
    env->SetIntArrayRegion(arr, 0, out_len, out.data());
    LOG_INFO(Frontend, "Hoenn Pokédex: captured top {}x{} scale={}", width, height, scale);
    return arr;
}

'@
        $nc = $nc -replace "\} // extern `"C`"", ($jni + "`n} // extern `"C`"")
        [System.IO.File]::WriteAllText($nativeCpp, $nc)
        Write-Host "Patched native.cpp hoennCaptureTopScreen JNI"
    }
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
# Offline Pokédex assets (species.json + NOTICE)
$AssetsSrc = Join-Path $Overlay "assets"
$AssetsDst = Join-Path $Main "assets"
if (Test-Path $AssetsSrc) {
    Get-ChildItem $AssetsSrc -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($AssetsSrc.Length).TrimStart('\','/')
        $target = Join-Path $AssetsDst $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        Copy-Item $_.FullName $target -Force
    }
    Write-Host "Applied assets (pokedex)"
}
if (Test-Path (Join-Path $Overlay "res\layout")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Main "res\layout") | Out-Null
    Copy-Item (Join-Path $Overlay "res\layout\*") (Join-Path $Main "res\layout\") -Force
}
# Hub layouts come from overlay; no special delete needed when design files exist
if (Test-Path (Join-Path $Overlay "res\drawable")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Main "res\drawable") | Out-Null
    Copy-Item (Join-Path $Overlay "res\drawable\*") (Join-Path $Main "res\drawable\") -Force
}
if (Test-Path (Join-Path $Overlay "res\color")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $Main "res\color") | Out-Null
    Copy-Item (Join-Path $Overlay "res\color\*") (Join-Path $Main "res\color\") -Force
}
# Design fidelity: fonts, motion, press animators
foreach ($resDir in @("font", "anim", "animator")) {
    $srcDir = Join-Path $Overlay "res\$resDir"
    if (Test-Path $srcDir) {
        $dest = Join-Path $Main "res\$resDir"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item (Join-Path $srcDir "*") $dest -Force
        Write-Host "Applied res\$resDir"
    }
}
Get-ChildItem (Join-Path $Overlay "res") -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -like "mipmap*" } |
    ForEach-Object {
        $dest = Join-Path $Main "res\$($_.Name)"
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
        Copy-Item (Join-Path $_.FullName "*") $dest -Force
    }
foreach ($vals in @("hoenn_colors.xml", "hoenn_dimens.xml", "hoenn_styles.xml", "hoenn_theme.xml", "hoenn_strings_design.xml")) {
    $src = Join-Path $Overlay "res\values\$vals"
    if (Test-Path $src) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Main "res\values") | Out-Null
        Copy-Item $src (Join-Path $Main "res\values\$vals") -Force
    }
}
if (Test-Path (Join-Path $Overlay "AndroidManifest.xml")) {
    Copy-Item (Join-Path $Overlay "AndroidManifest.xml") (Join-Path $Main "AndroidManifest.xml") -Force
}
if (Test-Path (Join-Path $Overlay "jni\android_common\android_common.h")) {
    Copy-Item (Join-Path $Overlay "jni\android_common\android_common.h") (Join-Path $Main "jni\android_common\android_common.h") -Force
}
if (Test-Path (Join-Path $Overlay "app-build.gradle.kts")) {
    Copy-Item (Join-Path $Overlay "app-build.gradle.kts") (Join-Path $Android "app\build.gradle.kts") -Force
    Write-Host "Applied overlay app-build.gradle.kts"
}
# Ensure ML Kit is present even if overlay gradle was partial
$gradleApp = Join-Path $Android "app\build.gradle.kts"
if ((Test-Path $gradleApp) -and ((Get-Content $gradleApp -Raw) -notmatch "text-recognition")) {
    $g = Get-Content $gradleApp -Raw
    $g = $g.Replace(
        'implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.2")',
        "implementation(`"org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.2`")`r`n    implementation(`"com.google.mlkit:text-recognition:16.0.1`")")
    [System.IO.File]::WriteAllText($gradleApp, $g)
    Write-Host "Patched ML Kit text-recognition into build.gradle.kts"
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