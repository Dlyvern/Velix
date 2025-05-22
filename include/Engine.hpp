#ifndef ENGINE_HPP
#define ENGINE_HPP

// #if defined(VULKAN_API)
// namespace gfx = vk;
// #else
// namespace gfx = GLitch;
// #endif

class Engine
{
public:
    static bool run();

private:
    static void init();
    static void initOpenGL();
    static void initImgui();

    static void glCheckError(const char* file, int line);
    #define callGLCheckError() glCheckError(__FILE__, __LINE__)
};


#endif //ENGINE_HPP