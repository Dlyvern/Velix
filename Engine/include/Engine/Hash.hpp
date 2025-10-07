#ifndef ELIX_HASH_HPP
#define ELIX_HASH_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <cstddef>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

namespace hashing
{
    void hash(std::size_t& data, std::size_t v);
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