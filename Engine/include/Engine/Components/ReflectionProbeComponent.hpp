#ifndef ELIX_REFLECTION_PROBE_COMPONENT_HPP
#define ELIX_REFLECTION_PROBE_COMPONENT_HPP

#include "Core/Macros.hpp"
#include "Core/Image.hpp"
#include "Core/Sampler.hpp"
#include "Engine/Components/ECS.hpp"
#include "Engine/Skybox.hpp"

#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class ReflectionProbeComponent : public ECS
{
public:
    float radius{5.0f};
    float intensity{1.0f};
    std::string hdrPath;

    void setHDRPath(const std::string &path, VkDescriptorPool descriptorPool);
    void reload(VkDescriptorPool descriptorPool);

    /// Stores the result of a scene capture (from RenderGraph::captureSceneProbe).
    /// Takes ownership of the image and sampler; caller provides the cube VkImageView.
    void setCapturedCubemap(std::shared_ptr<core::Image> image,
                            VkImageView                  cubeView,
                            std::shared_ptr<core::Sampler> sampler);

    bool        hasCubemap() const;
    VkImageView getProbeEnvView() const;
    VkSampler   getProbeEnvSampler() const;
    static void flushDeferredCapturedCubemapReleases();

    bool hasCapturedScene() const { return m_capturedImageView != VK_NULL_HANDLE; }

    void onDetach() override;

private:
    void releaseCapturedCubemap();

    std::unique_ptr<Skybox> m_skybox;

    // Scene-captured cubemap (set by captureSceneProbe)
    std::shared_ptr<core::Image>   m_capturedImage;
    VkImageView                    m_capturedImageView{VK_NULL_HANDLE};
    std::shared_ptr<core::Sampler> m_capturedSampler;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_REFLECTION_PROBE_COMPONENT_HPP
