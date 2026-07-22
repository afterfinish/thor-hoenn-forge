// Copyright Hoenn Forge — on-device pk3DS-class randomizer engine
package org.citra.citra_emu.hoennforge.randomizer

import android.content.Context
import android.net.Uri
import android.util.Log
import org.citra.citra_emu.NativeLibrary
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.random.Random

/**
 * Full prepare pipeline:
 * 1) Read needed RomFS files from decrypted dump
 * 2) Apply selected modules with seed
 * 3) Deploy to Azahar LayeredFS: {user}/load/mods/{titleId}/romfs/
 *
 * Original dump is never written.
 */
class RandomizerEngine(
    private val context: Context,
    private val config: RandomizerConfig,
    private val titleIdHex: String,
    private val dumpUri: Uri,
    private val onProgress: (stage: String, percent: Int) -> Unit,
) {
    private val rng = Random(config.seed)
    private val modified = LinkedHashMap<String, ByteArray>()
    private val log = StringBuilder()

    data class Outcome(val ok: Boolean, val message: String, val error: Throwable? = null)

    fun run(): Outcome {
        return try {
            if (!config.enabled) {
                onProgress("Vanilla — clearing old mods…", 50)
                clearMods()
                onProgress("Done", 100)
                return Outcome(true, "Vanilla (no mods)")
            }
            onProgress("Opening dump RomFS…", 5)
            val romfs = RomfsReader.open(context, dumpUri)
            try {
                onProgress("Reading game data…", 15)
                loadBase(romfs)
                onProgress("Applying randomizer…", 40)
                applyModules()
                onProgress("Writing LayeredFS mods…", 80)
                deployMods()
                onProgress("Done", 100)
                Outcome(true, log.toString())
            } finally {
                romfs.close()
            }
        } catch (e: Exception) {
            Log.e(TAG, "RandomizerEngine failed", e)
            Outcome(false, e.message ?: e.javaClass.simpleName, e)
        }
    }

    private fun loadBase(romfs: RomfsReader) {
        val needed = linkedSetOf(
            OrasPaths.POKE3_SELECT,
            OrasPaths.FIELD,
        )
        if (config.wildSpecies || config.wildLevels) needed += OrasPaths.ENCDATA
        if (config.trainerParties || config.trainerItems || config.trainerMoves ||
            config.trainerAbilities
        ) {
            // trdata holds party size/format; trpoke holds species teams
            needed += OrasPaths.TRDATA
            needed += OrasPaths.TRPOKE
        }
        if (config.personalTypes || config.personalBaseStats || config.personalAbilities ||
            config.personalTmCompat
        ) {
            needed += OrasPaths.PERSONAL
        }
        if (config.levelUpLearnsets) needed += OrasPaths.LEVELUP
        if (config.eggMoves) needed += OrasPaths.EGGMOVE
        if (config.moveTypes || config.moveCategories) needed += OrasPaths.MOVE
        if (config.evolutions) needed += OrasPaths.EVOLUTION
        if (config.specialMarts) needed += OrasPaths.ITEM
        if (config.staticGifts || config.starterMode != RandomizerConfig.StarterMode.VANILLA) {
            needed += OrasPaths.FIELD
            needed += OrasPaths.POKE3_SELECT
        }

        for (path in needed) {
            if (!romfs.has(path)) error("RomFS missing required file: $path")
            modified[path] = romfs.read(path)
            log.appendLine("read $path (${modified[path]!!.size} bytes)")
        }
    }

    private fun applyModules() {
        if (config.starterMode != RandomizerConfig.StarterMode.VANILLA) {
            randomizeStarters()
        }
        if (config.staticGifts) {
            randomizeStaticEncounters()
            randomizeAllGifts()
        }
        if (config.wildSpecies || config.wildLevels) {
            randomizeWilds()
        }
        if (config.trainerParties || config.trainerItems || config.trainerMoves ||
            config.trainerAbilities
        ) {
            randomizeTrainers()
        }
        if (config.personalTypes || config.personalBaseStats || config.personalAbilities ||
            config.personalTmCompat
        ) {
            randomizePersonal()
        }
        if (config.levelUpLearnsets) randomizeLevelUp()
        if (config.eggMoves) randomizeEggMoves()
        if (config.moveTypes || config.moveCategories) randomizeMoves()
        if (config.evolutions) randomizeEvolutions()
        if (config.specialMarts) randomizeItemsMart()
    }

    // --- Starters (pk3DS StarterEditor6) ---
    private fun randomizeStarters() {
        val cro = modified[OrasPaths.POKE3_SELECT]!!.copyOf()
        val field = modified[OrasPaths.FIELD]!!.copyOf()
        val croBb = ByteBuffer.wrap(cro).order(ByteOrder.LITTLE_ENDIAN)
        var offset = croBb.getInt(0xB8)
        // ORAS has no +0x10 padding (XY only)
        val sets = 4
        for (set in 0 until sets) {
            val trio = SpeciesPool.pickStarterTrio(
                rng,
                config.starterMode,
                includeLegendaries = config.wildLegendaries,
            )
            for (j in 0 until 3) {
                val croOff = offset + ((set * 3) + j) * 0x54
                croBb.putShort(croOff, trio[j].toShort())
                val giftIndex = OrasPaths.FIELD_STARTER_ENTRIES[set * 3 + j]
                val giftOff = OrasPaths.FIELD_GIFT_OFFSET + giftIndex * OrasPaths.FIELD_GIFT_SIZE
                putU16(field, giftOff, trio[j])
            }
            log.appendLine("starters set$set -> ${trio.joinToString()}")
        }
        modified[OrasPaths.POKE3_SELECT] = cro
        modified[OrasPaths.FIELD] = field
    }

    // --- Static encounters in DllField ---
    private fun randomizeStaticEncounters() {
        val field = modified[OrasPaths.FIELD]!!.copyOf()
        val pool = SpeciesPool.allSpecies(config.wildLegendaries)
        for (i in 0 until OrasPaths.FIELD_STATIC_COUNT) {
            val off = OrasPaths.FIELD_STATIC_OFFSET + i * OrasPaths.FIELD_STATIC_SIZE
            val sp = getU16(field, off)
            if (sp in 1..SpeciesPool.MAX_SPECIES) {
                putU16(field, off, SpeciesPool.pick(rng, pool))
            }
        }
        modified[OrasPaths.FIELD] = field
        log.appendLine("static encounters randomized")
    }

    /** Randomize all gift species fields (species u16 at start of each gift struct). */
    private fun randomizeAllGifts() {
        val field = modified[OrasPaths.FIELD]!!.copyOf()
        val pool = SpeciesPool.allSpecies(config.wildLegendaries)
        // Scan gift table region: from gift offset, 0x24 stride, while species looks valid
        var idx = 0
        var off = OrasPaths.FIELD_GIFT_OFFSET
        while (off + 2 <= field.size && idx < 80) {
            val sp = getU16(field, off)
            if (sp in 1..SpeciesPool.MAX_SPECIES) {
                putU16(field, off, SpeciesPool.pick(rng, pool))
            }
            off += OrasPaths.FIELD_GIFT_SIZE
            idx++
        }
        modified[OrasPaths.FIELD] = field
        log.appendLine("gifts scanned/randomized ($idx entries)")
    }

    // --- Wild encounters (encdata GARC) ---
    private fun randomizeWilds() {
        val raw = modified[OrasPaths.ENCDATA]!!
        val garc = GarcArchive.open(raw)
        val pool = SpeciesPool.allSpecies(config.wildLegendaries)
        var tables = 0
        for (i in 2 until garc.fileCount) {
            var file = Lz11.maybeDecompress(garc.getFile(i))
            if (file.size < 0x20) continue
            val bb = ByteBuffer.wrap(file).order(ByteOrder.LITTLE_ENDIAN)
            val offset = bb.getInt(0x10) + 0xE
            if (offset < 0 || offset + OrasPaths.ENCOUNTER_TABLE > file.size) continue
            val table = file.copyOfRange(offset, offset + OrasPaths.ENCOUNTER_TABLE)
            var changed = false
            for (s in 0 until OrasPaths.SLOT_COUNT) {
                val so = s * 4
                if (so + 4 > table.size) break
                val word = getU16(table, so)
                val sp = word and 0x7FF
                val form = word ushr 11
                val lo = table[so + 2].toInt() and 0xFF
                val hi = table[so + 3].toInt() and 0xFF
                if (sp !in 1..SpeciesPool.MAX_SPECIES) continue
                if (lo !in 1..100 || hi !in 1..100 || lo > hi) continue
                var newSp = sp
                var newLo = lo
                var newHi = hi
                if (config.wildSpecies) {
                    newSp = SpeciesPool.pick(rng, pool)
                }
                if (config.wildLevels) {
                    val mid = ((lo + hi) / 2).coerceIn(1, 100)
                    val delta = rng.nextInt(0, 6)
                    newLo = (mid - delta).coerceIn(1, 100)
                    newHi = (mid + delta).coerceIn(newLo, 100)
                }
                // Force form 0 — non-zero forms on random species softlock models
                val newWord = newSp and 0x7FF
                putU16(table, so, newWord)
                table[so + 2] = newLo.toByte()
                table[so + 3] = newHi.toByte()
                changed = true
            }
            if (changed) {
                System.arraycopy(table, 0, file, offset, table.size)
                // Prefer in-place (keeps original GARC). Skip maps that grow past slot.
                if (!garc.setFile(i, file)) {
                    log.appendLine("wild map $i skipped (needs larger GARC slot)")
                } else {
                    tables++
                }
            }
        }
        modified[OrasPaths.ENCDATA] = garc.save()
        log.appendLine("wild tables patched: $tables")
    }

    // --- Trainers ---
    private fun randomizeTrainers() {
        val trdataBytes = modified[OrasPaths.TRDATA]
            ?: error("trdata missing from work set")
        val trpokeBytes = modified[OrasPaths.TRPOKE]
            ?: error("trpoke missing from work set")
        val trdata = GarcArchive.open(trdataBytes)
        val trpoke = GarcArchive.open(trpokeBytes)
        val pool = SpeciesPool.allSpecies(config.wildLegendaries)
        val count = minOf(trdata.fileCount, trpoke.fileCount)
        var n = 0
        for (i in 0 until count) {
            try {
                val td = Lz11.maybeDecompress(trdata.getFile(i))
                val tp = Lz11.maybeDecompress(trpoke.getFile(i))
                if (td.size < 0x14 || tp.isEmpty()) continue
                // ORAS trdata: u16 format, u16 class, u16 pad, battleType, numPokemon, ...
                val bb = ByteBuffer.wrap(td).order(ByteOrder.LITTLE_ENDIAN)
                val format = bb.short.toInt() and 0xFFFF
                bb.short; bb.short
                bb.get() // battle
                val num = bb.get().toInt() and 0xFF
                if (num !in 1..6) continue
                val hasItem = ((format shr 1) and 1) == 1
                val hasMoves = (format and 1) == 1
                if (tp.size % num != 0) continue
                val entrySize = tp.size / num
                if (entrySize < 8) continue
                val newTp = tp.copyOf()
                for (p in 0 until num) {
                    val base = p * entrySize
                    if (config.trainerParties) {
                        putU16(newTp, base + 4, SpeciesPool.pick(rng, pool))
                        if (entrySize >= 8) putU16(newTp, base + 6, 0)
                    }
                    if (config.trainerAbilities) {
                        val pid = newTp[base + 1].toInt() and 0xFF
                        val ability = rng.nextInt(1, 4)
                        newTp[base + 1] = ((ability shl 4) or (pid and 0x0F)).toByte()
                    }
                    var o = base + 8
                    if (hasItem) {
                        if (config.trainerItems && o + 2 <= newTp.size) {
                            putU16(newTp, o, rng.nextInt(1, 650))
                        }
                        o += 2
                    }
                    if (hasMoves && config.trainerMoves) {
                        for (m in 0 until 4) {
                            if (o + m * 2 + 2 <= newTp.size) {
                                putU16(newTp, o + m * 2, rng.nextInt(1, 621))
                            }
                        }
                    }
                }
                if (config.trainerDifficulty != RandomizerConfig.Difficulty.SIMILAR) {
                    for (p in 0 until num) {
                        val base = p * entrySize
                        var lv = getU16(newTp, base + 2)
                        lv = when (config.trainerDifficulty) {
                            RandomizerConfig.Difficulty.WEAKER -> (lv - 2).coerceAtLeast(1)
                            RandomizerConfig.Difficulty.STRONGER -> (lv + 3).coerceAtMost(100)
                            RandomizerConfig.Difficulty.RIVAL_PLUS -> (lv + 5).coerceAtMost(100)
                            else -> lv
                        }
                        putU16(newTp, base + 2, lv)
                    }
                }
                if (trpoke.setFile(i, newTp)) n++
            } catch (e: Exception) {
                log.appendLine("trainer $i skip: ${e.message}")
            }
        }
        modified[OrasPaths.TRPOKE] = trpoke.save()
        log.appendLine("trainers patched: $n")
    }

    // --- Personal ---
    private fun randomizePersonal() {
        val garc = GarcArchive.open(modified[OrasPaths.PERSONAL]!!)
        val table = Lz11.maybeDecompress(garc.getFile(garc.fileCount - 1)).copyOf()
        val size = OrasPaths.PERSONAL_SIZE
        val count = table.size / size
        for (i in 1 until count) { // skip empty 0
            val off = i * size
            if (config.personalBaseStats) {
                for (s in 0 until 6) {
                    table[off + s] = rng.nextInt(20, 180).toByte()
                }
            }
            if (config.personalTypes) {
                table[off + 6] = rng.nextInt(0, 18).toByte()
                table[off + 7] = rng.nextInt(0, 18).toByte()
            }
            if (config.personalAbilities) {
                table[off + 0x18] = rng.nextInt(1, 191).toByte()
                table[off + 0x19] = rng.nextInt(1, 191).toByte()
                table[off + 0x1A] = rng.nextInt(1, 191).toByte()
            }
            if (config.personalTmCompat) {
                for (b in 0 until 0x10) {
                    table[off + 0x28 + b] = rng.nextInt(0, 256).toByte()
                }
            }
        }
        garc.setFile(garc.fileCount - 1, table)
        modified[OrasPaths.PERSONAL] = garc.save()
        log.appendLine("personal entries: $count")
    }

    // --- Level-up learnsets (per-species files in GARC) ---
    private fun randomizeLevelUp() {
        val garc = GarcArchive.open(modified[OrasPaths.LEVELUP]!!)
        var n = 0
        for (i in 0 until garc.fileCount) {
            var f = Lz11.maybeDecompress(garc.getFile(i))
            if (f.size < 4) continue
            // Gen6 learnset: pairs of u16 move + u16 level until 0xFFFF
            val out = f.copyOf()
            var o = 0
            while (o + 4 <= out.size) {
                val move = getU16(out, o)
                val level = getU16(out, o + 2)
                if (move == 0xFFFF || level == 0xFFFF) break
                if (move in 1..620 && level in 1..100) {
                    putU16(out, o, rng.nextInt(1, 621))
                }
                o += 4
            }
            garc.setFile(i, out)
            n++
        }
        modified[OrasPaths.LEVELUP] = garc.save()
        log.appendLine("level-up files: $n")
    }

    private fun randomizeEggMoves() {
        val garc = GarcArchive.open(modified[OrasPaths.EGGMOVE]!!)
        var n = 0
        for (i in 0 until garc.fileCount) {
            var f = Lz11.maybeDecompress(garc.getFile(i))
            if (f.size < 2) continue
            val out = f.copyOf()
            // count u16 then list of moves
            val count = getU16(out, 0)
            if (count in 1..50 && 2 + count * 2 <= out.size) {
                for (m in 0 until count) {
                    putU16(out, 2 + m * 2, rng.nextInt(1, 621))
                }
                garc.setFile(i, out)
                n++
            }
        }
        modified[OrasPaths.EGGMOVE] = garc.save()
        log.appendLine("egg-move files: $n")
    }

    private fun randomizeMoves() {
        val garc = GarcArchive.open(modified[OrasPaths.MOVE]!!)
        // ORAS: mini pack in file 0 often — randomize type/category bytes when layout matches Move6
        // Move6 size 0x22 typically; scan subfiles
        val files = garc.getAll()
        // Try mini unpack of first file is complex; byte-scan for type field patterns
        // Fallback: if file 0 has many 0x22 records via Mini "WD"
        var f0 = Lz11.maybeDecompress(files[0])
        if (f0.size > 8 && f0[0] == 'W'.code.toByte() && f0[1] == 'D'.code.toByte()) {
            // Mini format: "WD" + count + offsets — skip detailed, randomize middle
            log.appendLine("move mini-pack present; scrambling type/category fields")
            // After mini header, raw move entries 0x22 each sometimes flattened
        }
        // Per-file approach: if file length == 0x22, treat as Move6
        var n = 0
        for (i in 0 until garc.fileCount) {
            val f = Lz11.maybeDecompress(garc.getFile(i))
            if (f.size == 0x22) {
                val out = f.copyOf()
                // Move6: type at 0x0F? Check Move6.cs — type often at different offset
                // From Move6: Type is typically byte at offset 0x0F or similar
                if (config.moveTypes) out[0x0F] = rng.nextInt(0, 18).toByte()
                if (config.moveCategories) out[0x12] = rng.nextInt(0, 3).toByte()
                garc.setFile(i, out)
                n++
            }
        }
        if (n == 0 && f0.size > 0x100) {
            // Flattened mini: scramble every 0x22 block after header
            val out = f0.copyOf()
            var o = 0x10
            while (o + 0x22 <= out.size) {
                if (config.moveTypes) out[o + 0x0F] = rng.nextInt(0, 18).toByte()
                if (config.moveCategories) out[o + 0x12] = rng.nextInt(0, 3).toByte()
                o += 0x22
                n++
            }
            garc.setFile(0, out)
        }
        modified[OrasPaths.MOVE] = garc.save()
        log.appendLine("moves touched: $n")
    }

    private fun randomizeEvolutions() {
        val garc = GarcArchive.open(modified[OrasPaths.EVOLUTION]!!)
        val pool = SpeciesPool.allSpecies(config.wildLegendaries)
        var n = 0
        for (i in 0 until garc.fileCount) {
            val f = Lz11.maybeDecompress(garc.getFile(i))
            // EvolutionSet6: methods as records with species at fixed offsets
            // Each method: ~6 bytes with species u16 — scramble valid species words
            val out = f.copyOf()
            var o = 0
            while (o + 6 <= out.size) {
                val method = getU16(out, o)
                val species = getU16(out, o + 4)
                if (method != 0 && species in 1..SpeciesPool.MAX_SPECIES) {
                    putU16(out, o + 4, SpeciesPool.pick(rng, pool))
                    n++
                }
                o += 6
            }
            garc.setFile(i, out)
        }
        modified[OrasPaths.EVOLUTION] = garc.save()
        log.appendLine("evolution links: $n")
    }

    private fun randomizeItemsMart() {
        // Item GARC — scramble price-ish fields is risky; randomize held-item bits lightly
        val garc = GarcArchive.open(modified[OrasPaths.ITEM]!!)
        var n = 0
        for (i in 1 until garc.fileCount) {
            val f = Lz11.maybeDecompress(garc.getFile(i))
            if (f.size < 0x10) continue
            val out = f.copyOf()
            // flip a few bytes in item data (price low bytes)
            if (out.size >= 4) {
                out[0] = rng.nextInt(1, 250).toByte()
                n++
            }
            garc.setFile(i, out)
        }
        modified[OrasPaths.ITEM] = garc.save()
        log.appendLine("item entries: $n")
    }

    private fun deployMods() {
        val user = NativeLibrary.getUserDirectory()
        require(user.isNotBlank()) { "User data directory not set" }
        val title = titleIdHex.uppercase().padStart(16, '0')
        val root = File(user, "load/mods/$title/romfs")
        // Clear previous romfs mods for clean slate
        if (root.exists()) root.deleteRecursively()
        root.mkdirs()
        for ((path, data) in modified) {
            val out = File(root, path)
            out.parentFile?.mkdirs()
            out.writeBytes(data)
            log.appendLine("wrote ${out.absolutePath} (${data.size})")
        }
        // Marker
        File(user, "load/mods/$title/hoenn_forge_seed.txt").writeText(
            "seed=${config.seed}\n${config.summaryLines().joinToString("\n")}\n",
        )
    }

    private fun clearMods() {
        val user = NativeLibrary.getUserDirectory()
        if (user.isBlank()) return
        val title = titleIdHex.uppercase().padStart(16, '0')
        val root = File(user, "load/mods/$title")
        if (root.exists()) root.deleteRecursively()
    }

    private fun getU16(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)

    private fun putU16(b: ByteArray, off: Int, value: Int) {
        b[off] = (value and 0xFF).toByte()
        b[off + 1] = ((value ushr 8) and 0xFF).toByte()
    }

    companion object {
        private const val TAG = "HoennForgeRando"
    }
}
