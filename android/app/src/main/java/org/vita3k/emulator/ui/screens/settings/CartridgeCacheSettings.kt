package org.vita3k.emulator.ui.screens.settings

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.vita3k.emulator.R
import org.vita3k.emulator.data.AppRepository
import org.vita3k.emulator.data.CartridgeCacheEntry
import org.vita3k.emulator.data.CartridgeCacheStore
import org.vita3k.emulator.data.CartridgeCacheSummary

/**
 * Thor: shows what the cartridge cache holds per title, how much of the
 * storage it takes, and lets the user drop one title or all of them from the
 * device itself. The Thor's internal storage filled up silently before this
 * existed, because nothing ever showed the cache or evicted it.
 */
@Composable
internal fun CartridgeCacheCard(
    storagePath: String,
    onShowHelp: (SettingsHelpEntry) -> Unit
) {
    var summary by remember(storagePath) { mutableStateOf<CartridgeCacheSummary?>(null) }
    var titles by remember { mutableStateOf<Map<String, String>>(emptyMap()) }
    var refreshTick by remember { mutableIntStateOf(0) }
    var busy by remember { mutableStateOf(false) }
    var failedTitle by remember { mutableStateOf<String?>(null) }
    var pendingDelete by remember { mutableStateOf<CartridgeCacheEntry?>(null) }
    var confirmClearAll by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()

    LaunchedEffect(storagePath, refreshTick) {
        summary = withContext(Dispatchers.IO) { CartridgeCacheStore.scan(storagePath) }
        if (titles.isEmpty()) {
            titles = runCatching { AppRepository.getAppList().associate { it.titleId to it.title } }
                .getOrDefault(emptyMap())
        }
    }

    fun nameOf(titleId: String): String = titles[titleId]?.takeIf { it.isNotBlank() } ?: titleId

    fun runDeletion(action: () -> Boolean, failureTitle: String) {
        busy = true
        scope.launch {
            val ok = withContext(Dispatchers.IO) { action() }
            failedTitle = if (ok) null else failureTitle
            busy = false
            refreshTick++
        }
    }

    val title = stringResource(R.string.settings_cartridge_cache_title)
    val current = summary
    val usage = current?.let {
        stringResource(
            R.string.settings_cartridge_cache_usage,
            CartridgeCacheStore.formatBytes(it.totalBytes),
            CartridgeCacheStore.formatBytes(it.freeBytes)
        )
    }

    SettingsSectionCard(
        title = title,
        summary = usage,
        help = SettingsHelpEntry(
            title = title,
            body = stringResource(R.string.settings_cartridge_cache_help),
            scope = SettingsScope.Global
        ),
        onShowHelp = onShowHelp
    ) {
        SettingsNote(text = stringResource(R.string.settings_cartridge_cache_summary))
        when {
            current == null -> SettingsLoadingState()
            current.entries.isEmpty() -> SettingsNote(text = stringResource(R.string.settings_cartridge_cache_empty))
            else -> current.entries.forEach { entry ->
                SettingsActionRow(
                    title = nameOf(entry.titleId),
                    value = stringResource(
                        R.string.settings_cartridge_cache_entry_value,
                        entry.titleId,
                        CartridgeCacheStore.formatBytes(entry.bytes),
                        entry.fileNames.take(3).joinToString(", ")
                    ),
                    onClick = { pendingDelete = entry },
                    enabled = !busy,
                    actionLabel = stringResource(R.string.action_delete),
                    onActionClick = { pendingDelete = entry },
                    onShowHelp = onShowHelp
                )
            }
        }
        failedTitle?.let { failed ->
            SettingsNote(text = stringResource(R.string.settings_cartridge_cache_delete_failed, failed))
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            OutlinedButton(onClick = { refreshTick++ }, enabled = !busy) {
                Text(stringResource(R.string.settings_cartridge_cache_refresh))
            }
            FilledTonalButton(
                onClick = { confirmClearAll = true },
                enabled = !busy && (current?.entries?.isNotEmpty() == true)
            ) {
                Text(stringResource(R.string.settings_cartridge_cache_clear_all))
            }
        }
    }

    pendingDelete?.let { entry ->
        AlertDialog(
            onDismissRequest = { pendingDelete = null },
            title = { Text(stringResource(R.string.settings_cartridge_cache_confirm_delete_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.settings_cartridge_cache_confirm_delete_message,
                        nameOf(entry.titleId),
                        CartridgeCacheStore.formatBytes(entry.bytes)
                    )
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    pendingDelete = null
                    runDeletion({ CartridgeCacheStore.delete(storagePath, entry.titleId) }, nameOf(entry.titleId))
                }) {
                    Text(stringResource(R.string.action_delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { pendingDelete = null }) {
                    Text(stringResource(R.string.action_cancel))
                }
            }
        )
    }

    if (confirmClearAll) {
        AlertDialog(
            onDismissRequest = { confirmClearAll = false },
            title = { Text(stringResource(R.string.settings_cartridge_cache_confirm_clear_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.settings_cartridge_cache_confirm_clear_message,
                        CartridgeCacheStore.formatBytes(current?.totalBytes ?: 0L)
                    )
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    confirmClearAll = false
                    runDeletion({ CartridgeCacheStore.deleteAll(storagePath) }, title)
                }) {
                    Text(stringResource(R.string.settings_cartridge_cache_clear_all))
                }
            },
            dismissButton = {
                TextButton(onClick = { confirmClearAll = false }) {
                    Text(stringResource(R.string.action_cancel))
                }
            }
        )
    }
}
