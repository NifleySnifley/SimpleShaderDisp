#include "preprocessor.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <regex>
#include <streambuf>

static const std::string UNIFORMS =
"uniform int iFrame;"
"uniform float iTime;"
"uniform float iTimeDelta;"
"uniform vec2 iResolution;"
"uniform vec2 iMouse;"
"uniform sampler2D iChannel0;"
"uniform sampler2D iChannel1;"
"uniform sampler2D iChannel2;"
"uniform sampler2D iChannel3;"
"uniform sampler2D iChannel4;"
"uniform sampler2D iChannel5;"
"uniform sampler2D iChannel6;"
"uniform sampler2D iChannel7;"
"uniform sampler2D iChannel8;"
"uniform sampler2D iChannel9;";

// TODO: preProcessShaderSource
std::string preProcessShaderSource(std::string shaderSrc, bool allowIncludes, bool addUniforms) {
    std::stringstream ss(shaderSrc); // Stringstream allows easy iteration of lines

    std::string tmp;
    std::stringstream tmpStream;

    std::string result;

    if (addUniforms) {
        result += UNIFORMS + "\n";
    }

    while (std::getline(ss, tmp, '\n')) {
        if (tmp.starts_with("#include") && allowIncludes) {
            std::string incInfo = tmp.substr(8);
            std::regex removeIncChrs("\\s|\"|<|>");
            incInfo = std::regex_replace(incInfo, removeIncChrs, "");
            // printf("Succesfully resolved include %s\n", incInfo.c_str());

            std::filesystem::path p(incInfo);
            if (std::filesystem::exists(p)) {
                std::ifstream file = std::ifstream(p);
                tmpStream << file.rdbuf();
                tmp = tmpStream.str();
                std::replace(tmp.begin(), tmp.end(), '\n', ';');
                tmpStream.clear();
            }
        }
        else {
            tmp += '\n';
        }
        result += tmp;
        tmp.clear();
    }

    // std::cout << "Shader resolved to source: \"\n" << result << "\n\"\n" << std::endl;
    return result;
}

// TODO: preProcessShaderFile
std::string preProcessShaderFile(std::filesystem::path shaderPath, bool allowIncludes, bool addUniforms) {
    throw std::runtime_error("Unimplemented");
    return "";
}