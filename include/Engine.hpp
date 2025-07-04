#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <ElixirCore/Application.hpp>
#include "Editor.hpp"

class Engine
{
public:
    static int run();

    static inline std::unique_ptr<elix::Application> s_application{nullptr}; 
    static inline std::unique_ptr<Editor> s_editor{nullptr}; 
private:
    static void init();
};


#endif //ENGINE_HPP
