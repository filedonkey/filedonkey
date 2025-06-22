#ifndef CORE_H
#define CORE_H

#include <memory>
#include <string>

//------------------------------------------------------------------------------------
// Typedefs
//------------------------------------------------------------------------------------

typedef char      i8;
typedef short     i16;
typedef int       i32;
typedef long long i64;

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef float  r32;
typedef double r64;

typedef unsigned char byte;

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
