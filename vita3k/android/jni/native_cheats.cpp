// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

// Thor: JNI bridge for the cheat engine (vita3k/cheat). The Compose frontend uses it for the
// cheat catalog, the per-game cheat sheet and the pause menu. When the title is running, the
// calls act on the live engine state (the same way the Qt dialog does); otherwise they act on
// the title's cheat file, so cheats can be turned on before the game boots.

#include "android_state.h"

#include <cheat/functions.h>
#include <config/functions.h>
#include <emuenv/state.h>
#include <io/state.h>
#include <kernel/state.h>
#include <util/log.h>

#include <fmt/format.h>
#include <jni.h>

#include <string>

namespace {

cheat::JitInvalidate make_invalidate(EmuEnvState &emuenv) {
    return [&emuenv](uint32_t address, size_t size) {
        emuenv.kernel.invalidate_jit_cache(address, size);
    };
}

// The live engine holds the title's cheats only while that title runs.
bool is_live(const EmuEnvState &emuenv, const std::string &title_id) {
    return !title_id.empty() && emuenv.io.title_id == title_id && emuenv.cheat.has_cheats();
}

fs::path resolve(const EmuEnvState &emuenv, const std::string &title_id) {
    return cheat::resolve_cheat_file(emuenv.cheat_path, emuenv.static_assets_path, emuenv.shared_path, emuenv.vita_fs_path, title_id);
}

// Loads the title's file the way the engine does before boot: the `_V1` marker decides the
// initial on/off state.
cheat::CheatFile parse_for_edit(const EmuEnvState &emuenv, const std::string &title_id) {
    cheat::CheatFile file;
    const fs::path path = resolve(emuenv, title_id);
    if (path.empty())
        return file;
    file = cheat::parse_cheat_file(path, title_id);
    for (auto &cheat : file.cheats)
        cheat.enabled = cheat.enabled_on_boot;
    return file;
}

std::string codes_text(const cheat::Cheat &cheat) {
    std::string out;
    for (const auto &line : cheat.lines)
        out += fmt::format("${:04X} {:08X} {:08X}\n", line.control, line.first, line.second);
    return out;
}

// NewStringUTF aborts the process on bytes that are not valid modified UTF-8, and the
// databases were written with several 8-bit encodings. Invalid bytes become '?'.
std::string sanitize_utf8(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        const auto c = static_cast<unsigned char>(in[i]);
        size_t len = 0;
        if (c == 0)
            len = 0;
        else if (c < 0x80)
            len = 1;
        else if ((c & 0xE0) == 0xC0)
            len = 2;
        else if ((c & 0xF0) == 0xE0)
            len = 3;
        else if ((c & 0xF8) == 0xF0)
            len = 4;
        bool valid = len > 0 && i + len <= in.size();
        for (size_t k = 1; valid && k < len; k++) {
            if ((static_cast<unsigned char>(in[i + k]) & 0xC0) != 0x80)
                valid = false;
        }
        if (valid) {
            out.append(in, i, len);
            i += len;
        } else {
            out.push_back('?');
            i += 1;
        }
    }
    return out;
}

jstring to_jstring(JNIEnv *env, const std::string &value) {
    return env->NewStringUTF(sanitize_utf8(value).c_str());
}

} // namespace

extern "C" {

JNIEXPORT jobjectArray JNICALL
Java_org_vita3k_emulator_NativeLib_getCheats(JNIEnv *env, jclass, jstring title_id_str) {
    jclass info_class = env->FindClass("org/vita3k/emulator/data/NativeCheatInfo");
    if (!info_class)
        return nullptr;
    jmethodID ctor = env->GetMethodID(info_class, "<init>", "(Ljava/lang/String;ZZZILjava/lang/String;)V");
    if (!ctor)
        return env->NewObjectArray(0, info_class, nullptr);

    auto *emuenv = get_emuenv();
    if (!emuenv)
        return env->NewObjectArray(0, info_class, nullptr);

    const std::string title_id = jstring_to_string(env, title_id_str);
    const cheat::CheatFile file = is_live(*emuenv, title_id) ? cheat::snapshot(emuenv->cheat) : parse_for_edit(*emuenv, title_id);

    jobjectArray result = env->NewObjectArray(static_cast<jsize>(file.cheats.size()), info_class, nullptr);
    for (size_t i = 0; i < file.cheats.size(); i++) {
        const auto &cheat = file.cheats[i];
        jstring name = to_jstring(env, cheat.name);
        jstring codes = to_jstring(env, codes_text(cheat));
        jobject info = env->NewObject(info_class, ctor, name,
            static_cast<jboolean>(cheat.enabled), static_cast<jboolean>(cheat.enabled_on_boot),
            static_cast<jboolean>(cheat.broken), static_cast<jint>(cheat.lines.size()), codes);
        env->SetObjectArrayElement(result, static_cast<jsize>(i), info);
        env->DeleteLocalRef(info);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(codes);
    }
    return result;
}

JNIEXPORT jboolean JNICALL
Java_org_vita3k_emulator_NativeLib_setCheatEnabled(JNIEnv *env, jclass, jstring title_id_str, jint index, jboolean enabled) {
    auto *emuenv = get_emuenv();
    if (!emuenv || index < 0)
        return JNI_FALSE;

    const std::string title_id = jstring_to_string(env, title_id_str);
    if (is_live(*emuenv, title_id)) {
        cheat::set_cheat_enabled(emuenv->cheat, static_cast<size_t>(index), enabled, emuenv->mem, make_invalidate(*emuenv));
        // The choice is remembered across sessions, so it is written back at once.
        return cheat::save(emuenv->cheat) ? JNI_TRUE : JNI_FALSE;
    }

    cheat::CheatFile file = parse_for_edit(*emuenv, title_id);
    if (static_cast<size_t>(index) >= file.cheats.size())
        return JNI_FALSE;
    file.cheats[static_cast<size_t>(index)].enabled = enabled;
    return cheat::save_cheat_file(file) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_vita3k_emulator_NativeLib_setAllCheatsEnabled(JNIEnv *env, jclass, jstring title_id_str, jboolean enabled) {
    auto *emuenv = get_emuenv();
    if (!emuenv)
        return JNI_FALSE;

    const std::string title_id = jstring_to_string(env, title_id_str);
    if (is_live(*emuenv, title_id)) {
        cheat::set_all_cheats_enabled(emuenv->cheat, enabled, emuenv->mem, make_invalidate(*emuenv));
        return cheat::save(emuenv->cheat) ? JNI_TRUE : JNI_FALSE;
    }

    cheat::CheatFile file = parse_for_edit(*emuenv, title_id);
    if (file.cheats.empty())
        return JNI_FALSE;
    for (auto &cheat : file.cheats)
        cheat.enabled = enabled;
    return cheat::save_cheat_file(file) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_vita3k_emulator_NativeLib_getCheatsMasterEnabled(JNIEnv *, jclass) {
    auto *emuenv = get_emuenv();
    return (emuenv && emuenv->cfg.enable_cheats) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_vita3k_emulator_NativeLib_setCheatsMasterEnabled(JNIEnv *, jclass, jboolean enabled) {
    auto *emuenv = get_emuenv();
    if (!emuenv)
        return JNI_FALSE;

    if (emuenv->cfg.enable_cheats != static_cast<bool>(enabled)) {
        emuenv->cfg.enable_cheats = enabled;
        config::serialize_config(emuenv->cfg, emuenv->cfg.config_path);
    }
    if (!emuenv->io.title_id.empty())
        cheat::set_enabled(emuenv->cheat, enabled, emuenv->mem, make_invalidate(*emuenv));
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_org_vita3k_emulator_NativeLib_reloadCheats(JNIEnv *env, jclass, jstring title_id_str) {
    auto *emuenv = get_emuenv();
    if (!emuenv)
        return JNI_FALSE;

    const std::string title_id = jstring_to_string(env, title_id_str);
    if (title_id.empty() || emuenv->io.title_id != title_id)
        return JNI_TRUE; // nothing is loaded for a title that is not running

    // The saved originals go away with the old cheats, so undo the ARM writes first.
    cheat::set_all_cheats_enabled(emuenv->cheat, false, emuenv->mem, make_invalidate(*emuenv));
    const fs::path path = resolve(*emuenv, title_id);
    if (path.empty()) {
        cheat::unload(emuenv->cheat);
        return JNI_FALSE;
    }
    return cheat::load_file(emuenv->cheat, path, title_id) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_org_vita3k_emulator_NativeLib_getCheatFilePath(JNIEnv *env, jclass, jstring title_id_str) {
    auto *emuenv = get_emuenv();
    if (!emuenv)
        return env->NewStringUTF("");

    const std::string title_id = jstring_to_string(env, title_id_str);
    const fs::path path = resolve(*emuenv, title_id);
    return to_jstring(env, path.empty() ? std::string() : path.generic_string());
}

} // extern "C"
