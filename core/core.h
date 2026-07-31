#ifndef CORE_H
#define CORE_H

#include <cstdint>
#include <memory>
#include <string>

//------------------------------------------------------------------------------------
// Typedefs
//------------------------------------------------------------------------------------

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef float  r32;
typedef double r64;

typedef uint8_t byte;

typedef std::string  str;
typedef std::wstring wstr;
typedef const char*  cstr;

constexpr u64 KB = 1000;
constexpr u64 MB = 1000 * KB;
constexpr u64 GB = 1000 * MB;

constexpr u64 KiB = 1024;
constexpr u64 MiB = 1024 * KiB;
constexpr u64 GiB = 1024 * MiB;

//------------------------------------------------------------------------------------
// Refs
//------------------------------------------------------------------------------------

template<typename T>
using Scope = std::unique_ptr<T>;

template<typename T, typename ... Args>
constexpr Scope<T> MakeScope(Args&& ... args)
{
    return std::make_unique<T>(std::forward<Args>(args)...);
}

template<typename T>
using Ref = std::shared_ptr<T>;

template<typename T, typename ... Args>
constexpr Ref<T> MakeRef(Args&& ... args)
{
    return std::make_shared<T>(std::forward<Args>(args)...);
}

template<typename T>
using Weak = std::weak_ptr<T>;

//------------------------------------------------------------------------------------

#endif // CORE_H
