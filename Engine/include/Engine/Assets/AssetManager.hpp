#ifndef ELIX_ASSET_MANAGER_HPP
#define ELIX_ASSET_MANAGER_HPP

#include "Core/Macros.hpp"

#include "Engine/Assets/Asset.hpp"
#include "Engine/Assets/AssetHandle.hpp"
#include "Engine/Assets/AssetStreamingWorker.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

struct AssetStats
{
    uint32_t loadedTextures{0};
    uint32_t loadedModels{0};
    uint32_t loadedMaterials{0};
    uint32_t loadingInProgress{0};
    uint32_t failed{0};
    size_t cpuMemoryBytes{0};
};

class AssetManager
{
public:
    static AssetManager &getInstance();

    AssetManager(const AssetManager &) = delete;
    AssetManager &operator=(const AssetManager &) = delete;

    // Post an async load for the handle.
    // No-op if the handle is already Loading, Ready, or empty.
    void requestLoad(AssetHandle<ModelAsset> &handle);
    void requestLoad(AssetHandle<TextureAsset> &handle);
    void requestLoad(AssetHandle<MaterialAsset> &handle);

    // Reset handle back to Unloaded. CPU data is freed when the last shared_ptr is released.
    template <typename T>
    void unload(AssetHandle<T> &handle)
    {
        if (handle.empty())
            return;
        std::lock_guard lock(m_mutex);
        m_liveAssets.erase(handle.path());
        handle.reset();
    }

    // Cheap O(N) scan over live handles. Call at most once per frame.
    AssetStats getStats() const;

    void shutdown();

private:
    AssetManager();

    // Shared deduplication entry (type-erased).
    struct LiveEntry
    {
        AssetState state{AssetState::Unloaded};
        std::weak_ptr<void> data; // weak ref to the shared asset data
        Asset::AssetType type{Asset::AssetType::NONE};
        size_t sizeBytes{0};
    };

    template <typename T>
    void postLoad(AssetHandle<T> &handle, Asset::AssetType type);

    AssetStreamingWorker m_worker;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, LiveEntry> m_liveAssets;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSET_MANAGER_HPP
