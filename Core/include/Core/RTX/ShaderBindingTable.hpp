#ifndef ELIX_CORE_RTX_SHADER_BINDING_TABLE_HPP
#define ELIX_CORE_RTX_SHADER_BINDING_TABLE_HPP

#include "Core/Buffer.hpp"
#include "Core/Macros.hpp"
#include "Core/RTX/RayTracingPipeline.hpp"

#include <volk.h>

#include <cstdint>
#include <memory>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(core)
ELIX_CUSTOM_NAMESPACE_BEGIN(rtx)

class ShaderBindingTable
{
public:
    using SharedPtr = std::shared_ptr<ShaderBindingTable>;

    static SharedPtr create(const RayTracingPipeline &pipeline,
                            const std::vector<uint32_t> &raygenGroups,
                            const std::vector<uint32_t> &missGroups,
                            const std::vector<uint32_t> &hitGroups,
                            const std::vector<uint32_t> &callableGroups = {});

    ~ShaderBindingTable();

    const core::Buffer::SharedPtr &buffer() const
    {
        return m_buffer;
    }

    const VkStridedDeviceAddressRegionKHR &raygenRegion() const
    {
        return m_raygenRegion;
    }

    const VkStridedDeviceAddressRegionKHR &missRegion() const
    {
        return m_missRegion;
    }

    const VkStridedDeviceAddressRegionKHR &hitRegion() const
    {
        return m_hitRegion;
    }

    const VkStridedDeviceAddressRegionKHR &callableRegion() const
    {
        return m_callableRegion;
    }

    bool isValid() const
    {
        return m_buffer != nullptr;
    }

    void destroy();

private:
    ShaderBindingTable() = default;

    bool createInternal(const RayTracingPipeline &pipeline,
                        const std::vector<uint32_t> &raygenGroups,
                        const std::vector<uint32_t> &missGroups,
                        const std::vector<uint32_t> &hitGroups,
                        const std::vector<uint32_t> &callableGroups);

    core::Buffer::SharedPtr m_buffer{nullptr};
    VkStridedDeviceAddressRegionKHR m_raygenRegion{};
    VkStridedDeviceAddressRegionKHR m_missRegion{};
    VkStridedDeviceAddressRegionKHR m_hitRegion{};
    VkStridedDeviceAddressRegionKHR m_callableRegion{};
};

ELIX_CUSTOM_NAMESPACE_END
ELIX_NESTED_NAMESPACE_END

#endif // ELIX_CORE_RTX_SHADER_BINDING_TABLE_HPP
