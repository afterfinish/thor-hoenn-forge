// Copyright Hoenn Forge — read files from decrypted NCSD/NCCH RomFS
package org.citra.citra_emu.hoennforge.randomizer

import android.content.Context
import android.net.Uri
import android.os.ParcelFileDescriptor
import java.io.FileInputStream
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.channels.FileChannel

/**
 * Extracts individual RomFS paths from a decrypted .3ds/.cci dump.
 */
class RomfsReader private constructor(
    private val channel: FileChannel,
    private val pfd: ParcelFileDescriptor?,
    private val fileDataBase: Long,
    private val files: Map<String, Pair<Long, Long>>,
) {
    fun read(path: String): ByteArray {
        val key = normalize(path)
        val meta = files[key] ?: error("RomFS missing: $key")
        val (off, len) = meta
        require(len in 0..Int.MAX_VALUE) { "Bad size for $key: $len" }
        val buf = ByteArray(len.toInt())
        val n = channel.read(ByteBuffer.wrap(buf), fileDataBase + off)
        require(n == len.toInt()) { "Short read $key ($n / $len)" }
        return buf
    }

    fun has(path: String): Boolean = files.containsKey(normalize(path))

    fun close() {
        try {
            channel.close()
        } catch (_: Exception) {
        }
        try {
            pfd?.close()
        } catch (_: Exception) {
        }
    }

    companion object {
        fun open(context: Context, dumpUri: Uri): RomfsReader {
            val pfd = context.contentResolver.openFileDescriptor(dumpUri, "r")
                ?: error("Cannot open dump")
            val channel = FileInputStream(pfd.fileDescriptor).channel
            return openChannel(channel, pfd)
        }

        fun openFile(path: String): RomfsReader {
            val raf = RandomAccessFile(path, "r")
            return openChannel(raf.channel, null)
        }

        private fun openChannel(channel: FileChannel, pfd: ParcelFileDescriptor?): RomfsReader {
            val header = ByteArray(0x400)
            readFully(channel, 0, header)
            require(ascii(header, 0x100, 4) == "NCSD") { "Not NCSD (.3ds/.cci)" }
            val part0 = u32(header, 0x120).toLong() * 0x200L
            val ncch = ByteArray(0x200)
            readFully(channel, part0, ncch)
            require(ascii(ncch, 0x100, 4) == "NCCH") { "Missing NCCH" }
            val romfsMu = u32(ncch, 0x1B0).toLong()
            require(romfsMu != 0L) { "No RomFS in NCCH" }
            val romfsBase = part0 + romfsMu * 0x200L + 0x1000L
            val rh = ByteArray(0x28)
            readFully(channel, romfsBase, rh)
            val bb = ByteBuffer.wrap(rh).order(ByteOrder.LITTLE_ENDIAN)
            val headerLen = bb.int
            require(headerLen == 0x28) {
                "Bad RomFS header ($headerLen) — dump may be encrypted"
            }
            bb.int; bb.int // dir hash off/len
            val dirMetaOff = bb.int.toLong()
            val dirMetaLen = bb.int.toLong()
            bb.int; bb.int // file hash
            val fileMetaOff = bb.int.toLong()
            val fileMetaLen = bb.int.toLong()
            val fileDataOff = bb.int.toLong()

            val dirMeta = ByteArray(dirMetaLen.toInt())
            val fileMeta = ByteArray(fileMetaLen.toInt())
            readFully(channel, romfsBase + dirMetaOff, dirMeta)
            readFully(channel, romfsBase + fileMetaOff, fileMeta)

            fun readName(table: ByteArray, off: Int, nameLen: Int): String {
                if (nameLen <= 0) return ""
                val chars = CharArray(nameLen / 2)
                var i = 0
                var p = off
                while (i < chars.size) {
                    val lo = table[p].toInt() and 0xFF
                    val hi = table[p + 1].toInt() and 0xFF
                    chars[i++] = ((hi shl 8) or lo).toChar()
                    p += 2
                }
                return String(chars)
            }

            val dirNames = HashMap<Int, String>()
            val dirParents = HashMap<Int, Int>()
            var o = 0
            while (o + 0x18 <= dirMeta.size) {
                val dbb = ByteBuffer.wrap(dirMeta, o, 0x18).order(ByteOrder.LITTLE_ENDIAN)
                val parent = dbb.int
                dbb.int; dbb.int; dbb.int; dbb.int
                val nameLen = dbb.int
                if (nameLen < 0 || o + 0x18 + nameLen > dirMeta.size) break
                dirNames[o] = readName(dirMeta, o + 0x18, nameLen)
                dirParents[o] = parent
                val entrySize = 0x18 + nameLen
                o += (entrySize + 3) and 3.inv()
            }

            fun dirPath(off: Int): String {
                if (off == 0) return ""
                val parts = ArrayList<String>()
                var cur = off
                var guard = 0
                while (cur != 0 && guard++ < 64) {
                    val n = dirNames[cur] ?: break
                    if (n.isNotEmpty()) parts.add(n)
                    val p = dirParents[cur] ?: break
                    if (p == cur) break
                    cur = p
                }
                return parts.asReversed().joinToString("/")
            }

            val map = LinkedHashMap<String, Pair<Long, Long>>()
            o = 0
            while (o + 0x20 <= fileMeta.size) {
                val fbb = ByteBuffer.wrap(fileMeta, o, 0x20).order(ByteOrder.LITTLE_ENDIAN)
                val parent = fbb.int
                fbb.int
                val dataOff = fbb.long
                val dataLen = fbb.long
                fbb.int
                val nameLen = fbb.int
                if (nameLen < 0 || o + 0x20 + nameLen > fileMeta.size) break
                val name = readName(fileMeta, o + 0x20, nameLen)
                val parentPath = dirPath(parent)
                val full = if (parentPath.isEmpty()) name else "$parentPath/$name"
                map[normalize(full)] = dataOff to dataLen
                val entrySize = 0x20 + nameLen
                o += (entrySize + 3) and 3.inv()
            }
            require(map.isNotEmpty()) { "RomFS empty — dump may be encrypted" }
            return RomfsReader(channel, pfd, romfsBase + fileDataOff, map)
        }

        fun normalize(path: String): String =
            path.trim().trimStart('/').replace('\\', '/')

        private fun u32(b: ByteArray, off: Int): Int =
            ByteBuffer.wrap(b, off, 4).order(ByteOrder.LITTLE_ENDIAN).int

        private fun ascii(b: ByteArray, off: Int, n: Int): String =
            b.copyOfRange(off, off + n).toString(Charsets.US_ASCII)

        private fun readFully(ch: FileChannel, pos: Long, buf: ByteArray) {
            var off = 0
            while (off < buf.size) {
                val n = ch.read(ByteBuffer.wrap(buf, off, buf.size - off), pos + off)
                if (n < 0) error("EOF at $pos")
                off += n
            }
        }
    }
}
