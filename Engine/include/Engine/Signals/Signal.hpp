#ifndef ELIX_SIGNAL_HPP
#define ELIX_SIGNAL_HPP

#include "Core/Macros.hpp"

#include "Engine/Signals/Connection.hpp"

#include <memory>
#include <vector>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

template<typename... Args>
class Signal
{
private:
    struct Slot
    {
        std::weak_ptr<void> lifetime;
        std::function<void(Args...)> function;
    };

public:

    Signal() = default;

    template<typename T>
    Connection connect(const std::shared_ptr<T>& receiver, void (T::*method)(Args...))
    {
        Slot slot;
        slot.lifetime = receiver;

        slot.function = [weak = std::weak_ptr<T>(receiver), method](Args... args)
        {
            if(auto locked = weak.lock())
            {
                (locked.get()->*method)(args...);
            }
        };

        m_slots.push_back(slot);

        const size_t index = m_slots.size() - 1;

        return Connection([this, index]
        {
            if(index < m_slots.size())
                m_slots[index].function = nullptr;
        });
    }

    void emit(Args... args)
    {
        for(const auto& slot : m_slots)
        {
            if(!slot.function)
                continue;

            if(slot.lifetime.expired())
            {
                slot.function = nullptr;
                continue;
            }

            slot.function(args...);
        }
    }

    void clear()
    {
        m_slots.clear();
    }
    
private:
    std::vector<Slot> m_slots;
};

ELIX_NESTED_NAMESPACE_END

#endif //ELIX_SIGNAL_HPP