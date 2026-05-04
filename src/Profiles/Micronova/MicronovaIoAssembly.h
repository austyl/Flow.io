#pragma once

#include "Board/BoardSpec.h"

namespace Profiles {
namespace Micronova {

struct ModuleInstances;

bool configureIoModule(const BoardSpec& board, ModuleInstances& modules);

}  // namespace Micronova
}  // namespace Profiles
