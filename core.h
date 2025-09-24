#ifndef CORE_H
#define CORE_H

#include <cstdint>
#include <memory>
#include <string>

//------------------------------------------------------------------------------------
// Typedefs
//------------------------------------------------------------------------------------

typedef char    i8; // int8_t defined as "signed char" that causes some issues
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
