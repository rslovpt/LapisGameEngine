#pragma once
// Fundamental types, platform detection and helper macros used everywhere.

#include <cstdint>
#include <cstddef>
#include <memory>
#include <utility>

// ---------------------------------------------------------------------------
// Platform detection
// ---------------------------------------------------------------------------
#if defined(_WIN32)
    #define LAPIS_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define LAPIS_PLATFORM_LINUX 1
#elif defined(__APPLE__)
    #define LAPIS_PLATFORM_MACOS 1
#else
    #error "Lapis: unsupported platform"
#endif

#if defined(_DEBUG) || !defined(NDEBUG)
    #define LAPIS_DEBUG 1
#endif

#if defined(_MSC_VER)
    #define LAPIS_DEBUG_BREAK() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
    #define LAPIS_DEBUG_BREAK() __builtin_trap()
#else
    #define LAPIS_DEBUG_BREAK() ((void)0)
#endif

#define LAPIS_BIT(x) (1u << (x))
#define LAPIS_UNUSED(x) ((void)(x))

namespace Lapis {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

// Ownership aliases: Scope = unique ownership, Ref = shared ownership.
template <typename T> using Scope = std::unique_ptr<T>;
template <typename T> using Ref   = std::shared_ptr<T>;

template <typename T, typename... Args>
constexpr Scope<T> CreateScope(Args&&... args) { return std::make_unique<T>(std::forward<Args>(args)...); }

template <typename T, typename... Args>
constexpr Ref<T> CreateRef(Args&&... args) { return std::make_shared<T>(std::forward<Args>(args)...); }

// Non-copyable mixin for GPU resources and other unique handles.
class NonCopyable {
protected:
    NonCopyable() = default;
    ~NonCopyable() = default;
public:
    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;
};

} // namespace Lapis
