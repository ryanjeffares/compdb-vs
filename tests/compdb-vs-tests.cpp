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

namespace compdbvs::tests {
static auto testResult() -> void
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

static auto testCreateCompileCommands() -> void
{
    {
        const auto tlogFiles = findTlogFiles(fs::current_path().parent_path() / "tests" / "test-build-dir-1", "Debug");
        mu_check(tlogFiles);
        mu_check(tlogFiles->size() == 2_uz);

        const auto compileCommands = createCompileCommands("build", *tlogFiles);
        mu_check(compileCommands);
        mu_check(compileCommands->size() == 5_uz);
    }

    {
        const auto tlogFiles = findTlogFiles(fs::current_path().parent_path() / "tests" / "test-build-dir-2", "Debug");
        mu_check(tlogFiles);
        mu_check(tlogFiles->empty());

        const auto compileCommands = createCompileCommands("build", *tlogFiles);
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
    MU_RUN_TEST(testResult);
    MU_RUN_TEST(testCreateCompileCommands);
}
} // namespace compdbvs_tests

auto main([[maybe_unused]] int argc, [[maybe_unused]] const char* argv[]) -> int
{
    MU_RUN_SUITE(compdbvs::tests::testSuite);
    MU_REPORT();

    return MU_EXIT_CODE;
}

