#ifndef ELIX_COMPRESSOR_HPP
#define ELIX_COMPRESSOR_HPP

#include "Core/Macros.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Compressor
{
public:
    enum class Algorithm : uint8_t
    {
        None = 0,
        Deflate = 1
    };

    static bool compress(const std::vector<uint8_t> &input, std::vector<uint8_t> &output, Algorithm algorithm = Algorithm::Deflate, int compressionLevel = 6);
    static bool decompress(const std::vector<uint8_t> &input, size_t expectedSize, std::vector<uint8_t> &output, Algorithm algorithm);
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_COMPRESSOR_HPP
