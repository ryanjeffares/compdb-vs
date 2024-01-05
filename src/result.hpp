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

#ifndef COMPDBVS_RESULT_HPP
#define COMPDBVS_RESULT_HPP

#include <fmt/format.h>

#include <exception>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace compdbvs {
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
        return m_data.index() == 0;
    }

    [[nodiscard]] auto isErr() const noexcept -> bool
    {
        return m_data.index() == 1;
    }

    [[nodiscard]] auto value() -> TResult&
    {
        if (isErr()) {
            throw BadResultAccess("Tried to get value on an error result");
        }

        return std::get<0>(m_data);
    }

    [[nodiscard]] auto value() const -> const TResult&
    {
        if (isErr()) {
            throw BadResultAccess("Tried to get value on an error result");
        }

        return std::get<0>(m_data);
    }

    [[nodiscard]] auto error() -> TException&
    {
        if (isOk()) {
            throw BadResultAccess("Tried to get error on an value result");
        }

        return std::get<1>(m_data);
    }

    [[nodiscard]] auto error() const -> const TException&
    {
        if (isOk()) {
            throw BadResultAccess("Tried to get error on an value result");
        }

        return std::get<1>(m_data);
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
} // namespace compdbvs

#endif // #ifndef COMPDBVS_RESULT_HPP

