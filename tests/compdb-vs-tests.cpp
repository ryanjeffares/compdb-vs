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

#include "../src/result.hpp"
#include "../src/compdb-vs.hpp"

#include <minunit/minunit.h>
#include <fstream>
#include <sstream>

namespace compdbvs::tests {
static auto test_Result() -> void
{
    {
        Result<int, std::exception> res = 1;
        mu_check(res.isOk());
        mu_assert_int_eq(*res, 1);

        res = std::exception{"Oops!"};
        mu_check(res.isErr());
        mu_assert_string_eq(res.error().what(), "Oops!");
    }

    {
        Result<std::string, std::exception> res = std::string{"Hello, World!"};
        mu_check(res.isOk());
        mu_check(res);
        mu_check(res->size() == 13_uz);
    }
}

static auto test_getCorrectCasingForPath() -> void
{
    auto toUpper = [] (std::string& string) -> void {
        for (auto& c : string) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    };

    const auto path = fs::current_path();
    fs::path upperCase;
    for (auto& element : path) {
        auto s = element.string();
        toUpper(s);
        upperCase /= s;
    }

    mu_check(fs::equivalent(path, upperCase));

    auto fixed = detail::getCorrectCasingForPath(upperCase);
    mu_check(fixed);
    mu_check(fs::equivalent(path, *fixed));
    mu_check(path == *fixed);

    fs::path doesntExist = "C:/Foo";
    fixed = detail::getCorrectCasingForPath(doesntExist);
    mu_check(fixed.isErr());
}

static auto test_getFileEncoding() -> void
{
    auto ff = static_cast<char>(0xFF);
    auto fe = static_cast<char>(0xFE);

    {
        std::stringstream utf8{std::ios_base::binary};
        utf8 << "Hello";
        const auto encoding = detail::getFileEncoding(utf8);
        mu_check(encoding == detail::FileEncoding::Utf8);
    }

    {
        std::string utf16LeString;
        utf16LeString.push_back(ff);
        utf16LeString.push_back(fe);

        std::stringstream utf16Le;
        utf16Le << utf16LeString;

        const auto encoding = detail::getFileEncoding(utf16Le);
        mu_check(encoding == detail::FileEncoding::Utf16LittleEndian);
    }

    {
        std::string utf16BeString;
        utf16BeString.push_back(fe);
        utf16BeString.push_back(ff);

        std::stringstream utf16Be;
        utf16Be << utf16BeString;

        const auto encoding = detail::getFileEncoding(utf16Be);
        mu_check(encoding == detail::FileEncoding::Utf16BigEndian);
    }
}

static auto test_readFileLines() -> void
{
    {
        const auto filePath = fs::current_path().parent_path() / "tests" / "test-project-1" / "CMakeLists.txt";
        std::ifstream fileStream{filePath};
        const auto lines = detail::readFileLines(fileStream);
        mu_check(lines);
        mu_check(lines->size() == 15_uz);
    }

    {
        std::stringstream stream;
        stream << "Hello\n";
        stream << "World\n";
        stream << "!";
        const auto lines = detail::readFileLines(stream);
        mu_check(lines);
        mu_check(lines->size() == 3_uz);
    }

    {
        const auto filePath = "C:/Foo";
        std::ifstream fileStream{filePath};
        const auto lines = detail::readFileLines(fileStream);
        mu_check(!lines);
    }
}

static auto test_findIncludePaths() -> void
{
    using namespace std::string_view_literals;

    {
        auto command = "cl.exe /c /I\"C:\\USERS\\RYAND\\DOCUMENTS\\DEV\\TOOLS\\COMPDB-VS\\BUILD\\_DEPS\\FMT-SRC\\INCLUDE\" /nologo /W1 /WX- /diagnostics:column /O2 /Ob2 /D _MBCS /D WIN32 /D _WINDOWS /D NDEBUG /D \"CMAKE_INTDIR=\\\"Release\\\"\" /Gm- /EHsc /MD /GS /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR /Fo\"FMT.DIR\\RELEASE\\\\\" /Fd\"C:\\USERS\\RYAND\\DOCUMENTS\\DEV\\TOOLS\\COMPDB-VS\\BUILD\\RELEASE\\FMT.PDB\" /external:W1 /Gd /TP C:\\Users\\ryand\\Documents\\Dev\\tools\\compdb-vs\\build\\_deps\\fmt-src\\src\\os.cc"sv;

        const auto includePaths = detail::findIncludePaths(command);
        mu_check(includePaths);
        mu_check(includePaths->size() == 1_uz);
    }

    {
        auto command = "cl.exe /c /I \"C:\\USERS\\RYAND\\DOCUMENTS\\DEV\\TOOLS\\COMPDB-VS\\BUILD\\_DEPS\\FMT-SRC\\INCLUDE\" /I \"C:\\USERS\\RYAND\\DOCUMENTS\\DEV\\TOOLS\\COMPDB-VS\\BUILD\\_DEPS\\FMT-SRC\\INCLUDE\" /i\"C:\\USERS\\RYAND\\DOCUMENTS\\DEV\\TOOLS\\COMPDB-VS\\BUILD\\_DEPS\\FMT-SRC\\INCLUDE\" /nologo /W1 /WX- /diagnostics:column /O2 /Ob2 /D _MBCS /D WIN32 /D _WINDOWS /D NDEBUG /D \"CMAKE_INTDIR=\\\"Release\\\"\" /Gm- /EHsc /MD /GS /fp:precise /Zc:wchar_t /Zc:forScope /Zc:inline /GR /Fo\"FMT.DIR\\RELEASE\\\\\" /Fd\"C:\\USERS\\RYAND\\DOCUMENTS\\DEV\\TOOLS\\COMPDB-VS\\BUILD\\RELEASE\\FMT.PDB\" /external:W1 /Gd /TP C:\\Users\\ryand\\Documents\\Dev\\tools\\compdb-vs\\build\\_deps\\fmt-src\\src\\os.cc"sv;

        const auto includePaths = detail::findIncludePaths(command);
        mu_check(includePaths);
        mu_check(includePaths->size() == 2_uz);
    }

    {
        auto command = "/I \""sv;
        const auto includePaths = detail::findIncludePaths(command);
        mu_check(!includePaths);
    }

    {
        auto command = "/I    "sv;
        const auto includePaths = detail::findIncludePaths(command);
        mu_check(!includePaths);
    }
}

static auto test_fullProgramFlow() -> void
{
    {
        const auto tlogFiles = findTlogFiles(fs::current_path().parent_path() / "tests" / "test-project-1", "Debug");
        mu_check(tlogFiles);
        mu_check(tlogFiles->size() == 4_uz);

        {
            const auto compileCommands = createCompileCommands("build", *tlogFiles, false);
            mu_check(compileCommands);
            mu_check(compileCommands->size() == 7_uz);
        }

        {
            const auto compileCommands = createCompileCommands("build", *tlogFiles, true);
            mu_check(compileCommands);
            mu_check(compileCommands->size() == 5_uz);
        }
    }

    {
        const auto tlogFiles = findTlogFiles(fs::current_path().parent_path() / "tests" / "test-project-2", "Debug");
        mu_check(tlogFiles);
        mu_check(tlogFiles->empty());

        const auto compileCommands = createCompileCommands("build", *tlogFiles, true);
        mu_check(compileCommands);
        mu_check(compileCommands->empty());
    }

    {
        const auto tlogFiles = findTlogFiles(fs::current_path().parent_path() / "foo", "bar");
        mu_check(!tlogFiles);
    }
}

MU_TEST_SUITE(testSuite)
{
    MU_RUN_TEST(test_Result);
    MU_RUN_TEST(test_getCorrectCasingForPath);
    MU_RUN_TEST(test_getFileEncoding);
    MU_RUN_TEST(test_readFileLines);
    MU_RUN_TEST(test_findIncludePaths);
    MU_RUN_TEST(test_fullProgramFlow);
}
} // namespace compdbvs_tests

auto main([[maybe_unused]] int argc, [[maybe_unused]] const char* argv[]) -> int
{
    MU_RUN_SUITE(compdbvs::tests::testSuite);
    MU_REPORT();

    return MU_EXIT_CODE;
}

