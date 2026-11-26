#pragma once
// LAPIS_ASSERT(cond, "message {}", args...)  - checked in debug builds only.
// LAPIS_VERIFY(cond, "message {}", args...)  - always evaluated; breaks in debug.

#include "Lapis/Core/Base.h"
#include "Lapis/Core/Log.h"

#if LAPIS_DEBUG
    #define LAPIS_ASSERT(cond, ...)                                                        \
        do {                                                                               \
            if (!(cond)) {                                                                 \
                LAPIS_FATAL("Assertion failed: {} ({}:{})", #cond, __FILE__, __LINE__);    \
                LAPIS_FATAL(__VA_ARGS__);                                                  \
                LAPIS_DEBUG_BREAK();                                                       \
            }                                                                              \
        } while (0)
#else
    #define LAPIS_ASSERT(cond, ...) ((void)0)
#endif

#define LAPIS_VERIFY(cond, ...)                                                            \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            LAPIS_ERROR("Verify failed: {} ({}:{})", #cond, __FILE__, __LINE__);           \
            LAPIS_ERROR(__VA_ARGS__);                                                      \
            if (LAPIS_DEBUG_ENABLED) LAPIS_DEBUG_BREAK();                                  \
        }                                                                                  \
    } while (0)

#if LAPIS_DEBUG
    #define LAPIS_DEBUG_ENABLED 1
#else
    #define LAPIS_DEBUG_ENABLED 0
#endif
