#include "Engine/Hash.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

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