// Copyright Hoenn Forge — GARC v4 pack/unpack (pk3DS-compatible)
package org.citra.citra_emu.hoennforge.randomizer

import java.nio.ByteBuffer
import java.nio.ByteOrder

class GarcArchive private constructor(
    private var files: Array<ByteArray>,
    private val version: Int,
    private val padTo: Int,
) {
    val fileCount: Int get() = files.size

    fun getFile(index: Int): ByteArray = files[index].copyOf()

    fun setFile(index: Int, data: ByteArray) {
        files[index] = data
    }

    fun getAll(): Array<ByteArray> = Array(files.size) { files[it].copyOf() }

    fun save(): ByteArray {
        // Pack as GARC v4 with single subfile per entry (pk3DS PackGARC style)
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
            fatbBody += 4 + 12 // vector + one subentry
        }
        val fatbHeader = 0xC + fatbBody
        val fimbHeader = 0xC
        val dataOffset = 0x1C + fatoHeader + fatbHeader + fimbHeader
        // actually GARC header is HeaderSize (0x1C for v4)
        val garcHeaderSize = 0x1C
        val headerAndTables = garcHeaderSize + fatoHeader + fatbHeader + fimbHeader
        val dataSize = padded.sum()
        val total = headerAndTables + dataSize

        val out = ByteArray(total)
        val bb = ByteBuffer.wrap(out).order(ByteOrder.LITTLE_ENDIAN)
        // GARC header
        bb.put('C'.code.toByte()); bb.put('R'.code.toByte())
        bb.put('A'.code.toByte()); bb.put('G'.code.toByte())
        bb.putInt(garcHeaderSize)
        bb.putShort(0xFEFF.toShort())
        bb.putShort(version.toShort())
        bb.putInt(4) // chunk count
        bb.putInt(headerAndTables) // data offset
        bb.putInt(total)
        bb.putInt(lengths.maxOrNull() ?: 0) // largest unpadded

        // FATO
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

        // FATB
        bb.put('B'.code.toByte()); bb.put('T'.code.toByte())
        bb.put('A'.code.toByte()); bb.put('F'.code.toByte())
        bb.putInt(fatbHeader)
        bb.putInt(count)
        var od = 0
        for (i in 0 until count) {
            bb.putInt(1) // vector: only sub 0
            bb.putInt(od)
            bb.putInt(od + padded[i])
            bb.putInt(lengths[i])
            od += padded[i]
        }

        // FIMB
        bb.put('B'.code.toByte()); bb.put('M'.code.toByte())
        bb.put('I'.code.toByte()); bb.put('F'.code.toByte())
        bb.putInt(0xC)
        bb.putInt(dataSize)

        // File data
        for (i in 0 until count) {
            System.arraycopy(files[i], 0, out, bb.position(), lengths[i])
            bb.position(bb.position() + padded[i])
        }
        return out
    }

    companion object {
        const val VER_4 = 0x0400

        fun open(data: ByteArray): GarcArchive {
            val bb = ByteBuffer.wrap(data).order(ByteOrder.LITTLE_ENDIAN)
            val magic = ByteArray(4).also { bb.get(it) }
            require(magic.contentEquals(byteArrayOf(0x43, 0x52, 0x41, 0x47))) { "Not a GARC" }
            val headerSize = bb.int
            bb.short // endian
            val version = bb.short.toInt() and 0xFFFF
            bb.int // chunks
            val dataOffset = bb.int
            bb.int // file size
            val padTo = if (version == VER_4) {
                bb.int // largest
                4
            } else {
                bb.int; bb.int
                bb.int // pad nearest
            }
            // FATO
            bb.position(headerSize)
            bb.int // magic OT AF
            bb.int // fato header size
            val entryCount = bb.short.toInt() and 0xFFFF
            bb.short
            bb.position(bb.position() + entryCount * 4)
            // FATB
            bb.int // magic
            bb.int // header
            bb.int // file count
            val starts = IntArray(entryCount)
            val lengths = IntArray(entryCount)
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
                    starts[i] = first.first
                    lengths[i] = first.third
                }
            }
            val files = Array(entryCount) { i ->
                if (lengths[i] <= 0) ByteArray(0)
                else data.copyOfRange(dataOffset + starts[i], dataOffset + starts[i] + lengths[i])
            }
            return GarcArchive(files, version, padTo)
        }
    }
}
