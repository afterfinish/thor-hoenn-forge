// Copyright Hoenn Forge — ORAS hardcore-nuzlocke cap ladder
package org.citra.citra_emu.hoennforge

/**
 * The standard ORAS hardcore-nuzlocke ladder: the cap in force *before* each boss, so
 * stage 0 is the run up to Roxanne.
 *
 * This mirrors kCaps in `core/hoenn_levelcap.cpp`. The native side is authoritative —
 * it is the one doing the clamping — and this copy exists so the UI can name the stages
 * without a JNI round trip per row. Keep the two in step.
 */
object LevelCapLadder {
    data class Stage(val boss: String, val cap: Int)

    val STAGES = listOf(
        Stage("Roxanne", 14),
        Stage("Brawly", 16),
        Stage("Wattson", 21),
        Stage("Flannery", 28),
        Stage("Norman", 30),
        Stage("Winona", 35),
        Stage("Tate & Liza", 45),
        Stage("Wallace", 46),
        Stage("Sidney", 52),
        Stage("Phoebe", 53),
        Stage("Glacia", 54),
        Stage("Drake", 55),
        Stage("Steven", 59),
    )

    val LAST = STAGES.size - 1

    fun clamp(stage: Int): Int = stage.coerceIn(0, LAST)

    fun capFor(stage: Int): Int = STAGES[clamp(stage)].cap

    fun bossFor(stage: Int): String = STAGES[clamp(stage)].boss

    /** "Lv 14 → 16 → 21 → … → 59", for the onboarding card. */
    fun summary(): String = STAGES.joinToString(" → ", prefix = "Lv ") { it.cap.toString() }

    /** "Roxanne — Lv 14", for the quick menu and the stage picker. */
    fun label(stage: Int): String {
        val s = STAGES[clamp(stage)]
        return "${s.boss} — Lv ${s.cap}"
    }
}
