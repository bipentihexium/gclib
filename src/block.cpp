#include <gclib/block.hpp>
#include <cstdlib>
#include <bit>
#include <gclib/params.hpp>

namespace gclib {
	constexpr size_t bytes_to_lines(size_t bytes) {
		return (bytes + line_size-1) / line_size;
	}
	constexpr size_t metadata_lines = bytes_to_lines(sizeof(block));
	static_assert(max_align <= line_size);

	void block::clear() {
		static_assert(metadata_lines < 64);
		next_free = 0;
		free[0] = 0xffffffffffffffffull << metadata_lines;
		#pragma unroll
		for (size_t i = 1; i < line_groups; i++) {
			free[i] = 0xffffffffffffffffull;
		}
		//used_space = 0;
	}
	void block::prepare() {
		next_free = 0;
		while (next_free < line_groups && !free[next_free]) { next_free++; }
	}
	bool block::is_full() const {
		return next_free == line_groups;
	}
	void block::next_range(void **begin, void **end) {
		const size_t offset = std::countr_zero(free[next_free]);
		*begin = reinterpret_cast<uint8_t *>(this) + (64*next_free + offset) * line_size;
		uint64_t free_end = free[next_free] + (1ull << offset);
		if (free_end == 0) { // overflow into next line group
			do {
				free[next_free++] = 0;
			} while (next_free < line_groups && free[next_free] == 0xffffffffffffffffull);
			if (next_free == line_groups) { // rest of the block is free
				*end = reinterpret_cast<uint8_t *>(this) + block_size;
				return;
			}
			free_end = free[next_free] + 1;
		}
		const size_t offset_end = std::countr_zero(free_end);
		*end = reinterpret_cast<uint8_t *>(this) + (64*next_free + offset_end) * line_size;
		free[next_free] = free_end & (free_end - 1);
		while (next_free < line_groups && !free[next_free]) { next_free++; }
	}
	void block::add_object(void *at, size_t bytes) {
		//used_space += bytes;
		const size_t first_line = (reinterpret_cast<uint8_t *>(at) - reinterpret_cast<uint8_t *>(this)) / line_size;
		const size_t last_line = (reinterpret_cast<uint8_t *>(at) + bytes - 1 - reinterpret_cast<uint8_t *>(this)) / line_size;
		const size_t first_line_group = first_line / 64;
		const size_t last_line_group = last_line / 64;
		const size_t first_line_group_part = first_line % 64;
		const size_t last_line_group_part = last_line % 64;
		for (size_t i = first_line_group + 1; i < last_line_group; i++) {
			free[i] = 0;
		}
		if (first_line_group == last_line_group) {
			uint64_t mask = first_line_group_part == 0 ? 0 :
				0xffffffffffffffffull >> (64 - first_line_group_part);
			mask |= last_line_group_part == 63 ? 0 :
				0xffffffffffffffffull << (last_line_group_part + 1);
			free[first_line_group] &= mask;
		} else {
			free[first_line_group] &= first_line_group_part == 0 ? 0 :
				0xffffffffffffffffull >> (64 - first_line_group_part);
			free[last_line_group] &= last_line_group_part == 63 ? 0 :
				0xffffffffffffffffull << (last_line_group_part + 1);
		}
	}
	size_t block::count_holes() const {
		size_t holes = 0;
		for (size_t i = 0; i < line_groups; i++) {
			holes += std::popcount(~free[i] & (free[i] >> 1));
			if (i && (free[i-1] >> 63) == 0 && (free[i] & 1) == 1) {
				holes++;
			}
		}
		return holes;
	}

	block *alloc_block() {
		block *out = (block *)std::aligned_alloc(block_size, block_size);
		out->clear();
		return out;
	}
	void free_block(block *b) {
		std::free(b);
	}
	block *obj_block(void *obj) {
		return reinterpret_cast<block *>(reinterpret_cast<std::uintptr_t>(obj) & ~(block_size - 1));
	}
}

