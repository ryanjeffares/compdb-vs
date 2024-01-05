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

#include <fmt/core.h>
#include <fmt/ranges.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#define COMPDB_VS_MAJOR_VERSION 1
#define COMPDB_VS_MINOR_VERSION 0
#define COMPDB_VS_PATCH_NUMBER  0

static constexpr std::size_t operator""_uz (unsigned long long int value)
{
    return static_cast<std::size_t>(value);
}

struct [[nodiscard]] CompileCommand
{
    std::string directory;
    std::string command;
    std::string file;
};

template<typename TResult, typename TException> requires(std::is_base_of_v<std::exception, TException>)
class [[nodiscard]] Result
{
public:
    class BadResultAccess : public std::exception
    {
    public:
        explicit BadResultAccess(std::string_view message)
            : m_message{fmt::format("Bad variant access: {}", message)}
        {

        }

        BadResultAccess(const BadResultAccess&) = default;
        BadResultAccess(BadResultAccess&&) noexcept = default;
        BadResultAccess& operator=(const BadResultAccess&) = default;
        BadResultAccess& operator=(BadResultAccess&&) noexcept = default;

        ~BadResultAccess() override = default;

        [[nodiscard]] const char* what() const override
        {
            return m_message.c_str();
        }

    private:
        std::string m_message;
    };

    Result(TResult result) : m_data{std::move(result)}
    {

    }

    Result(TException exception) : m_data{std::move(exception)}
    {

    }

    Result(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(const Result&) = default;
    Result& operator=(Result&&) noexcept = default;
    ~Result() = default;

    [[nodiscard]] auto isOk() const noexcept -> bool
    {
        return m_data.index() == 0_uz;
    }

    [[nodiscard]] auto isErr() const noexcept -> bool
    {
        return m_data.index() == 1_uz;
    }

    [[nodiscard]] auto value() -> TResult&
    {
        if (isErr()) {
            throw BadResultAccess("Tried to get value on an error result");
        }

        return std::get<0_uz>(m_data);
    }

    [[nodiscard]] auto value() const -> const TResult&
    {
        if (isErr()) {
            throw BadResultAccess("Tried to get value on an error result");
        }

        return std::get<0_uz>(m_data);
    }

    [[nodiscard]] auto error() -> TException&
    {
        if (isOk()) {
            throw BadResultAccess("Tried to get error on an value result");
        }

        return std::get<1_uz>(m_data);
    }

    [[nodiscard]] auto error() const -> const TException&
    {
        if (isOk()) {
            throw BadResultAccess("Tried to get error on an value result");
        }

        return std::get<1_uz>(m_data);
    }

    [[nodiscard]] auto operator*() -> TResult&
    {
        return value();
    }

    [[nodiscard]] auto operator*() const -> const TResult&
    {
        return value();
    }

    [[nodiscard]] auto operator->() -> TResult*
    {
        return &value();
    }

    [[nodiscard]] auto operator->() const -> const TResult*
    {
        return &value();
    }

    [[nodiscard]] operator bool() const noexcept
    {
        return isOk();
    }

private:
    std::variant<TResult, TException> m_data;
};

namespace fs = std::filesystem;

[[nodiscard]] static auto findTlogFiles(
    const fs::path& buildDir,
    std::string_view config
) -> Result<std::vector<fs::path>, std::runtime_error>;

[[nodiscard]] static auto createCompileCommands(
    const fs::path& buildDir,
    std::span<const fs::path> tlogFiles
) -> Result<std::vector<CompileCommand>, std::runtime_error>;

static bool s_verbose = false;

template<typename... Ts>
static auto log(fmt::format_string<Ts...> message, Ts&&... formatArgs) -> void
{
    if (s_verbose) {
        fmt::print(message, std::forward<Ts>(formatArgs)...);
    }
}

template<typename... Ts>
static auto logError(fmt::format_string<Ts...> message, Ts&&... formatArgs) -> void
{
    fmt::print(stderr, message, std::forward<Ts>(formatArgs)...);
}

static auto help() -> void
{
    fmt::print("compdb-vs {}:{}:{}\n\n", COMPDB_VS_MAJOR_VERSION, COMPDB_VS_MINOR_VERSION, COMPDB_VS_PATCH_NUMBER);

    fmt::print("Usage:\n");
    fmt::print("    compdb-vs.exe [options]\n\n");

    fmt::print("Options:\n");
    fmt::print("    --help/-h                   Print this message and exit\n");
    fmt::print("    --config/-c <config>        Specify the build config you want to generate a compilation database for (Debug, Release etc) [default: Debug]\n");
    fmt::print("    --build-dir/-bd <dir-name>  Specify the build directory relative to the current working directory to look for VS build files and generate the compilation database [default: build]\n");
    fmt::print("    --verbose/-v                Enable verbose mode\n");
}

auto main(int argc, const char* argv[]) -> int
{
    std::string config = "Debug";
    std::string buildDir = "build";
    const auto numArgs = static_cast<std::size_t>(argc);

    for (auto i = 1_uz; i < numArgs; i++) {
        const auto arg = argv[i];

        if (std::strcmp(arg, "--config") == 0 || std::strcmp(arg, "-c") == 0) {
            if (i == numArgs - 1_uz) {
                logError("Expected value for config\n");
                return -1;
            }

            config = argv[++i];
        } else if (std::strcmp(arg, "--build-dir") == 0 || std::strcmp(arg, "-bd") == 0) {
            if (i == numArgs - 1_uz) {
                logError("Expected value for build-dir\n");
                return -1;
            }

            buildDir = argv[++i];
        } else if (std::strcmp(arg, "--verbose") == 0 || std::strcmp(arg, "-v") == 0) {
            s_verbose = true;
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            help();
            return 0;
        }
    }
    
    const auto fullBuildDir = fs::current_path() / buildDir;

    const auto tlogFiles = findTlogFiles(fullBuildDir, config);
    if (!tlogFiles) {
        logError("{}\n", tlogFiles.error().what());
        return -1;
    }

    log("\n");

    const auto compileCommands = createCompileCommands(fullBuildDir, *tlogFiles);
    if (!compileCommands) {
        logError("{}\n", compileCommands.error().what());
        return -1;
    }

    log("\n");

    using namespace nlohmann;
    auto outputJson = json::array();

    log("Writing compile_commands.json...\n");

    for (const auto& command : *compileCommands) {
#ifdef COMPDBVS_DEBUG
        log("Command:\n");
        log("directory: {}\n", command.directory);
        log("command: {}\n", command.command);
        log("file: {}\n", command.file);
        log("\n");
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
        logError("Failed to write compile_commands.json\n");
        return -1;
    }
}

static auto findTlogFiles(
    const fs::path& buildDir,
    std::string_view config
) -> Result<std::vector<fs::path>, std::runtime_error>
{
    std::vector<fs::path> tlogFiles;

    if (!fs::is_directory(buildDir)) {
        return std::runtime_error{fmt::format("Couldn't open build directory {}", buildDir.string())};
    }

    try {
        for (const auto& entry : fs::directory_iterator(buildDir)) {
            const auto& path = entry.path();

            if (fs::is_directory(path)) {
                log("Looking in {}...\n", buildDir.string());
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

static auto createCompileCommands(
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
                file.unget();
                file.unget();
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

                // go from the end of the command until we find the last occurance of a Windows drive letter and ':'
                // that will be the start of the full path to the source file
                std::string targetFile;
                for (auto i = line.size() - 2_uz; i > 0_uz; i--) {
                    if (std::isalpha(line[i]) && line[i + 1_uz] == ':') {
                        targetFile = line.substr(i);
                        break;
                    }
                }

                if (targetFile.empty()) {
                    return std::runtime_error{fmt::format("Couldn't find source file in command: {}\n", line)};
                }

                log("Source File: {}\n", targetFile);

                std::string command{"cl.exe "};
                command += line;

                // TODO: clangd docs say to prefer a list of arguments, but that doesn't seem to work...
                // std::vector<std::string> arguments{"cl.exe"};
                // I swear you're supposed to be able to construct a string/string_view from just the result of split...
                // for (const auto arg : line | std::views::split(' ') | std::views::transform([] (const auto split) {
                //     return std::string{split.data(), split.size()};
                // })) {
                //     if (std::find_if(extensions.cbegin(), extensions.cend(), [arg] (const std::string_view extension) {
                //         return arg.ends_with(extension);
                //     }) != extensions.cend()) {
                //         targetFile = std::string{std::string_view{arg}};
                //     }
                //     arguments.emplace_back(arg);
                // }

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

