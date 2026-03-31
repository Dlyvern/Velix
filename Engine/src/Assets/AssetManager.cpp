#include "Engine/Assets/AssetManager.hpp"
#include "Engine/Assets/AssetsLoader.hpp"

ELIX_NESTED_NAMESPACE_BEGIN(engine)

AssetManager &AssetManager::getInstance()
{
    static AssetManager instance;
    return instance;
}

AssetManager::AssetManager() = default;

void AssetManager::requestLoad(AssetHandle<ModelAsset> &handle)
{
    postLoad(handle, Asset::AssetType::MODEL);
}

void AssetManager::requestLoad(AssetHandle<TextureAsset> &handle)
{
    postLoad(handle, Asset::AssetType::TEXTURE);
}

void AssetManager::requestLoad(AssetHandle<MaterialAsset> &handle)
{
    postLoad(handle, Asset::AssetType::MATERIAL);
}

template <typename T>
void AssetManager::postLoad(AssetHandle<T> &handle, Asset::AssetType type)
{
    if (handle.empty())
        return;

    // Fast path: already loading or done.
    const AssetState current = handle.state();
    if (current == AssetState::Loading || current == AssetState::Ready)
        return;

    // Check deduplication map: another handle might already have the data.
    {
        std::lock_guard lock(m_mutex);
        auto it = m_liveAssets.find(handle.path());
        if (it != m_liveAssets.end())
        {
            if (auto sp = std::static_pointer_cast<T>(it->second.data.lock()))
            {
                handle.resolve(sp);
                return;
            }
            // Weak ref expired — remove stale entry and reload.
            m_liveAssets.erase(it);
        }

        // Mark loading and insert entry.
        if (!handle.markLoading())
            return; // race: another thread already claimed this handle

        LiveEntry entry;
        entry.state = AssetState::Loading;
        entry.type = type;
        m_liveAssets.emplace(handle.path(), entry);
    }

    const std::string path = handle.path();

    // Post job to background thread.
    AssetStreamingWorker::LoadJob job;
    job.path = path;
    job.execute = [this, &handle, path, type]()
    {
        std::shared_ptr<T> data;

        // Load from disk (blocking, but on worker thread).
        if constexpr (std::is_same_v<T, ModelAsset>)
        {
            auto opt = AssetsLoader::loadModel(path);
            if (opt.has_value())
                data = std::make_shared<ModelAsset>(std::move(*opt));
        }
        else if constexpr (std::is_same_v<T, TextureAsset>)
        {
            auto opt = AssetsLoader::loadTexture(path);
            if (opt.has_value())
                data = std::make_shared<TextureAsset>(std::move(*opt));
        }
        else if constexpr (std::is_same_v<T, MaterialAsset>)
        {
            auto opt = AssetsLoader::loadMaterial(path);
            if (opt.has_value())
                data = std::make_shared<MaterialAsset>(std::move(*opt));
        }

        if (data)
        {
            // Update deduplication map.
            std::lock_guard lock(m_mutex);
            auto it = m_liveAssets.find(path);
            if (it != m_liveAssets.end())
            {
                it->second.data = data;
                it->second.state = AssetState::Ready;
                it->second.sizeBytes = sizeof(T); // approximate
            }

            handle.resolve(data);
        }
        else
        {
            std::lock_guard lock(m_mutex);
            m_liveAssets.erase(path);
            handle.fail();
        }
    };

    m_worker.enqueue(std::move(job));
}

// Explicit template instantiations so the linker can find them.
template void AssetManager::postLoad<ModelAsset>(AssetHandle<ModelAsset> &, Asset::AssetType);
template void AssetManager::postLoad<TextureAsset>(AssetHandle<TextureAsset> &, Asset::AssetType);
template void AssetManager::postLoad<MaterialAsset>(AssetHandle<MaterialAsset> &, Asset::AssetType);

AssetStats AssetManager::getStats() const
{
    AssetStats s{};
    std::lock_guard lock(m_mutex);
    for (const auto &[path, entry] : m_liveAssets)
    {
        if (entry.state == AssetState::Loading)
        {
            ++s.loadingInProgress;
            continue;
        }
        if (entry.state == AssetState::Failed)
        {
            ++s.failed;
            continue;
        }
        if (entry.state != AssetState::Ready)
            continue;

        s.cpuMemoryBytes += entry.sizeBytes;

        switch (entry.type)
        {
        case Asset::AssetType::TEXTURE:
            ++s.loadedTextures;
            break;
        case Asset::AssetType::MODEL:
            ++s.loadedModels;
            break;
        case Asset::AssetType::MATERIAL:
            ++s.loadedMaterials;
            break;
        default:
            break;
        }
    }
    return s;
}

void AssetManager::shutdown()
{
    m_worker.shutdown();
    std::lock_guard lock(m_mutex);
    m_liveAssets.clear();
}

ELIX_NESTED_NAMESPACE_END
