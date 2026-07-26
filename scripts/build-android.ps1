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
# Hoenn free-look core + GPU-path camera + frame limiter reset (turbo after savestate)
foreach ($pair in @(
    @("core\hoenn_freecam.cpp", "src\core\hoenn_freecam.cpp"),
    @("core\hoenn_freecam.h", "src\core\hoenn_freecam.h"),
    @("core\cheats.cpp", "src\core\cheats\cheats.cpp"),
    @("core\perf_stats.h", "src\core\perf_stats.h"),
    @("core\perf_stats.cpp", "src\core\perf_stats.cpp"),
    @("video_core\hoenn_gpu_cam.cpp", "src\video_core\hoenn_gpu_cam.cpp"),
    @("video_core\hoenn_gpu_cam.h", "src\video_core\hoenn_gpu_cam.h"),
    @("video_core\shader\generator\shader_uniforms.cpp", "src\video_core\shader\generator\shader_uniforms.cpp")
)) {
    $src = Join-Path $Overlay $pair[0]
    $dst = Join-Path $Azahar $pair[1]
    if (Test-Path $src) {
        New-Item -ItemType Directory -Force -Path (Split-Path $dst -Parent) | Out-Null
        Copy-Item $src $dst -Force
        Write-Host "Applied $($pair[0])"
    }
}
# Remove retired follower experiment sources from emulator tree
foreach ($dead in @(
    "src\core\hoenn_follower.cpp",
    "src\core\hoenn_follower.h"
)) {
    $p = Join-Path $Azahar $dead
    if (Test-Path $p) {
        Remove-Item $p -Force
        Write-Host "Removed $dead"
    }
}
# After LoadState success: reset frame limiter so turbo limit applies immediately
$coreCpp = Join-Path $Azahar "src\core\core.cpp"
if (Test-Path $coreCpp) {
    $cc = Get-Content $coreCpp -Raw -Encoding UTF8
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
    $ldr = Get-Content $ldrRo -Raw -Encoding UTF8
    $ldrDirty = $false
    # Strip retired follower hooks
    if ($ldr -match "hoenn_follower\.h|FollowerProbe") {
        $ldr = $ldr -replace '(?m)^\s*#include "core/hoenn_follower\.h"\r?\n', ''
        $ldr = $ldr -replace '(?m)^\s*Hoenn::FollowerProbe::GetInstance\(\)\.OnModuleLoaded\([^;]+;\r?\n', ''
        $ldr = $ldr -replace '(?m)^\s*Hoenn::FollowerProbe::GetInstance\(\)\.OnModuleUnloaded\([^;]+;\r?\n', ''
        $ldrDirty = $true
        Write-Host "Stripped follower hooks from ldr_ro.cpp"
    }
    if ($ldr -notmatch "hoenn_freecam\.h") {
        $ldr = $ldr -replace '(#include "core/hle/service/ldr_ro/ldr_ro\.h")', "`$1`r`n#include `"core/hoenn_freecam.h`""
        $ldrDirty = $true
    }
    if ($ldr -notmatch "FreeCam::GetInstance\(\)\.OnModuleLoaded") {
        $ldr = $ldr -replace `
            '(LOG_INFO\(Service_LDR, "CRO \\"\{\}\\" loaded at 0x\{:08X\}, fixed_end=0x\{:08X\}", cro\.ModuleName\(\),\s*\r?\n\s*cro_address, cro_address \+ fix_size\);)', `
            "`$1`r`n    Hoenn::FreeCam::GetInstance().OnModuleLoaded(cro.ModuleName(), cro_address);"
        $ldr = $ldr -replace `
            '(LOG_INFO\(Service_LDR, "Unloading CRO \\"\{\}\\"", cro\.ModuleName\(\)\);)', `
            "`$1`r`n    Hoenn::FreeCam::GetInstance().OnModuleUnloaded(cro.ModuleName());"
        $ldrDirty = $true
        Write-Host "Patched ldr_ro.cpp freecam battle/field hooks"
    } else {
        Write-Host "ldr_ro.cpp freecam hooks already present"
    }
    if ($ldrDirty) {
        [System.IO.File]::WriteAllText($ldrRo, $ldr)
    }
}
# Ensure citra_core CMakeLists lists hoenn_freecam only (no follower)
$coreCmake = Join-Path $Azahar "src\core\CMakeLists.txt"
if (Test-Path $coreCmake) {
    $cm = Get-Content $coreCmake -Raw -Encoding UTF8
    if ($cm -match "hoenn_follower") {
        $cm = $cm -replace '(?m)^\s*hoenn_follower\.cpp\r?\n', ''
        $cm = $cm -replace '(?m)^\s*hoenn_follower\.h\r?\n', ''
        [System.IO.File]::WriteAllText($coreCmake, $cm)
        Write-Host "Stripped hoenn_follower from core CMakeLists"
        $cm = Get-Content $coreCmake -Raw -Encoding UTF8
    }
    if ($cm -notmatch "hoenn_freecam\.cpp") {
        $cm = $cm -replace "(cheats/gateway_cheat\.h\r?\n)", "`$1    hoenn_freecam.cpp`n    hoenn_freecam.h`n"
        [System.IO.File]::WriteAllText($coreCmake, $cm)
        Write-Host "Patched core CMakeLists for hoenn_freecam"
    }
}
# video_core CMakeLists: register the GPU-path camera. Appended rather than spliced into
# the source list so an upstream bump cannot break the patch.
$vcCmake = Join-Path $Azahar "src\video_core\CMakeLists.txt"
if (Test-Path $vcCmake) {
    $vc = Get-Content $vcCmake -Raw -Encoding UTF8
    if ($vc -notmatch "hoenn_gpu_cam\.cpp") {
        $vc = $vc.TrimEnd() + @"


# Hoenn Forge: GPU-path free look (view rotation in the PICA vertex-shader uniforms).
target_sources(video_core PRIVATE
    hoenn_gpu_cam.cpp
    hoenn_gpu_cam.h
)
"@
        [System.IO.File]::WriteAllText($vcCmake, $vc)
        Write-Host "Patched video_core CMakeLists for hoenn_gpu_cam"
    } else {
        Write-Host "video_core CMakeLists hoenn_gpu_cam already present"
    }
}
# NativeLibrary: freelook + zoom + pokedex capture only (strip retired RE/follower APIs)
$nlPath = Join-Path $Android "app\src\main\java\org\citra\citra_emu\NativeLibrary.kt"
if (Test-Path $nlPath) {
    $nl = Get-Content $nlPath -Raw -Encoding UTF8
    # Strip retired experiment / cam probe / follower declarations if present
    foreach ($dead in @(
        'setHoennFreelookExperiment', 'getHoennFreelookExperiment', 'getHoennFreelookExperimentCount',
        'getHoennFreelookExperimentLabel', 'hoennScanCamCandidates', 'hoennGetCamCandidateLabels',
        'hoennSetCamProbeIndex', 'hoennGetCamProbeIndex', 'hoennDumpCamRE',
        'setHoennFollowerProbe', 'isHoennFollowerProbeEnabled', 'hoennFollowerStatus',
        'setHoennFollowerPartySpecies'
    )) {
        $nl = $nl -replace "(?m)^\s*external fun $dead\([^\)]*\)(: [^\r\n]+)?\r?\n", ''
    }
    if ($nl -notmatch "setHoennFreelook") {
        $nl = $nl -replace "(external fun isRunning\(\): Boolean)", @"
`$1

    /** Hoenn Forge: free look (houses) + L/R zoom. */
    external fun setHoennFreelook(enabled: Boolean)
    external fun isHoennFreelookEnabled(): Boolean
    external fun setHoennZoomAssist(enabled: Boolean)
    external fun isHoennZoomAssistEnabled(): Boolean
"@
    }
    if ($nl -notmatch "setHoennZoomAssist") {
        $nl = $nl -replace "(external fun isHoennFreelookEnabled\(\): Boolean)", @"
`$1
    external fun setHoennZoomAssist(enabled: Boolean)
    external fun isHoennZoomAssistEnabled(): Boolean
"@
    }
    if ($nl -notmatch "hoennCaptureTopScreen") {
        $nl = $nl -replace "(external fun isHoennZoomAssistEnabled\(\): Boolean)", @"
`$1

    /** Hoenn Forge Pokédex: capture top 3DS screen. */
    external fun hoennCaptureTopScreen(resScale: Int): IntArray?
"@
    }
    if ($nl -notmatch "hoennGpuCamSet") {
        $nl = $nl -replace "(external fun hoennCaptureTopScreen\(resScale: Int\): IntArray\?)", @"
`$1

    /** Hoenn Forge: GPU-path free look knobs. Param ids mirror Hoenn::GpuCam::Param. */
    external fun hoennGpuCamSet(param: Int, value: Float)
    external fun hoennGpuCamGet(param: Int): Float
"@
        Write-Host "NativeLibrary.kt GPU cam JNI ensured"
    }
    [System.IO.File]::WriteAllText($nlPath, $nl)
    Write-Host "NativeLibrary.kt Hoenn camera JNI ensured (ship only)"
}
# native.cpp freelook + zoom only (no RE experiment / follower thrash)
$nativeCpp = Join-Path $Android "app\src\main\jni\native.cpp"
if (Test-Path $nativeCpp) {
    $nc = Get-Content $nativeCpp -Raw -Encoding UTF8
    # Drop retired includes / APIs that call removed FreeCam methods
    $nc = $nc -replace '#include "core/hoenn_follower\.h"\r?\n', ''
    foreach ($fn in @(
        'hoennScanCamCandidates', 'hoennGetCamCandidateLabels', 'hoennSetCamProbeIndex',
        'hoennGetCamProbeIndex', 'hoennDumpCamRE', 'setHoennFreelookExperiment',
        'getHoennFreelookExperiment', 'getHoennFreelookExperimentCount',
        'getHoennFreelookExperimentLabel', 'setHoennFollowerProbe',
        'isHoennFollowerProbeEnabled', 'hoennFollowerStatus', 'setHoennFollowerPartySpecies'
    )) {
        $rx = "(?ms)^(?:void|jboolean|jint|jstring|jobjectArray)\s+Java_org_citra_citra_1emu_NativeLibrary_$fn\b.*?\n\}\r?\n"
        $nc = [regex]::Replace($nc, $rx, '')
    }
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
        Write-Host "Patched native.cpp zoom JNI"
    }
    [System.IO.File]::WriteAllText($nativeCpp, $nc)
    $nc = Get-Content $nativeCpp -Raw -Encoding UTF8
    # L3 turbo: reset frame limiter when temporary limit is set (post-savestate lag)
    if ($nc -notmatch "setTemporaryFrameLimit[\s\S]*frame_limiter\.Reset") {
        $nc = Get-Content $nativeCpp -Raw -Encoding UTF8
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
    $nc = Get-Content $nativeCpp -Raw -Encoding UTF8
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
    # GPU-path free look: one generic get/set pair so new knobs need no new JNI symbols
    $nc = Get-Content $nativeCpp -Raw -Encoding UTF8
    if ($nc -notmatch "hoennGpuCamSet") {
        if ($nc -notmatch "hoenn_gpu_cam\.h") {
            $nc = $nc -replace '(#include "core/hoenn_freecam\.h")', "`$1`n#include `"video_core/hoenn_gpu_cam.h`""
        }
        $jni = @'

void Java_org_citra_citra_1emu_NativeLibrary_hoennGpuCamSet([[maybe_unused]] JNIEnv* env,
                                                            [[maybe_unused]] jobject obj,
                                                            jint param, jfloat value) {
    Hoenn::GpuCam::SetParam(static_cast<int>(param), static_cast<float>(value));
}

jfloat Java_org_citra_citra_1emu_NativeLibrary_hoennGpuCamGet([[maybe_unused]] JNIEnv* env,
                                                              [[maybe_unused]] jobject obj,
                                                              jint param) {
    return static_cast<jfloat>(Hoenn::GpuCam::GetParam(static_cast<int>(param)));
}

'@
        $nc = $nc -replace "\} // extern `"C`"", ($jni + "`n} // extern `"C`"")
        [System.IO.File]::WriteAllText($nativeCpp, $nc)
        Write-Host "Patched native.cpp GPU cam JNI"
    } else {
        Write-Host "native.cpp GPU cam JNI already present"
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
# Retired experiment Kotlin (must not linger from older builds)
foreach ($dead in @(
    "org\citra\citra_emu\hoennforge\HoennFollower.kt",
    "org\citra\citra_emu\hoennforge\FollowerGhostOverlay.kt",
    "org\citra\citra_emu\hoennforge\OrasPartyReader.kt"
)) {
    $p = Join-Path $JavaDst $dead
    if (Test-Path $p) {
        Remove-Item $p -Force
        Write-Host "Removed retired $dead"
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
if ((Test-Path $gradleApp) -and ((Get-Content $gradleApp -Raw -Encoding UTF8) -notmatch "text-recognition")) {
    $g = Get-Content $gradleApp -Raw -Encoding UTF8
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
    # Lines with hoenn_* were already stripped above - always re-append the full snippet
    # so string value updates (not just new keys) ship on every build.
    $text = $text -replace '</resources>', ($snippet + "`n`n</resources>")
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