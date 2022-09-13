#pragma once
/*
 * Copyright 2010-2021 Tarantool AUTHORS: please see AUTHORS file.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * AUTHORS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
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
#include <type_traits>
#include "../Utils/Common.hpp"

namespace mpp {

/**
 * Getter of unsigned integer type with the size as given type.
 */
template <class T>
using under_uint_t = std::tuple_element_t<
	tnt::tuple_find_size_v<sizeof(T), tnt::uint_types>,
	tnt::uint_types>;

/**
 * Getter of signed integer type with the size as given type.
 */
template <class T>
using under_int_t = std::tuple_element_t<
	tnt::tuple_find_size_v<sizeof(T), tnt::int_types>,
	tnt::int_types>;

/**
 * bswap overloads.
 */
inline uint8_t  bswap(uint8_t x)  { return x; }
inline uint16_t bswap(uint16_t x) { return __builtin_bswap16(x); }
inline uint32_t bswap(uint32_t x) { return __builtin_bswap32(x); }
inline uint64_t bswap(uint64_t x) { return __builtin_bswap64(x); }

/**
 * msgpack encode bswap: convert any type to uint and bswap it.
 */
template <class T>
under_uint_t<T> bswap(T t)
{
	static_assert(std::is_enum_v<T> || std::is_floating_point_v<T> ||
		      (std::is_integral_v<T> &&
		       !std::is_same_v<std::remove_cv_t<T>, bool>));
	under_uint_t<T> tmp;
	memcpy(&tmp, &t, sizeof(T));
	return bswap(tmp);
}

/**
 * msgpack decode bswap: bswap given uint and convert it to any type.
 */
template <class T>
T bswap(under_uint_t<T> t)
{
	static_assert(std::is_enum_v<T> || std::is_floating_point_v<T> ||
		      (std::is_integral_v<T> &&
		       !std::is_same_v<std::remove_cv_t<T>, bool>));
	t = bswap(t);
	T tmp;
	memcpy(&tmp, &t, sizeof(T));
	return tmp;
}

} // namespace mpp {