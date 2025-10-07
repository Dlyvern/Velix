#include "Engine/Hash.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace hashing
{
    void hash(std::size_t& data, std::size_t v)
    {
        data ^= v + 0x9e3779b97f4a7c15ULL + (data << 6) + (data >> 2);
    }
}

void Hash::setData(std::size_t data)
{
    m_data = data;
}

std::size_t Hash::getData() const
{
    return m_data;
}

void Hash::hash(std::size_t data)
{
    hashing::hash(m_data, data);
}

ELIX_NESTED_NAMESPACE_END