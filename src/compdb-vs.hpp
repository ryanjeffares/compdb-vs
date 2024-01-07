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

#ifndef COMPDB_VS_HPP
#define COMPDB_VS_HPP

#include "result.hpp"

#include <fmt/color.h>
#include <fmt/core.h>

#include <cstddef>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

inline constexpr std::size_t operator""_uz (unsigned long long int value)
{
    return static_cast<std::size_t>(value);
}

namespace compdbvs {
namespace fs = std::filesystem;

struct [[nodiscard]] CompileCommand
{
    std::string directory;
    std::string command;
    std::string file;
};

[[nodiscard]] auto findTlogFiles(
    const fs::path& buildDir,
    std::string_view config
) -> Result<std::vector<fs::path>, std::runtime_error>;

[[nodiscard]] auto createCompileCommands(
    const fs::path& buildDir,
    std::span<const fs::path> tlogFiles
) -> Result<std::vector<CompileCommand>, std::runtime_error>;

extern bool g_verbose;

template<typename... Ts>
inline auto log(fmt::format_string<Ts...> message, Ts&&... formatArgs) -> void
{
    if (g_verbose) {
        fmt::print(message, std::forward<Ts>(formatArgs)...);
    }
}

template<typename... Ts>
inline auto logError(fmt::format_string<Ts...> message, Ts&&... formatArgs) -> void
{
    fmt::print(stderr, fmt::emphasis::bold | fmt::fg(fmt::color::red), "ERROR: ");
    fmt::print(stderr, message, std::forward<Ts>(formatArgs)...);
}
} // namespace compdbvs

#endif // #ifndef COMPDBVS_HPP

