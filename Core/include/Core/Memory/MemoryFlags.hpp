#ifndef ELIX_MEMORY_FLAGS_HPP
#define ELIX_MEMORY_FLAGS_HPP

#include "Core/Macros.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(memory)

enum class MemoryUsage
{
    GPU_ONLY,
    CPU_TO_GPU,
    GPU_TO_CPU,
    CPU_ONLY,
    AUTO // ONLY WITH VMA ALLOCATOR
};

inline bool isMemoryMappable(MemoryUsage memoryUsage)
{
    return memoryUsage == MemoryUsage::CPU_TO_GPU ||
           memoryUsage == MemoryUsage::GPU_TO_CPU ||
           memoryUsage == MemoryUsage::CPU_ONLY;
}

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_MEMORY_FLAGS_HPP