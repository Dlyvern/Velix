#include "Engine/Assets/Compressor.hpp"

#include <algorithm>

#if defined(ELIX_HAS_ZLIB)
#include <zlib.h>
#endif

ELIX_NESTED_NAMESPACE_BEGIN(engine)

bool Compressor::compress(const std::vector<uint8_t> &input, std::vector<uint8_t> &output, Algorithm algorithm, int compressionLevel)
{
    if (algorithm == Algorithm::None)
    {
        output = input;
        return true;
    }

    if (input.empty())
    {
        output.clear();
        return true;
    }

#if defined(ELIX_HAS_ZLIB)
    if (algorithm == Algorithm::Deflate)
    {
        const uLong sourceSize = static_cast<uLong>(input.size());
        const uLongf boundSize = compressBound(sourceSize);
        output.resize(static_cast<size_t>(boundSize));

        uLongf compressedSize = boundSize;
        const int level = std::max(-1, std::min(9, compressionLevel));
        const int result = compress2(reinterpret_cast<Bytef *>(output.data()),
                                     &compressedSize,
                                     reinterpret_cast<const Bytef *>(input.data()),
                                     sourceSize,
                                     level);
        if (result != Z_OK)
        {
            output.clear();
            return false;
        }

        output.resize(static_cast<size_t>(compressedSize));
        return true;
    }
#endif

    output.clear();
    return false;
}

bool Compressor::decompress(const std::vector<uint8_t> &input, size_t expectedSize, std::vector<uint8_t> &output, Algorithm algorithm)
{
    if (algorithm == Algorithm::None)
    {
        output = input;
        return true;
    }

    if (expectedSize == 0u)
    {
        output.clear();
        return input.empty();
    }

    if (input.empty())
        return false;

#if defined(ELIX_HAS_ZLIB)
    if (algorithm == Algorithm::Deflate)
    {
        output.resize(expectedSize);
        uLongf destinationSize = static_cast<uLongf>(expectedSize);

        const int result = uncompress(reinterpret_cast<Bytef *>(output.data()),
                                      &destinationSize,
                                      reinterpret_cast<const Bytef *>(input.data()),
                                      static_cast<uLong>(input.size()));
        if (result != Z_OK || destinationSize != expectedSize)
        {
            output.clear();
            return false;
        }

        return true;
    }
#endif

    output.clear();
    return false;
}

ELIX_NESTED_NAMESPACE_END
