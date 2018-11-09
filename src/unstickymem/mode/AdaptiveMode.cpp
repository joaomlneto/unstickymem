#include <numa.h>
#include <numaif.h>

#include "unstickymem/mode/AdaptiveMode.hpp"
#include "unstickymem/Logger.hpp"

namespace unstickymem {

static Mode::Registrar<AdaptiveMode> registrar(AdaptiveMode::name(),
                                               AdaptiveMode::description());

void AdaptiveMode::start() {
  LFATAL("Magic! This is the adaptive algorithm");

  // interleave memory by default
  LINFO("Setting default memory policy to interleaved");
  set_mempolicy(MPOL_INTERLEAVE,
                numa_get_mems_allowed()->maskp,
                numa_get_mems_allowed()->size);
}

}  // namespace unstickymem
