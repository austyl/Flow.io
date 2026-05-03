#include "Profiles/Micronova/MicronovaProfile.h"

#include "Board/BoardCatalog.h"
#include "Core/FirmwareVersion.h"
#include "Domain/DomainCatalog.h"

namespace Profiles {
namespace Micronova {

const FirmwareProfile& profile()
{
    static const FirmwareProfile kProfile{
        "Micronova",
        &BoardCatalog::micronovaBoardRev1(),
        &DomainCatalog::supervisor(),
        {
            "Flow.io Micronova",
            "flowio-micronova",
            FirmwareVersion::Full,
            "rt"
        },
        nullptr,
        setupProfile,
        loopProfile
    };
    return kProfile;
}

}  // namespace Micronova
}  // namespace Profiles
