#ifndef ELIX_RENDER_GRAPH_HPP
#define ELIX_RENDER_GRAPH_HPP

#include "Core/Macros.hpp"
#include "Core/SwapChain.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/CommandPool.hpp"
#include "Core/SyncObject.hpp"

#include "Engine/TextureImage.hpp"

#include "Engine/Render/RenderGraphPassResourceBuilder.hpp"
#include "Engine/Render/RenderGraphPassRecourceCompiler.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/GraphicsPipelineBuilder.hpp"

#include "Core/DescriptorSetLayout.hpp"

#include "Engine/Scene.hpp"

#include "Core/GraphicsPipeline.hpp"
#include "Core/PipelineLayout.hpp"

#include <typeindex>
#include <unordered_map>

#include "Engine/Camera.hpp"

#include "Engine/UniformBufferObject.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class RenderGraph
{
public:
    RenderGraph(VkDevice device, core::SwapChain::SharedPtr swapchain, Scene::SharedPtr scene);

    template<typename T, typename... Args>
    T* addPass(Args&&... args)
    {
        static_assert(!std::is_abstract_v<T>, "RenderGraph::addPass() Cannot add abstract component!");
        static_assert(std::is_base_of_v<IRenderGraphPass, T>, "RenderGraph::addPass() T must derive from IRenderGraphPass class");

        const auto type = std::type_index(typeid(T));
        auto renderPass = std::make_shared<T>(std::forward<Args>(args)...);
        T* ptr = renderPass.get();

        // m_passExecutionOrder.push_back(renderPass);
        m_renderGraphPasses[type] = std::move(renderPass);

        return ptr;
    }

    void prepareFrame(Camera::SharedPtr camera);
    void draw();
    void setup();

    static constexpr uint16_t MAX_FRAMES_IN_FLIGHT = 2;

    core::DescriptorSetLayout::SharedPtr getMaterialDescriptorSetLayout() const
    {
        return m_materialSetLayout;
    }

    core::GraphicsPipeline::SharedPtr getGraphicsPipeline() const
    {
        return m_graphicsPipeline;
    }

    core::PipelineLayout::SharedPtr  getPipelineLayout() const
    {
        return m_pipelineLayout;
    }

    const std::vector<VkDescriptorSet>& getCameraDescriptorSets() const
    {
        return m_cameraDescriptorSets;
    }

    const std::vector<VkDescriptorSet>& getLightDescriptorSets() const
    {
        return m_directionalLightDescriptorSets;
    }

    VkDescriptorPool getDescriptorPool() const
    {
        return m_descriptorPool;
    }

    core::CommandPool::SharedPtr getCommandPool() const
    {
        return m_commandPool;
    }

    void createRenderGraphResources();
    void createDescriptorSetPool();
    void createDescriptorSetLayouts();
    void createCameraDescriptorSets(VkSampler sampler, VkImageView imageView);
    void createGraphicsPipeline();
    void createDirectionalLightDescriptorSets();

    void cleanResources();

private:
    struct CameraUBO
    {
        glm::mat4 view;
        glm::mat4 projection;
    };

    struct LightData
    {
        glm::vec4 position;
        glm::vec4 direction;
        glm::vec4 colorStrength;
        glm::vec4 parameters;
    };

    struct LightSSBO
    {
        int lightCount;
        glm::vec3 padding{0.0f};
        LightData lights[];
    };  

    struct LightSpaceMatrixUBO
    {
        glm::mat4 lightSpaceMatrix;
    };

    void begin();
    void end();

    Scene::SharedPtr m_scene{nullptr};

    void compile();
    std::unordered_map<std::type_index, IRenderGraphPass::SharedPtr> m_renderGraphPasses;
    
    VkDevice m_device{VK_NULL_HANDLE};

    core::SwapChain::SharedPtr m_swapchain{nullptr};
    core::CommandPool::SharedPtr m_commandPool{nullptr};

    core::SyncObject::UniquePtr m_syncObject{nullptr};

    core::GraphicsPipeline::SharedPtr m_graphicsPipeline{nullptr};
    
    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};
    
    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};

    bool m_rebuildSwapchain{false};
    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};


    core::DescriptorSetLayout::SharedPtr m_materialSetLayout{nullptr};

    core::DescriptorSetLayout::SharedPtr m_directionalLightSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_directionalLightDescriptorSets;
    std::vector<core::Buffer::SharedPtr> m_lightSSBOs;


    std::vector<UniformBufferObject<CameraUBO>::SharedPtr> m_cameraUniformObjects;
    core::DescriptorSetLayout::SharedPtr m_cameraSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_cameraDescriptorSets;

    std::vector<UniformBufferObject<LightSpaceMatrixUBO>::SharedPtr> m_lightSpaceMatrixUniformObjects;

    std::vector<core::CommandBuffer::SharedPtr> m_commandBuffers;
    std::vector<std::vector<core::CommandBuffer::SharedPtr>> m_secondaryCommandBuffers;

    core::RenderPass::SharedPtr m_swapChainRenderPass{nullptr};

    RenderGraphPassRecourceBuilder m_resourceBuilder;
    RenderGraphPassResourceHash m_resourceStorage;
    RenderGraphPassResourceCompiler m_resourceCompiler;

    RenderGraphPassPerFrameData m_perFrameData;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_GRAPH_HPP