// Copyright Hoenn Forge — read party lead species from ORAS main save
package org.citra.citra_emu.hoennforge

import android.util.Log
import org.citra.citra_emu.NativeLibrary
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Pulls party mon species from the on-disk Gen6 `main` save (PK6 decrypt).
 * Validated against dogfood save: party lead cluster at 0x14200 stride 0x104.
 */
object OrasPartyReader {
    private const val TAG = "HoennForgeParty"
    private const val SIZE_PARTY = 0x104
    private const val SIZE_STORED = 0xE8

    // Shuffle orders for Gen6 PK6 (PKHeX)
    private val BLOCK_POS = arrayOf(
        intArrayOf(0, 1, 2, 3), intArrayOf(0, 1, 3, 2), intArrayOf(0, 2, 1, 3), intArrayOf(0, 3, 1, 2),
        intArrayOf(0, 2, 3, 1), intArrayOf(0, 3, 2, 1), intArrayOf(1, 0, 2, 3), intArrayOf(1, 0, 3, 2),
        intArrayOf(2, 0, 1, 3), intArrayOf(3, 0, 1, 2), intArrayOf(2, 0, 3, 1), intArrayOf(3, 0, 2, 1),
        intArrayOf(1, 2, 0, 3), intArrayOf(1, 3, 0, 2), intArrayOf(2, 1, 0, 3), intArrayOf(3, 1, 0, 2),
        intArrayOf(2, 3, 0, 1), intArrayOf(3, 2, 0, 1), intArrayOf(1, 2, 3, 0), intArrayOf(1, 3, 2, 0),
        intArrayOf(2, 1, 3, 0), intArrayOf(3, 1, 2, 0), intArrayOf(2, 3, 1, 0), intArrayOf(3, 2, 1, 0),
    )

    data class PartyInfo(val species: List<Int>, val source: String) {
        val lead: Int get() = species.firstOrNull() ?: 0
    }

    fun readParty(titleId: Long): PartyInfo? {
        val main = findMainSave(titleId) ?: run {
            Log.w(TAG, "no main save for title ${titleId.toString(16)}")
            return null
        }
        return try {
            val bytes = main.readBytes()
            val party = extractParty(bytes)
            if (party.isEmpty()) {
                Log.w(TAG, "no party PK6 in ${main.absolutePath} (${bytes.size} bytes)")
                null
            } else {
                Log.i(TAG, "party from ${main.name}: $party")
                PartyInfo(party, main.absolutePath)
            }
        } catch (e: Exception) {
            Log.e(TAG, "read party failed", e)
            null
        }
    }

    fun findMainSave(titleId: Long): File? {
        val user = try {
            NativeLibrary.getUserDirectory()
        } catch (_: Exception) {
            ""
        }
        if (user.isBlank()) return null
        // Prefer running title; also try the other ORAS SKU (OR vs AS)
        val lows = linkedSetOf(
            (titleId and 0xFFFFFFFFL).toString(16).padStart(8, '0'),
            "0011c500", // Alpha Sapphire USA
            "0011c400", // Omega Ruby USA
        )
        val bases = listOf(
            "sdmc/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/title/00040000",
        )
        for (low in lows) {
            for (base in bases) {
                for (data in listOf("00000001", "00000000")) {
                    val f = File(user, "$base/$low/data/$data/main")
                    if (f.isFile && f.length() > 0x20000) return f
                }
            }
        }
        // Fallback: any main under ORAS title folders
        for (base in bases) {
            val titleRoot = File(user, base)
            if (!titleRoot.isDirectory) continue
            titleRoot.walkTopDown().maxDepth(6).firstOrNull {
                it.name == "main" && it.isFile && it.length() > 0x20000 &&
                    (it.absolutePath.contains("0011c5", ignoreCase = true) ||
                        it.absolutePath.contains("0011c4", ignoreCase = true))
            }?.let { return it }
        }
        return null
    }

    /** Prefer known dogfood offset; else longest 0x104-stride PK6 run. */
    fun extractParty(bytes: ByteArray): List<Int> {
        // Dogfood-validated cluster (ORAS main, 3 mon party)
        val at14200 = readSlots(bytes, 0x14200, 6)
        if (at14200.isNotEmpty()) return at14200

        // Doc-style offsets used by some dumps
        for (base in intArrayOf(0x19600, 0x1C600, 0x16500)) {
            val p = readSlots(bytes, base, 6)
            if (p.isNotEmpty()) return p
        }

        // Full scan for longest 0x104 run of valid PK6
        val hits = ArrayList<Pair<Int, Int>>()
        var i = 0
        while (i + SIZE_STORED <= bytes.size) {
            val sp = decryptSpecies(bytes, i)
            if (sp != null) hits.add(i to sp)
            i += 4
        }
        var best = emptyList<Int>()
        for ((idx, pair) in hits.withIndex()) {
            if (idx > 0 && hits[idx - 1].first + SIZE_PARTY == pair.first) continue
            val run = ArrayList<Int>()
            var j = idx
            var expect = pair.first
            while (j < hits.size && hits[j].first == expect) {
                run.add(hits[j].second)
                expect += SIZE_PARTY
                j++
            }
            if (run.size > best.size) best = run
        }
        return best
    }

    private fun readSlots(bytes: ByteArray, base: Int, n: Int): List<Int> {
        val out = ArrayList<Int>()
        for (s in 0 until n) {
            val off = base + s * SIZE_PARTY
            val sp = decryptSpecies(bytes, off) ?: break
            out.add(sp)
        }
        return out
    }

    /** Decrypt PK6 stored region; return species or null if checksum/species invalid. */
    fun decryptSpecies(bytes: ByteArray, off: Int): Int? {
        if (off < 0 || off + SIZE_STORED > bytes.size) return null
        val pk = bytes.copyOfRange(off, off + SIZE_STORED)
        val bb = ByteBuffer.wrap(pk).order(ByteOrder.LITTLE_ENDIAN)
        val ec = bb.getInt(0)
        if (ec == 0) return null
        val chk = bb.getShort(6).toInt() and 0xFFFF
        var seed = ec.toLong() and 0xFFFFFFFFL
        for (i in 8 until SIZE_STORED step 2) {
            seed = (0x41C64E6DL * seed + 0x6073L) and 0xFFFFFFFFL
            val r = ((seed shr 16) and 0xFFFF).toInt()
            pk[i] = (pk[i].toInt() xor (r and 0xFF)).toByte()
            pk[i + 1] = (pk[i + 1].toInt() xor ((r shr 8) and 0xFF)).toByte()
        }
        val sv = (ec ushr 13) and 31
        val order = BLOCK_POS[sv % 24]
        val blocks = Array(4) { i -> pk.copyOfRange(8 + i * 56, 8 + (i + 1) * 56) }
        val un = arrayOfNulls<ByteArray>(4)
        for (i in 0 until 4) {
            un[order[i]] = blocks[i]
        }
        for (i in 0 until 4) {
            System.arraycopy(un[i]!!, 0, pk, 8 + i * 56, 56)
        }
        var sum = 0
        for (i in 8 until SIZE_STORED step 2) {
            sum = (sum + ((pk[i].toInt() and 0xFF) or ((pk[i + 1].toInt() and 0xFF) shl 8))) and 0xFFFF
        }
        if (sum != chk) return null
        val sp = (pk[8].toInt() and 0xFF) or ((pk[9].toInt() and 0xFF) shl 8)
        if (sp !in 1..721) return null
        return sp
    }
}
