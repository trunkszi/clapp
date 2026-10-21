/** \file \brief Pattern matching helpers for variants and optional variants. */
#pragma once

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace clapp {
    template<class... Fs>
    struct overloaded : Fs... {
        using Fs::operator()...;
    };
    template<class... Fs>
    overloaded(Fs...) -> overloaded<Fs...>;

    /** Visit a variant, preserving its cv/ref category and the visitor result. */
    template<class Variant, class... Fs>
        requires requires(Variant&& v) { std::visit(overloaded{std::declval<Fs>()...}, std::forward<Variant>(v)); }
    constexpr decltype(auto) match(Variant&& value, Fs&&... functions) {
        return std::visit(overloaded{std::forward<Fs>(functions)...}, std::forward<Variant>(value));
    }

    namespace detail {
        template<class Optional, class... Fs>
        constexpr auto match_optional(Optional&& value, Fs&&... functions) {
            using result_type = decltype(clapp::match(*std::forward<Optional>(value), std::forward<Fs>(functions)...));
            if constexpr (std::is_void_v<result_type>) {
                if (!value) return false;
                clapp::match(*std::forward<Optional>(value), std::forward<Fs>(functions)...);
                return true;
            } else {
                using stored_type = std::remove_cvref_t<result_type>;
                if (!value) return std::optional<stored_type>{};
                return std::optional<stored_type>{clapp::match(*std::forward<Optional>(value), std::forward<Fs>(functions)...)};
            }
        }
    }

    /** Empty optional yields nullopt (or false for a void visitor). */
    template<class... Ts, class... Fs>
    constexpr auto match(std::optional<std::variant<Ts...>>& value, Fs&&... functions) {
        return detail::match_optional(value, std::forward<Fs>(functions)...);
    }
    template<class... Ts, class... Fs>
    constexpr auto match(const std::optional<std::variant<Ts...>>& value, Fs&&... functions) {
        return detail::match_optional(value, std::forward<Fs>(functions)...);
    }
    template<class... Ts, class... Fs>
    constexpr auto match(std::optional<std::variant<Ts...>>&& value, Fs&&... functions) {
        return detail::match_optional(std::move(value), std::forward<Fs>(functions)...);
    }
    template<class... Ts, class... Fs>
    constexpr auto match(const std::optional<std::variant<Ts...>>&& value, Fs&&... functions) {
        return detail::match_optional(std::move(value), std::forward<Fs>(functions)...);
    }
}
