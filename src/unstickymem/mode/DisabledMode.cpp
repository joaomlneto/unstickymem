#include <memory>
#include <functional>

#include "unstickymem/mode/DisabledMode.hpp"
#include "unstickymem/Logger.hpp"

namespace unstickymem {

static Mode::Registrar<DisabledMode> registrar(DisabledMode::name(),
                                               DisabledMode::description());

void DisabledMode::start() {
  LINFO("Doing nothing");
}

}  // namespace unstickymem
