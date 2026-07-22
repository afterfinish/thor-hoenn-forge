// Copyright Hoenn Forge — Nintendo LZ11 (type 0x11) used inside ORAS GARCs
package org.citra.citra_emu.hoennforge.randomizer

import java.nio.ByteBuffer
import java.nio.ByteOrder

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
}
