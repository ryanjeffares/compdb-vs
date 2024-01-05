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

struct CompileCommand
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

        ~BadResultAccess() override = default;

        const char* what() const override
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
    [[nodiscard]] Result& operator=(const Result&) = default;
    [[nodiscard]] Result& operator=(Result&&) noexcept = default;
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
static void log(fmt::format_string<Ts...> message, Ts&&... formatArgs)
{
    if (s_verbose) {
        fmt::print(message, std::forward<Ts>(formatArgs)...);
    }
}

template<typename... Ts>
static void logError(fmt::format_string<Ts...> message, Ts&&... formatArgs)
{
    fmt::print(stderr, message, std::forward<Ts>(formatArgs)...);
}

static void help()
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

int main(int argc, const char* argv[])
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
        return std::runtime_error(fmt::format("Couldn't open build directory {}", buildDir.string()));
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
        return std::runtime_error(fmt::format("Failed to iterator through directory {}: {}", buildDir.string(), e.what()));
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
            std::string_view delimiter = "\r\n";
            auto posStart = 0_uz;
            auto posEnd = 0_uz;
            auto delimLen = delimiter.size();

            std::string token;
            std::vector<std::string> res;

            while ((posEnd = string.find(delimiter, posStart)) != std::string::npos) {
                token = string.substr(posStart, posEnd - posStart);
                posStart = posEnd + delimLen;
                res.push_back(token);
            }

            res.emplace_back(string.substr(posStart));

            return res;
        };

        switch (encoding) {
            case EncodingResult::Utf16LittleEndian: {
                std::string converted;
                converted.reserve(contents.size() / 2_uz);
                for (auto i = 0_uz; i < contents.size(); i += 2) {
                    converted.push_back(contents[i]);
                }

                return getLines(converted);
            }
            case EncodingResult::Utf16BigEndian: {
                std::string converted;
                converted.reserve(contents.size() / 2_uz);
                for (auto i = 1_uz; i < contents.size(); i += 2) {
                    converted.push_back(contents[i]);
                }

                return getLines(converted);
            }
            case EncodingResult::NotUtf16:
            default:
                return getLines(contents);
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
            return std::runtime_error(fmt::format("Failed to open file {}", file.string()));
        }

        const auto lines = readLines(inFileStream);

        for (const auto& line : lines) {
            if (line.starts_with("/c")) {
                log("Command: {}\n", line);

                if (std::none_of(extensions.cbegin(), extensions.cend(), [&line] (const std::string_view extension) {
                    return line.ends_with(extension);
                })) {
                    return std::runtime_error(fmt::format("Command did not end with source file: {}", line));
                }

                // TODO: allow for spaces in the path to the source file
                auto targetFile = line.substr(line.find_last_of(' '));

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

