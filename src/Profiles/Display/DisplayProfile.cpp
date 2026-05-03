#include "Profiles/Display/DisplayProfile.h"

#include "Board/BoardCatalog.h"
#include "Core/FirmwareVersion.h"
#include "Domain/DomainCatalog.h"

namespace Profiles {
namespace Display {

const FirmwareProfile& profile()
{
    static const FirmwareProfile kProfile{
        "Display",
        &BoardCatalog::flowIODINv1(),
        &DomainCatalog::supervisor(),
        {
            "Flow.io Display",
            "flowio-display",
            FirmwareVersion::Full,
            "rt"
        },
        nullptr,
        setupProfile,
        loopProfile
    };
    return kProfile;
}

}  // namespace Display
}  // namespace Profiles
