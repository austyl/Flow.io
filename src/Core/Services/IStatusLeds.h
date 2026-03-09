#pragma once
/**
 * @file IStatusLeds.h
 * @brief Service interface for 8-bit status LED panel masks.
 */

#include <stdint.h>

/**
 * @brief Service wrapper to drive/read the logical LED mask.
 *
 * Logical bit `1` means LED on. Physical active-low/high is handled by IO backend.
 */
struct StatusLedsService {
    bool (*setMask)(void* ctx, uint8_t mask, uint32_t tsMs);
    bool (*getMask)(void* ctx, uint8_t* outMask);
    void* ctx;
};
