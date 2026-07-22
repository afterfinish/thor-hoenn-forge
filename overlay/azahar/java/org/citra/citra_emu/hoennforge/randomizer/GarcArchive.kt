// Copyright Hoenn Forge — GARC v4 pack/unpack + in-place replace (pk3DS-compatible)
package org.citra.citra_emu.hoennforge.randomizer

import java.nio.ByteBuffer
import java.nio.ByteOrder

class GarcArchive private constructor(
    private val original: ByteArray,
    private var files: Array<ByteArray>,
    private val starts: IntArray,
    private val maxLengths: IntArray,
    private val dataOffset: Int,
    private val version: Int,
    private val padTo: Int,
) {
    val fileCount: Int get() = files.size

    fun getFile(index: Int): ByteArray = files[index].copyOf()

    /**
     * Prefer in-place replace when [data] fits the original subfile budget.
     * Returns false if the file must grow (caller may full-repack).
     */
    fun setFile(index: Int, data: ByteArray): Boolean {
        if (data.size > maxLengths[index]) return false
        files[index] = data
        return true
    }

    /** Force-set even if larger (requires saveRepack). */
    fun setFileForce(index: Int, data: ByteArray) {
        files[index] = data
    }

    fun getAll(): Array<ByteArray> = Array(files.size) { files[it].copyOf() }

    /** Write files back into the original GARC container without rebuilding (no growth). */
    fun saveInPlace(): ByteArray {
        val out = original.copyOf()
        for (i in files.indices) {
            val data = files[i]
            val max = maxLengths[i]
            require(data.size <= max) {
                "File $i length ${data.size} exceeds slot $max — use saveRepack()"
            }
            val dest = dataOffset + starts[i]
            // zero pad remainder of original slot
            java.util.Arrays.fill(out, dest, dest + max, 0xFF.toByte())
            System.arraycopy(data, 0, out, dest, data.size)
            // Update length field in FATB for this file's first subentry
            patchFatbLength(out, i, data.size)
        }
        return out
    }

    /** Full rebuild allowing growth (used when any file expanded). */
    fun saveRepack(): ByteArray {
        val pad = if (padTo <= 0) 4 else padTo
        val count = files.size
        val fatoHeader = 0xC + count * 4
        var fatbBody = 0
        val lengths = IntArray(count)
        val padded = IntArray(count)
        for (i in 0 until count) {
            lengths[i] = files[i].size
            var p = lengths[i] % pad
            if (p != 0) p = pad - p
            padded[i] = lengths[i] + p
            fatbBody += 4 + 12
        }
        val fatbHeader = 0xC + fatbBody
        val fimbHeader = 0xC
        val garcHeaderSize = 0x1C
        val headerAndTables = garcHeaderSize + fatoHeader + fatbHeader + fimbHeader
        val dataSize = padded.sum()
        val total = headerAndTables + dataSize
        val out = ByteArray(total)
        val bb = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN)
        bb.put('C'.code.toByte()); bb.put('R'.code.toByte())
        bb.put('A'.code.toByte()); bb.put('G'.code.toByte())
        bb.putInt(garcHeaderSize)
        bb.putShort(0xFEFF.toShort())
        bb.putShort(version.toShort())
        bb.putInt(4)
        bb.putInt(headerAndTables)
        bb.putInt(total)
        bb.putInt(lengths.maxOrNull() ?: 0)
        bb.put('O'.code.toByte()); bb.put('T'.code.toByte())
        bb.put('A'.code.toByte()); bb.put('F'.code.toByte())
        bb.putInt(fatoHeader)
        bb.putShort(count.toShort())
        bb.putShort(0xFFFF.toShort())
        var op = 0
        for (i in 0 until count) {
            bb.putInt(op)
            op += 4 + 12
        }
        bb.put('B'.code.toByte()); bb.put('T'.code.toByte())
        bb.put('A'.code.toByte()); bb.put('F'.code.toByte())
        bb.putInt(fatbHeader)
        bb.putInt(count)
        var od = 0
        for (i in 0 until count) {
            bb.putInt(1)
            bb.putInt(od)
            bb.putInt(od + padded[i])
            bb.putInt(lengths[i])
            od += padded[i]
        }
        bb.put('B'.code.toByte()); bb.put('M'.code.toByte())
        bb.put('I'.code.toByte()); bb.put('F'.code.toByte())
        bb.putInt(0xC)
        bb.putInt(dataSize)
        for (i in 0 until count) {
            System.arraycopy(files[i], 0, out, bb.position(), lengths[i])
            bb.position(bb.position() + padded[i])
        }
        return out
    }

    fun save(): ByteArray {
        val needsRepack = files.indices.any { files[it].size > maxLengths[it] }
        return if (needsRepack) saveRepack() else saveInPlace()
    }

    private fun patchFatbLength(out: ByteArray, fileIndex: Int, newLen: Int) {
        // Walk FATB to the fileIndex-th entry's first subentry length field
        val bb = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN)
        val headerSize = bb.getInt(4)
        var pos = headerSize
        val fatoHdrSize = bb.getInt(pos + 4)
        val fatoCount = bb.getShort(pos + 8).toInt() and 0xFFFF
        pos += 12 + fatoCount * 4
        pos += 12 // FATB magic+hdr+count
        for (i in 0 until fatoCount) {
            var vector = bb.getInt(pos)
            pos += 4
            var first = true
            for (b in 0 until 32) {
                val exists = (vector and 1) == 1
                vector = vector ushr 1
                if (exists) {
                    // start, end, length
                    if (i == fileIndex && first) {
                        bb.putInt(pos + 8, newLen)
                        // keep end as start+max for in-place; game uses length
                        return
                    }
                    first = false
                    pos += 12
                }
            }
        }
    }

    companion object {
        const val VER_4 = 0x0400

        fun open(data: ByteArray): GarcArchive {
            val bb = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
            val magic = ByteArray(4).also { bb.get(it) }
            require(magic.contentEquals(byteArrayOf(0x43, 0x52, 0x41, 0x47))) { "Not a GARC" }
            val headerSize = bb.int
            bb.short
            val version = bb.short.toInt() and 0xFFFF
            bb.int
            val dataOffset = bb.int
            bb.int
            val padTo = if (version == VER_4) {
                bb.int
                4
            } else {
                bb.int; bb.int
                bb.int
            }
            bb.position(headerSize)
            bb.int
            bb.int
            val entryCount = bb.short.toInt() and 0xFFFF
            bb.short
            bb.position(bb.position() + entryCount * 4)
            bb.int
            bb.int
            bb.int
            val starts = IntArray(entryCount)
            val maxLens = IntArray(entryCount)
            val files = Array(entryCount) { ByteArray(0) }
            for (i in 0 until entryCount) {
                var vector = bb.int
                var first: Triple<Int, Int, Int>? = null
                for (b in 0 until 32) {
                    val exists = (vector and 1) == 1
                    vector = vector ushr 1
                    if (exists) {
                        val start = bb.int
                        val end = bb.int
                        val length = bb.int
                        if (first == null) first = Triple(start, end, length)
                    }
                }
                if (first != null) {
                    val (start, end, length) = first
                    starts[i] = start
                    // Budget is original slot size (end-start), not just logical length
                    maxLens[i] = (end - start).coerceAtLeast(length)
                    files[i] = data.copyOfRange(dataOffset + start, dataOffset + start + length)
                }
            }
            return GarcArchive(data, files, starts, maxLens, dataOffset, version, padTo)
        }
    }
}
