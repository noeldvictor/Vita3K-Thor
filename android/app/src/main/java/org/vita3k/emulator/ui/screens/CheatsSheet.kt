package org.vita3k.emulator.ui.screens

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.vita3k.emulator.NativeLib
import org.vita3k.emulator.R
import org.vita3k.emulator.data.NativeCheatInfo

/**
 * Thor: the cheats of one title, with a switch per cheat and the master switch.
 *
 * `live` means the title is running: switches act on the engine at once. Otherwise the
 * choice is written to the title's cheat file and takes effect when the game boots. In both
 * cases the choice is remembered across sessions.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CheatsSheet(
    titleId: String,
    gameTitle: String,
    live: Boolean,
    onDismiss: () -> Unit
) {
    val scope = rememberCoroutineScope()
    var cheats by remember { mutableStateOf<List<NativeCheatInfo>>(emptyList()) }
    var master by remember { mutableStateOf(true) }
    var filePath by remember { mutableStateOf("") }
    var loaded by remember { mutableStateOf(false) }
    var expandedIndex by remember { mutableStateOf(-1) }
    var message by remember { mutableStateOf<String?>(null) }
    val failedText = stringResource(R.string.cheats_toggle_failed)

    suspend fun refresh() {
        val list = withContext(Dispatchers.IO) {
            runCatching { NativeLib.getCheats(titleId).toList() }.getOrDefault(emptyList())
        }
        val masterEnabled = withContext(Dispatchers.IO) {
            runCatching { NativeLib.getCheatsMasterEnabled() }.getOrDefault(true)
        }
        val path = withContext(Dispatchers.IO) {
            runCatching { NativeLib.getCheatFilePath(titleId) }.getOrDefault("")
        }
        cheats = list
        master = masterEnabled
        filePath = path
        loaded = true
    }

    // JNI work stays off the main thread: a cheat file can hold hundreds of codes.
    fun perform(block: () -> Boolean) {
        scope.launch {
            val ok = withContext(Dispatchers.IO) { runCatching(block).getOrDefault(false) }
            message = if (ok) null else failedText
            refresh()
        }
    }

    LaunchedEffect(titleId) { refresh() }

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp)
                .padding(bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Text(gameTitle.ifBlank { titleId }, style = MaterialTheme.typography.titleLarge)
            Text(
                text = "$titleId · " + stringResource(if (live) R.string.cheats_live else R.string.cheats_offline),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )

            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Column(modifier = Modifier.weight(1f)) {
                    Text(stringResource(R.string.cheats_master_switch), style = MaterialTheme.typography.titleSmall)
                    Text(
                        stringResource(R.string.cheats_master_hint),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
                Switch(
                    checked = master,
                    onCheckedChange = { value -> perform { NativeLib.setCheatsMasterEnabled(value) } }
                )
            }

            HorizontalDivider()

            when {
                !loaded -> CircularProgressIndicator()
                cheats.isEmpty() -> Text(stringResource(R.string.cheats_none_for_title))
                else -> {
                    Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        TextButton(onClick = { perform { NativeLib.setAllCheatsEnabled(titleId, true) } }) {
                            Text(stringResource(R.string.cheats_all_on))
                        }
                        TextButton(onClick = { perform { NativeLib.setAllCheatsEnabled(titleId, false) } }) {
                            Text(stringResource(R.string.cheats_all_off))
                        }
                        if (live) {
                            TextButton(onClick = { perform { NativeLib.reloadCheats(titleId) } }) {
                                Text(stringResource(R.string.cheats_reload))
                            }
                        }
                    }
                    Text(
                        stringResource(R.string.cheats_status_count, cheats.count { it.enabled }, cheats.size),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    LazyColumn(modifier = Modifier.heightIn(max = 420.dp)) {
                        itemsIndexed(cheats) { index, cheat ->
                            CheatRow(
                                cheat = cheat,
                                expanded = expandedIndex == index,
                                onToggleExpanded = { expandedIndex = if (expandedIndex == index) -1 else index },
                                onEnabledChange = { value -> perform { NativeLib.setCheatEnabled(titleId, index, value) } }
                            )
                        }
                    }
                }
            }

            if (filePath.isNotEmpty()) {
                Text(
                    stringResource(R.string.cheats_file, filePath),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            message?.let {
                Text(it, color = MaterialTheme.colorScheme.error, style = MaterialTheme.typography.bodySmall)
            }
        }
    }
}

@Composable
private fun CheatRow(
    cheat: NativeCheatInfo,
    expanded: Boolean,
    onToggleExpanded: () -> Unit,
    onEnabledChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onToggleExpanded)
            .padding(vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(cheat.name, style = MaterialTheme.typography.bodyLarge)
            if (cheat.broken) {
                Text(
                    stringResource(R.string.cheats_broken),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.error
                )
            }
            if (expanded && cheat.codes.isNotBlank()) {
                Text(
                    cheat.codes.trimEnd(),
                    style = MaterialTheme.typography.bodySmall,
                    fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
        Switch(
            checked = cheat.enabled,
            enabled = !cheat.broken,
            onCheckedChange = onEnabledChange
        )
    }
}
