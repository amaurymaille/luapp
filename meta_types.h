#pragma once

#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>

namespace Meta {

template<typename... Args>
struct first_of;

template<typename T, typename... Args>
struct first_of<T, Args...> {
    typedef T type;
};

template<typename... Args>
using first_of_t = typename first_of<Args...>::type;

template<typename F>
struct function_reducer;

template<typename F, typename T, typename... Args>
struct function_reducer<F(T, Args...)> {
    using type = F(Args...);
};

template<typename F, typename T>
struct function_reducer<F(T)> {
    using type = F();
};

template<typename T>
using function_reducer_t = typename function_reducer<T>::type;

template<typename T>
struct is_variant : public std::false_type { };

template<typename... Args>
struct is_variant<std::variant<Args...>> : public std::true_type { };

template<typename T>
constexpr bool is_variant_v = is_variant<T>::value;

template<typename T>
struct is_optional : public std::false_type {};

template<typename T>
struct is_optional<std::optional<T>> : public std::true_type {};

template<typename T>
constexpr bool is_optional_v = is_optional<T>::value;

template<typename T>
struct is_tuple : public std::false_type {};

template<typename... Args>
struct is_tuple<std::tuple<Args...>> : public std::true_type {};

template<typename T>
constexpr bool is_tuple_v = is_tuple<T>::value;

template<typename... Args>
struct ArgsList;

template<typename T, typename... Args>
struct ArgsList<T, Args...> {
    typedef T Head;
    typedef ArgsList<Args...> Tail;
    constexpr static bool end = false;
};

template<typename T>
struct ArgsList<T> {
    typedef T Head;
    constexpr static bool end = true;
};

template<typename... Args>
constexpr bool al_end_v = ArgsList<Args...>::end;

template<typename... Args>
struct variant_args;

template<typename... Args>
struct variant_args<std::variant<Args...>> {
    typedef ArgsList<Args...> VariantArgs;
};

template<typename... Args>
using variant_args_al = variant_args<Args...>::VariantArgs;

template<typename T>
struct variant_from_al;

template<typename... Args>
struct variant_from_al_rec;

template<typename T, typename... VariantArgs>
struct variant_from_al_rec<ArgsList<T>, VariantArgs...> {
    typedef std::variant<VariantArgs..., T> type;
};

template<typename... VariantArgs, typename... ListArgs>
struct variant_from_al_rec<ArgsList<ListArgs...>, VariantArgs...> {
    typedef variant_from_al_rec<typename ArgsList<ListArgs...>::Tail, VariantArgs..., typename ArgsList<ListArgs...>::Head>::type type;
};

template<typename... Args>
struct variant_from_al<ArgsList<Args...>> {
    typedef variant_from_al_rec<typename ArgsList<Args...>::Tail, typename ArgsList<Args...>::Head>::type type;
};

}
