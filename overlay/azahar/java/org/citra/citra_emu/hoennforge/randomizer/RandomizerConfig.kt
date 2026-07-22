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

    /** Shareable seed label like HF-7Q4K-92XA (base36). */
    fun seedDisplay(): String {
        val raw = seed.toString(36).uppercase().padStart(8, '0')
        val a = raw.take(4)
        val b = raw.drop(4).take(4).padEnd(4, '0')
        return "HF-$a-$b"
    }

    fun modeLabel(): String = when {
        !enabled -> "Vanilla"
        preset == Preset.LIGHT -> "Randomized · Light"
        preset == Preset.STANDARD -> "Randomized · Standard"
        preset == Preset.CHAOS -> "Randomized · Chaos"
        else -> "Randomized · Custom"
    }

    fun hasSoftlockWarnings(): Boolean =
        enabled && (wildLegendaries || evolutions || preset == Preset.CHAOS)

    fun wildsOn(): Boolean = wildSpecies || wildLevels
    fun trainersOn(): Boolean =
        trainerParties || trainerItems || trainerMoves || trainerAbilities
    fun startersOn(): Boolean = starterMode != StarterMode.VANILLA
    fun personalOn(): Boolean =
        personalTypes || personalBaseStats || personalAbilities || personalTmCompat
    fun movesOn(): Boolean =
        moveTypes || moveCategories || levelUpLearnsets || eggMoves || tmList
    fun evolutionsOn(): Boolean = evolutions
    fun miscOn(): Boolean = specialMarts || staticGifts

    fun modulesOnCount(): Int =
        listOf(wildsOn(), trainersOn(), startersOn(), personalOn(), movesOn(), evolutionsOn(), miscOn())
            .count { it }

    fun wildsBlurb(): String = when {
        !wildsOn() -> "Vanilla"
        else -> buildString {
            if (wildSpecies) append("Random species")
            if (wildLevels) {
                if (isNotEmpty()) append(" · ")
                append("levels")
            }
            if (wildLegendaries) {
                if (isNotEmpty()) append(" · ")
                append("legendaries possible")
            }
            if (wildTypeTheme != TypeTheme.NONE) {
                if (isNotEmpty()) append(" · ")
                append("theme ${wildTypeTheme.name.lowercase()}")
            }
        }
    }

    fun trainersBlurb(): String = when {
        !trainersOn() -> "Vanilla"
        else -> buildString {
            if (trainerParties) append("Random teams")
            if (trainerItems) {
                if (isNotEmpty()) append(" · ")
                append("items")
            } else if (trainerParties) {
                append(" · vanilla items")
            }
            if (trainerMoves) {
                if (isNotEmpty()) append(" · ")
                append("moves")
            }
            if (trainerAbilities) {
                if (isNotEmpty()) append(" · ")
                append("abilities")
            }
            if (isNotEmpty()) append(" · ")
            append(
                when (trainerDifficulty) {
                    Difficulty.WEAKER -> "weaker"
                    Difficulty.SIMILAR -> "similar strength"
                    Difficulty.STRONGER -> "stronger"
                    Difficulty.RIVAL_PLUS -> "rival+"
                },
            )
        }
    }

    fun startersBlurb(): String = when (starterMode) {
        StarterMode.VANILLA -> "Vanilla"
        StarterMode.FULL_RANDOM -> "Fully random"
        StarterMode.GEN_LIMITED -> "Random from limited gens"
        StarterMode.TYPE_BALANCED -> "Type-balanced trio"
    }

    fun personalBlurb(): String =
        if (!personalOn()) "Vanilla" else "Types / stats / abilities / TM"

    fun movesBlurb(): String =
        if (!movesOn()) "Vanilla" else "Learnsets · moves · TMs"

    fun evolutionsBlurb(): String =
        if (!evolutionsOn()) "Vanilla" else "Random chains · softlock risk"

    fun miscBlurb(): String = when {
        !miscOn() -> "Vanilla"
        specialMarts && staticGifts -> "Marts + static gifts"
        specialMarts -> "Special marts"
        else -> "Static gifts"
    }

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
            // Design: Light = wild encounters only — a gentle remix
            Preset.LIGHT -> RandomizerConfig(
                enabled = true,
                seed = seed,
                preset = Preset.LIGHT,
                wildSpecies = true,
                wildLevels = true,
                wildLegendaries = false,
                trainerParties = false,
                starterMode = StarterMode.VANILLA,
            )
            // Design: Standard = wilds, trainers and starters — balanced chaos
            Preset.STANDARD -> RandomizerConfig(
                enabled = true,
                seed = seed,
                preset = Preset.STANDARD,
                wildSpecies = true,
                wildLevels = true,
                wildLegendaries = false,
                trainerParties = true,
                trainerItems = false,
                trainerMoves = false,
                trainerAbilities = false,
                trainerDifficulty = Difficulty.SIMILAR,
                starterMode = StarterMode.TYPE_BALANCED,
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
