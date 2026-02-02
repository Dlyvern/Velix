# VelixEngine

## About engine

Our goal is to provide users small and flexible engine that was designed to be downloaded and used immediately. 

It provides low-level access for developers who want full control, while still offering higher-level Vulkan abstractions for faster development. You can use raw vulkan or our vulkan-wrappers it's up to you how to use our engine. You can just build your own game engine on top of our abstraction or use it as we developed it


## Design Philosophy

- Explicit over implicit
- Minimal abstractions, no magic
- No 'how tf it works under the hood'
- Engine code should be readable, debuggable, and replaceable
- The engine should never block you from doing low-level work


## Features

- Vulkan-based renderer with optional abstraction layer
- ImGui-based tooling
- Scripting(With C++ SDK)
- Cross-platform focused (Linux/Windows)

## Who Is Velix For?

Velix is built for developers who:
- Want to understand and control their engine
- Are comfortable working close to graphics APIs
- Prefer flexibility over hand-holding

Velix is **not** aimed at beginners or rapid prototyping with visual scripting.(Maybe later)



## Getting Started

```bash
git clone https://github.com/Dlyvern/Velix.git
cd Velix
mkdir build
cd build
cmake cmake -D CMAKE_BUILD_TYPE=Debug|Release ..
make -j$(nproc)
```

## License Apache


## Final Notes

Velix is built as a foundation, not a cage.
You are encouraged to modify it, extend it, or tear it apart to suit your needs.

## LAYERS(HIGH|LOW level)
later...