package org.vita3k.emulator.data

/**
 * Thor: one cheat of a title as the native cheat engine reports it. Built by
 * native_cheats.cpp; the constructor order is part of the JNI signature.
 */
data class NativeCheatInfo(
    val name: String,
    val enabled: Boolean,
    val enabledOnBoot: Boolean,
    val broken: Boolean,
    val lineCount: Int,
    val codes: String
)
