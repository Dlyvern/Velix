#include "Engine/Render/IBLManager.hpp"

#include "Engine/Assets/AssetsLoader.hpp"
#include "Engine/Assets/Asset.hpp"

#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

static std::string toLower(const std::string &s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });
    return result;
}

// Face direction bases — forward, right, up (matches createCubemapFromEquirectangular)
static const glm::vec3 kFaceDirs[6][3] = {
    {{1, 0, 0},  {0, 0, -1}, {0, -1, 0}}, // +X
    {{-1, 0, 0}, {0, 0,  1}, {0, -1, 0}}, // -X
    {{0, 1, 0},  {1, 0,  0}, {0,  0, 1}}, // +Y
    {{0, -1, 0}, {1, 0,  0}, {0,  0,-1}}, // -Y
    {{0, 0, 1},  {1, 0,  0}, {0, -1, 0}}, // +Z
    {{0, 0, -1}, {-1, 0, 0}, {0, -1, 0}}, // -Z
};

bool IBLManager::loadHDRData(const std::string &path,
                             std::vector<float> &outRGB,
                             int &outWidth, int &outHeight)
{
    const std::string ext = toLower(std::filesystem::path(path).extension().string());

    if (ext == ".elixasset")
    {
        auto asset = AssetsLoader::loadTexture(path);
        if (!asset.has_value() || asset->width == 0u || asset->height == 0u)
            return false;

        outWidth  = static_cast<int>(asset->width);
        outHeight = static_cast<int>(asset->height);

        const size_t pixelCount = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);
        outRGB.resize(pixelCount * 3u);

        if (asset->encoding == TextureAsset::PixelEncoding::RGBA32F)
        {
            const size_t floatCount = asset->pixels.size() / sizeof(float);
            if (floatCount < pixelCount * 4u)
                return false;

            const auto *src = reinterpret_cast<const float *>(asset->pixels.data());
            for (size_t i = 0; i < pixelCount; ++i)
            {
                outRGB[i * 3u + 0u] = src[i * 4u + 0u];
                outRGB[i * 3u + 1u] = src[i * 4u + 1u];
                outRGB[i * 3u + 2u] = src[i * 4u + 2u];
            }
            return true;
        }
        else if (asset->encoding == TextureAsset::PixelEncoding::RGBA8)
        {
            if (asset->pixels.size() < pixelCount * 4u)
                return false;

            for (size_t i = 0; i < pixelCount; ++i)
            {
                outRGB[i * 3u + 0u] = static_cast<float>(asset->pixels[i * 4u + 0u]) / 255.0f;
                outRGB[i * 3u + 1u] = static_cast<float>(asset->pixels[i * 4u + 1u]) / 255.0f;
                outRGB[i * 3u + 2u] = static_cast<float>(asset->pixels[i * 4u + 2u]) / 255.0f;
            }
            return true;
        }
        return false;
    }

    int channels = 0;
    float *data = stbi_loadf(path.c_str(), &outWidth, &outHeight, &channels, STBI_rgb);
    if (!data)
        return false;

    const size_t pixelCount = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);
    outRGB.assign(data, data + pixelCount * 3u);
    stbi_image_free(data);
    return true;
}

// ---------------------------------------------------------------------------
// Shared equirectangular sampler
// ---------------------------------------------------------------------------

glm::vec3 IBLManager::sampleEquirect(const float *data, int w, int h, const glm::vec3 &dir)
{
    const glm::vec3 d = glm::normalize(dir);

    const float phi   = std::atan2(d.z, d.x);
    const float theta = std::acos(glm::clamp(d.y, -1.0f, 1.0f));

    float xf = (phi / (2.0f * glm::pi<float>()) + 0.5f) * static_cast<float>(w);
    float yf = (theta / glm::pi<float>()) * static_cast<float>(h);

    const int x0 = static_cast<int>(std::floor(xf));
    const int y0 = static_cast<int>(std::floor(yf));
    const int x1 = (x0 + 1) % w;
    const int y1 = std::min(y0 + 1, h - 1);
    const float tx = xf - static_cast<float>(x0);
    const float ty = yf - static_cast<float>(y0);

    glm::vec3 c00, c01, c10, c11;
    for (int c = 0; c < 3; ++c)
    {
        c00[c] = data[(y0 * w + x0) * 3 + c];
        c01[c] = data[(y0 * w + x1) * 3 + c];
        c10[c] = data[(y1 * w + x0) * 3 + c];
        c11[c] = data[(y1 * w + x1) * 3 + c];
    }
    return glm::mix(glm::mix(c00, c01, tx), glm::mix(c10, c11, tx), ty);
}

// ---------------------------------------------------------------------------
// Diffuse irradiance — cosine-weighted hemisphere convolution
// ---------------------------------------------------------------------------

void IBLManager::computeIrradianceEquirect(const float *src, int srcW, int srcH,
                                           std::vector<float> &out, int outW, int outH)
{
    out.resize(static_cast<size_t>(outW) * static_cast<size_t>(outH) * 3u, 0.0f);

    constexpr int PHI_SAMPLES   = 24;
    constexpr int THETA_SAMPLES = 12;
    const float dPhi   = 2.0f * glm::pi<float>() / static_cast<float>(PHI_SAMPLES);
    const float dTheta = 0.5f * glm::pi<float>() / static_cast<float>(THETA_SAMPLES);

    for (int oy = 0; oy < outH; ++oy)
    {
        const float outV  = (static_cast<float>(oy) + 0.5f) / static_cast<float>(outH);
        const float thetaN = outV * glm::pi<float>();

        for (int ox = 0; ox < outW; ++ox)
        {
            const float outU = (static_cast<float>(ox) + 0.5f) / static_cast<float>(outW);
            const float phiN = outU * 2.0f * glm::pi<float>() - glm::pi<float>();

            const glm::vec3 N = glm::normalize(glm::vec3{
                std::sin(thetaN) * std::cos(phiN),
                std::cos(thetaN),
                std::sin(thetaN) * std::sin(phiN)});

            const glm::vec3 upRef = (std::abs(N.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 right = glm::normalize(glm::cross(upRef, N));
            const glm::vec3 fwd   = glm::cross(N, right);

            glm::vec3 irradiance(0.0f);
            float nSamples = 0.0f;

            for (int pi = 0; pi < PHI_SAMPLES; ++pi)
            {
                const float phi = static_cast<float>(pi) * dPhi;
                for (int ti = 0; ti < THETA_SAMPLES; ++ti)
                {
                    const float theta = static_cast<float>(ti) * dTheta;
                    const glm::vec3 ls{std::sin(theta) * std::cos(phi),
                                       std::sin(theta) * std::sin(phi),
                                       std::cos(theta)};
                    const glm::vec3 L = ls.x * right + ls.y * fwd + ls.z * N;

                    irradiance += sampleEquirect(src, srcW, srcH, L)
                                  * std::cos(theta) * std::sin(theta);
                    nSamples += 1.0f;
                }
            }

            irradiance = glm::pi<float>() * irradiance / nSamples;

            const size_t idx = (static_cast<size_t>(oy) * static_cast<size_t>(outW)
                                + static_cast<size_t>(ox)) * 3u;
            out[idx + 0u] = irradiance.r;
            out[idx + 1u] = irradiance.g;
            out[idx + 2u] = irradiance.b;
        }
    }
}

// ---------------------------------------------------------------------------
// GGX pre-filtered specular — one cubemap face at given roughness
// ---------------------------------------------------------------------------

void IBLManager::preFilterFace(const float *env, int envW, int envH,
                               int face, float roughness, uint32_t faceSize,
                               std::vector<float> &out)
{
    constexpr int SAMPLES = 256;
    const float a  = roughness * roughness;
    const float a2 = a * a;

    out.resize(static_cast<size_t>(faceSize) * faceSize * 4u, 0.0f);

    for (uint32_t y = 0; y < faceSize; ++y)
    {
        for (uint32_t x = 0; x < faceSize; ++x)
        {
            const float u = (2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(faceSize)) - 1.0f;
            const float v = (2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(faceSize)) - 1.0f;

            const glm::vec3 R = glm::normalize(
                kFaceDirs[face][0] + u * kFaceDirs[face][1] + v * kFaceDirs[face][2]);

            // N = V = R (view-independent approximation)
            const glm::vec3 N = R;
            const glm::vec3 V = R;

            const glm::vec3 upRef  = (std::abs(N.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            const glm::vec3 right2 = glm::normalize(glm::cross(upRef, N));
            const glm::vec3 up2    = glm::cross(N, right2);

            glm::vec3 prefilteredColor(0.0f);
            float totalWeight = 0.0f;

            for (int i = 0; i < SAMPLES; ++i)
            {
                const float Xi1 = static_cast<float>(i) / static_cast<float>(SAMPLES);
                const float Xi2 = static_cast<float>(radicalInverse(static_cast<uint32_t>(i))) * 2.328306e-10f;

                const float phi  = 2.0f * glm::pi<float>() * Xi1;
                const float cosT = (roughness < 0.001f)
                                       ? 1.0f
                                       : std::sqrt((1.0f - Xi2) / std::max(1.0f + (a2 - 1.0f) * Xi2, 1e-6f));
                const float sinT = std::sqrt(std::max(1.0f - cosT * cosT, 0.0f));

                const glm::vec3 H = glm::normalize(
                    sinT * std::cos(phi) * right2 +
                    sinT * std::sin(phi) * up2 +
                    cosT * N);

                const glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);
                const float NdotL = std::max(glm::dot(N, L), 0.0f);

                if (NdotL > 0.0f)
                {
                    prefilteredColor += sampleEquirect(env, envW, envH, L) * NdotL;
                    totalWeight      += NdotL;
                }
            }

            if (totalWeight > 0.0f)
                prefilteredColor /= totalWeight;

            const size_t idx = (static_cast<size_t>(y) * faceSize + x) * 4u;
            out[idx + 0u] = prefilteredColor.r;
            out[idx + 1u] = prefilteredColor.g;
            out[idx + 2u] = prefilteredColor.b;
            out[idx + 3u] = 1.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// BRDF LUT
// ---------------------------------------------------------------------------

uint32_t IBLManager::radicalInverse(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return bits;
}

float IBLManager::geometrySmithGGX(float NdotV, float NdotL, float roughness)
{
    float r = roughness;
    float k = (r * r) / 2.0f;

    auto G1 = [k](float NdotX)
    {
        return NdotX / std::max(NdotX * (1.0f - k) + k, 1e-6f);
    };
    return G1(NdotV) * G1(NdotL);
}

void IBLManager::generateBRDFLUT(std::vector<float> &out, int size)
{
    out.resize(static_cast<size_t>(size) * static_cast<size_t>(size) * 2u, 0.0f);

    const int SAMPLES = 192;

    for (int y = 0; y < size; ++y)
    {
        const float roughness = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
        const float a  = roughness * roughness;
        const float a2 = a * a;

        for (int x = 0; x < size; ++x)
        {
            const float NdotV = std::max((static_cast<float>(x) + 0.5f) / static_cast<float>(size), 1e-4f);
            const glm::vec3 V{std::sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV};

            float A = 0.0f, B = 0.0f;

            for (int i = 0; i < SAMPLES; ++i)
            {
                const float Xi1 = static_cast<float>(i) / static_cast<float>(SAMPLES);
                const float Xi2 = static_cast<float>(radicalInverse(static_cast<uint32_t>(i))) * 2.328306e-10f;

                const float phi  = 2.0f * glm::pi<float>() * Xi1;
                const float cosT = std::sqrt((1.0f - Xi2) / std::max(1.0f + (a2 - 1.0f) * Xi2, 1e-6f));
                const float sinT = std::sqrt(1.0f - cosT * cosT);

                const glm::vec3 H{sinT * std::cos(phi), sinT * std::sin(phi), cosT};
                const glm::vec3 L = 2.0f * glm::dot(V, H) * H - V;

                if (L.z > 0.0f)
                {
                    const float NdotL = std::max(L.z, 0.0f);
                    const float NdotH = std::max(H.z, 0.0f);
                    const float VdotH = std::max(glm::dot(V, H), 0.0f);

                    const float G     = geometrySmithGGX(NdotV, NdotL, roughness);
                    const float G_vis = (G * VdotH) / std::max(NdotH * NdotV, 1e-6f);
                    const float Fc    = std::pow(1.0f - VdotH, 5.0f);

                    A += (1.0f - Fc) * G_vis;
                    B += Fc * G_vis;
                }
            }

            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 2u;
            out[idx + 0u] = A / static_cast<float>(SAMPLES);
            out[idx + 1u] = B / static_cast<float>(SAMPLES);
        }
    }
}

// ---------------------------------------------------------------------------
// generate()
// ---------------------------------------------------------------------------

bool IBLManager::generate(const std::string &hdrPath)
{
    if (hdrPath.empty())
        return false;

    if (isReady() && m_lastPath == hdrPath)
        return true;

    std::vector<float> rgbData;
    int w = 0, h = 0;

    if (!loadHDRData(hdrPath, rgbData, w, h))
        return false;

    // 1. Diffuse irradiance — proper cosine-weighted hemisphere convolution
    constexpr int IRR_W = 64, IRR_H = 32;
    std::vector<float> irrEquirect;
    computeIrradianceEquirect(rgbData.data(), w, h, irrEquirect, IRR_W, IRR_H);

    if (!m_irradiance)
        m_irradiance = std::make_shared<Texture>();

    if (!m_irradiance->createCubemapFromEquirectangular(irrEquirect.data(), IRR_W, IRR_H, 32u))
        return false;

    // 2. Pre-filtered specular — GGX importance sampling, 5 mip levels
    constexpr uint32_t BASE_SIZE = 128u;
    constexpr uint32_t NUM_MIPS  = 5u;

    std::vector<std::vector<std::vector<float>>> mipFaceData(NUM_MIPS);
    std::vector<uint32_t> mipSizes(NUM_MIPS);

    for (uint32_t mip = 0; mip < NUM_MIPS; ++mip)
    {
        const float roughness = static_cast<float>(mip) / static_cast<float>(NUM_MIPS - 1u);
        const uint32_t mipSize = std::max(1u, BASE_SIZE >> mip);
        mipSizes[mip] = mipSize;
        mipFaceData[mip].resize(6);

        for (int face = 0; face < 6; ++face)
            preFilterFace(rgbData.data(), w, h, face, roughness, mipSize, mipFaceData[mip][face]);
    }

    if (!m_envSpecular)
        m_envSpecular = std::make_shared<Texture>();

    if (!m_envSpecular->createPreFilteredCubemap(mipFaceData, mipSizes))
        return false;

    // 3. BRDF LUT
    constexpr int LUT_SIZE = 256;
    std::vector<float> lutData;
    generateBRDFLUT(lutData, LUT_SIZE);

    if (!m_brdfLUT)
        m_brdfLUT = std::make_shared<Texture>();

    if (!m_brdfLUT->createFromMemory(lutData.data(),
                                     lutData.size() * sizeof(float),
                                     static_cast<uint32_t>(LUT_SIZE),
                                     static_cast<uint32_t>(LUT_SIZE),
                                     VK_FORMAT_R32G32_SFLOAT,
                                     2u))
        return false;

    m_lastPath = hdrPath;
    return true;
}

// ---------------------------------------------------------------------------
// createFallback()
// ---------------------------------------------------------------------------

void IBLManager::createFallback()
{
    if (!m_irradiance)
        m_irradiance = std::make_shared<Texture>();
    const float blackPixel[3] = {0.0f, 0.0f, 0.0f};
    m_irradiance->createCubemapFromEquirectangular(blackPixel, 1, 1, 1u);

    if (!m_envSpecular)
        m_envSpecular = std::make_shared<Texture>();
    // 1x1 black single-mip cubemap fallback
    std::vector<std::vector<std::vector<float>>> fallbackMips(1, std::vector<std::vector<float>>(6, std::vector<float>(4, 0.0f)));
    for (int f = 0; f < 6; ++f)
        fallbackMips[0][f][3] = 1.0f;
    std::vector<uint32_t> fallbackSizes = {1u};
    m_envSpecular->createPreFilteredCubemap(fallbackMips, fallbackSizes);

    if (!m_brdfLUT)
        m_brdfLUT = std::make_shared<Texture>();
    const float brdfFallback[2] = {0.5f, 0.0f};
    m_brdfLUT->createFromMemory(brdfFallback, sizeof(brdfFallback), 1u, 1u,
                                VK_FORMAT_R32G32_SFLOAT, 2u);

    m_lastPath.clear();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

VkImageView IBLManager::irradianceView() const
{
    return m_irradiance ? m_irradiance->vkImageView() : VK_NULL_HANDLE;
}

VkSampler IBLManager::irradianceSampler() const
{
    return m_irradiance ? m_irradiance->vkSampler() : VK_NULL_HANDLE;
}

VkImageView IBLManager::brdfLUTView() const
{
    return m_brdfLUT ? m_brdfLUT->vkImageView() : VK_NULL_HANDLE;
}

VkSampler IBLManager::brdfLUTSampler() const
{
    return m_brdfLUT ? m_brdfLUT->vkSampler() : VK_NULL_HANDLE;
}

VkImageView IBLManager::envView() const
{
    return m_envSpecular ? m_envSpecular->vkImageView() : VK_NULL_HANDLE;
}

VkSampler IBLManager::envSampler() const
{
    return m_envSpecular ? m_envSpecular->vkSampler() : VK_NULL_HANDLE;
}

ELIX_NESTED_NAMESPACE_END
