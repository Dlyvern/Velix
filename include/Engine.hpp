#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <ElixirCore/Application.hpp>

class Engine
{
public:
    static bool run();


    static inline elix::Application* s_application{nullptr}; 

private:
    static void init();
    static void initImgui();
};


#endif //ENGINE_HPP