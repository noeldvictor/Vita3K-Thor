#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

struct EmuEnvState;

namespace sce_sharedfb {

std::string quick_state_snapshot_text(EmuEnvState &emuenv);
bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, bool *created = nullptr, uint32_t *memsize = nullptr, uint32_t *base1 = nullptr, uint32_t *base2 = nullptr, std::string *detail = nullptr);
bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail = nullptr);

} // namespace sce_sharedfb
