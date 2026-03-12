#ifndef ELIX_EVENT_BUS_HPP
#define ELIX_EVENT_BUS_HPP

#include "Core/Macros.hpp"

#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <algorithm>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class EventBus
{
public:
    using SubscriptionToken = uint64_t;

    template <typename EventType>
    static SubscriptionToken subscribe(std::function<void(const EventType &)> handler)
    {
        auto &bucket = getBucket(typeid(EventType));
        const SubscriptionToken token = bucket.nextToken++;
        bucket.handlers.push_back({token, [h = std::move(handler)](const void *data)
                                   {
                                       h(*static_cast<const EventType *>(data));
                                   }});
        return token;
    }

    template <typename EventType>
    static void emit(const EventType &event)
    {
        auto it = s_buckets.find(typeid(EventType));
        if (it == s_buckets.end())
            return;

        const auto snapshot = it->second.handlers;
        for (const auto &entry : snapshot)
            entry.invoke(&event);
    }

    template <typename EventType>
    static void unsubscribe(SubscriptionToken token)
    {
        auto it = s_buckets.find(typeid(EventType));
        if (it == s_buckets.end())
            return;

        auto &handlers = it->second.handlers;
        handlers.erase(
            std::remove_if(handlers.begin(), handlers.end(),
                           [token](const HandlerEntry &e)
                           { return e.token == token; }),
            handlers.end());
    }

    template <typename EventType>
    static void clear()
    {
        s_buckets.erase(typeid(EventType));
    }

    static void clearAll()
    {
        s_buckets.clear();
    }

private:
    struct HandlerEntry
    {
        SubscriptionToken token{0};
        std::function<void(const void *)> invoke;
    };

    struct Bucket
    {
        SubscriptionToken nextToken{1};
        std::vector<HandlerEntry> handlers;
    };

    static Bucket &getBucket(const std::type_index &type)
    {
        return s_buckets[type];
    }

    static inline std::unordered_map<std::type_index, Bucket> s_buckets;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_EVENT_BUS_HPP
