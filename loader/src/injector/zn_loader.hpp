#pragma once

namespace zn {
/// Load Native ZN modules whose zn_modules.txt targets match this executable.
void loadAllModules(unsigned char connect_retry = 1);
}  // namespace zn
