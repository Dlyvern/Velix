#include "Engine/ShaderDataExtractor.hpp"
#include <vector>
#include <fstream>

#include <spirv_cross.hpp>

#include <spirv_glsl.hpp>

ELIX_NESTED_NAMESPACE_BEGIN(engine)

std::vector<ShaderReflection> ShaderDataExtractor::parse(const core::ShaderHandler& handler, const std::string& filePath)
{
    std::vector<ShaderReflection> reflections;

    if(handler.getCode().empty())
    {
        std::cerr << "SPIRV from shader is empty" << std::endl;
        return reflections;
    }

    spirv_cross::CompilerGLSL compiler(handler.getCode());
    spirv_cross::ShaderResources resources = compiler.get_shader_resources();

    for(const auto& uniformBuffer : resources.uniform_buffers)
    {
        auto set = compiler.get_decoration(uniformBuffer.id, spv::DecorationDescriptorSet);
        auto binding = compiler.get_decoration(uniformBuffer.id, spv::DecorationBinding);
        auto type = compiler.get_type(uniformBuffer.base_type_id);
        auto name = compiler.get_name(uniformBuffer.id);
        auto size = compiler.get_declared_struct_size(type);

        std::cout << "UBO: " << name << " set=" << set << " binding=" << binding
              << " size=" << size << std::endl;
    }
    
    for (auto& sampler : resources.sampled_images)
    {
        auto set = compiler.get_decoration(sampler.id, spv::DecorationDescriptorSet);
        auto binding = compiler.get_decoration(sampler.id, spv::DecorationBinding);
        auto name = compiler.get_name(sampler.id);

        std::cout << "Texture: " << name << " set=" << set << " binding=" << binding << std::endl;
    }

    for (auto& pushConstant : resources.push_constant_buffers)
    {
        auto& type = compiler.get_type(pushConstant.base_type_id);
        size_t size = compiler.get_declared_struct_size(type);
        std::cout << "PushConstant: " << compiler.get_name(pushConstant.id)
                << " size=" << size << std::endl;
    }

    for (auto& input : resources.stage_inputs)
    {
        auto location = compiler.get_decoration(input.id, spv::DecorationLocation);
        auto name = compiler.get_name(input.id);
        std::cout << "Vertex Input: " << name << " location=" << location << std::endl;
    }


    // auto& type = compiler.get_type(resource.type_id);
    // if (type.basetype == spirv_cross::SPIRType::SampledImage)
    //     descriptor.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    // else if (type.basetype == spirv_cross::SPIRType::Sampler)
    //     descriptor.type = VK_DESCRIPTOR_TYPE_SAMPLER;
    // else if (type.storage == spv::StorageClassUniform)
    //     descriptor.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // else if (type.storage == spv::StorageClassStorageBuffer)
    //     descriptor.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;


    return reflections;
}

ELIX_NESTED_NAMESPACE_END