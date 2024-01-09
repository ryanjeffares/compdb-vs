/*
 * Copyright 2024 Ryan Jeffares
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the “Software”), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
 * to whom the Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * compdb-vs
 *
 * Generate a compilation database based on Visual Studio build files
*/

#include "compdb-vs.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <fstream>

#define COMPDB_VS_MAJOR_VERSION 1
#define COMPDB_VS_MINOR_VERSION 0
#define COMPDB_VS_PATCH_NUMBER  1

static auto help() -> void
{
    fmt::print("compdb-vs {}.{}.{}\n\n", COMPDB_VS_MAJOR_VERSION, COMPDB_VS_MINOR_VERSION, COMPDB_VS_PATCH_NUMBER);

    fmt::print("Usage:\n");
    fmt::print("    compdb-vs.exe [options]\n\n");

    fmt::print("Options:\n");
    fmt::print("    --help/-h                   Print this message and exit\n");
    fmt::print("    --config/-c <config>        Specify the build config you want to generate a compilation database for (Debug, Release etc) [default: Debug]\n");
    fmt::print("    --build-dir/-b <dir-name>   Specify the build directory relative to the current working directory to look for VS build files and generate the compilation database [default: build]\n");
    fmt::print("    --skip-headers/-sh          Skip adding header files to the compilation database\n");
    fmt::print("    --verbose/-v                Enable verbose mode\n");
}

auto main(int argc, const char* argv[]) -> int
{
    namespace fs = std::filesystem;

    const auto start = std::chrono::steady_clock::now();

    std::string config = "Debug";
    std::string buildDir = "build";
    const auto numArgs = static_cast<std::size_t>(argc);
    auto skipHeaders = false;

    for (auto i = 1_uz; i < numArgs; i++) {
        const auto arg = argv[i];

        if (std::strcmp(arg, "--config") == 0 || std::strcmp(arg, "-c") == 0) {
            if (i == numArgs - 1_uz) {
                compdbvs::logError("Expected value for config\n");
                return 1;
            }

            config = argv[++i];
        } else if (std::strcmp(arg, "--build-dir") == 0 || std::strcmp(arg, "-b") == 0) {
            if (i == numArgs - 1_uz) {
                compdbvs::logError("Expected value for build-dir\n");
                return 1;
            }

            buildDir = argv[++i];
        } else if (std::strcmp(arg, "--skip-headers") == 0 || std::strcmp(arg, "-sh") == 0) {
            skipHeaders = true;
        } else if (std::strcmp(arg, "--verbose") == 0 || std::strcmp(arg, "-v") == 0) {
            compdbvs::g_verbose = true;
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            help();
            return 0;
        } else {
            compdbvs::logError("Unrecognised argument '{}', see --help for usage\n", arg);
            return 1;
        }
    }
    
    compdbvs::logInfo("Finding .tlog files\n");

    const auto fullBuildDir = fs::current_path() / buildDir;

    const auto tlogFiles = compdbvs::findTlogFiles(fullBuildDir, config);
    if (!tlogFiles) {
        compdbvs::logError("{}\n", tlogFiles.error().what());
        return 1;
    }

    compdbvs::logInfo("Creating compile_commands.json\n");

    const auto compileCommands = compdbvs::createCompileCommands(fullBuildDir, *tlogFiles, skipHeaders);
    if (!compileCommands) {
        compdbvs::logError("{}\n", compileCommands.error().what());
        return 1;
    }

    using namespace nlohmann;
    auto outputJson = json::array();

    compdbvs::logInfo("Writing compile_commands.json\n");

    for (const auto& command : *compileCommands) {
#ifdef COMPDBVS_DEBUG
        compdbvs::log("Command:\n");
        compdbvs::log("directory: {}\n", command.directory);
        compdbvs::log("command: {}\n", command.command);
        compdbvs::log("file: {}\n", command.file);
        compdbvs::log("\n");
#endif

        outputJson.push_back({
            {"directory", command.directory},
            {"command", command.command},
            {"file", command.file},
        });
    }

    const auto outputPath = fullBuildDir / "compile_commands.json";
    std::ofstream outStream{outputPath};
    outStream << std::setw(4) << outputJson;

    if (!outStream) {
        compdbvs::logError("Failed to write compile_commands.json\n");
        return 1;
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    compdbvs::logInfo("Finished in {} ms\n", duration);
}

