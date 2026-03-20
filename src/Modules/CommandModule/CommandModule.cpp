/**
 * @file CommandModule.cpp
 * @brief Implementation file.
 */
#include "CommandModule.h"
#define LOG_MODULE_ID ((LogModuleId)LogModuleIdValue::CommandModule)
#include "Core/ModuleLog.h"


bool CommandModule::svcRegister(void* ctx, const char* cmd, CommandHandler fn, void* userCtx) {
    return ((CommandRegistry*)ctx)->registerHandler(cmd, fn, userCtx);
}

bool CommandModule::svcExecute(void* ctx, const char* cmd, const char* json, const char* args,
                               char* reply, size_t replyLen) {
    return ((CommandRegistry*)ctx)->execute(cmd, json, args, reply, replyLen);
}

void CommandModule::init(ConfigStore&, ServiceRegistry& services) {
    static CommandService svc{ svcRegister, svcExecute, nullptr };
    svc.ctx = &registry;
    if (!services.add(ServiceId::Command, &svc)) {
        LOGE("service registration failed: %s", toString(ServiceId::Command));
    }
    
    logHub = services.get<LogHubService>(ServiceId::LogHub);
    LOGI("CommandService registered");
}
