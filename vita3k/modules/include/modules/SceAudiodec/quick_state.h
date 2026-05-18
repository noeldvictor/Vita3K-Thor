#pragma once

#include <cstddef>
#include <map>
#include <string>

struct EmuEnvState;

namespace sce_audiodec {

std::string quick_state_snapshot_text(EmuEnvState &emuenv);
bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, size_t *decoder_count, size_t *library_count, bool *exact_restore, std::string *detail = nullptr);
bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail = nullptr);

} // namespace sce_audiodec
