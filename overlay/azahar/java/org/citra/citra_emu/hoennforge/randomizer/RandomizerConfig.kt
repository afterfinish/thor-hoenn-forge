// Copyright Hoenn Forge
package org.citra.citra_emu.hoennforge.randomizer

import org.json.JSONObject
import kotlin.random.Random

/**
 * pk3DS-class randomizer options selected during onboarding.
 * Serializable to JSON for prepare pipeline and Home display.
 *
 * UI exposes all modules; engine ships by phase (see docs/randomizer-options.md).
 */
data class RandomizerConfig(
    val enabled: Boolean = false,
    val seed: Long = Random.nextLong(0, Long.MAX_VALUE),
    val preset: Preset = Preset.NONE,
    // Wild encounters
    val wildSpecies: Boolean = false,
    val wildLevels: Boolean = false,
    val wildLegendaries: Boolean = false,
    val wildTypeTheme: TypeTheme = TypeTheme.NONE,
    // Trainers
    val trainerParties: Boolean = false,
    val trainerItems: Boolean = false,
    val trainerMoves: Boolean = false,
    val trainerAbilities: Boolean = false,
    val trainerDifficulty: Difficulty = Difficulty.SIMILAR,
    // Starters
    val starterMode: StarterMode = StarterMode.VANILLA,
    // Personal (advanced)
    val personalTypes: Boolean = false,
    val personalBaseStats: Boolean = false,
    val personalAbilities: Boolean = false,
    val personalTmCompat: Boolean = false,
    // Moves & TMs (advanced)
    val moveTypes: Boolean = false,
    val moveCategories: Boolean = false,
    val levelUpLearnsets: Boolean = false,
    val eggMoves: Boolean = false,
    val tmList: Boolean = false,
    // Evolutions / misc (advanced)
    val evolutions: Boolean = false,
    val specialMarts: Boolean = false,
    val staticGifts: Boolean = false,
) {
    enum class Preset { NONE, LIGHT, STANDARD, CHAOS }
    enum class TypeTheme { NONE, MONO, DUAL }
    enum class Difficulty { WEAKER, SIMILAR, STRONGER, RIVAL_PLUS }
    enum class StarterMode { VANILLA, FULL_RANDOM, GEN_LIMITED, TYPE_BALANCED }

    fun toJson(): JSONObject = JSONObject().apply {
        put("enabled", enabled)
        put("seed", seed)
        put("preset", preset.name)
        put("wildSpecies", wildSpecies)
        put("wildLevels", wildLevels)
        put("wildLegendaries", wildLegendaries)
        put("wildTypeTheme", wildTypeTheme.name)
        put("trainerParties", trainerParties)
        put("trainerItems", trainerItems)
        put("trainerMoves", trainerMoves)
        put("trainerAbilities", trainerAbilities)
        put("trainerDifficulty", trainerDifficulty.name)
        put("starterMode", starterMode.name)
        put("personalTypes", personalTypes)
        put("personalBaseStats", personalBaseStats)
        put("personalAbilities", personalAbilities)
        put("personalTmCompat", personalTmCompat)
        put("moveTypes", moveTypes)
        put("moveCategories", moveCategories)
        put("levelUpLearnsets", levelUpLearnsets)
        put("eggMoves", eggMoves)
        put("tmList", tmList)
        put("evolutions", evolutions)
        put("specialMarts", specialMarts)
        put("staticGifts", staticGifts)
    }

    fun toJsonString(): String = toJson().toString()

    fun seedDisplay(): String = seed.toString()

    fun modeLabel(): String = when {
        !enabled -> "Vanilla"
        preset == Preset.LIGHT -> "Randomized · Light"
        preset == Preset.STANDARD -> "Randomized · Standard"
        preset == Preset.CHAOS -> "Randomized · Chaos"
        else -> "Randomized · Custom"
    }

    fun hasSoftlockWarnings(): Boolean =
        enabled && (wildLegendaries || evolutions || preset == Preset.CHAOS)

    fun summaryLines(): List<String> {
        if (!enabled) return listOf("Vanilla (no randomizer)")
        val lines = mutableListOf("Seed: $seed", "Preset: ${preset.name}")
        if (wildSpecies || wildLevels) {
            lines += "Wilds: species=$wildSpecies levels=$wildLevels " +
                "legends=$wildLegendaries theme=${wildTypeTheme.name}"
        }
        if (trainerParties || trainerItems || trainerMoves || trainerAbilities) {
            lines += "Trainers: parties=$trainerParties items=$trainerItems " +
                "moves=$trainerMoves abilities=$trainerAbilities " +
                "difficulty=${trainerDifficulty.name}"
        }
        if (starterMode != StarterMode.VANILLA) {
            lines += "Starters: ${starterMode.name}"
        }
        if (personalTypes || personalBaseStats || personalAbilities || personalTmCompat) {
            lines += "Personal: types=$personalTypes stats=$personalBaseStats " +
                "abilities=$personalAbilities tmCompat=$personalTmCompat"
        }
        if (moveTypes || moveCategories || levelUpLearnsets || eggMoves || tmList) {
            lines += "Moves/TMs: types=$moveTypes cats=$moveCategories " +
                "levelUp=$levelUpLearnsets egg=$eggMoves tmList=$tmList"
        }
        if (evolutions) lines += "Evolutions: ON (softlock risk)"
        if (specialMarts || staticGifts) {
            lines += "Misc: marts=$specialMarts staticGifts=$staticGifts"
        }
        return lines
    }

    companion object {
        fun vanilla(): RandomizerConfig = RandomizerConfig(enabled = false)

        fun newSeed(): Long = Random.nextLong(0, Long.MAX_VALUE)

        fun fromPreset(
            preset: Preset,
            seed: Long = newSeed(),
        ): RandomizerConfig = when (preset) {
            Preset.NONE -> vanilla().copy(seed = seed)
            Preset.LIGHT -> RandomizerConfig(
                enabled = true,
                seed = seed,
                preset = Preset.LIGHT,
                wildSpecies = true,
                wildLevels = true,
                wildLegendaries = false,
                trainerParties = true,
                trainerDifficulty = Difficulty.SIMILAR,
                starterMode = StarterMode.FULL_RANDOM,
            )
            Preset.STANDARD -> RandomizerConfig(
                enabled = true,
                seed = seed,
                preset = Preset.STANDARD,
                wildSpecies = true,
                wildLevels = true,
                wildLegendaries = false,
                trainerParties = true,
                trainerItems = true,
                trainerMoves = true,
                trainerAbilities = true,
                trainerDifficulty = Difficulty.SIMILAR,
                starterMode = StarterMode.FULL_RANDOM,
            )
            Preset.CHAOS -> RandomizerConfig(
                enabled = true,
                seed = seed,
                preset = Preset.CHAOS,
                wildSpecies = true,
                wildLevels = true,
                wildLegendaries = true,
                trainerParties = true,
                trainerItems = true,
                trainerMoves = true,
                trainerAbilities = true,
                trainerDifficulty = Difficulty.STRONGER,
                starterMode = StarterMode.FULL_RANDOM,
                personalTypes = true,
                personalAbilities = true,
                levelUpLearnsets = true,
                evolutions = true,
                specialMarts = true,
            )
        }

        fun fromJson(raw: String?): RandomizerConfig {
            if (raw.isNullOrBlank()) return vanilla()
            return try {
                val o = JSONObject(raw)
                RandomizerConfig(
                    enabled = o.optBoolean("enabled", false),
                    seed = o.optLong("seed", 0L),
                    preset = enumOr(o.optString("preset"), Preset.NONE),
                    wildSpecies = o.optBoolean("wildSpecies"),
                    wildLevels = o.optBoolean("wildLevels"),
                    wildLegendaries = o.optBoolean("wildLegendaries"),
                    wildTypeTheme = enumOr(o.optString("wildTypeTheme"), TypeTheme.NONE),
                    trainerParties = o.optBoolean("trainerParties"),
                    trainerItems = o.optBoolean("trainerItems"),
                    trainerMoves = o.optBoolean("trainerMoves"),
                    trainerAbilities = o.optBoolean("trainerAbilities"),
                    trainerDifficulty = enumOr(
                        o.optString("trainerDifficulty"),
                        Difficulty.SIMILAR,
                    ),
                    starterMode = enumOr(o.optString("starterMode"), StarterMode.VANILLA),
                    personalTypes = o.optBoolean("personalTypes"),
                    personalBaseStats = o.optBoolean("personalBaseStats"),
                    personalAbilities = o.optBoolean("personalAbilities"),
                    personalTmCompat = o.optBoolean("personalTmCompat"),
                    moveTypes = o.optBoolean("moveTypes"),
                    moveCategories = o.optBoolean("moveCategories"),
                    levelUpLearnsets = o.optBoolean("levelUpLearnsets"),
                    eggMoves = o.optBoolean("eggMoves"),
                    tmList = o.optBoolean("tmList"),
                    evolutions = o.optBoolean("evolutions"),
                    specialMarts = o.optBoolean("specialMarts"),
                    staticGifts = o.optBoolean("staticGifts"),
                )
            } catch (_: Exception) {
                vanilla()
            }
        }

        private inline fun <reified T : Enum<T>> enumOr(name: String?, default: T): T {
            if (name.isNullOrBlank()) return default
            return enumValues<T>().firstOrNull { it.name == name } ?: default
        }
    }
}
