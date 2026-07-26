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
//   The yaw axis is world-up expressed in eye space, recovered from the reference
//   matrix (column 1 of its 3x3). Yawing about the raw eye-space Y axis would roll the
//   horizon, because the game camera is pitched down. Pitch is about eye-space X, which
//   is screen-horizontal by construction.
//
// All of the above is confirmed on device: a fixed 12-degree yaw visibly transforms real
// Route 104 geometry. What the first device run also proved is that *coherence* is the
// whole remaining problem, so three rules now govern this file.
//
//   1. O must be identical for every draw in a frame. It is built from a cached up-axis
//      belonging to one locked reference row, never from whichever row happens to
//      qualify on the current upload. The first version rebuilt O per upload from a
//      flapping row, which is why Route 104 came apart: draws in the same frame received
//      different rotations, and the terrain was flung off screen while the props stayed.
//   2. Nothing may be capped. The first version tracked at most 12 candidate rows while
//      21 qualified, so a changing subset of the scene was transformed and the rest was
//      not. All 94 triples are now swept on every upload.
//   3. Selection is decided over a window of draws, not per upload, and the lock is
//      sticky. A row wins because most draws in the window carried it *and* it never
//      changed; it is only displaced after being cold for many consecutive windows.

#include "video_core/hoenn_gpu_cam.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "common/logging/log.h"

namespace Hoenn::GpuCam {
namespace {

constexpr int kRows = 96;
constexpr int kLastTriple = kRows - 3; // valid triple starts are 0..93
constexpr int kNumTriples = kLastTriple + 1;

// A "window" stands in for one frame's worth of draws: SetFromRegs has no frame boundary
// to hook, so the window closes after this many uniform uploads or after kWindowMaxTime,
// whichever comes first. Scores are ratios, so only the order of magnitude matters.
constexpr u32 kWindowUploads = 512;
constexpr int kClockCheckInterval = 64;
constexpr auto kWindowMaxTime = std::chrono::milliseconds(200);

// Thresholds, as 1/256ths of the window's upload count.
constexpr u32 kAcquireHitFrac = 128; // a candidate must appear in >= 50% of uploads
constexpr int kColdWindowsToSwitch = 4;

// The driver parks the GPU camera on map transitions, quiet windows and battles.
// Re-acquiring from scratch on every resume is what makes the choice oscillate, so a
// park shorter than this keeps the lock and the window statistics.
constexpr auto kResumeGrace = std::chrono::seconds(8);

// Orthonormality tolerances. Generous enough for f24 -> f32 rounding.
constexpr float kLenSqTol = 0.04f;
constexpr float kDotTol = 0.02f;
constexpr float kMinAbsDet = 0.90f;

constexpr float kDegToRad = 0.017453292519943295f;

struct Shared {
    std::atomic<bool> active{false};
    std::atomic<bool> probe{false};
    std::atomic<int> row_mode{kRowModeAll};
    std::atomic<bool> transpose{false};
    std::atomic<float> radius{kDefaultRadius};
    std::atomic<float> yaw{0.0f};
    std::atomic<float> pitch{0.0f};
    std::atomic<int> detected_row{-1};
    std::atomic<int> qualify_count{0};
    std::atomic<int> invert{0};
    std::atomic<int> pivot_mode{kPivotMeasured};
};
Shared g;

// Scan state. Touched only from the thread that issues draws (Azahar has no separate GPU
// thread — the rasterizer runs on the emulation thread), so no locking here.
struct Scan {
    // Current window.
    u32 hits[kNumTriples]{};
    u32 changes[kNumTriples]{};
    u32 translated[kNumTriples]{}; // hits whose .w column was a real translation
    float prev[kNumTriples][12]{};
    bool prev_valid[kNumTriples]{};
    u32 window_uploads = 0;
    // Eye-space distance of the objects being drawn, sampled from per-object matrices
    // (the ones that change every upload). This is the only honest source for the orbit
    // radius: the value in the game's camera object is in some other unit entirely.
    double depth_sum = 0.0;
    u32 depth_n = 0;
    float depth_min = 0.0f;
    float depth_max = 0.0f;
    /// Mean eye-space translation *vector* of those same matrices. The magnitude alone is
    /// not enough to place a pivot: ORAS looks down at the world from a tilted overhead
    /// camera, so the things being drawn sit forward *and* below the eye. A pivot at
    /// (0, 0, +-|t|) is therefore thousands of units above the player, and orbiting about
    /// it swings the player out of frame whichever sign is used. The direction is the
    /// missing half of the answer.
    double pivot_sum[3]{};
    int clock_countdown = kClockCheckInterval;
    std::chrono::steady_clock::time_point window_start{};

    // Last closed window. Selection and the census read only this.
    u32 last_hits[kNumTriples]{};
    u32 last_changes[kNumTriples]{};
    u32 last_translated[kNumTriples]{};
    u32 last_uploads = 0;
    u32 steady_rows = 0;
    float last_depth_mean = 0.0f;
    float last_depth_min = 0.0f;
    float last_depth_max = 0.0f;
    float last_pivot[3]{};
    bool last_pivot_valid = false;

    int locked_row = -1;
    int cold_windows = 0;

    /// Rows that "all qualifying triples" mode transforms, decided once per window from
    /// last_hits rather than per upload.
    ///
    /// Deciding per upload tore the player model apart. Rows 37..70 on a 3-stride are the
    /// character's bone palette, and each only passes the orthonormality test on roughly
    /// 200-230 of 512 uploads — a bone in a scaled or degenerate pose fails it. So every
    /// frame a different subset of bones was rotated and the rest stayed put, and the mesh
    /// stretched between them. Rotating *all* the bones rigidly rotates the character,
    /// which is correct; rotating a varying subset is what breaks it. Membership therefore
    /// has to be a property of the row over time, not of the row this instant.
    bool apply_set[kNumTriples]{};

    // World-up in eye space, taken from the locked row. Held across uploads where that
    // row does not qualify, so O stays identical for every draw in the frame.
    float up[3]{0.0f, 1.0f, 0.0f};
    bool up_valid = false;

    bool was_active = false;
    std::chrono::steady_clock::time_point parked_at{};
    std::chrono::steady_clock::time_point last_log{};
};
Scan s;

void ResetScan() {
    std::memset(s.hits, 0, sizeof(s.hits));
    std::memset(s.changes, 0, sizeof(s.changes));
    std::memset(s.translated, 0, sizeof(s.translated));
    std::memset(s.prev_valid, 0, sizeof(s.prev_valid));
    std::memset(s.last_hits, 0, sizeof(s.last_hits));
    std::memset(s.last_changes, 0, sizeof(s.last_changes));
    std::memset(s.last_translated, 0, sizeof(s.last_translated));
    std::memset(s.apply_set, 0, sizeof(s.apply_set));
    s.depth_sum = 0.0;
    s.depth_n = 0;
    s.depth_min = 0.0f;
    s.depth_max = 0.0f;
    s.pivot_sum[0] = s.pivot_sum[1] = s.pivot_sum[2] = 0.0;
    s.last_pivot[0] = s.last_pivot[1] = s.last_pivot[2] = 0.0f;
    s.last_pivot_valid = false;
    s.last_uploads = 0;
    s.steady_rows = 0;
    s.window_uploads = 0;
    s.clock_countdown = kClockCheckInterval;
    s.locked_row = -1;
    s.cold_windows = 0;
    s.up_valid = false;
    s.up[0] = 0.0f;
    s.up[1] = 1.0f;
    s.up[2] = 0.0f;
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

/// Ordered so the cheapest test that rejects most rows runs first: this now runs for all
/// 94 triples on every uniform upload, on data that is already in L1.
bool Qualifies(const std::array<Common::Vec4f, kRows>& f, int r) {
    const Common::Vec4f& a = f[static_cast<size_t>(r)];
    const float la = a.x * a.x + a.y * a.y + a.z * a.z;
    if (!(std::fabs(la - 1.0f) <= kLenSqTol)) {
        return false; // inverted comparison, so NaN falls out here too
    }
    const Common::Vec4f& b = f[static_cast<size_t>(r) + 1];
    const float lb = b.x * b.x + b.y * b.y + b.z * b.z;
    if (!(std::fabs(lb - 1.0f) <= kLenSqTol)) {
        return false;
    }
    const Common::Vec4f& c = f[static_cast<size_t>(r) + 2];
    const float lc = c.x * c.x + c.y * c.y + c.z * c.z;
    if (!(std::fabs(lc - 1.0f) <= kLenSqTol)) {
        return false;
    }
    if (!Finite3(a) || !Finite3(b) || !Finite3(c)) {
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

/// Close the window: promote the accumulators, then re-run selection.
///
/// The view matrix is the triple that (a) most draws in the window carried and (b) never
/// changed while they did. Ranking on "constant hits" separates it from a per-object
/// world-view matrix, which scores just as many hits but changes on nearly every one,
/// and from a transient bone or prop matrix, which fails the hit threshold outright.
void CloseWindow(std::chrono::steady_clock::time_point now) {
    std::memcpy(s.last_hits, s.hits, sizeof(s.hits));
    std::memcpy(s.last_changes, s.changes, sizeof(s.changes));
    std::memcpy(s.last_translated, s.translated, sizeof(s.translated));
    s.last_uploads = s.window_uploads;
    s.last_depth_mean =
        s.depth_n ? static_cast<float>(s.depth_sum / static_cast<double>(s.depth_n)) : 0.0f;
    s.last_depth_min = s.depth_min;
    s.last_depth_max = s.depth_max;
    if (s.depth_n) {
        const double n = static_cast<double>(s.depth_n);
        s.last_pivot[0] = static_cast<float>(s.pivot_sum[0] / n);
        s.last_pivot[1] = static_cast<float>(s.pivot_sum[1] / n);
        s.last_pivot[2] = static_cast<float>(s.pivot_sum[2] / n);
        s.last_pivot_valid = true;
    }
    s.pivot_sum[0] = s.pivot_sum[1] = s.pivot_sum[2] = 0.0;
    std::memset(s.hits, 0, sizeof(s.hits));
    std::memset(s.changes, 0, sizeof(s.changes));
    std::memset(s.translated, 0, sizeof(s.translated));
    s.depth_sum = 0.0;
    s.depth_n = 0;
    s.depth_min = 0.0f;
    s.depth_max = 0.0f;
    s.window_uploads = 0;
    s.window_start = now;

    if (s.last_uploads == 0) {
        return;
    }

    const u32 acquire_floor = (s.last_uploads * kAcquireHitFrac) / 256;

    // Membership for "all qualifying triples", fixed for the coming window. The bar is
    // deliberately low: a bone that qualified on 40% of uploads is a real transform that
    // happens to fail the rigidity test in some poses, and it must be rotated together
    // with its siblings or the mesh tears. A genuine one-off — a prop drawn for a single
    // frame — sits far below this and stays out.
    const u32 member_floor = std::max<u32>(1, s.last_uploads / 8);
    for (int r = 0; r < kNumTriples; ++r) {
        s.apply_set[r] = s.last_hits[r] >= member_floor;
    }

    // Rank: constant across the window first, then "carries a real translation", then
    // raw hit count. The second key matters because a view matrix and the normal matrix
    // derived from it are both perfectly constant and both orthonormal — they differ
    // only in that the normal matrix has a zero .w column. Route 104 showed exactly that
    // pair (rows 75 and 90, both 512 hits and 0 changes).
    int best = -1;
    u32 best_const = 0;
    int best_translated = 0;
    u32 best_hits = 0;
    u32 steady = 0;
    for (int r = 0; r < kNumTriples; ++r) {
        const u32 h = s.last_hits[r];
        if (h == 0 || h < acquire_floor) {
            continue;
        }
        ++steady;
        const u32 constant = h - std::min(h, s.last_changes[r]);
        const int translated = (s.last_translated[r] * 2 >= h) ? 1 : 0;
        const bool better =
            best < 0 || constant > best_const ||
            (constant == best_const &&
             (translated > best_translated || (translated == best_translated && h > best_hits)));
        if (better) {
            best = r;
            best_const = constant;
            best_translated = translated;
            best_hits = h;
        }
    }
    s.steady_rows = steady;

    if (s.locked_row < 0) {
        s.locked_row = best;
        s.cold_windows = 0;
        if (best >= 0) {
            LOG_INFO(Render, "Hoenn GPU cam: locked row {} ({} hits, {} constant, of {} uploads)",
                     best, best_hits, best_const, s.last_uploads);
        }
        return;
    }

    // Sticky, but not stubborn. The first attempt only dropped a lock once the row was
    // nearly absent, which let a stale row acquired in another scene survive on a
    // trickle of hits while three far better rows were ignored — Route 104 sat locked on
    // row 28 while rows 75 and 90 were carried by every single draw. The locked row must
    // now stay a legitimate acquisition candidate, and it gets several windows to.
    if (s.last_hits[s.locked_row] < acquire_floor) {
        ++s.cold_windows;
    } else {
        s.cold_windows = 0;
    }
    if (s.cold_windows >= kColdWindowsToSwitch && best >= 0 && best != s.locked_row) {
        LOG_INFO(Render,
                 "Hoenn GPU cam: lock {} fell out of the steady set for {} windows, "
                 "switching to {} ({} hits, {} constant, translated={})",
                 s.locked_row, s.cold_windows, best, best_hits, best_const, best_translated);
        s.locked_row = best;
        s.cold_windows = 0;
        s.up_valid = false;
    }
}

/// One line naming every row that a meaningful share of draws carried in the last
/// window, most-used first, as `row:hits/changes` with a trailing `T` when the row
/// carries a real translation. The view matrix is the row with many hits, zero changes
/// and a T; the same row without a T is the normal matrix derived from it. A per-object
/// world-view matrix has many hits and almost as many changes. This is meant to be read straight off logcat instead of sweeping rows
/// by hand. Destroys last_hits, which is rebuilt every window anyway.
void LogCensus() {
    char buf[512];
    int n = 0;
    int listed = 0;
    const u32 floor = (s.last_uploads * 24) / 256; // carried by >= ~9% of draws

    while (listed < 16 && n < static_cast<int>(sizeof(buf)) - 32) {
        int pick = -1;
        u32 pick_hits = 0;
        for (int r = 0; r < kNumTriples; ++r) {
            const u32 h = s.last_hits[r];
            if (h == 0 || h <= floor) {
                continue;
            }
            if (pick < 0 || h > pick_hits) {
                pick = r;
                pick_hits = h;
            }
        }
        if (pick < 0) {
            break;
        }
        n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), "%s%d:%u/%u%s",
                           listed ? " " : "", pick, pick_hits,
                           std::min(pick_hits, s.last_changes[pick]),
                           (s.last_translated[pick] * 2 >= pick_hits) ? "T" : "");
        s.last_hits[pick] = 0; // consumed
        ++listed;
    }
    if (listed == 0) {
        std::snprintf(buf, sizeof(buf), "(none)");
    }
    LOG_INFO(Render, "Hoenn GPU cam census over {} uploads, row:hits/changes = {}",
             s.last_uploads, buf);
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
        // Zero is meaningful and safe: it swivels about the eye instead of orbiting the
        // player, so no amount of picking the wrong matrix can fling geometry away.
        g.radius.store(std::clamp(value, 0.0f, 100000.0f), std::memory_order_relaxed);
        LOG_INFO(Render, "Hoenn GPU cam: radius={}", value);
        break;
    case ParamInvert: {
        const int bits = static_cast<int>(value) & (kInvertYaw | kInvertPitch);
        g.invert.store(bits, std::memory_order_relaxed);
        LOG_INFO(Render, "Hoenn GPU cam: invert={}", bits);
        break;
    }
    case ParamPivotMode: {
        int mode = static_cast<int>(value);
        if (mode < 0 || mode >= kPivotModeCount) {
            mode = kPivotMeasured;
        }
        g.pivot_mode.store(mode, std::memory_order_relaxed);
        LOG_INFO(Render, "Hoenn GPU cam: pivot mode={}", mode);
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
    case ParamPivotMode:
        return static_cast<float>(g.pivot_mode.load(std::memory_order_relaxed));
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
            s.parked_at = std::chrono::steady_clock::now();
            g.qualify_count.store(0, std::memory_order_relaxed);
        }
        return;
    }
    if (!s.was_active) {
        s.was_active = true;
        // Keep the lock across a brief park. The driver disables us on every map
        // transition, quiet window and battle, and re-sweeping on each resume is exactly
        // what made the choice oscillate between rows 3 and 28 on Route 104.
        const auto now = std::chrono::steady_clock::now();
        if (s.locked_row < 0 || now - s.parked_at > kResumeGrace) {
            ResetScan();
        }
        s.window_start = now;
        s.clock_countdown = kClockCheckInterval;
    }

    const int invert = g.invert.load(std::memory_order_relaxed);
    const float yaw_sign = (invert & kInvertYaw) ? -1.0f : 1.0f;
    const float pitch_sign = (invert & kInvertPitch) ? -1.0f : 1.0f;

    const bool probe = g.probe.load(std::memory_order_relaxed);
    const float yaw_deg =
        yaw_sign * (probe ? kProbeYawDeg : g.yaw.load(std::memory_order_relaxed));
    const float pitch_deg = probe ? 0.0f : pitch_sign * g.pitch.load(std::memory_order_relaxed);
    const int mode = g.row_mode.load(std::memory_order_relaxed);
    const bool transpose = g.transpose.load(std::memory_order_relaxed);

    // Radius <= 0 means "measure it". The camera object's own distance field (+0xA8) is on
    // a different scale entirely from the vertex data — using its ~2300 threw Route 104
    // about three screens off centre. The eye-space depth of the per-object matrices is the
    // honest source, and it is already being sampled every window: Petalburg Woods reports
    // mean 4943 (min 4926, max 5165). Reading it live also tracks maps with a different
    // camera distance instead of baking in one map's number as the next wrong constant.
    const float radius_pref = g.radius.load(std::memory_order_relaxed);
    const float radius = radius_pref > 0.0f ? radius_pref : s.last_depth_mean;

    // --- Sweep every triple, every upload. No candidate list, no cap. ------------------
    bool live_now[kNumTriples];
    int live = 0;
    for (int r = 0; r < kNumTriples; ++r) {
        if (!Qualifies(f, r)) {
            live_now[r] = false;
            continue;
        }
        live_now[r] = true;
        ++live;
        ++s.hits[r];
        float cur[12];
        ReadTriple(f, r, cur);
        // prev is deliberately kept across uploads where the row did not qualify, so a
        // row that flickers out to a non-rigid value and back still counts as changing.
        const bool changed = s.prev_valid[r] && !SameTriple(s.prev[r], cur);
        if (changed) {
            ++s.changes[r];
        }
        std::memcpy(s.prev[r], cur, sizeof(cur));
        s.prev_valid[r] = true;

        const float t2 = cur[3] * cur[3] + cur[7] * cur[7] + cur[11] * cur[11];
        if (t2 > 1.0e-4f) {
            ++s.translated[r];
            // Only per-object matrices tell us how far away the things being drawn are;
            // a view matrix's translation is the distance to the world origin, which is
            // arbitrary. Sample the ones that change every upload.
            if (changed) {
                const float t = std::sqrt(t2);
                s.depth_sum += static_cast<double>(t);
                s.pivot_sum[0] += static_cast<double>(cur[3]);
                s.pivot_sum[1] += static_cast<double>(cur[7]);
                s.pivot_sum[2] += static_cast<double>(cur[11]);
                ++s.depth_n;
                if (s.depth_min == 0.0f || t < s.depth_min) {
                    s.depth_min = t;
                }
                if (t > s.depth_max) {
                    s.depth_max = t;
                }
            }
        }
    }

    // --- Window bookkeeping ------------------------------------------------------------
    ++s.window_uploads;
    bool check_clock = s.window_uploads >= kWindowUploads;
    if (!check_clock && --s.clock_countdown <= 0) {
        s.clock_countdown = kClockCheckInterval;
        check_clock = true;
    }
    if (check_clock) {
        const auto now = std::chrono::steady_clock::now();
        if (s.window_uploads >= kWindowUploads || now - s.window_start >= kWindowMaxTime) {
            CloseWindow(now);
        }
        if (now - s.last_log >= std::chrono::seconds(1)) {
            s.last_log = now;
            LOG_INFO(Render,
                     "Hoenn GPU cam: locked={} cold={} steady={} live={} mode={} probe={} "
                     "yaw={:.1f} pitch={:.1f} d={:.0f} transpose={} invert={} | pivotmode={} "
                     "pivot=({:.0f}, {:.0f}, {:.0f}) | eye depth mean={:.0f} min={:.0f} "
                     "max={:.0f}",
                     s.locked_row, s.cold_windows, s.steady_rows, live, mode, probe, yaw_deg,
                     pitch_deg, radius, transpose, invert,
                     g.pivot_mode.load(std::memory_order_relaxed), s.last_pivot[0],
                     s.last_pivot[1], s.last_pivot[2], s.last_depth_mean, s.last_depth_min,
                     s.last_depth_max);
            LogCensus();
        }
    }

    g.detected_row.store(s.locked_row, std::memory_order_relaxed);
    g.qualify_count.store(live, std::memory_order_relaxed);

    // --- Reference row: the single source of O ------------------------------------------
    const int ref_row = (mode >= 0 && mode <= kLastTriple) ? mode : s.locked_row;
    if (ref_row >= 0 && ref_row <= kLastTriple && live_now[ref_row]) {
        const Common::Vec4f& m0 = f[static_cast<size_t>(ref_row)];
        const Common::Vec4f& m1 = f[static_cast<size_t>(ref_row) + 1];
        const Common::Vec4f& m2 = f[static_cast<size_t>(ref_row) + 2];
        const float ux = transpose ? m1.x : m0.y;
        const float uy = m1.y;
        const float uz = transpose ? m1.z : m2.y;
        if (std::isfinite(ux) && std::isfinite(uy) && std::isfinite(uz) &&
            (ux * ux + uy * uy + uz * uz) > 0.25f) {
            s.up[0] = ux;
            s.up[1] = uy;
            s.up[2] = uz;
            s.up_valid = true;
        }
    }

    if (ref_row < 0 || !s.up_valid) {
        return; // no trustworthy axis to rotate about yet
    }
    if (!probe && std::fabs(yaw_deg) < 0.01f && std::fabs(pitch_deg) < 0.01f) {
        return;
    }

    // Positive stick-right yaws the camera right, which means rotating the world left.
    const Mat3 r_yaw = AxisAngle(s.up[0], s.up[1], s.up[2], -yaw_deg * kDegToRad);
    const Mat3 r_pitch = AxisAngle(1.0f, 0.0f, 0.0f, pitch_deg * kDegToRad);
    const Mat3 rot = Mul(r_pitch, r_yaw);

    // Orbit about the pivot p, as trans = p - R*p. p == 0 degenerates to a pure swivel
    // about the eye, which cannot displace geometry however wrong everything else is.
    //
    // Where p goes is a convention that cannot be read off the uniforms, and both
    // (0, 0, -d) and (0, 0, +d) were wrong on device: at d ~4950 a 12 deg yaw threw the
    // whole scene off screen either way. The reason is that d is a *distance* while a
    // pivot is a *point*. ORAS looks down from a tilted overhead camera, so the drawn
    // objects sit forward and below the eye — roughly (0, -3500, -3500) for |t| ~4950 —
    // and a pivot on the Z axis at that distance is thousands of units above the player.
    // PivotMeasured uses the sampled mean translation vector instead, which carries the
    // direction as well as the distance. The axis modes are kept so the convention can
    // still be falsified by hand.
    float p[3]{};
    switch (g.pivot_mode.load(std::memory_order_relaxed)) {
    case kPivotMeasured:
        if (s.last_pivot_valid) {
            const float len = std::sqrt(s.last_pivot[0] * s.last_pivot[0] +
                                        s.last_pivot[1] * s.last_pivot[1] +
                                        s.last_pivot[2] * s.last_pivot[2]);
            // A positive radius rescales the measured direction, so the knob stays a
            // magnitude override rather than becoming a second, conflicting pivot.
            const float k = (radius_pref > 0.0f && len > 1.0e-3f) ? radius_pref / len : 1.0f;
            p[0] = s.last_pivot[0] * k;
            p[1] = s.last_pivot[1] * k;
            p[2] = s.last_pivot[2] * k;
        }
        break;
    case kPivotForwardPos:
        p[2] = radius;
        break;
    case kPivotForwardNeg:
        p[2] = -radius;
        break;
    case kPivotNone:
    default:
        break;
    }

    const float tr[3] = {
        p[0] - (rot.m[0][0] * p[0] + rot.m[0][1] * p[1] + rot.m[0][2] * p[2]),
        p[1] - (rot.m[1][0] * p[0] + rot.m[1][1] * p[1] + rot.m[1][2] * p[2]),
        p[2] - (rot.m[2][0] * p[0] + rot.m[2][1] * p[1] + rot.m[2][2] * p[2]),
    };

    if (mode >= 0 && mode <= kLastTriple) {
        if (transpose) {
            ApplyColumns(f, mode, rot);
        } else {
            ApplyRows(f, mode, rot, tr);
        }
    } else if (mode == kRowModeAll) {
        int next_free = 0;
        for (int r = 0; r < kNumTriples; ++r) {
            // Membership comes from the closed window, not from live_now. Gating on
            // whether the row happens to qualify *this* upload is what tore the player
            // model into a spike: the bone palette only passes orthonormality on ~40% of
            // uploads, so a different subset of bones moved each frame. Until the first
            // window closes apply_set is empty, so fall back to live_now to stay useful.
            const bool member = s.last_uploads ? s.apply_set[r] : live_now[r];
            if (!member || r < next_free) {
                continue; // overlapping triples would be transformed twice
            }
            // Still refuse to write over anything non-finite — cheap, and membership says
            // nothing about what this particular upload put in the row.
            if (!Finite3(f[static_cast<size_t>(r)]) || !Finite3(f[static_cast<size_t>(r) + 1]) ||
                !Finite3(f[static_cast<size_t>(r) + 2])) {
                continue;
            }
            next_free = r + 3;
            if (transpose) {
                ApplyColumns(f, r, rot);
            } else {
                ApplyRows(f, r, rot, tr);
            }
        }
    } else if (live_now[ref_row]) {
        if (transpose) {
            ApplyColumns(f, ref_row, rot);
        } else {
            ApplyRows(f, ref_row, rot, tr);
        }
    }
}

} // namespace Hoenn::GpuCam
