// #include <fmt/core.h>
#include <iostream>
#include <memory>
#include <fstream>

#include <cxxopts.hpp>


#include "engine/engine.hpp"


int main(int argc, char *argv[])
{
    cxxopts::Options options("yeet", "I'm Finna yeet");
    options.add_options()("h,help", "Print usage")("f, filename", "The filename to execute", cxxopts::value<std::vector<std::string>>());
    ;

    std::string engineFilePath;

    try
    {
        auto result = options.parse(argc, argv);
        if (result.count("help"))
        {
            std::cout << options.help() << std::endl;
            return 0;
        }

        if (result.count("filename"))
        {
            auto filenames = result["filename"].as<std::vector<std::string>>();
            if (filenames.empty())
            {
                std::cerr << "No filename provided." << std::endl;
                return 1;
            }
            std::string filename = filenames[0];
            engineFilePath = filename;
            std::ifstream file(filename);
            if (!file)
            {
                std::cerr << "Failed to open file: " << filename << std::endl;
                return 1;
            }
            std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            auto engine = std::make_unique<yeet::Engine>(engineFilePath);
            try
            {
                engine->run(contents);
            }
            catch (const yeet::YeetCompileException &e)
            {
                std::cerr << e.what() << std::endl;
                return 1;
            }
            catch (const std::exception &e)
            {
                std::cerr << "EDN parse error: " << e.what() << std::endl;
                return 1;
            }
            catch (const char *msg)
            {
                std::cerr << "EDN parse error: " << msg << std::endl;
                return 1;
            }
        }
        else
        {
            std::cerr << "No filename provided." << std::endl;
            return 1;
        }
    }
    catch (const cxxopts::exceptions::exception &e)
    {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
