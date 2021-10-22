#pragma once

#include <iostream>
#include <filesystem>

// Resolve includes and add shadertoy uniforms to the source code of a shader
std::string preProcessShaderSource(std::string shaderSrc, bool allowIncludes, bool addUniforms);

// Resolve includes and add shadertoy uniforms to the source code of a shader loaded from a file
std::string preProcessShaderFile(std::filesystem::path shaderPath, bool allowIncludes, bool addUniforms);