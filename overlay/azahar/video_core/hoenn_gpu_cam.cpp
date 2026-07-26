// Copyright Hoenn Forge — GPU-path free look for ORAS (research proposal P1)
//
// See hoenn_gpu_cam.h for why this lives in the graphics pipeline rather than in guest
// memory. Summary of the maths:
//
//   The PICA200 has no fixed-function T&L, so the world->eye transform must be present
//   somewhere in the 96 vertex-shader float uniform rows. A 3x4 row-major transform
//   occupies three consecutive rows, each holding (a, b, c | t). Its 3x3 part is
//   orthonormal exactly when the transform is rigid, which a view (or world-view)
//   matrix is and a projection or MVP matrix is not — that is the detector.
//
//   Given such a matrix M we do NOT replace it. We left-multiply:
//
//       M' = O * M          O = T(p) * R * T(-p),  p = (0, 0, -d)
//
//   O acts purely in eye space, so it orbits the camera around the point d units in
//   front of it — the player — and it is correct whether M is a pure view matrix or a
//   per-object world-view matrix, because the view part is a left factor of both.
//
//   The yaw axis is world-up expressed in eye space, recovered from M itself (column 1
//   of its 3x3, i.e. M * (0,1,0)). Yawing about the raw eye-space Y axis would roll the
//   horizon, because the game camera is pitched down. Pitch is about eye-space X, which
//   is screen-horizontal by construction.

#include "video_core/hoenn_gpu_cam.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include "common/logging/log.h"

namespace Hoenn::GpuCam {
namespace {

constexpr int kRows = 96;
constexpr int kLastTriple = kRows - 3; // valid triple starts are 0..93
constexpr int kMaxCandidates = 12;

// A full 94-triple sweep costs ~1.4k flops. Uniform uploads happen per draw, so the
// sweep is amortised: candidates found by the last sweep are re-checked every upload
// (cheap), the full sweep runs every kRescanUploads uploads. At one upload per frame
// that is a re-detect roughly twice a second; at thousands per frame it is noise.
constexpr int kRescanUploads = 32;

// Orthonormality tolerances. Generous enough for f24 -> f32 rounding.
constexpr float kLenSqTol = 0.04f;
constexpr float kDotTol = 0.02f;
constexpr float kMinAbsDet = 0.90f;

constexpr float kDegToRad = 0.017453292519943295f;

struct Shared {
    std::atomic<bool> active{false};
    std::atomic<bool> probe{false};
    std::atomic<int> row_mode{kRowModeAuto};
    std::atomic<bool> transpose{false};
    std::atomic<float> radius{kDefaultRadius};
    std::atomic<float> yaw{0.0f};
    std::atomic<float> pitch{0.0f};
    std::atomic<int> detected_row{-1};
    std::atomic<int> qualify_count{0};
    std::atomic<int> invert{0};
};
Shared g;

/// How many consecutive uploads the locked row may fail to qualify before we go looking
/// for a different one. Roughly a frame's worth of draws, so a handful of billboard or
/// UI draws that happen to reuse the same row cannot make the selection oscillate.
constexpr int kLockMissLimit = 128;

/// Migration hysteresis: a rival row has to be this much more stable than the locked one
/// before we switch, so the choice cannot flip back and forth within a scene.
constexpr int kMigrateMargin = 240;

// Scan state. Touched only from the thread that issues draws (Azahar has no separate
// GPU thread — the rasterizer runs on the emulation thread), so no locking here.
struct Scan {
    int candidates[kMaxCandidates]{};
    bool candidate_live[kMaxCandidates]{};
    int candidate_count = 0;
    int total_qualifying = 0;
    float prev[kLastTriple + 1][12]{};
    bool prev_valid[kLastTriple + 1]{};
    u16 stable[kLastTriple + 1]{};
    int uploads_since_scan = kRescanUploads;
    int locked_row = -1;
    int locked_miss = 0;
    bool last_probe = false;
    bool was_active = false;
    std::chrono::steady_clock::time_point last_log{};
};
Scan s;

void ResetScan() {
    s.candidate_count = 0;
    s.total_qualifying = 0;
    std::memset(s.prev_valid, 0, sizeof(s.prev_valid));
    std::memset(s.stable, 0, sizeof(s.stable));
    s.uploads_since_scan = kRescanUploads;
    s.locked_row = -1;
    s.locked_miss = 0;
}

bool Finite3(const Common::Vec4f& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
           std::isfinite(v.w);
}

/// True for identity, mirrors and 90-degree screen rotations: every element sits on
/// {-1, 0, 1}. Those are orthonormal but are never a field camera, and they show up in
/// 2D layout and texture-coordinate transforms. Rejecting them keeps the detector off
/// the bottom screen and the HUD without needing the render target here.
bool IsAxisAligned(const Common::Vec4f& a, const Common::Vec4f& b, const Common::Vec4f& c) {
    const float e[9] = {a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z};
    for (const float v : e) {
        const float av = std::fabs(v);
        if (av > 0.03f && av < 0.97f) {
            return false;
        }
    }
    return true;
}

bool Qualifies(const std::array<Common::Vec4f, kRows>& f, int r) {
    const Common::Vec4f& a = f[static_cast<size_t>(r)];
    const Common::Vec4f& b = f[static_cast<size_t>(r) + 1];
    const Common::Vec4f& c = f[static_cast<size_t>(r) + 2];
    if (!Finite3(a) || !Finite3(b) || !Finite3(c)) {
        return false;
    }

    const float la = a.x * a.x + a.y * a.y + a.z * a.z;
    if (std::fabs(la - 1.0f) > kLenSqTol) {
        return false;
    }
    const float lb = b.x * b.x + b.y * b.y + b.z * b.z;
    if (std::fabs(lb - 1.0f) > kLenSqTol) {
        return false;
    }
    const float lc = c.x * c.x + c.y * c.y + c.z * c.z;
    if (std::fabs(lc - 1.0f) > kLenSqTol) {
        return false;
    }

    if (std::fabs(a.x * b.x + a.y * b.y + a.z * b.z) > kDotTol) {
        return false;
    }
    if (std::fabs(a.x * c.x + a.y * c.y + a.z * c.z) > kDotTol) {
        return false;
    }
    if (std::fabs(b.x * c.x + b.y * c.y + b.z * c.z) > kDotTol) {
        return false;
    }

    // |det| == 1 for any orthonormal 3x3; accept either handedness.
    const float det = a.x * (b.y * c.z - b.z * c.y) - a.y * (b.x * c.z - b.z * c.x) +
                      a.z * (b.x * c.y - b.y * c.x);
    if (std::fabs(det) < kMinAbsDet) {
        return false;
    }

    return !IsAxisAligned(a, b, c);
}

struct Mat3 {
    float m[3][3];
};

constexpr Mat3 kIdentity3{{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};

Mat3 AxisAngle(float ax, float ay, float az, float rad) {
    const float len = std::sqrt(ax * ax + ay * ay + az * az);
    if (!(len > 1.0e-5f) || !std::isfinite(len)) {
        return kIdentity3;
    }
    ax /= len;
    ay /= len;
    az /= len;
    const float c = std::cos(rad);
    const float sn = std::sin(rad);
    const float t = 1.0f - c;
    Mat3 r{};
    r.m[0][0] = t * ax * ax + c;
    r.m[0][1] = t * ax * ay - sn * az;
    r.m[0][2] = t * ax * az + sn * ay;
    r.m[1][0] = t * ax * ay + sn * az;
    r.m[1][1] = t * ay * ay + c;
    r.m[1][2] = t * ay * az - sn * ax;
    r.m[2][0] = t * ax * az - sn * ay;
    r.m[2][1] = t * ay * az + sn * ax;
    r.m[2][2] = t * az * az + c;
    return r;
}

Mat3 Mul(const Mat3& a, const Mat3& b) {
    Mat3 o{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            o.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return o;
}

/// f[r..r+2] hold the rows of a 3x4 world->eye transform, translation in .w.
/// M' = O * M, so new_row_i = sum_k R[i][k] * old_row_k, then .w += trans[i].
void ApplyRows(std::array<Common::Vec4f, kRows>& f, int r, const Mat3& rot, const float tr[3]) {
    const Common::Vec4f r0 = f[static_cast<size_t>(r)];
    const Common::Vec4f r1 = f[static_cast<size_t>(r) + 1];
    const Common::Vec4f r2 = f[static_cast<size_t>(r) + 2];
    for (int i = 0; i < 3; ++i) {
        Common::Vec4f out;
        out.x = rot.m[i][0] * r0.x + rot.m[i][1] * r1.x + rot.m[i][2] * r2.x;
        out.y = rot.m[i][0] * r0.y + rot.m[i][1] * r1.y + rot.m[i][2] * r2.y;
        out.z = rot.m[i][0] * r0.z + rot.m[i][1] * r1.z + rot.m[i][2] * r2.z;
        out.w = rot.m[i][0] * r0.w + rot.m[i][1] * r1.w + rot.m[i][2] * r2.w + tr[i];
        f[static_cast<size_t>(r + i)] = out;
    }
}

/// Diagnostic layout: f[r..r+2] hold the columns of the 3x3, so M' = O * M is just
/// v_i' = R * v_i. Where the translation lives in this layout is shader-specific, so we
/// leave .w alone — the result rotates the world about its origin instead of orbiting
/// the player, which is still plainly visible and tells us the layout guess was wrong.
void ApplyColumns(std::array<Common::Vec4f, kRows>& f, int r, const Mat3& rot) {
    for (int i = 0; i < 3; ++i) {
        Common::Vec4f v = f[static_cast<size_t>(r + i)];
        const float x = v.x;
        const float y = v.y;
        const float z = v.z;
        v.x = rot.m[0][0] * x + rot.m[0][1] * y + rot.m[0][2] * z;
        v.y = rot.m[1][0] * x + rot.m[1][1] * y + rot.m[1][2] * z;
        v.z = rot.m[2][0] * x + rot.m[2][1] * y + rot.m[2][2] * z;
        f[static_cast<size_t>(r + i)] = v;
    }
}

void ReadTriple(const std::array<Common::Vec4f, kRows>& f, int r, float out[12]) {
    for (int i = 0; i < 3; ++i) {
        const Common::Vec4f& v = f[static_cast<size_t>(r + i)];
        out[i * 4 + 0] = v.x;
        out[i * 4 + 1] = v.y;
        out[i * 4 + 2] = v.z;
        out[i * 4 + 3] = v.w;
    }
}

bool SameTriple(const float a[12], const float b[12]) {
    for (int i = 0; i < 12; ++i) {
        if (std::fabs(a[i] - b[i]) > 1.0e-6f) {
            return false;
        }
    }
    return true;
}

} // namespace

void SetActive(bool active, float yaw_deg, float pitch_deg) {
    g.yaw.store(std::clamp(yaw_deg, -kYawClampDeg, kYawClampDeg), std::memory_order_relaxed);
    g.pitch.store(std::clamp(pitch_deg, -kPitchClampDeg, kPitchClampDeg),
                  std::memory_order_relaxed);
    g.active.store(active, std::memory_order_relaxed);
}

void Disable() {
    g.yaw.store(0.0f, std::memory_order_relaxed);
    g.pitch.store(0.0f, std::memory_order_relaxed);
    g.active.store(false, std::memory_order_relaxed);
}

bool IsProbeEnabled() {
    return g.probe.load(std::memory_order_relaxed);
}

void SetParam(int param, float value) {
    switch (param) {
    case ParamProbe:
        g.probe.store(value != 0.0f, std::memory_order_relaxed);
        LOG_INFO(Render, "Hoenn GPU cam: probe={}", value != 0.0f);
        break;
    case ParamRowMode: {
        int mode = static_cast<int>(value);
        if (mode < kRowModeAll) {
            mode = kRowModeAll;
        }
        if (mode > kLastTriple) {
            mode = kLastTriple;
        }
        g.row_mode.store(mode, std::memory_order_relaxed);
        LOG_INFO(Render, "Hoenn GPU cam: row mode={}", mode);
        break;
    }
    case ParamTranspose:
        g.transpose.store(value != 0.0f, std::memory_order_relaxed);
        LOG_INFO(Render, "Hoenn GPU cam: transpose={}", value != 0.0f);
        break;
    case ParamRadius:
        g.radius.store(std::clamp(value, 1.0f, 100000.0f), std::memory_order_relaxed);
        LOG_INFO(Render, "Hoenn GPU cam: radius={}", value);
        break;
    case ParamInvert: {
        const int bits = static_cast<int>(value) & (kInvertYaw | kInvertPitch);
        g.invert.store(bits, std::memory_order_relaxed);
        LOG_INFO(Render, "Hoenn GPU cam: invert={}", bits);
        break;
    }
    default:
        break;
    }
}

float GetParam(int param) {
    switch (param) {
    case ParamProbe:
        return g.probe.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    case ParamRowMode:
        return static_cast<float>(g.row_mode.load(std::memory_order_relaxed));
    case ParamTranspose:
        return g.transpose.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    case ParamRadius:
        return g.radius.load(std::memory_order_relaxed);
    case ParamDetectedRow:
        return static_cast<float>(g.detected_row.load(std::memory_order_relaxed));
    case ParamQualifyCount:
        return static_cast<float>(g.qualify_count.load(std::memory_order_relaxed));
    case ParamActive:
        return g.active.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    case ParamYaw:
        return g.yaw.load(std::memory_order_relaxed);
    case ParamPitch:
        return g.pitch.load(std::memory_order_relaxed);
    case ParamInvert:
        return static_cast<float>(g.invert.load(std::memory_order_relaxed));
    default:
        return 0.0f;
    }
}

void ApplyToUniforms(std::array<Common::Vec4f, kRows>& f) {
    if (!g.active.load(std::memory_order_relaxed)) {
        if (s.was_active) {
            s.was_active = false;
            ResetScan();
            g.detected_row.store(-1, std::memory_order_relaxed);
            g.qualify_count.store(0, std::memory_order_relaxed);
        }
        return;
    }
    if (!s.was_active) {
        s.was_active = true;
        ResetScan();
    }

    // Which way the world should swing for a given stick direction depends on the
    // engine's handedness, which we cannot know without looking at the screen. Expose it
    // rather than guess, so a wrong guess costs a menu tap instead of a rebuild.
    const int invert = g.invert.load(std::memory_order_relaxed);
    const float yaw_sign = (invert & kInvertYaw) ? -1.0f : 1.0f;
    const float pitch_sign = (invert & kInvertPitch) ? -1.0f : 1.0f;

    const bool probe = g.probe.load(std::memory_order_relaxed);
    const float yaw_deg =
        yaw_sign * (probe ? kProbeYawDeg : g.yaw.load(std::memory_order_relaxed));
    const float pitch_deg = probe ? 0.0f : pitch_sign * g.pitch.load(std::memory_order_relaxed);
    const int mode = g.row_mode.load(std::memory_order_relaxed);
    const bool transpose = g.transpose.load(std::memory_order_relaxed);
    const float radius = g.radius.load(std::memory_order_relaxed);

    if (probe != s.last_probe) {
        s.last_probe = probe;
        s.locked_row = -1;
        s.locked_miss = 0;
    }

    // Periodic full sweep: rebuild the candidate list and clear stability for anything
    // that stopped qualifying.
    if (++s.uploads_since_scan >= kRescanUploads) {
        s.uploads_since_scan = 0;
        s.candidate_count = 0;
        s.total_qualifying = 0;
        for (int r = 0; r <= kLastTriple; ++r) {
            if (Qualifies(f, r)) {
                ++s.total_qualifying;
                if (s.candidate_count < kMaxCandidates) {
                    s.candidates[s.candidate_count++] = r;
                }
            } else {
                s.stable[r] = 0;
                s.prev_valid[r] = false;
            }
        }
    }

    // Per-upload pass over the candidates only. The view matrix is the triple that stays
    // byte-identical across uploads; per-object world-view matrices change every upload.
    int lowest = -1;
    int best = -1;
    int best_stable = -1;
    int live = 0;
    for (int ci = 0; ci < s.candidate_count; ++ci) {
        const int r = s.candidates[ci];
        if (!Qualifies(f, r)) {
            s.candidate_live[ci] = false;
            s.stable[r] = 0;
            s.prev_valid[r] = false;
            continue;
        }
        s.candidate_live[ci] = true;
        ++live;
        float cur[12];
        ReadTriple(f, r, cur);
        if (s.prev_valid[r] && SameTriple(s.prev[r], cur)) {
            if (s.stable[r] < 60000) {
                ++s.stable[r];
            }
        } else {
            s.stable[r] = 0;
        }
        std::memcpy(s.prev[r], cur, sizeof(cur));
        s.prev_valid[r] = true;
        if (lowest < 0) {
            lowest = r;
        }
        if (static_cast<int>(s.stable[r]) > best_stable) {
            best_stable = static_cast<int>(s.stable[r]);
            best = r;
        }
    }

    bool apply_all = false;
    int source_row = -1;
    int reported_row = -1;
    if (mode >= 0 && mode <= kLastTriple) {
        // Manual override: trust the user, no qualification test. This is the escape
        // hatch that lets the row index be swept on device without a rebuild.
        source_row = mode;
        reported_row = mode;
    } else if (mode == kRowModeAll) {
        apply_all = true;
        source_row = lowest;
        reported_row = lowest;
    } else {
        // Auto, with a lock. Engines commonly upload a per-object world-view matrix at
        // one fixed row, and some of those objects (billboards, UI quads) carry a matrix
        // that is not orthonormal. Without the lock, such a draw would fall through to
        // whatever other row happened to qualify and get rotated instead, shearing the
        // frame apart. Locked and unrecognised means: leave this draw alone.
        //
        // The probe deliberately takes the lowest-index qualifying triple, so the
        // experiment result is reproducible and easy to reason about.
        const int pick = probe ? lowest : best;
        if (s.locked_row >= 0 && Qualifies(f, s.locked_row)) {
            s.locked_miss = 0;
            source_row = s.locked_row;
            if (!probe && pick >= 0 && pick != s.locked_row &&
                static_cast<int>(s.stable[pick]) >
                    static_cast<int>(s.stable[s.locked_row]) + kMigrateMargin) {
                s.locked_row = pick;
                source_row = pick;
            }
        } else if (s.locked_row >= 0 && ++s.locked_miss <= kLockMissLimit) {
            source_row = -1;
        } else {
            s.locked_row = pick;
            s.locked_miss = 0;
            source_row = pick;
        }
        reported_row = s.locked_row;
    }

    g.detected_row.store(reported_row, std::memory_order_relaxed);
    g.qualify_count.store(s.total_qualifying, std::memory_order_relaxed);

    const auto now = std::chrono::steady_clock::now();
    if (now - s.last_log >= std::chrono::seconds(1)) {
        s.last_log = now;
        LOG_INFO(Render,
                 "Hoenn GPU cam: row={} applying={} qualifying={} live={} stable={} mode={} "
                 "probe={} yaw={:.1f} pitch={:.1f} d={:.0f} transpose={} invert={}",
                 reported_row, source_row, s.total_qualifying, live, best_stable, mode, probe,
                 yaw_deg, pitch_deg, radius, transpose, invert);
    }

    if (source_row < 0) {
        return;
    }
    if (!probe && std::fabs(yaw_deg) < 0.01f && std::fabs(pitch_deg) < 0.01f) {
        return;
    }

    // World-up in eye space, recovered from the source matrix. Row layout: column 1 of
    // the 3x3, i.e. (m[0].y, m[1].y, m[2].y). Column layout: the second stored vector.
    const Common::Vec4f& m0 = f[static_cast<size_t>(source_row)];
    const Common::Vec4f& m1 = f[static_cast<size_t>(source_row) + 1];
    const Common::Vec4f& m2 = f[static_cast<size_t>(source_row) + 2];
    float ux, uy, uz;
    if (transpose) {
        ux = m1.x;
        uy = m1.y;
        uz = m1.z;
    } else {
        ux = m0.y;
        uy = m1.y;
        uz = m2.y;
    }
    if (!std::isfinite(ux) || !std::isfinite(uy) || !std::isfinite(uz) ||
        (ux * ux + uy * uy + uz * uz) < 0.25f) {
        ux = 0.0f;
        uy = 1.0f;
        uz = 0.0f;
    }

    // Positive stick-right yaws the camera right, which means rotating the world left.
    const Mat3 r_yaw = AxisAngle(ux, uy, uz, -yaw_deg * kDegToRad);
    const Mat3 r_pitch = AxisAngle(1.0f, 0.0f, 0.0f, pitch_deg * kDegToRad);
    const Mat3 rot = Mul(r_pitch, r_yaw);

    // Orbit about p = (0, 0, -d): trans = p - R*p.
    const float tr[3] = {radius * rot.m[0][2], radius * rot.m[1][2],
                         radius * rot.m[2][2] - radius};

    if (apply_all) {
        int next_free = 0;
        for (int ci = 0; ci < s.candidate_count; ++ci) {
            if (!s.candidate_live[ci]) {
                continue; // did not qualify on this upload
            }
            const int r = s.candidates[ci];
            if (r < next_free) {
                continue; // overlapping triples would be transformed twice
            }
            next_free = r + 3;
            if (transpose) {
                ApplyColumns(f, r, rot);
            } else {
                ApplyRows(f, r, rot, tr);
            }
        }
    } else if (transpose) {
        ApplyColumns(f, source_row, rot);
    } else {
        ApplyRows(f, source_row, rot, tr);
    }
}

} // namespace Hoenn::GpuCam
