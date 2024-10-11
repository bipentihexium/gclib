#ifndef GCLIB_BLOCK_HPP_
#define GCLIB_BLOCK_HPP_
#include <cstddef>
#include <cstdint>
#include "params.hpp"

namespace gclib {
	constexpr size_t max_align = alignof(max_align_t);
	inline constexpr size_t bytes_to_maxalings(size_t bytes) {
		return (bytes + max_align-1) / max_align;
	}
	struct block {
		uint64_t free[line_groups];
		uint64_t next_free;
		uint64_t flag;
		//uint64_t used_space;
		void clear();
		void prepare();
		bool is_full() const;
		void next_range(void **begin, void **end);
		void add_object(void *at, size_t bytes);
		size_t count_holes() const;
	};
	block *alloc_block();
	void free_block(block *b);
	block *obj_block(void *obj);
}

#endif

