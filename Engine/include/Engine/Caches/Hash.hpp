#ifndef ELIX_HASH_HPP
#define ELIX_HASH_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <cstddef>

ELIX_NESTED_NAMESPACE_BEGIN(engine)
ELIX_CUSTOM_NAMESPACE_BEGIN(hashing)

template <typename T, typename S>
inline void hash(T &data, S v)
{
    data ^= v + 0x9e3779b9 + (data << 6) + (data >> 2);
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_HASH_HPP