#pragma once

#include <cstddef>
#include <map>
#include <string>

struct EmuEnvState;

namespace sce_fiber {

std::string quick_state_snapshot_text(EmuEnvState &emuenv);
bool quick_state_validate_snapshot_values(const std::map<std::string, std::string> &values, size_t *fiber_count, size_t *active_thread_count, std::string *detail = nullptr);
void quick_state_discard_live_host_state(EmuEnvState &emuenv);
bool quick_state_restore_snapshot(EmuEnvState &emuenv, const std::map<std::string, std::string> &values, std::string *detail = nullptr);

} // namespace sce_fiber
