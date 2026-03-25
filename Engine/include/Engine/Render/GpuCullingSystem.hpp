#ifndef ELIX_GPU_CULLING_SYSTEM_HPP
#define ELIX_GPU_CULLING_SYSTEM_HPP

#include "Core/Macros.hpp"
#include "Core/Buffer.hpp"

#include <array>
#include <vector>
#include <unordered_map>
#include <cstdint>

#include <vulkan/vulkan.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class GpuCullingSystem
{
public:
    static constexpr uint32_t MAX_GPU_CULL_BATCHES = 30000u;

    static std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &viewProj);

    static bool isSphereInsideFrustum(const glm::vec3 &center, float radius,
                                      const std::array<glm::vec4, 6> &planes);

    void initialize(VkDevice device, uint32_t framesInFlight);
    void cleanup(VkDevice device);

    void dispatch(VkCommandBuffer cmd, uint32_t currentFrame, uint32_t batchCount,
                  const std::array<glm::vec4, 6> &planes);

    bool isInitialized() const { return m_pipeline != VK_NULL_HANDLE; }

    core::Buffer *getBatchBoundsSSBO(uint32_t frameIndex);

    core::Buffer *getIndirectDrawBuffer(uint32_t frameIndex);

private:
    struct PushConstants
    {
        uint32_t batchCount;
        uint32_t pad[3];
        glm::vec4 planes[6];
    };

    std::vector<core::Buffer::SharedPtr> m_batchBoundsSSBOs;
    std::vector<core::Buffer::SharedPtr> m_indirectDrawBuffers;
    std::vector<VkDescriptorSet> m_descriptorSets;

    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_descriptorSetLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_GPU_CULLING_SYSTEM_HPP
