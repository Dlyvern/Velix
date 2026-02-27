//* Yeah, I know that store Macroses inside Core Layer is not ideal...

#ifndef ELIX_MACROS_HPP
#define ELIX_MACROS_HPP

#include <memory>

#define ELIX_NAMESPACE_BEGIN \
    namespace elix           \
    {
#define ELIX_NAMESPACE_END }

#define ELIX_NESTED_NAMESPACE_BEGIN(x) \
    namespace elix                     \
    {                                  \
        namespace x                    \
        {
#define ELIX_NESTED_NAMESPACE_END \
    }                             \
    }

#define ELIX_CUSTOM_NAMESPACE_BEGIN(x) \
    namespace x                        \
    {
#define ELIX_CUSTOM_NAMESPACE_END }

#define PROPERTY_FULL(type, name)                           \
private:                                                    \
    type m_##name;                                          \
                                                            \
public:                                                     \
    const type &get##name() const { return m_##name; }      \
    type &get##name() { return m_##name; }                  \
    void set##name(const type &value) { m_##name = value; } \
    void set##name(type &&value) { m_##name = std::move(value); }

#define PROPERTY_FULL_DEFAULT(type, name, defaultValue)     \
private:                                                    \
    type m_##name = defaultValue;                           \
                                                            \
public:                                                     \
    const type &get##name() const { return m_##name; }      \
    type &get##name() { return m_##name; }                  \
    void set##name(const type &value) { m_##name = value; } \
    void set##name(type &&value) { m_##name = std::move(value); }

#define ELIX_DECLARE_VK_LIFECYCLE()              \
public:                                          \
    bool isCreated() const { return m_created; } \
                                                 \
    void destroyVk()                             \
    {                                            \
        if (!m_created)                          \
            return;                              \
        destroyVkImpl();                         \
        m_created = false;                       \
    }                                            \
                                                 \
protected:                                       \
    virtual void destroyVkImpl();                \
    bool m_created = false;

#define ELIX_VK_CREATE_GUARD() \
    if (m_created)             \
        return;

#define ELIX_VK_CREATE_GUARD_DONE() \
    m_created = true;

#define DECLARE_VK_HANDLE_METHODS(HandleType)                   \
public:                                                         \
    HandleType vk() const { return m_handle; }                  \
    HandleType *pVk() { return &m_handle; }                     \
    const HandleType *pVk() const { return &m_handle; }         \
    operator HandleType() const { return m_handle; }            \
    operator HandleType *() { return &m_handle; }               \
    operator const HandleType *() const { return &m_handle; }   \
    bool isValid() const { return m_handle != VK_NULL_HANDLE; } \
                                                                \
private:                                                        \
    HandleType m_handle = VK_NULL_HANDLE;

#define DECLARE_VK_SMART_PTRS(ClassName, HandleType)             \
public:                                                          \
    class SPtr : public std::shared_ptr<ClassName>               \
    {                                                            \
    public:                                                      \
        using std::shared_ptr<ClassName>::shared_ptr;            \
        operator HandleType() const                              \
        {                                                        \
            return (*this)->m_handle;                            \
        }                                                        \
    };                                                           \
                                                                 \
    class UPtr : public std::unique_ptr<ClassName>               \
    {                                                            \
    public:                                                      \
        using std::unique_ptr<ClassName>::unique_ptr;            \
        operator HandleType() const                              \
        {                                                        \
            return (*this)->m_handle;                            \
        }                                                        \
    };                                                           \
    using SharedPtr = SPtr;                                      \
    using UniquePtr = UPtr;                                      \
    using WeakPtr = std::weak_ptr<ClassName>;                    \
    using Ptr = ClassName *;                                     \
                                                                 \
    template <typename... Args>                                  \
    static ClassName create(Args &&...args)                      \
    {                                                            \
        return ClassName(std::forward<Args>(args)...);           \
    }                                                            \
                                                                 \
    template <typename... Args>                                  \
    static SPtr createShared(Args &&...args)                     \
    {                                                            \
        return SPtr(new ClassName(std::forward<Args>(args)...)); \
    }                                                            \
                                                                 \
    template <typename... Args>                                  \
    static UPtr createUnique(Args &&...args)                     \
    {                                                            \
        return UPtr(new ClassName(std::forward<Args>(args)...)); \
    }

#endif // ELIX_MACROS_HPP
