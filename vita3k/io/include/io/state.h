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

#pragma once

#include <io/file.h>
#include <io/filesystem.h>
#include <io/types.h>
#include <io/util.h>

#include <map>
#include <set>
#include <unordered_map>

// Class for all needed information to access files on Vita3K.
class FileStats : public VitaStats {
    // Shared file pointer
    FilePtr wrapped_file;
    std::shared_ptr<ReadOnlyInMemFile> memory_file;

public:
    // Constructor used for files
    // Based on https://codereview.stackexchange.com/questions/4679/
    explicit FileStats(const char *vita, const std::string &t, const fs::path &file, const int open) {
        wrapped_file = create_shared_file(file, open);

        file_info.vita_loc = vita;
        file_info.translated = t;
        file_info.sys_loc = file;
        file_info.open_mode = open;
        file_info.file_mode = SCE_SO_IFREG | SCE_SO_IROTH;
        file_info.access_mode = SCE_S_IFREG;
    }

    explicit FileStats(const char *vita, const std::string &t, const fs::path &file, const int open, const std::vector<SceUInt8> &data) {
        memory_file = std::make_shared<ReadOnlyInMemFile>(reinterpret_cast<const char *>(data.data()), data.size());

        file_info.vita_loc = vita;
        file_info.translated = t;
        file_info.sys_loc = file;
        file_info.open_mode = open;
        file_info.file_mode = SCE_SO_IFREG | SCE_SO_IROTH;
        file_info.access_mode = SCE_S_IFREG;
    }

    bool is_regular_file() const {
        return file_info.file_mode & SCE_SO_IFREG;
    }

    // Check if the file is writable
    bool can_write_file() const {
        if (memory_file)
            return false;
        if (!is_regular_file())
            return false;

        return can_write(file_info.open_mode);
    }

    bool is_memory_file() const {
        return memory_file != nullptr;
    }

    SceOff size() const;

    // File operations
    FILE *get_file_pointer() const {
        return wrapped_file.get();
    }

    // File functions
    SceOff read(void *input_data, int element_size, SceSize element_count) const;
    SceOff write(const void *data, SceSize size, int count) const;
    int truncate(const SceSize size) const;
    bool seek(SceOff offset, SceIoSeekMode seek_mode) const;
    SceOff tell() const;
};

// Class for implementing Directory structure; path names are wide for Windows, normal for else
class DirStats : public VitaStats {
    // Shared directory pointer
    DirPtr dir_ptr;
    std::vector<std::string> memory_entries;
    mutable size_t memory_entry_index = 0;
    size_t native_entry_index = 0;
    bool memory_directory = false;

public:
    DirStats(const char *vita, const std::string &t, const fs::path &file, DirPtr ptr) {
        dir_ptr = std::move(ptr);

        file_info.vita_loc = vita;
        file_info.translated = t;
        file_info.sys_loc = file;
        file_info.open_mode = SCE_O_RDONLY;
        file_info.file_mode = SCE_SO_IFDIR | SCE_SO_IROTH;
        file_info.access_mode = SCE_S_IFDIR | SCE_S_IRUSR;
    }

    DirStats(const char *vita, const std::string &t, const fs::path &file, const std::vector<std::string> &entries) {
        memory_entries = entries;
        memory_directory = true;

        file_info.vita_loc = vita;
        file_info.translated = t;
        file_info.sys_loc = file;
        file_info.open_mode = SCE_O_RDONLY;
        file_info.file_mode = SCE_SO_IFDIR | SCE_SO_IROTH;
        file_info.access_mode = SCE_S_IFDIR | SCE_S_IRUSR;
    }

    auto get_dir_ptr() const {
        return get_system_dir_ptr(dir_ptr);
    }

    bool is_directory() const {
        return file_info.file_mode & SCE_SO_IFDIR;
    }

    bool is_memory_directory() const {
        return memory_directory;
    }

    std::string get_next_memory_entry() const {
        if (memory_entry_index >= memory_entries.size())
            return {};

        return memory_entries[memory_entry_index++];
    }

    void note_native_entry_read() {
        if (!memory_directory)
            native_entry_index++;
    }

    size_t entries_read() const {
        return memory_directory ? memory_entry_index : native_entry_index;
    }

    bool restore_entry_position(const size_t entry_index) {
        if (memory_directory) {
            if (entry_index > memory_entries.size())
                return false;
            memory_entry_index = entry_index;
            return true;
        }

        if (!dir_ptr)
            return entry_index == 0;

        for (size_t i = 0; i < entry_index; i++) {
            if (!get_system_dir_ptr(dir_ptr))
                return false;
        }
        native_entry_index = entry_index;
        return true;
    }
};

typedef std::map<SceUID, TtyType> TtyFiles;
typedef std::map<SceUID, FileStats> StdFiles;
typedef std::map<SceUID, DirStats> DirEntries;

struct IOState {
    struct ArchiveMount {
        struct Entry {
            std::string archive_name;
            std::uint64_t size = 0;
            bool directory = false;
        };

        fs::path archive_path;
        std::string content_root;
        std::unordered_map<std::string, Entry> entries;
        std::unordered_map<std::string, std::string> lower_to_path;
        std::map<std::string, std::set<std::string>> dir_children;

        bool mounted() const {
            return !archive_path.empty();
        }

        void clear() {
            archive_path.clear();
            content_root.clear();
            entries.clear();
            lower_to_path.clear();
            dir_children.clear();
        }
    };

    struct DevicePaths {
        std::string app0;
        std::string savedata0;
        std::string addcont0;
    } device_paths;

    std::string addcont;
    std::string content_id;
    std::string savedata;
    std::string title_id;
    std::string app_path;
    fs::path app0_host_path;
    ArchiveMount app0_archive;

    std::string user_id;
    std::string user_name;

    bool redirect_stdio;

    SceUID next_fd = 0;
    TtyFiles tty_files;
    StdFiles std_files;
    DirEntries dir_entries;

    std::unordered_map<std::string, std::string> cachemap;
    bool case_isens_find_enabled = false;

    std::mutex overlay_mutex;
    SceUID next_overlay_id = 1;
    // overlay in the order they should be applied
    std::vector<FiosOverlay> overlays;
};
