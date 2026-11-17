#!/usr/bin/env python3
"""
Linux port of scripts/build-android.ps1.

Applies the Hoenn Forge overlay onto the vendored Azahar tree
(emulator/azahar) and builds :app:assembleVanillaRelWithDebInfo.

Mirrors the PowerShell original step for step, but every patch asserts that it
actually applied instead of silently doing nothing.

Usage:
    python3 scripts/build-android-linux.py            # overlay + build
    python3 scripts/build-android-linux.py --overlay  # overlay only
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AZAHAR = ROOT / "emulator" / "azahar"
ANDROID = AZAHAR / "src" / "android"
OVERLAY = ROOT / "overlay" / "azahar"
MAIN = ANDROID / "app" / "src" / "main"
JAVA_DST = MAIN / "java"

warnings: list[str] = []


def say(msg: str) -> None:
    print(msg, flush=True)


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def write(p: Path, text: str) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding="utf-8", newline="\n")


def copy_over(rel_src: str, rel_dst: str) -> None:
    src = OVERLAY / rel_src
    dst = AZAHAR / rel_dst
    if not src.is_file():
        say(f"SKIP missing in overlay: {rel_src}")
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    say(f"Applied {rel_src}")


def insert_after(text: str, anchor: str, insert: str, label: str) -> str:
    n = text.count(anchor)
    if n != 1:
        raise SystemExit(f"PATCH FAILED [{label}]: anchor occurs {n}x: {anchor!r}")
    return text.replace(anchor, anchor + insert, 1)


def sub_once(text: str, pattern: str, repl: str, label: str) -> str:
    new, n = re.subn(pattern, repl, text, count=1)
    if n != 1:
        raise SystemExit(f"PATCH FAILED [{label}]: regex did not match")
    return new


def require_dir(p: Path) -> None:
    if not p.is_dir():
        raise SystemExit(f"missing: {p} (clone azahar into emulator/azahar first)")


# --------------------------------------------------------------------------- #
# 1. whole-file overrides
# --------------------------------------------------------------------------- #
def apply_file_overrides() -> None:
    for src, dst in [
        ("core/savestate.cpp", "src/core/savestate.cpp"),
        ("core/hoenn_freecam.cpp", "src/core/hoenn_freecam.cpp"),
        ("core/hoenn_freecam.h", "src/core/hoenn_freecam.h"),
        ("core/cheats.cpp", "src/core/cheats/cheats.cpp"),
        ("core/perf_stats.h", "src/core/perf_stats.h"),
        ("core/perf_stats.cpp", "src/core/perf_stats.cpp"),
        ("video_core/hoenn_gpu_cam.cpp", "src/video_core/hoenn_gpu_cam.cpp"),
        ("video_core/hoenn_gpu_cam.h", "src/video_core/hoenn_gpu_cam.h"),
        (
            "video_core/shader/generator/shader_uniforms.cpp",
            "src/video_core/shader/generator/shader_uniforms.cpp",
        ),
    ]:
        copy_over(src, dst)

    for dead in ("src/core/hoenn_follower.cpp", "src/core/hoenn_follower.h"):
        p = AZAHAR / dead
        if p.exists():
            p.unlink()
            say(f"Removed {dead}")


# --------------------------------------------------------------------------- #
# 2. core.cpp — reset frame limiter after a successful LoadState
# --------------------------------------------------------------------------- #
def patch_core_cpp() -> None:
    p = AZAHAR / "src/core/core.cpp"
    text = read(p)
    if "frame_limiter.Reset()" in text:
        say("core.cpp FrameLimiter::Reset already present")
        return
    text = sub_once(
        text,
        r"(System::LoadState\(slot\);[\s\S]*?return ResultStatus::ErrorSavestate;\s*\}\s*\n)"
        r"([ \t]*)(frame_limiter\.WaitOnce\(\);)",
        r"\1\2frame_limiter.Reset();\n\2\3",
        "core.cpp frame_limiter.Reset",
    )
    write(p, text)
    say("Patched core.cpp FrameLimiter::Reset after LoadState")


# --------------------------------------------------------------------------- #
# 3. ldr_ro.cpp — freecam hooks on CRO load/unload
# --------------------------------------------------------------------------- #
def patch_ldr_ro() -> None:
    p = AZAHAR / "src/core/hle/service/ldr_ro/ldr_ro.cpp"
    text = read(p)
    if "hoenn_freecam.h" in text:
        say("ldr_ro.cpp freecam hooks already present")
        return

    text = insert_after(
        text,
        '#include "core/hle/service/ldr_ro/ldr_ro.h"\n',
        '#include "core/hoenn_freecam.h"\n',
        "ldr_ro.cpp include",
    )
    text = insert_after(
        text,
        "             cro_address, cro_address + fix_size);\n",
        "    Hoenn::FreeCam::GetInstance().OnModuleLoaded(cro.ModuleName(), cro_address);\n",
        "ldr_ro.cpp OnModuleLoaded",
    )
    text = insert_after(
        text,
        '    LOG_INFO(Service_LDR, "Unloading CRO \\"{}\\"", cro.ModuleName());\n',
        "    Hoenn::FreeCam::GetInstance().OnModuleUnloaded(cro.ModuleName());\n",
        "ldr_ro.cpp OnModuleUnloaded",
    )
    write(p, text)
    say("Patched ldr_ro.cpp freecam battle/field hooks")


# --------------------------------------------------------------------------- #
# 4. hid.cpp — rotate the circle pad into the free-look frame
# --------------------------------------------------------------------------- #
def patch_hid() -> None:
    p = AZAHAR / "src/core/hle/service/hid/hid.cpp"
    text = read(p)
    if "Hoenn::GpuCam::RotateStick" in text:
        say("hid.cpp circle-pad rotation already present")
        return
    text = insert_after(
        text,
        '#include "core/hle/service/hid/hid.h"\n',
        '#include "video_core/hoenn_gpu_cam.h"\n',
        "hid.cpp include",
    )
    text = insert_after(
        text,
        "        std::tie(circle_pad_x_f, circle_pad_y_f) = circle_pad->GetStatus();\n",
        "        Hoenn::GpuCam::RotateStick(circle_pad_x_f, circle_pad_y_f);\n",
        "hid.cpp RotateStick",
    )
    write(p, text)
    say("Patched hid.cpp circle-pad rotation")


# --------------------------------------------------------------------------- #
# 5. CMake source registration
# --------------------------------------------------------------------------- #
def patch_cmake() -> None:
    core_cmake = AZAHAR / "src/core/CMakeLists.txt"
    text = read(core_cmake)
    changed = False
    if "hoenn_follower" in text:
        text = re.sub(r"(?m)^\s*hoenn_follower\.(cpp|h)\n", "", text)
        changed = True
        say("Stripped hoenn_follower from core CMakeLists")
    if "hoenn_freecam.cpp" not in text:
        text = insert_after(
            text,
            "    cheats/gateway_cheat.h\n",
            "    hoenn_freecam.cpp\n    hoenn_freecam.h\n",
            "core CMakeLists hoenn_freecam",
        )
        changed = True
        say("Patched core CMakeLists for hoenn_freecam")
    else:
        say("core CMakeLists hoenn_freecam already present")
    if changed:
        write(core_cmake, text)

    vc_cmake = AZAHAR / "src/video_core/CMakeLists.txt"
    vc = read(vc_cmake)
    if "hoenn_gpu_cam.cpp" in vc:
        say("video_core CMakeLists hoenn_gpu_cam already present")
        return
    vc = vc.rstrip("\n") + (
        "\n\n\n"
        "# Hoenn Forge: GPU-path free look (view rotation in the PICA vertex-shader uniforms).\n"
        "target_sources(video_core PRIVATE\n"
        "    hoenn_gpu_cam.cpp\n"
        "    hoenn_gpu_cam.h\n"
        ")\n"
    )
    write(vc_cmake, vc)
    say("Patched video_core CMakeLists for hoenn_gpu_cam")


# --------------------------------------------------------------------------- #
# 6. NativeLibrary.kt — JNI declarations
# --------------------------------------------------------------------------- #
NL_KT_ANCHOR = "    external fun isRunning(): Boolean\n"
NL_KT_BLOCK = """
    /** Hoenn Forge: free look (houses) + L/R zoom. */
    external fun setHoennFreelook(enabled: Boolean)
    external fun isHoennFreelookEnabled(): Boolean
    external fun setHoennZoomAssist(enabled: Boolean)
    external fun isHoennZoomAssistEnabled(): Boolean

    /** Hoenn Forge Pokédex: capture top 3DS screen. */
    external fun hoennCaptureTopScreen(resScale: Int): IntArray?

    /** Hoenn Forge: GPU-path free look knobs. Param ids mirror Hoenn::GpuCam::Param. */
    external fun hoennGpuCamSet(param: Int, value: Float)
    external fun hoennGpuCamGet(param: Int): Float
"""


def patch_native_library() -> None:
    p = MAIN / "java/org/citra/citra_emu/NativeLibrary.kt"
    text = read(p)
    if "setHoennFreelook" in text:
        say("NativeLibrary.kt Hoenn camera JNI already present")
        return
    text = insert_after(text, NL_KT_ANCHOR, NL_KT_BLOCK, "NativeLibrary.kt externals")
    write(p, text)
    say("NativeLibrary.kt Hoenn camera JNI ensured")


# --------------------------------------------------------------------------- #
# 7. native.cpp — JNI implementations
# --------------------------------------------------------------------------- #
JNI_CAPTURE = """
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
    // swapped=false -> top screen only
    const Layout::FramebufferLayout layout = Layout::SingleFrameLayout(width, height, false, false);

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

"""

JNI_FREELOOK = """
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

"""

JNI_GPUCAM = """
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

"""


def append_before_extern_close(text: str, block: str, label: str) -> str:
    idx = text.rfind('} // extern "C"')
    if idx < 0:
        raise SystemExit(f"PATCH FAILED [{label}]: no closing extern \"C\" block")
    return text[:idx] + block + "\n" + text[idx:]


def patch_native_cpp() -> None:
    p = MAIN / "jni/native.cpp"
    text = read(p)

    text = text.replace('#include "core/hoenn_follower.h"\n', "")

    needs_capture = "hoennCaptureTopScreen" not in text
    needs_freelook = "setHoennFreelook" not in text
    needs_gpucam = "hoennGpuCamSet" not in text

    if "core/hoenn_freecam.h" not in text:
        text = insert_after(
            text, '#include "core/core.h"\n', '#include "core/hoenn_freecam.h"\n',
            "native.cpp freecam include",
        )
    if "video_core/hoenn_gpu_cam.h" not in text:
        text = insert_after(
            text,
            '#include "core/hoenn_freecam.h"\n',
            '#include "video_core/hoenn_gpu_cam.h"\n',
            "native.cpp gpucam include",
        )
    if needs_capture and "core/frontend/framebuffer_layout.h" not in text:
        text = insert_after(
            text,
            '#include "core/core.h"\n',
            '#include "core/3ds.h"\n'
            '#include "core/frontend/framebuffer_layout.h"\n'
            '#include "video_core/gpu.h"\n'
            '#include "video_core/renderer_base.h"\n'
            '#include "core/hoenn_freecam.h"\n'
            "\n"
            "#include <chrono>\n"
            "#include <condition_variable>\n"
            "#include <mutex>\n"
            "#include <vector>\n",
            "native.cpp pokedex includes",
        )

    if needs_freelook:
        text = append_before_extern_close(text, JNI_FREELOOK, "native.cpp freelook JNI")
        say("Patched native.cpp freelook/zoom JNI")
    if needs_capture:
        text = append_before_extern_close(text, JNI_CAPTURE, "native.cpp capture JNI")
        say("Patched native.cpp hoennCaptureTopScreen JNI")
    if needs_gpucam:
        text = append_before_extern_close(text, JNI_GPUCAM, "native.cpp gpucam JNI")
        say("Patched native.cpp GPU cam JNI")

    # L3 turbo: reset the frame limiter when the temporary limit changes.
    if "frame_limiter.Reset" not in text:
        text = sub_once(
            text,
            r"(void Java_org_citra_citra_1emu_NativeLibrary_setTemporaryFrameLimit\(JNIEnv\* env, "
            r"jobject obj,\s*\n\s*jdouble speed\) \{\s*\n\s*Settings::temporary_frame_limit = speed;\s*\n"
            r"\s*Settings::is_temporary_frame_limit = true;\s*\n)(\})",
            r"\1    Core::System::GetInstance().frame_limiter.Reset();\n\2",
            "native.cpp setTemporaryFrameLimit",
        )
        text = sub_once(
            text,
            r"(void Java_org_citra_citra_1emu_NativeLibrary_disableTemporaryFrameLimit\(JNIEnv\* env, "
            r"jobject obj\) \{\s*\n\s*Settings::is_temporary_frame_limit = false;\s*\n)(\})",
            r"\1    Core::System::GetInstance().frame_limiter.Reset();\n\2",
            "native.cpp disableTemporaryFrameLimit",
        )
        say("Patched native.cpp temporary frame limit Reset")
    else:
        say("native.cpp temporary frame limit Reset already present")

    write(p, text)


# --------------------------------------------------------------------------- #
# 8. Kotlin / assets / resources
# --------------------------------------------------------------------------- #
def apply_android_tree() -> None:
    java_src = OVERLAY / "java"
    if java_src.is_dir():
        n = 0
        for f in java_src.rglob("*"):
            if f.is_file():
                dst = JAVA_DST / f.relative_to(java_src)
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(f, dst)
                n += 1
        say(f"Applied {n} Kotlin sources from overlay/java")

    for dead in (
        "org/citra/citra_emu/hoennforge/HoennFollower.kt",
        "org/citra/citra_emu/hoennforge/FollowerGhostOverlay.kt",
        "org/citra/citra_emu/hoennforge/OrasPartyReader.kt",
    ):
        p = JAVA_DST / dead
        if p.exists():
            p.unlink()
            say(f"Removed retired {dead}")

    assets_src = OVERLAY / "assets"
    if assets_src.is_dir():
        n = 0
        for f in assets_src.rglob("*"):
            if f.is_file():
                dst = MAIN / "assets" / f.relative_to(assets_src)
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(f, dst)
                n += 1
        say(f"Applied assets (pokedex, {n} files)")

    res = OVERLAY / "res"
    for sub in ("layout", "drawable", "color", "font", "anim", "animator"):
        src_dir = res / sub
        if not src_dir.is_dir():
            continue
        dst_dir = MAIN / "res" / sub
        dst_dir.mkdir(parents=True, exist_ok=True)
        n = 0
        for f in src_dir.iterdir():
            if f.is_file():
                shutil.copyfile(f, dst_dir / f.name)
                n += 1
        say(f"Applied res/{sub} ({n} files)")

    if res.is_dir():
        for d in res.iterdir():
            if d.is_dir() and d.name.startswith("mipmap"):
                dst_dir = MAIN / "res" / d.name
                dst_dir.mkdir(parents=True, exist_ok=True)
                for f in d.iterdir():
                    if f.is_file():
                        shutil.copyfile(f, dst_dir / f.name)
                say(f"Applied res/{d.name}")

    # Every values/ and values-<qualifier>/ folder. The Hoenn designs ship phone
    # defaults in values/ and the roomy (Thor / tablet) overrides in
    # values-h400dp/ and values-sw600dp/; dropping the qualifier folders silently
    # re-applies the 56 dp Thor scale to phones. hoenn_strings_snippet.xml is a
    # fragment consumed by inject_strings(), not a resource file, so it is skipped.
    for d in sorted(res.glob("values*")):
        if not d.is_dir():
            continue
        dst_dir = MAIN / "res" / d.name
        dst_dir.mkdir(parents=True, exist_ok=True)
        count = 0
        for f in d.iterdir():
            if f.is_file() and "snippet" not in f.name:
                shutil.copyfile(f, dst_dir / f.name)
                count += 1
        say(f"Applied res/{d.name} ({count} files)")

    for src_rel, dst_rel in (
        ("AndroidManifest.xml", "AndroidManifest.xml"),
        ("jni/android_common/android_common.h", "jni/android_common/android_common.h"),
    ):
        src = OVERLAY / src_rel
        if src.is_file():
            dst = MAIN / dst_rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dst)
            say(f"Applied {src_rel}")

    gradle_overlay = OVERLAY / "app-build.gradle.kts"
    if gradle_overlay.is_file():
        shutil.copyfile(gradle_overlay, ANDROID / "app/build.gradle.kts")
        say("Applied overlay app-build.gradle.kts")

    gradle_app = ANDROID / "app/build.gradle.kts"
    g = read(gradle_app)
    if "text-recognition" not in g:
        anchor = 'implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.2")'
        if anchor in g:
            g = g.replace(
                anchor,
                anchor + '\n    implementation("com.google.mlkit:text-recognition:16.0.1")',
                1,
            )
            write(gradle_app, g)
            say("Patched ML Kit text-recognition into build.gradle.kts")
        else:
            warnings.append("could not inject ML Kit text-recognition dependency")


def inject_strings() -> None:
    strings = MAIN / "res/values/strings.xml"
    snippet_path = OVERLAY / "res/values/hoenn_strings_snippet.xml"
    if not strings.is_file() or not snippet_path.is_file():
        warnings.append("strings.xml or snippet missing; skipped string injection")
        return

    drop = re.compile(
        r'name="hoenn_|Hoenn Forge UI strings|<!-- Prepare -->|<!-- Play mode -->|<!-- Randomizer -->'
    )
    lines = [ln for ln in read(strings).splitlines() if not drop.search(ln)]
    text = "\n".join(lines)
    text = text.replace(">Azahar</string>", ">Hoenn Forge</string>")
    snippet = read(snippet_path).rstrip()
    if "</resources>" not in text:
        raise SystemExit("PATCH FAILED [strings.xml]: no </resources>")
    text = text.replace("</resources>", snippet + "\n\n</resources>", 1)
    write(strings, text + "\n")
    say("Injected Hoenn Forge strings")


# --------------------------------------------------------------------------- #
# 9. build
# --------------------------------------------------------------------------- #
def build() -> int:
    env = dict(os.environ)
    env.setdefault("JAVA_HOME", "/usr/lib/jvm/java-17-openjdk-arm64")
    env.setdefault("ANDROID_HOME", "/home/oscar/Apps/Android/Sdk")
    env.setdefault("ANDROID_SDK_ROOT", env["ANDROID_HOME"])
    env["PATH"] = f"{env['JAVA_HOME']}/bin:" + env["PATH"]
    cmd = ["./gradlew", ":app:assembleVanillaRelWithDebInfo", "-x", "ktlintCheck", "--console=plain"]
    say("Running: " + " ".join(cmd))
    return subprocess.call(cmd, cwd=ANDROID, env=env)



def patch_refresh_rate() -> None:
    """Phones keep the panel's highest refresh (120 Hz); Thor stays at 60."""
    target = ANDROID / "app/src/main/java/org/citra/citra_emu/activities/EmulationActivity.kt"
    if not target.exists():
        warnings.append("EmulationActivity.kt missing for refresh patch")
        return
    text = target.read_text(encoding="utf-8")
    if "sixtyHz = CitraApplication" in text:
        return
    old = "RefreshRateUtil.enforceRefreshRate(this, sixtyHz = true)"
    new = ("RefreshRateUtil.enforceRefreshRate(\n"
           "            this,\n"
           "            sixtyHz = CitraApplication.appContext.resources.configuration.smallestScreenWidthDp >= 600,\n"
           "        )")
    if old not in text:
        warnings.append("refresh-rate anchor not found")
        return
    target.write_text(text.replace(old, new, 1), encoding="utf-8")
    say("Patched EmulationActivity refresh rate (phones: max, Thor: 60)")

def main() -> int:
    require_dir(AZAHAR)
    require_dir(ANDROID)
    require_dir(OVERLAY)

    apply_file_overrides()
    patch_core_cpp()
    patch_ldr_ro()
    patch_hid()
    patch_cmake()
    patch_native_library()
    patch_native_cpp()
    apply_android_tree()
    inject_strings()
    patch_refresh_rate()

    for w in warnings:
        say(f"WARNING: {w}")

    if "--overlay" in sys.argv:
        say("Overlay applied (build skipped)")
        return 0

    return build()


if __name__ == "__main__":
    sys.exit(main())
