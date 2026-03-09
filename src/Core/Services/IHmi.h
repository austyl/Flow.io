#pragma once
/**
 * @file IHmi.h
 * @brief HMI service interface.
 */

#include <stddef.h>
#include <stdint.h>

struct HmiService {
    bool (*requestRefresh)(void* ctx);
    bool (*openConfigHome)(void* ctx);
    bool (*openConfigModule)(void* ctx, const char* module);
    bool (*buildConfigMenuJson)(void* ctx, char* out, size_t outLen);
    bool (*setLedPage)(void* ctx, uint8_t page);
    uint8_t (*getLedPage)(void* ctx);
    void* ctx;
};
