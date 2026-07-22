// Copyright Hoenn Forge — Nintendo LZ11 (type 0x11) used inside ORAS GARCs
package org.citra.citra_emu.hoennforge.randomizer

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.ArrayList
import java.util.HashMap

object Lz11 {
    fun maybeDecompress(data: ByteArray): ByteArray {
        if (data.isEmpty() || data[0] != 0x11.toByte()) return data
        return decompress(data)
    }

    fun decompress(data: ByteArray): ByteArray {
        require(data.isNotEmpty() && data[0] == 0x11.toByte()) { "Not LZ11" }
        var src = 4
        var size = (data[1].toInt() and 0xFF) or
            ((data[2].toInt() and 0xFF) shl 8) or
            ((data[3].toInt() and 0xFF) shl 16)
        if (size == 0) {
            size = ByteBuffer.wrap(data, 4, 4).order(ByteOrder.LITTLE_ENDIAN).int
            src = 8
        }
        val out = ByteArray(size)
        var dst = 0
        while (dst < size && src < data.size) {
            val flags = data[src++].toInt() and 0xFF
            for (i in 0 until 8) {
                if (dst >= size) break
                if ((flags and (0x80 shr i)) != 0) {
                    if (src >= data.size) break
                    val b1 = data[src++].toInt() and 0xFF
                    val typ = b1 ushr 4
                    val length: Int
                    val disp: Int
                    when (typ) {
                        0 -> {
                            val b2 = data[src++].toInt() and 0xFF
                            val b3 = data[src++].toInt() and 0xFF
                            length = (((b1 and 0xF) shl 4) or (b2 ushr 4)) + 0x11
                            disp = ((b2 and 0xF) shl 8) or b3
                        }
                        1 -> {
                            val b2 = data[src++].toInt() and 0xFF
                            val b3 = data[src++].toInt() and 0xFF
                            val b4 = data[src++].toInt() and 0xFF
                            length = (((b1 and 0xF) shl 12) or (b2 shl 4) or (b3 ushr 4)) + 0x111
                            disp = ((b3 and 0xF) shl 8) or b4
                        }
                        else -> {
                            val b2 = data[src++].toInt() and 0xFF
                            length = typ + 1
                            disp = ((b1 and 0xF) shl 8) or b2
                        }
                    }
                    val d = disp + 1
                    var n = 0
                    while (n < length && dst < size) {
                        out[dst] = out[dst - d]
                        dst++
                        n++
                    }
                } else {
                    out[dst++] = data[src++]
                }
            }
        }
        return out
    }

    /**
     * Hash-accelerated greedy LZ11 compressor (Nintendo type 0x11).
     * Fast enough for ~500 ORAS encounter maps on-device during prepare.
     */
    fun compress(data: ByteArray): ByteArray {
        if (data.isEmpty()) return data
        require(data.size < 0x1000000) { "LZ11 payload too large" }
        val out = ArrayList<Byte>(data.size / 2 + 16)
        out.add(0x11)
        out.add((data.size and 0xFF).toByte())
        out.add(((data.size ushr 8) and 0xFF).toByte())
        out.add(((data.size ushr 16) and 0xFF).toByte())

        // 3-byte hash → recent positions (capped list)
        val index = HashMap<Int, ArrayList<Int>>(4096)
        var pos = 0
        while (pos < data.size) {
            val flagIndex = out.size
            out.add(0)
            var flags = 0
            for (bit in 0 until 8) {
                if (pos >= data.size) break
                val match = findMatchFast(data, pos, index)
                if (match != null) {
                    flags = flags or (0x80 shr bit)
                    writeRef(out, match.first, match.second)
                    val end = pos + match.second
                    while (pos < end) {
                        addIndex(index, data, pos)
                        pos++
                    }
                } else {
                    out.add(data[pos])
                    addIndex(index, data, pos)
                    pos++
                }
            }
            out[flagIndex] = flags.toByte()
        }
        return out.toByteArray()
    }

    fun compressFitting(data: ByteArray, maxBytes: Int): ByteArray? {
        val c = compress(data)
        return if (c.size <= maxBytes) c else null
    }

    private fun key3(data: ByteArray, pos: Int): Int {
        if (pos + 2 >= data.size) return -1
        return (data[pos].toInt() and 0xFF) or
            ((data[pos + 1].toInt() and 0xFF) shl 8) or
            ((data[pos + 2].toInt() and 0xFF) shl 16)
    }

    private fun addIndex(index: HashMap<Int, ArrayList<Int>>, data: ByteArray, pos: Int) {
        val k = key3(data, pos)
        if (k < 0) return
        val list = index.getOrPut(k) { ArrayList(4) }
        list.add(pos)
        // Keep window ≈ 4096 lookback
        while (list.isNotEmpty() && pos - list[0] > 0x1000) {
            list.removeAt(0)
        }
        if (list.size > 64) {
            list.subList(0, list.size - 32).clear()
        }
    }

    private fun findMatchFast(
        data: ByteArray,
        pos: Int,
        index: HashMap<Int, ArrayList<Int>>,
    ): Pair<Int, Int>? {
        val maxLen = minOf(0x10110, data.size - pos)
        if (maxLen < 3) return null
        val k = key3(data, pos)
        if (k < 0) return null
        val candidates = index[k] ?: return null
        var bestLen = 0
        var bestDisp = 0
        // Newest candidates first
        for (ci in candidates.indices.reversed()) {
            val s = candidates[ci]
            val disp = pos - s - 1
            if (disp < 0 || disp > 0xFFF) continue
            var len = 0
            while (len < maxLen && data[s + len] == data[pos + len]) {
                len++
            }
            if (len >= 3 && len > bestLen) {
                bestLen = len
                bestDisp = disp
                if (bestLen >= 32) break
            }
        }
        if (bestLen < 3) return null
        return bestDisp to bestLen
    }

    private fun writeRef(out: ArrayList<Byte>, disp: Int, length: Int) {
        require(disp in 0..0xFFF)
        when {
            length < 0x11 -> {
                val typ = length - 1
                out.add(((typ shl 4) or ((disp ushr 8) and 0xF)).toByte())
                out.add((disp and 0xFF).toByte())
            }
            length < 0x111 -> {
                val n = length - 0x11
                out.add(((n ushr 4) and 0xF).toByte())
                out.add((((n and 0xF) shl 4) or ((disp ushr 8) and 0xF)).toByte())
                out.add((disp and 0xFF).toByte())
            }
            else -> {
                val n = length - 0x111
                out.add((0x10 or ((n ushr 12) and 0xF)).toByte())
                out.add(((n ushr 4) and 0xFF).toByte())
                out.add((((n and 0xF) shl 4) or ((disp ushr 8) and 0xF)).toByte())
                out.add((disp and 0xFF).toByte())
            }
        }
    }
}
