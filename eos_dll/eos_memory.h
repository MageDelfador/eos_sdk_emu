#pragma once

#include "eos_client_api.h"

#include <cstring>
#include <new>
#include <string>

namespace sdk
{

inline size_t eos_effective_alignment(size_t alignment)
{
    auto& client = EOSSDK_Client::Inst();
    if (client._allocate_memory_func != nullptr && alignment < alignof(uint64_t))
        return alignof(uint64_t);
    return alignment;
}

inline void* eos_allocate_bytes(size_t size, size_t alignment)
{
    if (size == 0)
        return nullptr;

    alignment = eos_effective_alignment(alignment);

    auto& client = EOSSDK_Client::Inst();
    if (client._allocate_memory_func != nullptr)
        return client._allocate_memory_func(size, alignment);

    return ::operator new(size);
}

inline void eos_release_bytes(void* ptr)
{
    if (ptr == nullptr)
        return;

    auto& client = EOSSDK_Client::Inst();
    if (client._release_memory_func != nullptr)
        client._release_memory_func(ptr);
    else
        ::operator delete(ptr);
}

template<typename T>
inline T* eos_allocate_struct()
{
    void* mem = eos_allocate_bytes(sizeof(T), alignof(T));
    if (mem == nullptr)
        return nullptr;

    std::memset(mem, 0, sizeof(T));
    return static_cast<T*>(mem);
}

inline char* eos_allocate_c_string(std::string const& value)
{
    size_t const len = value.empty() ? 1 : value.length() + 1;
    char* str = static_cast<char*>(eos_allocate_bytes(len, alignof(char)));
    if (str == nullptr)
        return nullptr;

    if (value.empty())
    {
        str[0] = '\0';
        return str;
    }

    std::memcpy(str, value.c_str(), len);
    return str;
}

} // namespace sdk
