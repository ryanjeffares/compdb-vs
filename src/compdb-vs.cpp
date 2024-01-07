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

#include <fstream>
#include <ranges>

#include <windows.h>

namespace compdbvs {
bool g_verbose = false;

auto findTlogFiles(
    const fs::path& buildDir,
    std::string_view config
) -> Result<std::vector<fs::path>, std::runtime_error>
{
    std::vector<fs::path> tlogFiles;

    if (!fs::is_directory(buildDir)) {
        return std::runtime_error{fmt::format("Couldn't open build directory {}", buildDir.string())};
    }

    try {
        for (const auto& entry : fs::directory_iterator{buildDir}) {
            const auto& path = entry.path();

            if (fs::is_directory(path)) {
                log("Looking in {}...\n", path.string());
                auto innerFiles = findTlogFiles(path, config);
                if (!innerFiles) {
                    return innerFiles.error();
                }

                tlogFiles.insert(
                    tlogFiles.end(),
                    std::make_move_iterator(innerFiles->begin()),
                    std::make_move_iterator(innerFiles->end())
                );
            } else {
                const auto parent = path.parent_path().parent_path();
                if (parent.filename() == config && path.filename() == "CL.command.1.tlog") {
                    log("Found file {}\n", path.string());
                    tlogFiles.push_back(path);
                }
            }
        }
    } catch (const fs::filesystem_error& e) {
        return std::runtime_error{fmt::format("Failed to iterator through directory {}: {}", buildDir.string(), e.what())};
    }

    return tlogFiles;
}

[[nodiscard]] static auto getCorrectCasingForPath(const fs::path& filePath) -> Result<fs::path, std::runtime_error> {
    // why does std::filesystem not have a function to tell you if a path is a root
    // that works for Windows drive roots?
    auto isDriveRoot = [] (const fs::path& path) -> bool {
        if (!path.has_root_name()) {
            return false;
        }

        // skip the drive letter
        auto it = ++path.begin();

        // skip the root directory
        if (path.has_root_directory()) {
            it++;
        }

        return it == path.end();
    };

    // why does std::string not have functions to change case?
    auto toLower = [] (std::string_view string) -> std::string {
        std::string res;
        for (const auto c : string) {
            res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return res;
    };

    if (isDriveRoot(filePath)) {
        return filePath;
    }

    const auto parent = filePath.parent_path();
    for (const auto& entry : fs::directory_iterator{parent}) {
        // need to compare the actual text but ignore case because for some reason 
        // fs::equivalent returns true for 'C:/Users/' and 'C:/Documents and Settings/'
        if (toLower(entry.path().filename().string()) == toLower(filePath.filename().string())) {
            const auto res = getCorrectCasingForPath(parent);
            if (res) {
                return *res / entry.path().filename();
            } else {
                return res.error();
            }
        }
    }

    return std::runtime_error{fmt::format("Didn't find entry in parent for {} that matched {}", parent.string(), filePath.filename().string())};
}

auto createCompileCommands(
    const fs::path& buildDir,
    std::span<const fs::path> tlogFiles
) -> Result<std::vector<CompileCommand>, std::runtime_error>
{
    // the tlog files tend to be encoded as UTF16 little endian, so check for it and convert the text if needs be
    enum class EncodingResult {
        NotUtf16, // probably UTF8
        Utf16LittleEndian,
        Utf16BigEndian,
    };

    auto readLines = [] (std::ifstream& file) -> std::vector<std::string> {
        const auto encoding = [] (std::ifstream& file) -> EncodingResult {
            if (file.get() == 0xFF && file.get() == 0xFE) {
                return EncodingResult::Utf16LittleEndian;
            } else if (file.get() == 0xFE && file.get() == 0xFF) {
                return EncodingResult::Utf16BigEndian;
            } else {
                file.clear();
                file.seekg(0, std::ios::beg);
                return EncodingResult::NotUtf16;
            }
        }(file);

        std::stringstream stream;
        stream << file.rdbuf();
        const auto contents = stream.str();

        auto getLines = [] (std::string_view string) {
            std::vector<std::string> lines;

            using namespace std::literals;

            for (const auto split : std::views::split(string, "\r\n"sv) | std::views::transform([] (const auto split) {
                return std::string_view{split.data(), split.size()};
            })) {
                lines.emplace_back(split);
            }

            return lines;
        };

        if (encoding == EncodingResult::NotUtf16) {
            return getLines(contents);
        } else {
            std::string converted;
            converted.reserve(contents.size() / 2_uz);
            for (auto i = encoding == EncodingResult::Utf16LittleEndian ? 0_uz : 1_uz; i < contents.size(); i += 2) {
                converted.push_back(contents[i]);
            }

            return getLines(converted);
        }
    };

    std::vector<std::string_view> extensions = {
        ".C", ".CC", ".CPP", ".CXX",
    };

    std::vector<CompileCommand> compileCommands;

    for (const auto& file : tlogFiles) {
        log("File: {}\n", file.string());

        std::ifstream inFileStream{file, std::ios::binary};
        if (!inFileStream) {
            return std::runtime_error{fmt::format("Failed to open file {}", file.string())};
        }

        const auto lines = readLines(inFileStream);
        log("Num Lines: {}\n", lines.size());

        for (const auto& line : lines) {
            if (line.starts_with("/c")) {
                log("Command: {}\n", line);

                if (std::none_of(extensions.cbegin(), extensions.cend(), [&line] (const std::string_view extension) {
                    return line.ends_with(extension);
                })) {
                    return std::runtime_error{fmt::format("Command did not end with source file: {}", line)};
                }

                std::string command{"cl.exe "};

                // go from the end of the command until we find the last occurance of a Windows drive letter and ':'
                // that will be the start of the full path to the source file
                std::string targetFile;
                for (auto i = line.size() - 2_uz; i > 0_uz; i--) {
                    if (std::isalpha(line[i]) && line[i + 1_uz] == ':') {
                        const auto fileName = line.substr(i);

                        // paths in the tlog files seem to all be converted to all upper case.
                        auto correctCasing = getCorrectCasingForPath(fileName);
                        if (correctCasing) {
                            targetFile = correctCasing->string();
                            log("Source File: {}\n", targetFile);

                            auto lineFixedCase = line;
                            lineFixedCase.replace(i, fileName.size(), targetFile);
                            command.append(std::move(lineFixedCase));
                            break;
                        } else {
                            return correctCasing.error();
                        }
                    }
                }

                if (targetFile.empty()) {
                    return std::runtime_error{fmt::format("Couldn't find source file in command: {}\n", line)};
                }

                compileCommands.push_back(CompileCommand{
                    .directory = buildDir.string(),
                    .command = std::move(command),
                    .file = std::move(targetFile),
                });
            }
        }
    }

    return compileCommands;
}
} // namespace compdbvs

