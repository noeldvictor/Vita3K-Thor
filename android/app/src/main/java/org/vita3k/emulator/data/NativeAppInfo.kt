package org.vita3k.emulator.data

data class NativeAppInfo(
    val titleId: String,
    val title: String,
    val category: String,
    val appVer: String,
    val iconPath: String,
    val hasCustomConfig: Boolean,
    val compatibility: Int,
    val lastPlayed: Long,
    val playtime: Long,
    // Thor: virtual cartridges are listed from a scan root rather than
    // installed, and carry badges for encrypted content and available cheats.
    val virtualCartridge: Boolean = false,
    val encryptedContent: Boolean = false,
    val cheatsAvailable: Boolean = false
)
