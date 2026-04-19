#include "Board/BoardCatalog.h"

#include "App/BuildFlags.h"
#include "Board/FlowIODINBoards.h"
#include "Board/SupervisorBoardRev1.h"

namespace BoardCatalog {

const BoardSpec& flowIODINv1()
{
    return BoardProfiles::kFlowIODINv1;
}

const BoardSpec& flowIODINv2()
{
    return BoardProfiles::kFlowIODINv2;
}

const BoardSpec& supervisorBoardRev1()
{
    return BoardProfiles::kSupervisorBoardRev1;
}

const BoardSpec& activeBoard()
{
#if FLOW_BUILD_IS_FLOWIO
    return flowIODINv1();
#else
    return supervisorBoardRev1();
#endif
}

}  // namespace BoardCatalog
