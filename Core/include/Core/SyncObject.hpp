#ifndef ELIX_SYNC_OBJECT_HPP
#define ELIX_SYNC_OBJECT_HPP

#include "Core/Macros.hpp"
#include <cstdint>
#include <vector>
#include <volk.h>

#include <memory>

ELIX_NESTED_NAMESPACE_BEGIN(core)

class SyncObject
{
public:
    using SharedPtr = std::shared_ptr<SyncObject>;
    using UniquePtr = std::unique_ptr<SyncObject>;

    struct FrameSyncObjects 
    {
        VkSemaphore imageAvailableSemaphore;  // signaled when swapchain image is ready
        VkSemaphore renderFinishedSemaphore;  // signaled when rendering is finished
        VkFence inFlightFence;                // signaled when GPU work is done
    };

    SyncObject(VkDevice device, uint32_t framesInFlight);
    ~SyncObject();

    FrameSyncObjects& getSync(uint32_t frameIndex);

    FrameSyncObjects& operator[](uint32_t frameIndex) 
    {
        return m_syncObjects[frameIndex];
    }
private:
    void createSyncObject(FrameSyncObjects& object);
    std::vector<FrameSyncObjects> m_syncObjects;
    VkDevice m_device{VK_NULL_HANDLE};
    const uint32_t m_framesInFlight;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SYNC_OBJECT_HPP