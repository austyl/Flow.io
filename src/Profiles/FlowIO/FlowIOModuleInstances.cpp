#include "Profiles/FlowIO/FlowIOProfile.h"

#include "Board/BoardCatalog.h"

namespace Profiles {
namespace FlowIO {

ModuleInstances::ModuleInstances(const BoardSpec& board)
    : i2cCfgServerModule(board)
{
}

ModuleInstances& moduleInstances()
{
    static ModuleInstances instances{BoardCatalog::flowIODINv1()};
    return instances;
}

}  // namespace FlowIO
}  // namespace Profiles
