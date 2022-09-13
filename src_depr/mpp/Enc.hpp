#pragma once
/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <cstdint>
#include <cstring>
#include <iterator>
#include <tuple>
#include <variant>

#include "BSwap.hpp"
#include "Constants.hpp"
#include "Rules.hpp"
#include "Spec.hpp"
#include "../Utils/Common.hpp"
#include "../Utils/CStr.hpp"
#include "../Utils/Traits.hpp"

//TODO : add time_t?
//TODO : rollback in case of fail
//TODO : error handling + names
//TODO : min + max
//TODO : avoid reinterpret_cast
//TODO : add raw(N)
//TODO : add std::variant
//TODO : add std::optional
//TODO : universal buffer

namespace tnt {
namespace mpp {

using namespace ::mpp;

using std::integral_constant;
using tnt::CStr;

namespace encode_details {

struct Nothing {};

template <class T>
constexpr compact::Family detectFamily()
{
	using fixed_t = get_fixed_t<T>;
	constexpr bool is_type_fixed = !std::is_same_v<fixed_t, void>;
	using CRU = decltype(unwrap(std::declval<const T&>()));
	using CRUF = std::conditional_t<is_type_fixed, fixed_t, CRU>;
	using U = std::remove_cv_t<std::remove_reference_t<CRUF>>;
	using V = std::remove_cv_t<tnt::uni_integral_base_t<U>>;
	if constexpr (is_wrapped_family_v<T>) {
		return T::family;
	} else if constexpr (std::is_same_v<V, std::nullptr_t>) {
		return compact::MP_NIL;
	} else if constexpr (std::is_same_v<V, bool>) {
		return compact::MP_BOOL;
	} else if constexpr (tnt::is_signed_integer_v<V>) {
		return compact::MP_INT;
	} else if constexpr (tnt::is_unsigned_integer_v<V>) {
		return compact::MP_UINT;
	} else if constexpr (std::is_same_v<V, float>) {
		return compact::MP_FLT;
	} else if constexpr (std::is_same_v<V, double>) {
		return compact::MP_DBL;
	} else if constexpr (tnt::is_string_constant_v<V>) {
		return compact::MP_STR;
	} else if constexpr (tnt::is_char_ptr_v<V>) {
		return compact::MP_STR;
	} else if constexpr (is_contiguous_char_v<V>) {
		return compact::MP_STR;
	} else if constexpr (tnt::is_const_pairs_iterable_v<V>) {
		return compact::MP_MAP;
	} else if constexpr (tnt::is_const_iterable_v<V>) {
		return compact::MP_ARR;
	} else if constexpr (tnt::is_tuplish_of_pairish_v<V>) {
		if constexpr(tnt::tuple_size_v<V> == 0)
			return compact::MP_ARR;
		else
			return compact::MP_MAP;
	} else if constexpr (tnt::is_tuplish_v<V>) {
		return compact::MP_ARR;
	} else {
		static_assert(tnt::always_false_v<V>, "Failed to recognise type");
		return compact::MP_END;
	}
}

template <class T, size_t ...I>
constexpr auto
tuplishTotalLength32(std::index_sequence<I...>)
{
	constexpr uint32_t total = (0 + ... + sizeof(tnt::tuple_element_t<I, T>));
	return std::integral_constant<uint32_t, total>{};
}

template<class U>
auto uniLength32([[maybe_unused]]const U& u)
{
	if constexpr(tnt::is_string_constant_v<U>) {
		return std::integral_constant<uint32_t, U::size>{};
	} else if constexpr(tnt::is_tuplish_v<U>) {
		constexpr size_t s = tnt::tuple_size_v<U>;
		return tuplishTotalLength32<U>(std::make_index_sequence<s>{});
	} else {
		static_assert(tnt::is_contiguous_v<U>);
		return static_cast<uint32_t>(std::size(u) *
					     sizeof(*std::data(u)));
	}
}

template<class U>
auto uniSize32([[maybe_unused]]const U& u)
{
	if constexpr(tnt::is_tuplish_v<U>) {
		return std::integral_constant<uint32_t, tnt::tuple_size_v<U>>{};
	} else if constexpr (tnt::is_sizable_v<U>) {
		static_assert(tnt::is_const_iterable_v<U> ||
			      tnt::is_contiguous_v<U>);
		return static_cast<uint32_t>(std::size(u));
	} else {
		static_assert(tnt::is_const_iterable_v<U>);
		return static_cast<uint32_t>(std::distance(std::cbegin(u),
							   std::cend(u)));
	}
}

template<compact::Family FAMILY, class T, class U>
auto getValue([[maybe_unused]] const T& t, [[maybe_unused]] const U& u)
{
	if constexpr (FAMILY == compact::MP_STR) {
		static_assert(tnt::is_char_ptr_v<U> ||
			      tnt::is_string_constant_v<U> ||
			      is_contiguous_char_v<U>);
		if constexpr (tnt::is_char_ptr_v<U> ||
			      tnt::is_bounded_array_v<U>) {
			/* Note special rule for MP_STR and "" literals. */
			return static_cast<uint32_t>(strlen(u));
		} else {
			static_assert(tnt::is_string_constant_v<U> ||
				      is_contiguous_char_v<U>);
			return uniLength32(u);
		}
	} else if constexpr (FAMILY == compact::MP_BIN ||
			     FAMILY == compact::MP_EXT) {
		if constexpr(tnt::is_string_constant_v<U> ||
			     tnt::is_contiguous_v<U>) {
			return uniLength32(u);
		} else {
			static_assert(std::is_standard_layout_v<U>);
			return std::integral_constant<uint32_t, sizeof(U)>{};
		}
	} else if constexpr (FAMILY == compact::MP_ARR) {
		return uniSize32(u);
	} else if constexpr (FAMILY == compact::MP_MAP) {
		if constexpr(tnt::is_tuplish_of_pairish_v<U> ||
			     tnt::is_const_pairs_iterable_v<U>) {
			return uniSize32(u);
		} else if constexpr(tnt::is_tuplish_v<U>) {
			constexpr uint32_t s = tnt::tuple_size_v<U>;
			static_assert(s % 2 == 0);
			return std::integral_constant<uint32_t, s / 2>{};
		} else {
			uint32_t s = uniSize32(u);
			assert(s % 2 == 0);
			return s / 2;
		}
	} else {
		return u;
	}
}

template <compact::Family FAMILY, class T, class U>
auto getExtType([[maybe_unused]] const T& t, [[maybe_unused]] const U& u)
{
	if constexpr (FAMILY == compact::MP_EXT) {
		using E = std::remove_cv_t<decltype(t.ext_type)>;
		if constexpr(tnt::is_integral_constant_v<E>) {
			using V = std::remove_cv_t<decltype(t.ext_type.value)>;
			if constexpr(tnt::is_signed_integer_v<V>)
				static_assert(t.ext_type.value >= INT8_MIN &&
					      t.ext_type.value <= INT8_MAX);
			else
				static_assert(t.ext_type.value <= INT8_MAX);
			constexpr int8_t v = t.ext_type.value;
			return tnt::CStr<v>{};
		} else {
			using V = std::remove_cv_t<decltype(t.ext_type)>;
			if constexpr(tnt::is_signed_integer_v<V>)
				assert(t.ext_type.value >= INT8_MIN &&
				       t.ext_type.value <= INT8_MAX);
			else
				assert(t.ext_type.value <= INT8_MAX);
			int8_t v = t.ext_type;
			return v;
		}
	} else {
		return Nothing{};
	}
}

struct ChildrenTag {};
template <class V> struct Children : ChildrenTag {
	using type = V;
	const V& v;
	explicit Children(const V& u) : v(u) {}
};

struct ChildrenPairsTag {};
template <class V> struct ChildrenPairs : ChildrenPairsTag {
	using type = V;
	const V& v;
	explicit ChildrenPairs(const V& u) : v(u) {}
};

template<compact::Family FAMILY, class T, class U, class V>
auto getData([[maybe_unused]] const T& t, [[maybe_unused]] const U& u,
	     [[maybe_unused]] const V& value)
{
	if constexpr (FAMILY == compact::MP_STR) {
		if constexpr (tnt::is_char_ptr_v<U> ||
			      tnt::is_bounded_array_v<U>) {
			using check0_t = decltype(u[0]);
			using check1_t = std::remove_reference_t<check0_t>;
			using check2_t = std::remove_cv_t<check1_t>;
			static_assert(std::is_same_v<check2_t, char>);
			/* Here value is the size of the string/ */
			return std::string_view{u, value};
		} else if constexpr(tnt::is_string_constant_v<U>) {
			return u;
		} else {
			static_assert(is_contiguous_char_v<U>);
			using check0_t = decltype(std::data(u)[0]);
			using check1_t = std::remove_reference_t<check0_t>;
			using check2_t = std::remove_cv_t<check1_t>;
			static_assert(std::is_same_v<check2_t, char>);
			return std::string_view{std::data(u), std::size(u)};
		}

	} else if constexpr (FAMILY == compact::MP_BIN ||
			     FAMILY == compact::MP_EXT) {
		if constexpr(tnt::is_string_constant_v<U>) {
			return u;
		} else if constexpr(tnt::is_contiguous_v<U>) {
			auto p = reinterpret_cast<const char*>(std::data(u));
			return std::string_view{p, value};
		} else {
			static_assert(std::is_standard_layout_v<U>);
			auto p = reinterpret_cast<const char*>(&u);
			return std::string_view{p, value};
		}
	} else if constexpr (FAMILY == compact::MP_ARR) {
		return Children<U>{u};
	} else if constexpr (FAMILY == compact::MP_MAP) {
		if constexpr(tnt::is_tuplish_of_pairish_v<U> ||
			     tnt::is_const_pairs_iterable_v<U>)
			return ChildrenPairs<U>{u};
		else
			return Children<U>{u};
	} else {
		return Nothing{};
	}
}

template<compact::Family FAMILY, class T, class U>
auto getIS([[maybe_unused]] const T& t, [[maybe_unused]] const U& u)
{
	if constexpr (FAMILY == compact::MP_ARR || FAMILY == compact::MP_MAP) {
		if constexpr(tnt::is_tuplish_v<U>) {
			return std::make_index_sequence<tnt::tuple_size_v<U>>{};
		} else {
			static_assert(tnt::is_const_iterable_v<U> ||
				      tnt::is_contiguous_v<U>);
			return std::index_sequence<>{};
		}
	} else {
		return std::index_sequence<>{};
	}
}

template <class T, T V, size_t... I>
constexpr auto enc_bswap_h(std::integral_constant<T, V>, std::index_sequence<I...>)
{
	static_assert(tnt::is_unsigned_integer_v<T>);
	constexpr union { char bytes[2]; uint16_t value; } host_order = {{0, 1}};
	static_assert(host_order.value == 0x0100);
	return tnt::CStr<((V >> (8 * (sizeof...(I) - I - 1))) & 0xff)...>{};
}

template <class T, T V>
constexpr auto enc_bswap(std::integral_constant<T, V>)
{
	static_assert(tnt::is_integer_v<T>);
	using U = std::make_unsigned_t<T>;
	constexpr U u = static_cast<U>(V);
	return enc_bswap_h(std::integral_constant<U, u>{},
			   std::make_index_sequence<sizeof(T)>{});
}

template <class T>
under_uint_t<T> enc_bswap(T t)
{
	return bswap(t);
}

template <class RULE, bool IS_FIXED, class FIXED_T, class V>
constexpr bool can_encode_simple()
{
	if constexpr(tnt::is_integral_constant_v<V> || IS_FIXED ||
		     !has_complex_v<RULE>) {
		return true;
	} else if constexpr(!has_simplex_v<RULE>) {
		static_assert(has_complex_v<RULE>);
		using types = typename RULE::complex_types;
		return std::tuple_size_v<types> == 1;
	} else {
		return false;
	}
}

template <class RULE, bool IS_FIXED, class FIXED_T, class V>
constexpr auto getTagValSimple([[maybe_unused]] V value)
{
	if constexpr(RULE::family == compact::MP_NIL ||
		     RULE::family == compact::MP_IGNR) {
		// That type is completely independent on value
		static_assert(!IS_FIXED || std::is_same_v<FIXED_T, void>);
		constexpr char t = static_cast<char>(RULE::simplex_tag);
		return std::make_pair(tnt::CStr<t>{}, Nothing{});
	} else if constexpr(IS_FIXED && std::is_same_v<FIXED_T, void>) {
		static_assert(has_simplex_v<RULE>);

		// Check value.
		if constexpr(tnt::is_uni_integer_v<V> &&
			     tnt::is_integral_constant_v<V>) {
			static_assert(find_simplex_offset<RULE>(V::value) <
				      SimplexRange<RULE>::length);
		} else if constexpr(tnt::is_integer_v<V>) {
			assert(find_simplex_offset<RULE>(value) <
			       SimplexRange<RULE>::length);
		}

		if constexpr(tnt::is_integral_constant_v<V>) {
			constexpr uint8_t t = RULE::simplex_tag + V::value;
			return std::make_pair(tnt::CStr<t>{}, Nothing{});
		} else {
			uint8_t t = RULE::simplex_tag + value;
			return std::make_pair(t, Nothing{});
		}
	} else if constexpr(IS_FIXED) {
		static_assert(has_complex_v<RULE>);
		using types = typename RULE::complex_types;
		constexpr uint8_t offset = tnt::tuple_find_v<FIXED_T, types>;
		static_assert(offset < std::tuple_size_v<types>,
			      "Given fixed type is not in rule");
		constexpr char t = static_cast<char>(RULE::complex_tag + offset);
		auto tag = tnt::CStr<t>{};

		// Check value.
		if constexpr(tnt::is_integer_v<FIXED_T>) {
			using I = std::conditional_t<tnt::is_signed_integer_v<FIXED_T>,
						     under_int_t<FIXED_T>,
						     under_uint_t<FIXED_T>>;
			using L = std::numeric_limits<I>;
			if constexpr(tnt::is_integral_constant_v<V>) {
				constexpr I i = static_cast<I>(V::value);
				if constexpr(tnt::is_signed_integer_v<FIXED_T>)
					static_assert(i >= L::min);
				static_assert(i <= L::max);
			} else {
				I i = static_cast<I>(value);
				if constexpr(tnt::is_signed_integer_v<FIXED_T>)
					assert(i >= L::min);
				assert(i <= L::max);
			}
		}

		auto enc_val = enc_bswap(value);
		return std::make_pair(tag, enc_val);
	} else if constexpr(tnt::is_integral_constant_v<V>) {
		constexpr auto cv = V::value;
		if constexpr(RULE::is_negative &&
			     tnt::is_signed_integer_v<decltype(cv)>) {
			if constexpr(under_int_t<decltype(cv)>(cv) >= 0) {
				using rule = typename RULE::positive_rule;
				return getTagValSimple<rule, false, void>(value);
			}
		}
		constexpr size_t soff = find_simplex_offset<RULE>(V::value);
		if constexpr(soff < SimplexRange<RULE>::length) {
			constexpr uint8_t ut = RULE::simplex_tag + soff;
			constexpr int8_t t = static_cast<uint8_t>(ut);
			return std::make_pair(tnt::CStr<t>{}, Nothing{});
		} else {
			constexpr size_t coff = find_complex_offset<RULE>(cv);
			constexpr uint8_t ut = RULE::complex_tag + coff;
			constexpr int8_t t = static_cast<uint8_t>(ut);
			using types = typename RULE::complex_types;
			using type = std::tuple_element_t<coff, types>;
			std::integral_constant<type, cv> val;
			auto enc_val = enc_bswap(val);
			return std::make_pair(tnt::CStr<t>{}, enc_val);
		}
	} else {
		static_assert(!has_simplex_v<RULE> || !has_complex_v<RULE>);
		static_assert(!RULE::is_negative);
		if constexpr(has_simplex_v<RULE>) {
			size_t soff = find_simplex_offset<RULE>(value);
			uint8_t t = RULE::simplex_tag + soff;
			return std::make_pair(t, Nothing{});
		} else  {
			static_assert(has_complex_v<RULE>);
			using types = typename RULE::complex_types;
			static_assert(std::tuple_size_v<types> == 1);
			using type = std::tuple_element_t<0, types>;
			constexpr char t = static_cast<char>(RULE::complex_tag);
			auto enc_val = enc_bswap(static_cast<type>(value));
			return std::make_pair(tnt::CStr<t>{}, enc_val);
		}
	}
}

template <class TYPES, size_t IND, bool IS_SIGNED_NEGATIVE, class V>
constexpr bool surely_fits()
{
	constexpr size_t I = IND < std::tuple_size_v<TYPES> ? IND :
			     std::tuple_size_v<TYPES> - 1;
	using type = std::tuple_element_t<I, TYPES>;
	if constexpr(IND + 1 >= std::tuple_size_v<TYPES>) {
		return true;
	} else if constexpr(std::is_signed_v<type>) {
		if constexpr(tnt::is_signed_integer_v<V>)
			return sizeof(type) >= sizeof(V);
		else
			return sizeof(type) > sizeof(V);
	} else {
		return sizeof(type) >= sizeof(V);
	}
}

template <class TYPES, size_t IND, bool IS_SIGNED_NEGATIVE, class V>
bool check_fits([[maybe_unused]] V v)
{
	constexpr size_t I = IND < std::tuple_size_v<TYPES> ? IND :
			     std::tuple_size_v<TYPES> - 1;
	using type = std::tuple_element_t<I, TYPES>;
	if constexpr(IND >= std::tuple_size_v<TYPES>) {
		return true;
	} else if constexpr(!tnt::is_integer_v<type>) {
		return true;
	} else {
		using lim = std::numeric_limits<type>;
		if constexpr(std::is_signed_v<type>) {
			if constexpr(tnt::is_signed_integer_v<V>) {
				if constexpr(IS_SIGNED_NEGATIVE) {
					assert(v < 0);
				} else {
					if (v > lim::max())
						return false;
				}
				return v >= lim::min();
			} else {
				static_assert(!IS_SIGNED_NEGATIVE);
				using utype = std::make_unsigned_t<type>;
				auto max = static_cast<utype>(lim::max());
				return v <= max;
			}
		} else {
			static_assert(!IS_SIGNED_NEGATIVE);
			if constexpr(tnt::is_signed_integer_v<V>) {
				assert(v >= 0);
				auto u = static_cast<under_uint_t<V>>(v);
				return u <= lim::max();

			} else {
				return v <= lim::max();
			}
		}
	}
}

/** Terminal encode. */
template <class CONT, char... C, size_t...I>
bool
encode(CONT &cont, tnt::CStr<C...> prefix, std::index_sequence<I...>)
{
	static_assert(sizeof...(I) == 0);
	cont.write(prefix);
	return true;
}

template <class CONT, char... C, size_t... I, class T, class... MORE>
bool
encode(CONT &cont, tnt::CStr<C...> prefix,
       [[maybe_unused]] std::index_sequence<I...> ais,
       const T& t, const MORE&... more)
{
	const auto& u = unwrap(t);
	using U = std::remove_cv_t<std::remove_reference_t<decltype(u)>>;
	if constexpr(std::is_same_v<U, Nothing>) {
		return encode(cont, prefix, ais, more...);
	} else if constexpr(is_wrapped_raw_v<T>) {
		if constexpr(std::is_base_of_v<ChildrenTag, U>) {
			using V = typename U::type;
			const V& v = u.v;
			if constexpr(tnt::is_tuplish_v<V>) {
				std::index_sequence<> is;
				return encode(cont, prefix, is,
					      tnt::get<I>(v)..., more...);
			} else if constexpr(tnt::is_const_iterable_v<V>) {
				auto itr = std::begin(v);
				auto e = std::end(v);
				if (itr != e) {
					if (!encode(cont, prefix, ais, *itr))
						return false;
					++itr;
				}
				for (; itr != e; ++itr) {
					if (!encode(cont, CStr<>{}, ais, *itr))
						return false;
				}
				return encode(cont, prefix, ais, more...);
			} else {
				static_assert(tnt::is_contiguous_v<V>);
				auto itr = std::data(v);
				auto e = itr + std::size(v);
				if (itr != e) {
					if (!encode(cont, prefix, ais, *itr))
						return false;
					++itr;
				}
				for (; itr != e; ++itr) {
					if (!encode(cont, CStr<>{}, ais, *itr))
						return false;
				}
				return encode(cont, prefix, ais, more...);
			}
		} else if constexpr(std::is_base_of_v<ChildrenPairsTag, U>) {
			using V = typename U::type;
			const V& v = u.v;
			if constexpr(tnt::is_tuplish_v<V>) {
				std::index_sequence<> is;
				return encode(cont, prefix, is,
					      tnt::get<I>(v).first...,
					      tnt::get<I>(v).second...,
					      more...);
			} else if constexpr(tnt::is_const_iterable_v<V>) {
				auto itr = std::begin(v);
				auto e = std::end(v);
				if (itr != e) {
					if (!encode(cont, prefix, ais,
						itr->first, itr->second))
						return false;
					++itr;
				}
				for (; itr != e; ++itr) {
					if (!encode(cont, CStr<>{}, ais,
						    itr->first, itr->second))
						return false;
				}
				return encode(cont, prefix, ais, more...);
			} else {
				static_assert(tnt::is_contiguous_v<V>);
				auto itr = std::data(v);
				auto e = itr + std::size(v);
				if (itr != e) {
					if (!encode(cont, prefix, ais,
						    itr->first, itr->second))
						return false;
					++itr;
				}
				for (; itr != e; ++itr) {
					if (!encode(cont, CStr<>{}, ais,
						    itr->first, itr->second))
						return false;
				}
				return encode(cont, prefix, ais, more...);
			}
		} else if constexpr(tnt::is_string_constant_v<U>) {
			return encode(cont, prefix.join(u), ais, more...);
		} else if constexpr(tnt::is_contiguous_v<U>) {
			cont.write(prefix);
			cont.write({std::data(u), std::size(u)});
			return encode(cont, tnt::CStr<>{}, ais, more...);
		} else {
			static_assert(std::is_standard_layout_v<U>);
			cont.write(prefix);
			cont.write(u);
			return encode(cont, tnt::CStr<>{}, ais, more...);
		}
	} else {
		constexpr bool is_fixed = is_wrapped_fixed_v<T>;
		using fixed_t = get_fixed_t<T>;
		constexpr compact::Family family = detectFamily<T>();
		using rule_t = RuleByFamily_t<family>;
		auto value = getValue<family>(t, u);
		using V = decltype(value);
		auto ext = getExtType<family>(t, u);
		auto data = getData<family>(t, u, value);
		static_assert(ais.size() == 0);
		auto is = getIS<family>(t, u);
		if constexpr(can_encode_simple<rule_t, is_fixed, fixed_t, V>()) {
			auto tag_val = getTagValSimple<rule_t, is_fixed,
				fixed_t>(value);
			return encode(cont, prefix, is, as_raw(tag_val.first),
				      as_raw(tag_val.second), as_raw(ext),
				      as_raw(data), more...);
		} else {
			static_assert(has_complex_v<rule_t>);
			using types = typename rule_t::complex_types;
			static_assert(!tnt::is_integral_constant_v<V>);
			if constexpr(rule_t::is_negative &&
				     tnt::is_signed_integer_v<V> &&
				     !is_wrapped_family_v<T>) {
				if (under_int_t<V>(value) >= 0) {
					return encode(cont, prefix, is,
						      under_uint_t<V>(value),
						      more...);
				}
			}
			if constexpr(!rule_t::is_negative &&
				     tnt::is_signed_integer_v<V>) {
				assert(under_int_t<V>(value) >= 0);
			}
			if constexpr(has_simplex_v<rule_t>) {
				size_t soff = find_simplex_offset<rule_t>(value);
				if (soff < SimplexRange<rule_t>::length) {
					uint8_t tag = rule_t::simplex_tag + soff;
					cont.write(prefix);
					cont.write(tag);
					return encode(cont, tnt::CStr<>{}, is,
						      as_raw(ext), as_raw(data),
						      more...);
				}
			}
			auto complex = [&](auto IND) -> bool {
				constexpr size_t i = decltype(IND)::value;
				if constexpr(i >= std::tuple_size_v<types>) {
					return true;
				} else {
					using type =
						std::tuple_element_t<i, types>;
					constexpr uint8_t utag =
						rule_t::complex_tag + i;
					constexpr int8_t tag =
						static_cast<int8_t>(utag);
					auto tvalue = static_cast<type>(value);
					auto enc_val = enc_bswap(tvalue);
					auto new_prefix =
						prefix.join(tnt::CStr<tag>{});
					return encode(cont, new_prefix, is,
						      as_raw(enc_val),
						      as_raw(ext), as_raw(data),
						      more...);
				}
			};
			std::integral_constant<size_t, 0> ind0;
			std::integral_constant<size_t, 1> ind1;
			std::integral_constant<size_t, 2> ind2;
			std::integral_constant<size_t, 3> ind3;
			static_assert(std::tuple_size_v<types> <= 4);
			constexpr bool is_signed_negative =
				rule_t::is_negative &&
				tnt::is_signed_integer_v<V> &&
				!is_wrapped_family_v<T>;

			if constexpr(surely_fits<types, 0,
						 is_signed_negative, V>())
				return complex(ind0);
			if (check_fits<types, 0, is_signed_negative>(value))
				return complex(ind0);

			if constexpr(surely_fits<types, 1,
				is_signed_negative, V>())
				return complex(ind1);
			if (check_fits<types, 1, is_signed_negative>(value))
				return complex(ind1);

			if constexpr(surely_fits<types, 2,
				is_signed_negative, V>())
				return complex(ind2);
			if (check_fits<types, 2, is_signed_negative>(value))
				return complex(ind2);

			return complex(ind3);
		}
	}
}

} // namespace encode_details

template <class CONT, class... T>
bool
encode(CONT &cont, const T&... t)
{
	// TODO: Guard
	std::index_sequence<> is;
	bool res = encode_details::encode(cont, tnt::CStr<>{}, is, t...);
	return res;
}

} // namespace mpp
} // namespace tnt
