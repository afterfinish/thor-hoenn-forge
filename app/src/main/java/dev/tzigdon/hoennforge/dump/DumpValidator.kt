package dev.tzigdon.hoennforge.dump

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Validates user-selected 3DS dumps for OR/AS without loading the whole file.
 * Reads NCSD media ID at offset 0x108 when magic "NCSD" is at 0x100.
 */
object DumpValidator {

    private const val NCSD_MAGIC_OFFSET = 0x100
    private const val NCSD_MEDIA_ID_OFFSET = 0x108
    private const val HEADER_READ_SIZE = 0x120

    sealed class Result {
        data class Success(
            val displayName: String,
            val titleId: Long,
            val entry: OrasTitles.Entry,
            val sizeBytes: Long,
        ) : Result()

        data class Failure(val message: String) : Result()
    }

    fun validate(context: Context, uri: Uri): Result {
        val name = queryDisplayName(context, uri) ?: uri.lastPathSegment ?: "unknown"
        val lower = name.lowercase()
        if (!lower.endsWith(".3ds") && !lower.endsWith(".cci") && !lower.endsWith(".cxi")) {
            // Still try to parse; warn via message only if parse fails
        }

        val size = querySize(context, uri)
        if (size in 1 until 64L * 1024 * 1024) {
            return Result.Failure("File is too small to be a 3DS game dump.")
        }

        val header = try {
            readHeader(context, uri)
        } catch (e: IOException) {
            return Result.Failure("Could not read file: ${e.message ?: "I/O error"}")
        } catch (e: SecurityException) {
            return Result.Failure("Permission denied reading the dump. Pick the file again.")
        }

        if (header.size < HEADER_READ_SIZE) {
            return Result.Failure("Could not read enough of the file header.")
        }

        val magic = String(header, NCSD_MAGIC_OFFSET, 4, Charsets.US_ASCII)
        if (magic != "NCSD") {
            return Result.Failure(
                "Not a recognized decrypted cart dump (missing NCSD header). " +
                    "Prefer a decrypted .cci / .3ds that boots in Azahar.",
            )
        }

        val titleId = ByteBuffer.wrap(header, NCSD_MEDIA_ID_OFFSET, 8)
            .order(ByteOrder.LITTLE_ENDIAN)
            .long

        val entry = OrasTitles.find(titleId)
            ?: return Result.Failure(
                "This dump is not Omega Ruby or Alpha Sapphire " +
                    "(title ${OrasTitles.formatTitleId(titleId)}).",
            )

        return Result.Success(
            displayName = name,
            titleId = titleId,
            entry = entry,
            sizeBytes = size,
        )
    }

    private fun readHeader(context: Context, uri: Uri): ByteArray {
        val buffer = ByteArray(HEADER_READ_SIZE)
        context.contentResolver.openInputStream(uri)?.use { input ->
            var offset = 0
            while (offset < buffer.size) {
                val read = input.read(buffer, offset, buffer.size - offset)
                if (read <= 0) break
                offset += read
            }
            if (offset < buffer.size) {
                return buffer.copyOf(offset)
            }
        } ?: throw IOException("Unable to open dump")
        return buffer
    }

    private fun queryDisplayName(context: Context, uri: Uri): String? {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) return cursor.getString(idx)
                }
            }
        return null
    }

    private fun querySize(context: Context, uri: Uri): Long {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.SIZE), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val idx = cursor.getColumnIndex(OpenableColumns.SIZE)
                    if (idx >= 0) return cursor.getLong(idx)
                }
            }
        return -1L
    }
}
