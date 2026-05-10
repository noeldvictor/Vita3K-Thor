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

#include <io/file.h>
#include <io/types.h>

ReadOnlyInMemFile::ReadOnlyInMemFile() = default;

ReadOnlyInMemFile::ReadOnlyInMemFile(const char *data, size_t size) {
    if (size)
        buf.assign(data, data + size);
}

char *ReadOnlyInMemFile::alloc_data(size_t bufsize) {
    buf.resize(bufsize);
    return buf.data();
}

size_t ReadOnlyInMemFile::read(void *ibuf, size_t size) {
    size_t res = size;

    if (currentPos >= buf.size())
        return 0;

    if (currentPos + size > buf.size()) {
        res = buf.size() - currentPos;
    }

    if (res == 0)
        return 0;

    memcpy(ibuf, buf.data() + currentPos, res);
    currentPos += res;

    return res;
}

const char *ReadOnlyInMemFile::data() {
    return buf.data();
}

bool ReadOnlyInMemFile::seek(SceOff offset, int origin) {
    if (!valid())
        return false;

    SceOff new_pos = 0;
    if (origin == SCE_SEEK_SET) {
        new_pos = offset;
    } else if (origin == SCE_SEEK_CUR) {
        new_pos = static_cast<SceOff>(currentPos) + offset;
    } else if (origin == SCE_SEEK_END) {
        new_pos = static_cast<SceOff>(buf.size()) + offset;
    } else {
        return false;
    }

    if (new_pos < 0)
        return false;

    currentPos = static_cast<size_t>(new_pos);
    return true;
}
