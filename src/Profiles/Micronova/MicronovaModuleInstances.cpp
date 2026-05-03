#include "Profiles/Micronova/MicronovaProfile.h"

#include "Board/BoardCatalog.h"
#include "Board/BoardSpec.h"

namespace {

int micronovaTemperaturePin_(const BoardSpec& board)
{
    const OneWireBusSpec* spec = boardFindOneWire(board, BoardSignal::TempProbe1);
    return spec ? (int)spec->pin : 18;
}

}  // namespace

namespace Profiles {
namespace Micronova {

ModuleInstances::ModuleInstances(const BoardSpec& board)
    : wifiModule(board),
      ioModule(board),
      webInterfaceModule(board),
      micronovaBusModule(board),
      oneWireTemperature(micronovaTemperaturePin_(board))
{
    micronovaBoilerModule.setBus(&micronovaBusModule);
}

ModuleInstances& moduleInstances()
{
    static ModuleInstances instances{BoardCatalog::micronovaBoardRev1()};
    return instances;
}

}  // namespace Micronova
}  // namespace Profiles
