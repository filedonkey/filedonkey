#ifndef CORE_H
#define CORE_H

#include <memory>
#include <string>

//------------------------------------------------------------------------------------
// Typedefs
//------------------------------------------------------------------------------------

typedef __int8  i8;
typedef __int16 i16;
typedef __int32 i32;
typedef __int64 i64;

typedef unsigned __int8  u8;
typedef unsigned __int16 u16;
typedef unsigned __int32 u32;
typedef unsigned __int64 u64;

typedef float  r32;
typedef double r64;

typedef std::string  str;
typedef std::wstring wstr;
typedef const char* cstr;

typedef float* rgba;

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
