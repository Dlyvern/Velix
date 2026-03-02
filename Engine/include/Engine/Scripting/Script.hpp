#ifndef ELIX_SCRIPT_HPP
#define ELIX_SCRIPT_HPP

#include "Core/Macros.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

class Entity;

class Script
{
public:
    struct EntityRef
    {
        static constexpr uint32_t InvalidId = std::numeric_limits<uint32_t>::max();

        uint32_t id{InvalidId};

        EntityRef() = default;
        explicit EntityRef(uint32_t entityId) : id(entityId)
        {
        }

        bool isValid() const
        {
            return id != InvalidId;
        }

        bool operator==(const EntityRef &other) const
        {
            return id == other.id;
        }
    };

    enum class ExposedVariableType : uint8_t
    {
        Bool,
        Int,
        Float,
        String,
        Vec2,
        Vec3,
        Vec4,
        Entity
    };

    using ExposedVariableValue = std::variant<bool, int32_t, float, std::string, glm::vec2, glm::vec3, glm::vec4, EntityRef>;

    struct ExposedVariable
    {
        ExposedVariableType type{ExposedVariableType::Float};
        ExposedVariableValue value{0.0f};
    };

    using ExposedVariablesMap = std::unordered_map<std::string, ExposedVariable>;

    template <typename T>
    class Variable
    {
    public:
        explicit Variable(const char *name) : Variable(name, T{})
        {
        }

        Variable(const char *name, const T &defaultValue) : m_name(name ? name : ""),
                                                            m_defaultValue(defaultValue),
                                                            m_owner(Script::getCurrentConstructingScript())
        {
            if (m_owner && !m_name.empty())
                m_owner->registerAutoVariable<T>(m_name, m_defaultValue);
        }

        operator T() const
        {
            return get();
        }

        T get() const
        {
            if (!m_owner || m_name.empty())
                return m_defaultValue;

            return m_owner->getExposedValue<T>(m_name, m_defaultValue);
        }

        void set(const T &value)
        {
            m_defaultValue = value;

            if (m_owner && !m_name.empty())
                m_owner->setExposedVariable(m_name, value);
        }

        Variable &operator=(const T &value)
        {
            set(value);
            return *this;
        }

        template <typename U>
        Variable &operator+=(const U &rhs)
        {
            set(get() + rhs);
            return *this;
        }

        template <typename U>
        Variable &operator-=(const U &rhs)
        {
            set(get() - rhs);
            return *this;
        }

        template <typename U>
        Variable &operator*=(const U &rhs)
        {
            set(get() * rhs);
            return *this;
        }

        template <typename U>
        Variable &operator/=(const U &rhs)
        {
            set(get() / rhs);
            return *this;
        }

        template <typename U = T, typename std::enable_if_t<std::is_same_v<U, std::string>, int> = 0>
        Variable &operator=(const char *value)
        {
            set(value ? std::string(value) : std::string{});
            return *this;
        }

    private:
        std::string m_name;
        T m_defaultValue{};
        Script *m_owner{nullptr};
    };

    Script()
    {
        s_currentConstructingScript = this;
    }

    virtual void onUpdate(float deltaTime) {}
    virtual void onStart() {}
    virtual void onStop() {}

    static Script *getCurrentConstructingScript()
    {
        return s_currentConstructingScript;
    }

    void finalizeVariableRegistrationContext()
    {
        if (s_currentConstructingScript == this)
            s_currentConstructingScript = nullptr;
    }

    void exposeBool(const std::string &name, bool defaultValue)
    {
        exposeVariable(name, ExposedVariableType::Bool, defaultValue);
    }

    void exposeInt(const std::string &name, int32_t defaultValue)
    {
        exposeVariable(name, ExposedVariableType::Int, defaultValue);
    }

    void exposeFloat(const std::string &name, float defaultValue)
    {
        exposeVariable(name, ExposedVariableType::Float, defaultValue);
    }

    void exposeString(const std::string &name, const std::string &defaultValue)
    {
        exposeVariable(name, ExposedVariableType::String, defaultValue);
    }

    void exposeVec2(const std::string &name, const glm::vec2 &defaultValue)
    {
        exposeVariable(name, ExposedVariableType::Vec2, defaultValue);
    }

    void exposeVec3(const std::string &name, const glm::vec3 &defaultValue)
    {
        exposeVariable(name, ExposedVariableType::Vec3, defaultValue);
    }

    void exposeVec4(const std::string &name, const glm::vec4 &defaultValue)
    {
        exposeVariable(name, ExposedVariableType::Vec4, defaultValue);
    }

    void exposeEntity(const std::string &name, const EntityRef &defaultValue)
    {
        exposeVariable(name, ExposedVariableType::Entity, defaultValue);
    }

    void exposeEntity(const std::string &name, uint32_t entityId)
    {
        exposeEntity(name, EntityRef(entityId));
    }

    const ExposedVariablesMap &getExposedVariables() const
    {
        return m_exposedVariables;
    }

    bool setExposedVariable(const std::string &name, const ExposedVariableValue &value)
    {
        const auto it = m_exposedVariables.find(name);
        if (it == m_exposedVariables.end())
            return false;

        if (it->second.value.index() != value.index())
            return false;

        it->second.value = value;
        return true;
    }

    bool getExposedBool(const std::string &name, bool fallback = false) const
    {
        return getExposedValue<bool>(name, fallback);
    }

    int32_t getExposedInt(const std::string &name, int32_t fallback = 0) const
    {
        return getExposedValue<int32_t>(name, fallback);
    }

    float getExposedFloat(const std::string &name, float fallback = 0.0f) const
    {
        return getExposedValue<float>(name, fallback);
    }

    std::string getExposedString(const std::string &name, const std::string &fallback = "") const
    {
        return getExposedValue<std::string>(name, fallback);
    }

    glm::vec2 getExposedVec2(const std::string &name, const glm::vec2 &fallback = glm::vec2(0.0f)) const
    {
        return getExposedValue<glm::vec2>(name, fallback);
    }

    glm::vec3 getExposedVec3(const std::string &name, const glm::vec3 &fallback = glm::vec3(0.0f)) const
    {
        return getExposedValue<glm::vec3>(name, fallback);
    }

    glm::vec4 getExposedVec4(const std::string &name, const glm::vec4 &fallback = glm::vec4(0.0f)) const
    {
        return getExposedValue<glm::vec4>(name, fallback);
    }

    EntityRef getExposedEntity(const std::string &name) const
    {
        return getExposedEntity(name, EntityRef{});
    }

    EntityRef getExposedEntity(const std::string &name, const EntityRef &fallback) const
    {
        return getExposedValue<EntityRef>(name, fallback);
    }

    void applySerializedVariables(const ExposedVariablesMap &variables)
    {
        for (const auto &[name, variable] : variables)
        {
            m_pendingSerializedVariables[name] = variable;

            auto exposedIt = m_exposedVariables.find(name);
            if (exposedIt == m_exposedVariables.end())
                continue;

            if (exposedIt->second.type != variable.type)
                continue;

            exposedIt->second.value = variable.value;
        }
    }

    ExposedVariablesMap getSerializableVariables() const
    {
        ExposedVariablesMap mergedVariables = m_pendingSerializedVariables;
        for (const auto &[name, variable] : m_exposedVariables)
            mergedVariables[name] = variable;

        return mergedVariables;
    }

    Entity *getOwnerEntity() const
    {
        return m_ownerEntity;
    }

    void setOwnerEntity(Entity *entity)
    {
        m_ownerEntity = entity;
    }

    virtual ~Script()
    {
    }

protected:
    template <typename T>
    T getExposedValue(const std::string &name, const T &fallback) const
    {
        const auto it = m_exposedVariables.find(name);
        if (it == m_exposedVariables.end())
            return fallback;

        const T *value = std::get_if<T>(&it->second.value);
        return value ? *value : fallback;
    }

    void exposeVariable(const std::string &name, ExposedVariableType type, ExposedVariableValue value)
    {
        if (name.empty())
            return;

        auto exposedIt = m_exposedVariables.find(name);
        if (exposedIt == m_exposedVariables.end())
        {
            exposedIt = m_exposedVariables.emplace(name, ExposedVariable{type, std::move(value)}).first;
        }
        else if (exposedIt->second.type != type)
        {
            exposedIt->second.type = type;
            exposedIt->second.value = std::move(value);
        }

        auto pendingIt = m_pendingSerializedVariables.find(name);
        if (pendingIt != m_pendingSerializedVariables.end() && pendingIt->second.type == type)
            exposedIt->second.value = pendingIt->second.value;
    }

    template <typename T>
    void registerAutoVariable(const std::string &name, const T &defaultValue)
    {
        if constexpr (std::is_same_v<T, bool>)
            exposeBool(name, defaultValue);
        else if constexpr (std::is_same_v<T, int32_t>)
            exposeInt(name, defaultValue);
        else if constexpr (std::is_same_v<T, float>)
            exposeFloat(name, defaultValue);
        else if constexpr (std::is_same_v<T, std::string>)
            exposeString(name, defaultValue);
        else if constexpr (std::is_same_v<T, glm::vec2>)
            exposeVec2(name, defaultValue);
        else if constexpr (std::is_same_v<T, glm::vec3>)
            exposeVec3(name, defaultValue);
        else if constexpr (std::is_same_v<T, glm::vec4>)
            exposeVec4(name, defaultValue);
        else if constexpr (std::is_same_v<T, EntityRef>)
            exposeEntity(name, defaultValue);
        else
            static_assert(AlwaysFalse<T>::value, "Unsupported VX_VARIABLE type");
    }

private:
    template <typename>
    struct AlwaysFalse : std::false_type
    {
    };

    static inline thread_local Script *s_currentConstructingScript{nullptr};
    Entity *m_ownerEntity{nullptr};
    ExposedVariablesMap m_exposedVariables;
    ExposedVariablesMap m_pendingSerializedVariables;
};

ELIX_NESTED_NAMESPACE_END

#endif // ELIX_SCRIPT_HPP
