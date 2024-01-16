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
            if (const auto &path = entry.path(); fs::is_directory(path)) {
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
              if (const auto parent = path.parent_path().parent_path();
                  parent.filename() == config && path.filename() == "CL.command.1.tlog") {
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

auto createCompileCommands(
    const fs::path& buildDir,
    std::span<const fs::path> tlogFiles,
    bool skipHeaders
) -> Result<std::vector<CompileCommand>, std::runtime_error>
{
    std::vector<std::string_view> extensions = {
        ".C", ".CC", ".CPP", ".CXX", ".M", ".MM"
    };

    std::vector<CompileCommand> compileCommands;

    for (const auto& file : tlogFiles) {
        log("File: {}\n", file.string());

        std::ifstream inFileStream{file, std::ios::binary};
        const auto lines = detail::readFileLines(inFileStream);
        if (!lines) {
            return lines.error();
        }

        for (const auto& line : *lines) {
            if (!line.starts_with("/c")) {
                continue;
            }

            log("Command: {}\n", line);

            if (std::ranges::none_of(extensions, [&line] (const auto extension) {
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
                    if (auto correctCasing = detail::getCorrectCasingForPath(fileName)) {
                        targetFile = correctCasing->string();
                        log("Source File: {}\n", targetFile);

                        auto lineFixedCase = line;
                        lineFixedCase.replace(i, fileName.size(), targetFile);
                        command.append(lineFixedCase);

                        if (std::ranges::none_of(compileCommands, [&targetFile] (const auto& compileCommand) -> bool {
                            return compileCommand.file == targetFile;
                        })) {
                            compileCommands.push_back(CompileCommand{
                                .directory = buildDir.string(),
                                .command = std::move(command),
                                .file = std::move(targetFile),
                            });
                        }
                    } else {
                        logWarning("Failed to find source file \"{}\" in command \"{}\": \"{}\"\n", fileName, line, correctCasing.error().what());
                    }

                    break;
                }
            }
        }
    }

    if (!skipHeaders) {
        std::optional<std::vector<CompileCommand>> additionalCommands;

        while (true) {
            auto headersCommands = detail::createCompileCommandsForHeaders(
                buildDir,
                additionalCommands ? *additionalCommands : compileCommands,
                compileCommands
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

namespace detail {
[[nodiscard]] auto getCorrectCasingForPath(
    const fs::path& filePath
) -> Result<fs::path, std::runtime_error>
{
    // why does std::filesystem not have a function to tell you if a path is a root
    // that works for Windows drive roots?
    auto isDriveRoot = [] (const fs::path& path) -> bool {
        if (!path.has_root_name()) {
            return false;
        }

        // skip the drive letter
        auto it = path.begin();

        if (it == path.end()) {
            // is this right? this was just an empty path..
            return false;
        }
        
        ++it;

        // skip the root directory
        if (path.has_root_directory() && it != path.end()) {
            ++it;
        }

        return it == path.end();
    };

    // why does std::string not have functions to change case?
    auto toLower = [] (const std::string_view string) -> std::string {
        std::string res;
        for (const auto c : string) {
            res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return res;
    };

    if (!fs::exists(filePath)) {
        return std::runtime_error{fmt::format("{} did not exist", filePath.string())};
    }

    if (isDriveRoot(filePath) || !filePath.has_parent_path()) {
        return filePath;
    }

    const auto parent = filePath.parent_path();
    if (!fs::is_directory(parent)) {
        return std::runtime_error{fmt::format("Directory {} did not exist", parent.string())};
    }

    for (const auto& entry : fs::directory_iterator{parent}) {
        // need to compare the actual text but ignore case because for some reason 
        // fs::equivalent returns true for 'C:/Users/' and 'C:/Documents and Settings/'
        if (toLower(entry.path().filename().string()) == toLower(filePath.filename().string())) {
            if (const auto res = getCorrectCasingForPath(parent)) {
                return *res / entry.path().filename();
            } else {
                return res.error();
            }
        }
    }

    return std::runtime_error{
        fmt::format(
            "Didn't find entry in parent for {} that matched {}", 
            parent.string(), 
            filePath.filename().string()
        )
    };
}

[[nodiscard]] auto getFileEncoding(std::istream& stream) -> FileEncoding
{
    const auto first = static_cast<unsigned char>(stream.get());
    const auto second = static_cast<unsigned char>(stream.get());

    if (first == 0xFF && second == 0xFE) {
        return FileEncoding::Utf16LittleEndian;
    } else if (first == 0xFE && second == 0xFF) {
        return FileEncoding::Utf16BigEndian;
    } else {
        stream.clear();
        stream.seekg(0, std::ios::beg);
        return FileEncoding::Utf8;
    }
}

[[nodiscard]] auto readFileLines(
    std::istream& stream
) -> Result<std::vector<std::string>, std::runtime_error>
{
    if (!stream) {
        return std::runtime_error{"Invalid file stream"};
    }

    const auto encoding = getFileEncoding(stream);

    // TODO: make this more memory efficient
    std::stringstream readStream;
    readStream << stream.rdbuf();
    const auto contents = readStream.str();

    auto getLines = [] (std::string_view string) {
        std::vector<std::string> lines;

        using namespace std::literals;

        for (const auto split : string | std::views::split('\n') | std::views::transform([] (const auto s) {
            return std::string_view{s};
        })) {
            lines.emplace_back(split.ends_with('\r') ? split.substr(0_uz, split.size() - 1_uz) : split);
        }

        return lines;
    };

    if (encoding == FileEncoding::Utf8) {
        return getLines(contents);
    } else {
        std::string converted;
        converted.reserve(contents.size() / 2_uz);
        for (auto i = encoding == FileEncoding::Utf16LittleEndian ? 0_uz : 1_uz; i < contents.size(); i += 2) {
            converted.push_back(contents[i]);
        }

        return getLines(converted);
    }
}

[[nodiscard]] auto findIncludePaths(
    std::string_view command
) -> Result<std::vector<fs::path>, std::runtime_error>
{
    std::vector<fs::path> includePaths;

    auto pos = 0_uz;
    while ((pos = command.find("/I", pos)) != std::string::npos) {
        pos += 2_uz;

        while (pos < command.size() && std::isspace(command[pos])) {
            pos++;
        }

        if (pos == command.size()) {
            return std::runtime_error{fmt::format("Ill formed /I directive in command {}: no path given", command)};
        }

        const auto usesQuotes = command[pos] == '"';
        const auto start = usesQuotes ? pos + 1_uz : pos;
        const auto end = command.find(usesQuotes ? '"' : ' ', start);
        
        // if we're not using quotes but end is npos, ie there's nothing after the include path,
        // that's ok because the substr call after will just use the size of the string
        if (usesQuotes && end == std::string::npos) {
            return std::runtime_error{fmt::format("Ill formed /I directive in command {}: unterminated \"", command)};
        }

        auto includePath = command.substr(start, end == std::string::npos ? command.size() : end - start);
        log("Found include path {}\n", includePath);
        includePaths.emplace_back(includePath);
        pos = end + 1_uz;
    }

    return includePaths;
}

[[nodiscard]] auto createCompileCommandsForHeaders(
    const fs::path& buildDir,
    std::span<const CompileCommand> compileCommandsToCheck,
    std::span<const CompileCommand> allCompileCommands
) -> Result<std::vector<CompileCommand>, std::runtime_error>
{
    std::vector<CompileCommand> headerCompileCommands;

    auto createCompileCommand = [&] (
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

        const auto correctCasing = detail::getCorrectCasingForPath(filePath);
        if (!correctCasing) {
            return correctCasing.error();
        }

        auto headerPath = correctCasing->string();

        // need to check for duplicates
        if (std::ranges::any_of(allCompileCommands, [&headerPath] (const auto& compileCommand) -> bool {
            return compileCommand.file == headerPath;
        }) || std::ranges::any_of(headerCompileCommands, [&headerPath] (const auto& compileCommand) -> bool {
            return compileCommand.file == headerPath;
        })) {
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

    for (const auto& [directory, command, file] : compileCommandsToCheck) {
        const auto& sourceFile = file;
        const auto isObjC = sourceFile.ends_with("m");

        log("Finding included headers for {}\n", sourceFile);

        std::ifstream inFileStream{sourceFile, std::ios::binary};
        const auto lines = detail::readFileLines(inFileStream);
        if (!lines) {
            return lines.error();
        }

        struct IncludedFile
        {
            std::string filePath;
            bool usesQuotes;
        };

        std::vector<IncludedFile> includedFiles;

        // find included files
        for (const auto& line : *lines) {
            std::string_view l = line;
            for (auto i = 0_uz; i < line.size(); i++) {
                if (line[i] != ' ' && line[i] != '\t') {
                    l = {line.data() + i};
                    break;
                }
            }

            if (l.empty() || !l.starts_with("#include") || (isObjC && !l.starts_with("#import"))) {
                continue;
            }

            auto start = l.starts_with("#include") ? 8_uz : 7_uz; // length of "#include" / "#import"
            while (start < l.size() && (l[start] == ' ' || l[start] == '\t')) {
                start++;
            }

            if (l[start] == '"') {
                start++;
                if (const auto end = l.find('"', start); end != std::string::npos) {
                    auto includedFile = l.substr(start, end - start);
                    log("Found included file \"{}\"\n", includedFile);
                    includedFiles.emplace_back(IncludedFile{std::string{includedFile}, true});
                }
            } else if (l[start] == '<') {
                start++;
                if (const auto end = l.find('>', start); end != std::string::npos) {
                    auto includedFile = l.substr(start, end - start);
                    log("Found included file <{}>\n", includedFile);
                    includedFiles.emplace_back(IncludedFile{std::string{includedFile}, false});
                }
            }
        }

        log("Finding include paths for {}\n", sourceFile);

        // find this file's include paths
        auto includePaths = findIncludePaths(command);
        if (!includePaths) {
            return includePaths.error();
        }

        // for each include file, generate a compile command for that file on each include path
        for (const auto& [fileName, usesQuotes] : includedFiles) {
            // If the file is included using quotes, search in the source file's directory first
            // if it's also found on an include path, it will be ignored if it was found on the
            // source file's relative path first. This mirrors how the preprocessor works.
            if (usesQuotes) {
                const auto relativePath = fs::path{sourceFile}.parent_path();
                if (auto err = createCompileCommand(relativePath, fileName, sourceFile, command)) {
                    return *err;
                }
            }

            for (const auto& includePath : *includePaths) {
                if (auto err = createCompileCommand(includePath, fileName, sourceFile, command)) {
                    return *err;
                }
            }
        }
    }

    return headerCompileCommands;
}
} // namespace detail
} // namespace compdbvs

