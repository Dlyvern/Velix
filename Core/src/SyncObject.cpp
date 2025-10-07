#include "Core/SyncObject.hpp"
#include <stdexcept>

ELIX_NESTED_NAMESPACE_BEGIN(core)

SyncObject::SyncObject(VkDevice device, uint32_t framesInFlight) : m_device(device), m_framesInFlight(framesInFlight)
{
    m_syncObjects.resize(framesInFlight);
    
    for(uint32_t i = 0; i < framesInFlight; ++i)
        createSyncObject(m_syncObjects[i]);
}

SyncObject::FrameSyncObjects& SyncObject::getSync(uint32_t frameIndex)
{
    return m_syncObjects[frameIndex];
}

SyncObject::~SyncObject()
{
    for (auto& object : m_syncObjects) 
    {
        vkDestroySemaphore(m_device, object.imageAvailableSemaphore, nullptr);
        vkDestroySemaphore(m_device, object.renderFinishedSemaphore, nullptr);
        vkDestroyFence(m_device, object.inFlightFence, nullptr);
    }
}

void SyncObject::createSyncObject(FrameSyncObjects& object)
{
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateSemaphore(m_device, &semInfo, nullptr, &object.imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateSemaphore(m_device, &semInfo, nullptr, &object.renderFinishedSemaphore) != VK_SUCCESS ||
        vkCreateFence(m_device, &fenceInfo, nullptr, &object.inFlightFence) != VK_SUCCESS)
        throw std::runtime_error("Failed to create sync objects for a frame");
}

ELIX_NESTED_NAMESPACE_END