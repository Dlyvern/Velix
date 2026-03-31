#ifndef ELIX_ASSET_HANDLE_HPP
#define ELIX_ASSET_HANDLE_HPP

#include "Core/Macros.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

enum class AssetState : uint8_t
{
    Unloaded = 0,
    Loading = 1,
    Ready = 2,
    Failed = 3
};

template <typename T>
class AssetHandle
{
public:
    explicit AssetHandle() = default;
    explicit AssetHandle(std::string path) : m_path(std::move(path)) {}

    AssetHandle(const AssetHandle &) = delete;
    AssetHandle &operator=(const AssetHandle &) = delete;

    AssetHandle(AssetHandle &&other) noexcept
        : m_path(std::move(other.m_path)),
          m_data(std::move(other.m_data)),
          m_onLoaded(std::move(other.m_onLoaded))
    {
        m_state.store(other.m_state.load(std::memory_order_relaxed), std::memory_order_relaxed);
        other.m_state.store(AssetState::Unloaded, std::memory_order_relaxed);
    }

    AssetHandle &operator=(AssetHandle &&other) noexcept
    {
        if (this != &other)
        {
            m_path = std::move(other.m_path);
            m_data = std::move(other.m_data);
            m_onLoaded = std::move(other.m_onLoaded);
            m_state.store(other.m_state.load(std::memory_order_relaxed), std::memory_order_relaxed);
            other.m_state.store(AssetState::Unloaded, std::memory_order_relaxed);
        }
        return *this;
    }

    const std::string &path() const { return m_path; }
    bool empty() const { return m_path.empty(); }

    AssetState state() const { return m_state.load(std::memory_order_acquire); }
    bool ready() const { return state() == AssetState::Ready; }

    // Only valid when ready() == true.
    const T &get() const { return *m_data; }
    T &get() { return *m_data; }

    // Returns nullptr if not ready.
    const std::shared_ptr<T> &dataPtr() const { return m_data; }

    void setOnLoaded(std::function<void(AssetHandle<T> &)> cb) { m_onLoaded = std::move(cb); }

    // Transitions Unloaded → Loading.  Returns false if already loading/ready.
    bool markLoading()
    {
        AssetState expected = AssetState::Unloaded;
        return m_state.compare_exchange_strong(expected, AssetState::Loading,
                                               std::memory_order_acq_rel);
    }

    // Transitions Loading → Ready.  Called from the streaming worker thread.
    void resolve(std::shared_ptr<T> data)
    {
        m_data = std::move(data);
        m_state.store(AssetState::Ready, std::memory_order_release);
        if (m_onLoaded)
            m_onLoaded(*this);
    }

    // Transitions any state → Failed.
    void fail()
    {
        m_data.reset();
        m_state.store(AssetState::Failed, std::memory_order_release);
    }

    // Transitions Ready/Failed → Unloaded (frees CPU data).
    void reset()
    {
        m_data.reset();
        m_state.store(AssetState::Unloaded, std::memory_order_release);
    }

private:
    std::string m_path;
    std::atomic<AssetState> m_state{AssetState::Unloaded};
    std::shared_ptr<T> m_data;
    std::function<void(AssetHandle<T> &)> m_onLoaded;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_ASSET_HANDLE_HPP
