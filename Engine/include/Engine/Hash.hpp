#ifndef ELIX_HASH_HPP
#define ELIX_HASH_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <cstddef>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace hashing
{
    template<typename T, typename S>
    inline void hash(T& data, S v)
    {
        data ^= v + 0x9e3779b9 + (data << 6) + (data >> 2);
    }
} //namespace hashing

class Hash
{
public:
    void setData(std::size_t data);
    std::size_t getData() const;

    void hash(std::size_t data);

private:
    std::size_t m_data;
};

ELIX_NESTED_NAMESPACE_END


#endif //ELIX_HASH_HPP