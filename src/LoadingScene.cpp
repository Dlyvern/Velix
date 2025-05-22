#include "LoadingScene.hpp"
#include "ElixirCore/Filesystem.hpp"
#include "ElixirCore/AssetsManager.hpp"
#include "ElixirCore/DebugTextHolder.hpp"

LoadingScene::LoadingScene() = default;

void LoadingScene::create()
{
    const std::string fontName = filesystem::getFontsFolderPath().string() + "/ghostmane_font.ttf";
    m_text.setText("LOADING...");
    m_text.setFont(fontName);

    m_future = std::async(std::launch::async, [this] { loadAllAssets(); });
}

void LoadingScene::loadAllAssets()
{
    const std::vector<std::string> modelsPaths{
    "VendingMachine.fbx",  "cube.obj", "mannequin.fbx", "jail_door.fbx", "void.fbx", "ak47-animated.fbx"
    };

    const std::vector<std::string> texturesPaths{
        "Ch36_1001_Diffuse.png", "wall_parking_d.png",
        "concrete_d.png", "wall_parking_n.png",
        "wood_floor_d.png", "wood_floor_n.png",
        "wood_floor_ao.png", "older-wood-flooring_height.png",
        "wood_floor_m.png", "wood_floor_r.png",
    };

    const std::vector<std::string> materialsPaths
    {
        "default_material.mat", "floor_material.mat", "parking_wall_material.mat", "concrete.mat", "Material.mat",
    };

    auto startingTime = std::chrono::high_resolution_clock::now();

    {
        auto startTime = std::chrono::high_resolution_clock::now();
        AssetsManager::instance().preLoadModels(modelsPaths);
        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsedTime = endTime - startTime;
        std::cout << "Loading models time: " + std::to_string(elapsedTime.count()) << std::endl;
        // debug::DebugTextHolder::instance().addText("Loading models time: " + std::to_string(elapsedTime.count()));
    }

    {
        auto startTime = std::chrono::high_resolution_clock::now();
        AssetsManager::instance().preLoadTextures(texturesPaths);
        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsedTime = endTime - startTime;
        std::cout << "Loading textures time: " + std::to_string(elapsedTime.count()) << std::endl;
        // debug::DebugTextHolder::instance().addText("Loading textures time: " + std::to_string(elapsedTime.count()));
    }

    {
        auto startTime = std::chrono::high_resolution_clock::now();
        AssetsManager::instance().preLoadMaterials(materialsPaths);
        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsedTime = endTime - startTime;
        std::cout << "Loading materials time: " + std::to_string(elapsedTime.count()) << std::endl;
        // debug::DebugTextHolder::instance().addText("Loading materials time: " + std::to_string(elapsedTime.count()));
    }

    auto endingTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsedTime = endingTime - startingTime;

    std::cout << "Loading time: " + std::to_string(elapsedTime.count()) << std::endl;
    // debug::DebugTextHolder::instance().addText("Loading time: " + std::to_string(elapsedTime.count()));
}

void LoadingScene::update(float deltaTime)
{
    m_text.draw();

    if(m_future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
    {
        auto models = AssetsManager::instance().getAllStaticModels();

        for (const auto& model : models)
        {
            for (int index = 0; index < model->getMeshesSize(); index++)
            {
                const auto& mesh = model->getMesh(index);

                if (auto staticMesh = dynamic_cast<StaticMesh*>(mesh))
                    staticMesh->loadFromRaw();
            }
        }

        auto skinnedModels = AssetsManager::instance().getAllSkinnedModels();

        for (const auto& skinnedModel : skinnedModels)
        {
            for (int index = 0; index < skinnedModel->getMeshesSize(); index++)
            {
                const auto& mesh = skinnedModel->getMesh(index);

                if (auto skeletalMesh = dynamic_cast<SkeletalMesh*>(mesh))
                    skeletalMesh->loadFromRaw();
            }
        }


        m_endScene = true;

        if (onEndSceneCallback_)
            onEndSceneCallback_(this);
    }
}

bool LoadingScene::isOver()
{
    return m_endScene;
}

LoadingScene::~LoadingScene() = default;