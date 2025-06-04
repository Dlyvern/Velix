#include "Project.hpp"

void Project::setFullPath(const std::string &fullPath)
{
    m_fullPath = fullPath;
}

std::string Project::getFullPath() const
{
    return m_fullPath;
}

void Project::setName(const std::string &name)
{
    m_name = name;
}

void Project::setBuildDir(const std::string &buildDir)
{
    m_buildDir = buildDir;
}

void Project::setSourceDir(const std::string &sourceDir)
{
    m_sourceDir = sourceDir;
}

void Project::setScenesDir(const std::string &scenesDir)
{
    m_scenesDir = scenesDir;
}

void Project::setEntryScene(const std::string &entryScene)
{
    m_entryScene = entryScene;
}

void Project::setAssetsDir(const std::string &assetsDir)
{
    m_assetsDir = assetsDir;
}

std::string Project::getAssetsDir() const
{
    return m_assetsDir;
}

std::string Project::getEntryScene() const
{
    return m_entryScene;
}

std::string Project::getScenesDir() const
{
    return m_scenesDir;
}

std::string Project::getSourceDir() const
{
    return m_sourceDir;
}

std::string Project::getBuildDir() const
{
    return m_buildDir;
}

std::string Project::getName() const
{
    return m_name;
}
