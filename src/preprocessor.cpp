#include "preprocessor.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <string>
#include <regex>

// TODO: preProcessShaderSource
std::string preProcessShaderSource(std::string shaderSrc, bool allowIncludes, bool addUniforms) {
    std::stringstream ss(shaderSrc); // Stringstream allows easy iteration of lines

    std::string tmp;
    std::stringstream tmpStream;

    std::string result;

    while (std::getline(ss, tmp, '\n')) {
        if (allowIncludes) {
            int endOfInc = tmp.rfind("#include", 0);
            if (endOfInc != 0) {
                std::string incInfo = tmp.substr(endOfInc);
                std::regex removeIncChrs("\\s|\"|<|>", std::regex_constants::basic);
                std::regex_replace(std::ostreambuf_iterator<char>(tmpStream), incInfo.begin(), incInfo.end(), removeIncChrs, "");
                incInfo = tmpStream.str();
                tmpStream.clear();

                std::cout << incInfo << std::endl;
            }
        }
        result += tmp;
    }

    std::cout << result << std::endl;
    return result;
}

// TODO: preProcessShaderFile
std::string preProcessShaderFile(std::filesystem::path shaderPath, bool allowIncludes, bool addUniforms) {
    throw std::runtime_error("Unimplemented");
    return "";
}