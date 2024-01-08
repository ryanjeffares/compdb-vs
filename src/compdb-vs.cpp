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

    if (isDriveRoot(filePath) || !filePath.has_parent_path()) {
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

[[nodiscard]] static auto readLines(std::ifstream& file) -> std::vector<std::string>
{
    // the tlog files tend to be encoded as UTF16 little endian, so check for it and convert the text if needs be
    enum class EncodingResult {
        NotUtf16, // probably UTF8
        Utf16LittleEndian,
        Utf16BigEndian,
    };

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

        for (const auto split : string | std::views::split('\n') | std::views::transform([] (const auto split) {
            return std::string_view{split};
        })) {
            lines.emplace_back(split.ends_with('\r') ? split.substr(0_uz, split.size() - 1_uz) : split);
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
}

[[nodiscard]] static auto createCompileCommandsForHeaders(
    const fs::path& buildDir,
    std::span<const CompileCommand> sourceCompileCommands
) -> Result<std::vector<CompileCommand>, std::runtime_error>
{
    auto findIncludePaths = [] (
        std::string_view argPrefix,
        std::string_view command,
        std::vector<fs::path>& includePaths
    ) -> std::optional<std::runtime_error> {
        auto pos = 0_uz;
        while ((pos = command.find(argPrefix, pos)) != std::string::npos) {
            pos += argPrefix.size();

            while (pos < command.size() && (command[pos] == ' ' || command[pos] == '\t')) {
                pos++;
            }

            if (pos >= command.size() && command[pos] != '"') {
                return std::runtime_error{fmt::format("Ill formed /I directive in command {}", command)};
            }

            const auto start = pos + 1_uz;
            const auto end = command.find('"', start);
            if (end == std::string::npos) {
                return std::runtime_error{fmt::format("Ill formed /I directive in command {}", command)};
            }

            auto includePath = command.substr(start, end - start);
            log("Found include path {}\n", includePath);
            includePaths.emplace_back(includePath);
            pos = end + 1_uz;
        }

        return {};
    };


    std::vector<CompileCommand> headerCompileCommands;

    auto createCompileCommand = [&headerCompileCommands, &buildDir, sourceCompileCommands] (
        const fs::path& includePath,
        std::string_view includedFile,
        std::string_view sourceFile,
        std::string_view command
    ) -> std::optional<std::runtime_error> {
        auto filePath = includePath / includedFile;
        // because this path is made from an "#include" directive, it might contain "/../"
        // so normalise it
        filePath = filePath.lexically_normal();
        if (!fs::exists(filePath)) {
            log("Ignoring {} because it does not exist\n", filePath.string());
            return {};
        }

        const auto correctCasing = getCorrectCasingForPath(filePath);
        if (!correctCasing) {
            return correctCasing.error();
        }

        auto headerPath = correctCasing->string();

        // need to check for duplicates in the source and new commands
        if (std::any_of(
            sourceCompileCommands.begin(),
            sourceCompileCommands.end(),
            [&headerPath] (const CompileCommand& compileCommand) {
                return compileCommand.file == headerPath;
            }
        ) || std::any_of(
            headerCompileCommands.cbegin(),
            headerCompileCommands.cend(),
            [&headerPath] (const CompileCommand& compileCommand) {
                return compileCommand.file == headerPath;
            }
        )) {
            log("Ignoring {} because it has already had an entry in the database created for it\n", headerPath);
            return {};
        }

        log("Creating compile command for {}\n", headerPath);

        auto headerCommand = std::string{command};
        const auto fileNamePos = headerCommand.find(sourceFile);
        headerCommand.replace(fileNamePos, sourceFile.size(), headerPath);

        headerCompileCommands.emplace_back(CompileCommand{
            .directory = buildDir.string(),
            .command = std::move(headerCommand),
            .file = std::move(headerPath),
        });

        return {};
    };

    for (const auto& sourceCompileCommand : sourceCompileCommands) {
        const auto& sourceFile = sourceCompileCommand.file;
        log("Finding included headers for {}\n", sourceFile);

        std::ifstream inFileStream{sourceFile, std::ios::binary};
        if (!inFileStream) {
            return std::runtime_error{fmt::format("Failed to open file {} to create compile commands for included headers", sourceFile)};
        }

        const auto& command = sourceCompileCommand.command;

        struct IncludedFile
        {
            std::string filePath;
            bool usesQuotes;
        };

        std::vector<IncludedFile> includedFiles;

        for (const auto lines = readLines(inFileStream); const auto& line : lines) {
            if (!line.starts_with("#include")) {
                continue;
            }

            auto start = 8_uz; // length of "#include"
            while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
                start++;
            }

            if (line[start] == '"') {
                start++;
                const auto end = line.find('"', start);

                if (end != std::string::npos) {
                    auto includedFile = line.substr(start, end - start);
                    log("Found included file \"{}\"\n", includedFile);
                    includedFiles.emplace_back(IncludedFile{std::move(includedFile), true});
                }
            } else if (line[start] == '<') {
                start++;
                const auto end = line.find('>', start);

                if (end != std::string::npos) {
                    auto includedFile = line.substr(start, end - start);
                    log("Found included file <{}>\n", includedFile);
                    includedFiles.emplace_back(IncludedFile{std::move(includedFile), false});
                }
            }
        }
        
        log("Finding include paths for {}\n", sourceFile);

        std::vector<fs::path> includePaths;
        if (auto err = findIncludePaths("/I", command, includePaths)) {
            return *err;
        }

        for (const auto& [file, usesQuotes] : includedFiles) {
            if (usesQuotes) {
                // need to check relative to the source file as well as include paths
                // give precedence to files included with quotes, since if you write `#include "Foo.hpp"` because `Foo.hpp`
                // exists in the same directory as the file including it, but `Foo.hpp` also exists on one of your include paths,
                // you probably meant the one in the same directory.
                const auto relativePath = fs::path{sourceFile}.parent_path();
                if (auto err = createCompileCommand(relativePath, file, sourceFile, command)) {
                    return *err;
                }
            }

            for (const auto& includePath : includePaths) {
                if (auto err = createCompileCommand(includePath, file, sourceFile, command)) {
                    return *err;
                }
            }
        }
    }

    return headerCompileCommands;
}

auto createCompileCommands(
    const fs::path& buildDir,
    std::span<const fs::path> tlogFiles,
    bool skipHeaders
) -> Result<std::vector<CompileCommand>, std::runtime_error>
{
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

        for (const auto lines = readLines(inFileStream); const auto& line : lines) {
            if (!line.starts_with("/c")) {
                continue;
            }

            log("Command: {}\n", line);

            if (std::none_of(extensions.cbegin(), extensions.cend(), [&line] (const std::string_view extension) {
                return line.ends_with(extension);
            })) {
                return std::runtime_error{fmt::format("Command did not end with source file: {}", line)};
            }

            std::string command{"cl.exe "};

            // go from the end of the command until we find the last occurrence of a Windows drive letter and ':'
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
                        command.append(lineFixedCase);
                        break;
                    } else {
                        return correctCasing.error();
                    }
                }
            }

            if (targetFile.empty()) {
                return std::runtime_error{fmt::format("Couldn't find source file in command: {}\n", line)};
            }
            
            if (std::none_of(
                compileCommands.cbegin(),
                compileCommands.cend(),
                [&targetFile] (const CompileCommand& compileCommand) {
                    return compileCommand.file == targetFile;
                }
            )) {
                compileCommands.push_back(CompileCommand{
                    .directory = buildDir.string(),
                    .command = std::move(command),
                    .file = std::move(targetFile),
                });
            }
        }
    }

    if (!skipHeaders) {
        std::optional<std::vector<CompileCommand>> additionalCommands;

        while (true) {
            auto headersCommands = createCompileCommandsForHeaders(
                buildDir,
                additionalCommands ? *additionalCommands : compileCommands
            );

            if (!headersCommands) {
                return headersCommands.error();
            }

            additionalCommands = std::move(*headersCommands);
            if (additionalCommands->empty()) {
                break;
            }

            compileCommands.insert(
                compileCommands.end(),
                additionalCommands->begin(),
                additionalCommands->end()
            );
        }
    }

    return compileCommands;
}
} // namespace compdbvs

