#ifndef ELIX_IBL_MANAGER_HPP
#define ELIX_IBL_MANAGER_HPP

#include "Core/Macros.hpp"
#include "Engine/Texture.hpp"

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class IBLManager
{
public:
    bool generate(const std::string &hdrPath);

    void createFallback();

    VkImageView irradianceView() const;
    VkSampler irradianceSampler() const;
    VkImageView brdfLUTView() const;
    VkSampler brdfLUTSampler() const;
    VkImageView envView() const;
    VkSampler envSampler() const;

    const std::string &loadedPath() const { return m_lastPath; }
    bool isReady() const
    {
        return m_irradiance && m_brdfLUT && m_envSpecular &&
               m_irradiance->vkImageView() != VK_NULL_HANDLE && m_irradiance->vkSampler() != VK_NULL_HANDLE &&
               m_brdfLUT->vkImageView() != VK_NULL_HANDLE && m_brdfLUT->vkSampler() != VK_NULL_HANDLE &&
               m_envSpecular->vkImageView() != VK_NULL_HANDLE && m_envSpecular->vkSampler() != VK_NULL_HANDLE;
    }

private:
    static bool loadHDRData(const std::string &path,
                            std::vector<float> &outRGB,
                            int &outWidth, int &outHeight);

    // Bilinear sample of equirectangular RGB map
    static glm::vec3 sampleEquirect(const float *data, int w, int h, const glm::vec3 &dir);

    // Cosine-weighted hemisphere convolution → diffuse irradiance equirectangular map
    static void computeIrradianceEquirect(const float *src, int srcW, int srcH,
                                          std::vector<float> &out, int outW, int outH);

    // GGX importance-sampled pre-filtered specular for one cubemap face at a given roughness
    static void preFilterFace(const float *env, int envW, int envH,
                              int face, float roughness, uint32_t faceSize,
                              std::vector<float> &out);

    static uint32_t radicalInverse(uint32_t bits);
    static float geometrySmithGGX(float NdotV, float NdotL, float roughness);
    static void generateBRDFLUT(std::vector<float> &out, int size);

    Texture::SharedPtr m_irradiance;  // 32x32 cubemap — diffuse irradiance (properly convolved)
    Texture::SharedPtr m_brdfLUT;     // 256x256 2D   — GGX split-sum
    Texture::SharedPtr m_envSpecular; // 128x128 cubemap, 5 mip levels — GGX pre-filtered specular

    std::string m_lastPath;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_IBL_MANAGER_HPP
