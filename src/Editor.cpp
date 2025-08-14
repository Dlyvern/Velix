#include "Editor.hpp"
#include <VelixFlow/Logger.hpp>
#include <VelixFlow/AssetsLoader.hpp>
#include <VelixFlow/Filesystem.hpp>

#include <VelixFlow/UI/UIVerticalBox.hpp>
#include <VelixFlow/UI/UIHorizontalBox.hpp>
#include <VelixFlow/UI/UIText.hpp>
#include <VelixFlow/UI/UIButton.hpp>

void Editor::init()
{
    m_overlay = std::make_shared<elix::Scene>();

    const std::string resourcesFolder = elix::filesystem::getExecutablePath().string() + "/resources/";

    // auto velixVAsset = elix::AssetsLoader::loadAsset(resourcesFolder + "textures/VelixV.png");
    // auto velixV = dynamic_cast<elix::AssetTexture*>(velixVAsset.get())->getTexture();

    // m_editorCache.addAsset(resourcesFolder + "textures/VelixV.png", std::move(velixVAsset));

    // auto velixTextAsset = elix::AssetsLoader::loadAsset(resourcesFolder + "textures/VelixText.png");
    // auto velixText = dynamic_cast<elix::AssetTexture*>(velixTextAsset.get())->getTexture();

    // m_editorCache.addAsset(resourcesFolder + "textures/VelixText.png", std::move(velixTextAsset));

    // auto velixVLogo = std::make_shared<elix::ui::UIWidget>();
    // velixVLogo->setTexture(velixV);

    // auto velixTextLogo = std::make_shared<elix::ui::UIWidget>();
    // velixTextLogo->setTexture(velixText);

    auto font = std::make_shared<elix::ui::UIFont>();
    font->load(resourcesFolder + "fonts/KGRedHands.ttf");
    
    auto mainWidget = std::make_shared<elix::ui::UIWidget>();
    mainWidget->setSize({1000, 600});
    mainWidget->setPosition({760, 340});
    mainWidget->setAnchor(elix::ui::UIAnchor::CENTER);
    mainWidget->setAlpha(0.5);
    
    auto layout = std::make_shared<elix::ui::UIHorizontalBox>();
    layout->setSpacing(20.0f);

    mainWidget->setLayout(layout);
    auto openProjectButton = std::make_shared<elix::ui::UIButton>();
    openProjectButton->setText("Open project");
    openProjectButton->getText()->setFont(font);
    openProjectButton->setColor({1.0f, 0.0f, 0.0f, 1.0f});
    openProjectButton->setSize({200, 300});
    openProjectButton->onClicked.connect([](int i){elix::filesystem::openInFileManager(elix::filesystem::getHomeDirectory());});
    openProjectButton->setResizable(true);

    mainWidget->addChild(openProjectButton);

    addUI(mainWidget);

    // auto box = std::make_shared<elix::ui::UIVerticalBox>();
    // box->setAlpha(0.0f);
    // box->setName("Layout");

    // box->addChild(velixVLogo);
    // box->addChild(velixTextLogo);

    //TODO add ui to UIWidget
    //TODO set layout for widget

    // addUI(box);
}

std::shared_ptr<elix::Scene> Editor::getOverlay()
{
    return m_overlay;
}

void Editor::update(float deltaTime)
{
    m_overlay->update(deltaTime);
}

void Editor::addUI(const std::shared_ptr<elix::ui::UIWidget>& element)
{
    if(!m_overlay)
    {
        ELIX_LOG_WARN("Failed to add ui element. Editor is not initializated");
        return;
    }

    m_overlay->addUIElement(element);
}