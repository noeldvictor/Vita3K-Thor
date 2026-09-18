package org.vita3k.emulator.ui.screens

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.Search
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import org.vita3k.emulator.R
import org.vita3k.emulator.data.AppInfo
import org.vita3k.emulator.data.CheatCatalogGame
import org.vita3k.emulator.data.CheatDatabase
import org.vita3k.emulator.data.CheatIndexEntry

/**
 * Thor: every game the bundled cheat database covers, so the user can see whether a game
 * has cheats before fetching its cartridge from storage. Games already in the library are
 * marked and can open the cheat sheet from here.
 */
@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
fun CheatCatalogScreen(
    libraryApps: List<AppInfo>,
    onBack: () -> Unit
) {
    val context = LocalContext.current
    var games by remember { mutableStateOf<List<CheatCatalogGame>?>(null) }
    var query by rememberSaveable { mutableStateOf("") }
    var region by rememberSaveable { mutableStateOf("") }
    var libraryOnly by rememberSaveable { mutableStateOf(false) }
    var expandedKey by remember { mutableStateOf<String?>(null) }
    var sheetEntry by remember { mutableStateOf<CheatIndexEntry?>(null) }

    LaunchedEffect(Unit) {
        games = CheatDatabase.loadCatalog(context)
    }

    val libraryIds = remember(libraryApps) { libraryApps.map { it.titleId.uppercase() }.toSet() }
    val all = games.orEmpty()
    val filtered = remember(all, query, region, libraryOnly, libraryIds) {
        val needle = query.trim().lowercase()
        all.filter { game ->
            (needle.isEmpty() || game.title.lowercase().contains(needle) || game.entries.any { it.titleId.lowercase().contains(needle) }) &&
                (region.isEmpty() || game.regionCodes.contains(region)) &&
                (!libraryOnly || game.titleIds.any { it in libraryIds })
        }
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.cheats_catalog_title)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.action_back))
                    }
                }
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize(),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedTextField(
                value = query,
                onValueChange = { query = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                singleLine = true,
                placeholder = { Text(stringResource(R.string.cheats_search_hint)) },
                leadingIcon = { Icon(Icons.Default.Search, contentDescription = null) }
            )
            FlowRow(
                modifier = Modifier.padding(horizontal = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilterChip(
                    selected = region.isEmpty(),
                    onClick = { region = "" },
                    label = { Text(stringResource(R.string.cheats_region_all)) }
                )
                CheatDatabase.REGION_CODES.forEach { code ->
                    FilterChip(
                        selected = region == code,
                        onClick = { region = if (region == code) "" else code },
                        label = { Text(code) }
                    )
                }
                FilterChip(
                    selected = libraryOnly,
                    onClick = { libraryOnly = !libraryOnly },
                    label = { Text(stringResource(R.string.cheats_in_library_only)) }
                )
            }

            when {
                games == null -> Column(
                    modifier = Modifier.fillMaxWidth().padding(32.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) { CircularProgressIndicator() }
                all.isEmpty() -> Text(
                    stringResource(R.string.cheats_catalog_missing),
                    modifier = Modifier.padding(16.dp)
                )
                filtered.isEmpty() -> Text(
                    stringResource(R.string.cheats_catalog_empty),
                    modifier = Modifier.padding(16.dp)
                )
                else -> {
                    Text(
                        stringResource(R.string.cheats_catalog_summary, filtered.size, filtered.sumOf { it.cheatCount }),
                        modifier = Modifier.padding(horizontal = 16.dp),
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                    LazyColumn(
                        modifier = Modifier.fillMaxSize(),
                        contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 16.dp, vertical = 8.dp),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        items(filtered, key = { it.key }) { game ->
                            CatalogRow(
                                game = game,
                                libraryIds = libraryIds,
                                expanded = expandedKey == game.key,
                                onToggleExpanded = { expandedKey = if (expandedKey == game.key) null else game.key },
                                onManage = { entry -> sheetEntry = entry }
                            )
                        }
                    }
                }
            }
        }
    }

    sheetEntry?.let { entry ->
        CheatsSheet(
            titleId = entry.titleId,
            gameTitle = entry.title,
            live = false,
            onDismiss = { sheetEntry = null }
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun CatalogRow(
    game: CheatCatalogGame,
    libraryIds: Set<String>,
    expanded: Boolean,
    onToggleExpanded: () -> Unit,
    onManage: (CheatIndexEntry) -> Unit
) {
    val inLibrary = game.titleIds.any { it in libraryIds }
    Card(onClick = onToggleExpanded, modifier = Modifier.fillMaxWidth()) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(game.title, style = MaterialTheme.typography.titleMedium)
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
                        game.regionCodes.forEach { code -> Tag(code) }
                        Text(
                            stringResource(R.string.cheats_count, game.cheatCount),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        if (inLibrary) {
                            Text(
                                stringResource(R.string.cheats_in_library),
                                style = MaterialTheme.typography.labelMedium,
                                color = MaterialTheme.colorScheme.primary
                            )
                        }
                    }
                }
                Icon(
                    imageVector = if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            if (expanded) {
                game.entries.forEach { entry ->
                    HorizontalDivider()
                    val meta = buildString {
                        append(entry.titleId)
                        if (entry.region.isNotBlank()) append(" · ").append(entry.region)
                        if (entry.version.isNotBlank()) append(" · ").append(stringResource(R.string.cheats_version, entry.version))
                    }
                    Text(meta, style = MaterialTheme.typography.labelLarge)
                    if (entry.author.isNotBlank()) {
                        Text(
                            stringResource(R.string.cheats_author, entry.author),
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    entry.cheats.forEach { name ->
                        Text("• $name", style = MaterialTheme.typography.bodySmall)
                    }
                    if (entry.titleId in libraryIds) {
                        TextButton(onClick = { onManage(entry) }) {
                            Text(stringResource(R.string.cheats_menu_manage))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun Tag(text: String) {
    Text(
        text = text,
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSecondaryContainer,
        modifier = Modifier
            .background(MaterialTheme.colorScheme.secondaryContainer, RoundedCornerShape(4.dp))
            .padding(horizontal = 6.dp, vertical = 2.dp)
    )
}
