#ifndef ELIX_RENDER_GRAPH_HPP
#define ELIX_RENDER_GRAPH_HPP

#include "Core/Macros.hpp"
#include "Core/SwapChain.hpp"
#include "Core/CommandBuffer.hpp"
#include "Core/CommandPool.hpp"
#include "Core/SyncObject.hpp"
#include "Core/DescriptorSetLayout.hpp"
#include "Core/PipelineLayout.hpp"

#include "Engine/TextureImage.hpp"
#include "Engine/Render/RenderGraphPassResourceBuilder.hpp"
#include "Engine/Render/RenderGraphPassRecourceCompiler.hpp"
#include "Engine/Render/GraphPasses/IRenderGraphPass.hpp"
#include "Engine/Scene.hpp"
#include "Engine/Camera.hpp"

#include <typeindex>
#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class RenderGraph
{
public:
    RenderGraph(VkDevice device, core::SwapChain::SharedPtr swapchain, Scene::SharedPtr scene);

    template<typename T, typename... Args>
    T* addPass(Args&&... args)
    {
        static_assert(!std::is_abstract_v<T>, "RenderGraph::addPass() Cannot add abstract render pass!");
        static_assert(std::is_base_of_v<IRenderGraphPass, T>, "RenderGraph::addPass() T must derive from IRenderGraphPass class");

        const auto type = std::type_index(typeid(T));
        auto renderPass = std::make_shared<T>(std::forward<Args>(args)...);
        T* ptr = renderPass.get();

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
    void createCameraDescriptorSets(VkSampler sampler, VkImageView imageView);
    void createDirectionalLightDescriptorSets();
    void createDataFromScene();

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
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};

    core::SwapChain::SharedPtr m_swapchain{nullptr};
    core::CommandPool::SharedPtr m_commandPool{nullptr};

    core::PipelineLayout::SharedPtr m_pipelineLayout{nullptr};

    uint32_t m_imageIndex{0};
    uint32_t m_currentFrame{0};

    VkDescriptorPool m_descriptorPool{VK_NULL_HANDLE};

    core::DescriptorSetLayout::SharedPtr m_materialSetLayout{nullptr};

    core::DescriptorSetLayout::SharedPtr m_directionalLightSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_directionalLightDescriptorSets;
    std::vector<core::Buffer::SharedPtr> m_lightSSBOs;

    std::vector<void*> m_cameraMapped;
    std::vector<core::Buffer::SharedPtr> m_cameraUniformObjects;
    core::DescriptorSetLayout::SharedPtr m_cameraSetLayout{nullptr};
    std::vector<VkDescriptorSet> m_cameraDescriptorSets;

    std::vector<void*> m_lightMapped;
    std::vector<core::Buffer::SharedPtr> m_lightSpaceMatrixUniformObjects;

    std::vector<core::CommandBuffer::SharedPtr> m_commandBuffers;
    std::vector<std::vector<core::CommandBuffer::SharedPtr>> m_secondaryCommandBuffers;

    RenderGraphPassRecourceBuilder m_resourceBuilder;
    RenderGraphPassResourceHash m_resourceStorage;
    RenderGraphPassResourceCompiler m_resourceCompiler;

    RenderGraphPassPerFrameData m_perFrameData;

    std::vector<VkFence> m_inFlightFences;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_imagesInFlight;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_RENDER_GRAPH_HPP