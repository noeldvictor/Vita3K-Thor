package org.vita3k.emulator.data

import android.os.StatFs
import java.io.File
import java.util.Locale

/**
 * Thor: large members of a zip cartridge (the PSARC archives most games keep
 * their assets in, movies, big sound banks) are unpacked once into
 * `<storage>/cache/cartridge_archive/<TITLEID>/<archive key>/...`, because a
 * deflated zip entry cannot be read at an arbitrary offset and games seek
 * inside those files constantly. This mirrors `archive_cache_path()` in
 * `vita3k/io/src/io.cpp`.
 *
 * Deleting a title's cache is always safe: it is rebuilt the next time that
 * cartridge starts. Nothing under here is a save or a setting.
 */
data class CartridgeCacheEntry(
    val titleId: String,
    val bytes: Long,
    val fileNames: List<String>,
    val directory: File
)

data class CartridgeCacheSummary(
    val root: File,
    val entries: List<CartridgeCacheEntry>,
    val totalBytes: Long,
    /** Free space on the volume that holds the cache, or -1 when unknown. */
    val freeBytes: Long
)

internal object CartridgeCacheStore {
    fun root(storagePath: String): File = File(storagePath, "cache/cartridge_archive")

    fun scan(storagePath: String): CartridgeCacheSummary {
        val root = root(storagePath)
        val entries = (root.listFiles()?.filter { it.isDirectory } ?: emptyList())
            .map { titleDir ->
                var bytes = 0L
                val files = mutableListOf<Pair<String, Long>>()
                titleDir.walkTopDown().forEach { file ->
                    if (file.isFile) {
                        val length = file.length()
                        bytes += length
                        files += file.name to length
                    }
                }
                CartridgeCacheEntry(
                    titleId = titleDir.name,
                    bytes = bytes,
                    fileNames = files.sortedByDescending { it.second }.map { it.first },
                    directory = titleDir
                )
            }
            .sortedByDescending { it.bytes }
        val freeBytes = runCatching { StatFs(File(storagePath).path).availableBytes }.getOrDefault(-1L)
        return CartridgeCacheSummary(root, entries, entries.sumOf { it.bytes }, freeBytes)
    }

    /** Removes one title's cache directory. Only direct children of the cache root are ever touched. */
    fun delete(storagePath: String, titleId: String): Boolean {
        val root = root(storagePath)
        val target = File(root, titleId)
        if (target.parentFile?.canonicalPath != root.canonicalPath)
            return false
        if (!target.isDirectory)
            return true
        return target.deleteRecursively()
    }

    fun deleteAll(storagePath: String): Boolean {
        val root = root(storagePath)
        if (!root.isDirectory)
            return true
        return root.listFiles()?.all { child -> child.deleteRecursively() } ?: true
    }

    fun formatBytes(bytes: Long): String {
        if (bytes < 0)
            return "?"
        val units = arrayOf("B", "KB", "MB", "GB", "TB")
        var value = bytes.toDouble()
        var unit = 0
        while (value >= 1024.0 && unit < units.lastIndex) {
            value /= 1024.0
            unit++
        }
        return if (unit == 0) "$bytes B" else String.format(Locale.US, "%.1f %s", value, units[unit])
    }
}
