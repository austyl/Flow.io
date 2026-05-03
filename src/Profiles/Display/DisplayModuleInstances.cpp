#include "Profiles/Display/DisplayProfile.h"

namespace Profiles {
namespace Display {

ModuleInstances& moduleInstances()
{
    static ModuleInstances instances{};
    return instances;
}

}  // namespace Display
}  // namespace Profiles
