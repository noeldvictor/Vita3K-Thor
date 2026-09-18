package org.vita3k.emulator.data

import android.content.Context
import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

/** One `<TITLEID>.psv` file of the bundled database, as listed in cheats/index.json. */
data class CheatIndexEntry(
    val titleId: String,
    val title: String,
    val region: String,
    val regionCode: String,
    val version: String,
    val author: String,
    val cheats: List<String>
)

/** One game in the catalog: the entries of every region merged under one title. */
data class CheatCatalogGame(
    val key: String,
    val title: String,
    val entries: List<CheatIndexEntry>
) {
    val cheatCount: Int get() = entries.maxOf { it.cheats.size }
    val regionCodes: List<String> get() = entries.map { it.regionCode }.filter { it.isNotEmpty() }.distinct()
    val titleIds: List<String> get() = entries.map { it.titleId }
}

/**
 * Thor: the FinalCheat/VitaCheat database shipped in the APK under assets/cheats.
 *
 * The engine reads plain files, so the `.psv` files are extracted once per installed
 * build into `<storage>/cheats/db`, which is one of the roots the native lookup checks.
 * The catalog screen reads `index.json` straight from the assets.
 */
object CheatDatabase {
    private const val TAG = "Vita3K"
    private const val ASSET_DB_DIR = "cheats/db"
    private const val ASSET_INDEX = "cheats/index.json"
    private const val STAMP_FILE = ".bundled-build"
    val REGION_CODES = listOf("US", "EU", "JP", "ASIA")

    /**
     * Copies the bundled `.psv` files to `<storagePath>/cheats/db`. Runs before the native
     * side scans the app list so the cheat badges see the files on the first launch.
     */
    suspend fun ensureExtracted(context: Context, storagePath: String) = withContext(Dispatchers.IO) {
        val stamp = try {
            val info = context.packageManager.getPackageInfo(context.packageName, 0)
            "${info.versionName}-${info.lastUpdateTime}"
        } catch (e: Exception) {
            "unknown"
        }
        val target = File(storagePath, "cheats/db")
        val stampFile = File(target, STAMP_FILE)
        if (stampFile.exists() && runCatching { stampFile.readText() }.getOrNull() == stamp) {
            return@withContext
        }
        val assets = context.assets
        val names = try {
            assets.list(ASSET_DB_DIR).orEmpty()
        } catch (e: Exception) {
            emptyArray()
        }
        if (names.isEmpty()) {
            Log.w(TAG, "The APK carries no cheat database under assets/$ASSET_DB_DIR")
            return@withContext
        }
        if (!target.isDirectory && !target.mkdirs()) {
            Log.w(TAG, "Could not create $target for the cheat database")
            return@withContext
        }
        var copied = 0
        for (name in names) {
            try {
                assets.open("$ASSET_DB_DIR/$name").use { input ->
                    File(target, name).outputStream().use { output -> input.copyTo(output) }
                }
                copied++
            } catch (e: Exception) {
                Log.w(TAG, "Could not extract cheat file $name", e)
            }
        }
        try {
            assets.open(ASSET_INDEX).use { input ->
                File(storagePath, "cheats/index.json").outputStream().use { output -> input.copyTo(output) }
            }
        } catch (e: Exception) {
            Log.w(TAG, "Could not extract the cheat index", e)
        }
        runCatching { stampFile.writeText(stamp) }
        Log.i(TAG, "Extracted $copied of ${names.size} bundled cheat files to $target")
    }

    /** The catalog, grouped by game. Empty when the APK has no index. */
    suspend fun loadCatalog(context: Context): List<CheatCatalogGame> = withContext(Dispatchers.IO) {
        val json = try {
            context.assets.open(ASSET_INDEX).bufferedReader().use { it.readText() }
        } catch (e: Exception) {
            Log.w(TAG, "No cheat index in the APK", e)
            return@withContext emptyList()
        }
        parseCatalog(json)
    }

    fun parseCatalog(json: String): List<CheatCatalogGame> {
        val entries = ArrayList<CheatIndexEntry>()
        try {
            val games = JSONObject(json).optJSONArray("games") ?: return emptyList()
            for (i in 0 until games.length()) {
                val g = games.getJSONObject(i)
                val names = ArrayList<String>()
                val cheats = g.optJSONArray("cheats")
                if (cheats != null) {
                    for (k in 0 until cheats.length()) names.add(cheats.optString(k))
                }
                entries.add(
                    CheatIndexEntry(
                        titleId = g.optString("title_id").uppercase(),
                        title = g.optString("title").trim(),
                        region = g.optString("region"),
                        regionCode = g.optString("region_code"),
                        version = g.optString("version"),
                        author = g.optString("author"),
                        cheats = names
                    )
                )
            }
        } catch (e: Exception) {
            Log.w(TAG, "Could not parse the cheat index", e)
            return emptyList()
        }
        return entries
            .groupBy { normalizeKey(it.title).ifEmpty { it.titleId } }
            .map { (key, group) ->
                val sorted = group.sortedBy { it.titleId }
                // The longest title is usually the least abbreviated one.
                CheatCatalogGame(key = key, title = sorted.maxBy { it.title.length }.title, entries = sorted)
            }
            .sortedBy { it.title.lowercase() }
    }

    /** Titles differ only in case and punctuation between the regional files. */
    private fun normalizeKey(title: String): String =
        title.lowercase().replace(Regex("[^a-z0-9]+"), " ").trim()
}
