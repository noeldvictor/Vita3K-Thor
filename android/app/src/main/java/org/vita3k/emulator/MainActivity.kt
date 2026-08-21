package org.vita3k.emulator

import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.util.Log
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.appcompat.app.AppCompatActivity
import org.vita3k.emulator.data.AppStorage
import org.vita3k.emulator.ui.navigation.AppNavigation
import org.vita3k.emulator.ui.theme.Vita3KTheme
import org.vita3k.emulator.ui.viewmodel.AppsListViewModel
import org.vita3k.emulator.ui.viewmodel.InstallViewModel
import org.vita3k.emulator.ui.viewmodel.SettingsViewModel
import org.vita3k.emulator.ui.viewmodel.UserManagementViewModel

class MainActivity : AppCompatActivity() {
    private val appsListViewModel: AppsListViewModel by viewModels()
    private val installViewModel: InstallViewModel by viewModels()
    private val settingsViewModel: SettingsViewModel by viewModels()
    private val userManagementViewModel: UserManagementViewModel by viewModels()

    private val emulatorLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) {
        if (appsListViewModel.initialized) {
            appsListViewModel.reloadAppsList()
        }
    }

    private var pendingFolderCallback: ((String?) -> Unit)? = null
    private var pendingFileCallback: ((String?) -> Unit)? = null
    private var pendingArchiveFolderCallback: ((List<String>) -> Unit)? = null
    private var pendingInstallFileExtensions: Set<String>? = null
    private var pendingArchiveFolderExtensions: Set<String>? = null
    private var pendingStorageAction: (() -> Unit)? = null

    private val folderPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestMultiplePermissions()
    ) {
        if (StorageAccess.hasStorageAccess(this)) {
            launchPendingStorageAction()
        } else {
            cancelPendingStorageRequest()
        }
    }

    private val manageFolderAccessLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) {
        if (StorageAccess.hasStorageAccess(this)) {
            launchPendingStorageAction()
        } else {
            cancelPendingStorageRequest()
        }
    }

    private val filePickerLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        val selectedPath = if (result.resultCode == RESULT_OK) {
            result.data?.data?.let { uri -> StorageAccess.resolveUriToPath(this, uri) }
        } else {
            null
        }
        val validatedPath = selectedPath?.takeIf { path ->
            pendingInstallFileExtensions?.let { allowed ->
                StorageAccess.matchesAllowedExtension(path, allowed)
            } ?: true
        }
        dispatchFileResult(validatedPath)
    }

    private val folderPickerLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) { result ->
        if (pendingArchiveFolderCallback != null) {
            val selectedPaths = if (result.resultCode == RESULT_OK) {
                result.data?.data?.let { treeUri ->
                    StorageAccess.resolveTreeFilePaths(
                        context = this,
                        treeUri = treeUri,
                        allowedExtensions = pendingArchiveFolderExtensions ?: emptySet()
                    )
                } ?: emptyList()
            } else {
                emptyList()
            }
            dispatchArchiveFolderResult(selectedPaths)
            return@registerForActivityResult
        }

        val selectedPath = if (result.resultCode == RESULT_OK) {
            result.data?.data?.let { uri -> StorageAccess.resolveTreeUriToPath(this, uri) }
        } else {
            null
        }
        dispatchFolderResult(selectedPath)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        prepareFrontendRuntime()
        setTheme(R.style.Theme_Vita3K)

        val storagePath = AppStorage.storageRootPath(this)
        appsListViewModel.initialize(storagePath)

        setContent {
            Vita3KTheme {
                AppNavigation(
                    appsListViewModel = appsListViewModel,
                    installViewModel = installViewModel,
                    settingsViewModel = settingsViewModel,
                    userManagementViewModel = userManagementViewModel,
                    onAppLaunch = { app -> launchApp(app.titleId, app.title) }
                )
            }
        }

        handleCartridgeIntent(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleCartridgeIntent(intent)
    }

    /**
     * Thor: a .zip/.vpk opened from a file manager or shared to Vita3K is
     * mounted as a read-only virtual cartridge and booted directly. Nothing is
     * installed into ux0:app.
     */
    private fun handleCartridgeIntent(intent: Intent?) {
        val uri = cartridgeUriFrom(intent) ?: return

        // On a cold start onCreate runs before the emulator has finished
        // initialising, and mounting needs a live EmuEnv. Wait for it off the
        // main thread, then mount and boot.
        lifecycleScope.launch {
            withContext(Dispatchers.IO) {
                var waited = 0
                while (!NativeLib.isInitialized() && waited < INIT_WAIT_MS) {
                    delay(INIT_POLL_MS.toLong())
                    waited += INIT_POLL_MS
                }
            }

            if (!NativeLib.isInitialized()) {
                Log.e(TAG, "Emulator did not initialise in time; cannot mount cartridge")
                return@launch
            }

            mountAndBootCartridge(uri)
        }
    }

    private suspend fun mountAndBootCartridge(uri: Uri) {

        val path = withContext(Dispatchers.IO) {
            StorageAccess.resolveUriToPath(this@MainActivity, uri) ?: copyCartridgeLocally(uri)
        }
        if (path.isNullOrEmpty()) {
            Log.e(TAG, "Could not resolve a usable path for cartridge uri $uri")
            return
        }

        val titleId = withContext(Dispatchers.IO) { NativeLib.mountCartridge(path) }
        if (titleId.isEmpty()) {
            Log.e(TAG, "Failed to mount cartridge at $path")
            return
        }

        launchApp(titleId, titleId)
    }

    private fun cartridgeUriFrom(intent: Intent?): Uri? {
        if (intent == null) return null

        return when (intent.action) {
            Intent.ACTION_VIEW -> intent.data
            Intent.ACTION_SEND ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
                    intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
                else
                    @Suppress("DEPRECATION") intent.getParcelableExtra(Intent.EXTRA_STREAM)
            else -> null
        }
    }

    /**
     * Some providers hand over a content:// uri with no filesystem path behind
     * it. Copy those into app-local storage so the archive can be mounted.
     */
    private fun copyCartridgeLocally(uri: Uri): String? = runCatching {
        val outputDir = File(getExternalFilesDir(null), "cartridge_launch").apply { mkdirs() }
        val name = contentResolver.query(uri, null, null, null, null)?.use { cursor ->
            val index = cursor.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
            if (index >= 0 && cursor.moveToFirst()) cursor.getString(index) else null
        } ?: "cartridge.zip"

        val target = File(outputDir, name)
        contentResolver.openInputStream(uri)?.use { input ->
            target.outputStream().use { output -> input.copyTo(output) }
        } ?: return@runCatching null

        target.absolutePath
    }.getOrNull()

    private companion object {
        const val TAG = "Vita3K"
        const val INIT_WAIT_MS = 20_000
        const val INIT_POLL_MS = 50
    }

    fun requestStorageFolderChange(onResult: (String?) -> Unit) {
        requestFolderPath(onResult)
    }

    fun requestFolderPath(onResult: (String?) -> Unit) {
        pendingFolderCallback = onResult
        pendingFileCallback = null
        pendingArchiveFolderCallback = null
        pendingInstallFileExtensions = null
        pendingArchiveFolderExtensions = null
        pendingStorageAction = { launchFolderPicker() }
        ensureStorageAccess()
    }

    fun requestFilePath(mimeTypes: Array<String>, onResult: (String?) -> Unit) {
        pendingFileCallback = onResult
        pendingFolderCallback = null
        pendingArchiveFolderCallback = null
        pendingInstallFileExtensions = null
        pendingArchiveFolderExtensions = null
        pendingStorageAction = { launchFilePicker(mimeTypes) }
        ensureStorageAccess()
    }

    fun requestInstallFilePath(
        allowedExtensions: Set<String>,
        onResult: (String?) -> Unit
    ) {
        pendingFileCallback = onResult
        pendingFolderCallback = null
        pendingArchiveFolderCallback = null
        pendingInstallFileExtensions = allowedExtensions
        pendingArchiveFolderExtensions = null
        pendingStorageAction = { launchInstallFilePicker() }
        ensureStorageAccess()
    }

    fun requestArchiveFolderPaths(
        allowedExtensions: Set<String>,
        onResult: (List<String>) -> Unit
    ) {
        pendingFileCallback = null
        pendingFolderCallback = null
        pendingArchiveFolderCallback = onResult
        pendingInstallFileExtensions = null
        pendingArchiveFolderExtensions = allowedExtensions
        pendingStorageAction = { launchInstallFolderPicker() }
        ensureStorageAccess()
    }

    override fun onResume() {
        super.onResume()
        prepareFrontendRuntime()
    }

    private fun ensureStorageAccess() {
        if (StorageAccess.hasStorageAccess(this)) {
            launchPendingStorageAction()
            return
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            manageFolderAccessLauncher.launch(StorageAccess.createManageAllFilesIntent(this))
            return
        }

        folderPermissionLauncher.launch(StorageAccess.missingStoragePermissions(this))
    }

    private fun launchApp(titleId: String, appTitle: String) {
        emulatorLauncher.launch(Emulator.createLaunchIntent(this, titleId, appTitle))
    }

    private fun launchFilePicker(mimeTypes: Array<String>) {
        filePickerLauncher.launch(StorageAccess.createFilePickerIntent(mimeTypes))
    }

    private fun launchInstallFilePicker() {
        filePickerLauncher.launch(StorageAccess.createInstallFilePickerIntent())
    }

    private fun launchFolderPicker() {
        folderPickerLauncher.launch(StorageAccess.createFolderPickerIntent())
    }

    private fun launchInstallFolderPicker() {
        folderPickerLauncher.launch(StorageAccess.createInstallFolderPickerIntent())
    }

    private fun launchPendingStorageAction() {
        val action = pendingStorageAction ?: return
        pendingStorageAction = null
        action.invoke()
    }

    private fun cancelPendingStorageRequest() {
        pendingStorageAction = null
        dispatchFileResult(null)
        dispatchFolderResult(null)
        dispatchArchiveFolderResult(emptyList())
    }

    private fun dispatchFolderResult(path: String?) {
        val callback = pendingFolderCallback
        pendingFolderCallback = null
        callback?.invoke(path?.takeIf { it.isNotBlank() })
    }

    private fun dispatchFileResult(path: String?) {
        val callback = pendingFileCallback
        pendingFileCallback = null
        pendingInstallFileExtensions = null
        callback?.invoke(path?.takeIf { it.isNotBlank() })
    }

    private fun dispatchArchiveFolderResult(paths: List<String>) {
        val callback = pendingArchiveFolderCallback
        pendingArchiveFolderCallback = null
        pendingArchiveFolderExtensions = null
        callback?.invoke(paths)
    }

    private fun prepareFrontendRuntime() {
        NativeLib.prepareFrontend()
    }
}
