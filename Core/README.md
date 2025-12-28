## VELIX_CORE

# This layer is vulkan wrapper

# Vulkan Resource Lifecycle Pattern (ELIX)

This project uses a strict and explicit lifecycle model for Vulkan resources.  
The goal is **prevent double frees, accidental re-creation, and undefined lifetime behavior**, while still allowing resources to exist independently of Vulkan object creation.

---

## Core Idea

Every Vulkan wrapper follows these rules:

1. The C++ object **may exist without a Vulkan object**
2. Vulkan objects are created explicitly via `createVk()`
3. Vulkan objects are destroyed explicitly via `destroyVk()`
4. Destruction is **idempotent**
5. Creation is **guarded**
6. Destructors are always safe

No Vulkan call should ever happen twice by accident.

---

## Lifecycle State

Each wrapper tracks its own Vulkan state:

```cpp
bool m_created = false;
