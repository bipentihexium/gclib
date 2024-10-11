#ifndef GCLIB_PARAMS_HPP_
#define GCLIB_PARAMS_HPP_

#include <cstddef>

namespace gclib {
	constexpr size_t line_size = 128;
	constexpr size_t block_size = 256 * line_size;
	constexpr size_t big_object_treshold = block_size / 4;

	static_assert(block_size / line_size % 64 == 0);
	constexpr size_t line_groups = block_size / line_size / 64;

	constexpr size_t block_collect_factor = 128;
	constexpr size_t block_compact_ratio = 20;
}

#endif

