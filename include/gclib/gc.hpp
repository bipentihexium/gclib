#ifndef GCLIB_GC_HPP_
#define GCLIB_GC_HPP_
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <optional>
#include <stack>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "block.hpp"

namespace gclib {
	template<typename ObjSizeFun, typename PointerBeginFun, typename NextPointerFun> class gc;
	template<typename T, typename ObjSizeFun, typename PointerBeginFun, typename NextPointerFun>
	class unique_root {
	public:
		using element_type = T;
		using gc_type = gc<ObjSizeFun, PointerBeginFun, NextPointerFun>;
		using self_type = unique_root<T, ObjSizeFun, PointerBeginFun, NextPointerFun>;
		unique_root(const self_type &) = delete;
		constexpr inline unique_root(void *_data, gc_type *g) noexcept : data(reinterpret_cast<T *>(_data)), gc(g) {
			gc->add_root(reinterpret_cast<void **>(&data));
		}
		constexpr inline unique_root(self_type &&o) noexcept : data(o.data), gc(o.gc) {
			gc->move_root(reinterpret_cast<void **>(&o), reinterpret_cast<void **>(&data));
			o.data = nullptr;
		}
		constexpr inline self_type &operator=(self_type &&o) noexcept {
			if (data) gc->remove_root(reinterpret_cast<void **>(&data));
			gc = o.gc;
			data = o.data;
			o.data = nullptr;
			gc->move_root(reinterpret_cast<void **>(&o), reinterpret_cast<void **>(&data));
			return *this;
		}
		constexpr inline self_type &operator=(std::nullptr_t _) noexcept {
			if (data) gc->remove_root(reinterpret_cast<void **>(&data));
			data = nullptr;
			return *this;
		}
		constexpr inline ~unique_root() noexcept { if (data) gc->remove_root(reinterpret_cast<void **>(&data)); }
		constexpr inline T *get() noexcept { return data; }
		constexpr inline T &operator*() noexcept { return *data; }
		constexpr inline T *operator->() noexcept { return data; }
		template<typename R> constexpr inline R *as() noexcept { return reinterpret_cast<R *>(data); }
	private:
		friend class gc<ObjSizeFun, PointerBeginFun, NextPointerFun>;
		T *data;
		gc_type *gc;
	};
	template<typename ObjSizeFun, typename PointerBeginFun, typename NextPointerFun>
	class gc {
	public:
		template<typename T>
		using unique_root_type = unique_root<T, ObjSizeFun, PointerBeginFun, NextPointerFun>;

		inline gc(ObjSizeFun obj_size_fun, PointerBeginFun pointer_begin_fun, NextPointerFun next_pointer_fun) :
			size_fun(obj_size_fun), begin_fun(pointer_begin_fun), next_fun(next_pointer_fun) {
			bump_end = bump = nullptr;
			collet_counter = block_collect_factor;
			object_count = 0;
		}
		gc(const gc &) = delete;
		inline ~gc() {
			for (block *b : blocks) {
				free_block(b);
			}
			for (void *b : big_objects) {
				std::free(b);
			}
		}
		inline void *alloc(size_t bytes) {
			if (!--collet_counter) {
				collet_counter = block_collect_factor * blocks.size();
				collect();
			}
			object_count++;
			bytes = bytes_to_maxalings(bytes)*max_align;
			static_assert(big_object_treshold <= block_size);
			if (bytes <= big_object_treshold) {
				[[likely]];
				void *res;
				while (true) {
					if (bump == nullptr) {
						add_block();
						return alloc_in_bump(bytes);
					}
					res = alloc_in_bump(bytes);
					if (res != nullptr)
						break;
					next_bump();
				}
				return res;
			} else {
				[[unlikely]];
				void *out = std::malloc(bytes);
				big_objects.push_back(out);
				return out;
			}
		}
		inline void collect() {
			for (block *b : blocks) {
				b->clear();
			}
			std::unordered_set<void *> alive;
			for (void **root : roots) {
				mark(*root, alive);
			}
			std::erase_if(big_objects, [&alive](void *big) {
				if (alive.contains(big)) {
					return false;
				}
				std::free(big);
				return true;
			});
			if (blocks.size() > block_compact_ratio) {
				std::vector<std::pair<size_t, size_t>> blocks_by_holes;
				size_t blocks_with_holes_count = 0;
				for (size_t i = 0; i < blocks.size(); i++) {
					blocks_by_holes.push_back({blocks[i]->count_holes(), i});
					blocks[i]->flag = 0xffffffffffffffffull;
					if (blocks_by_holes.back().first > 1)
						blocks_with_holes_count++;
				}
				std::ranges::sort(blocks_by_holes, std::greater<std::pair<size_t, size_t>>());
				const size_t compact_count = std::min(blocks_by_holes.size() / block_compact_ratio, blocks_with_holes_count);
				if (compact_count > 0) {
					std::vector<size_t> to_compact(compact_count);
					std::transform(blocks_by_holes.begin(), blocks_by_holes.begin() + to_compact.size(), to_compact.begin(), [](const auto &pair) { return pair.second; });
					std::ranges::sort(to_compact, std::greater<size_t>());
					std::vector<std::vector<void *>> to_compact_objs;
					std::unordered_map<void *, std::vector<void **>> compacted_obj_outside_refs;
					size_t j = 0;
					for (size_t i : to_compact) {
						blocks[i]->flag = j++;
						to_compact_objs.emplace_back();
					}
					for (void *o : alive) {
						block *b = obj_block(o);
						if (b->flag != 0xffffffffffffffffull) {
							to_compact_objs[b->flag].push_back(o);
						} else {
							for (auto it = begin_fun(o); it; it = next_fun(o, *it)) {
								if (obj_block(**it)->flag != 0xffffffffffffffffull) {
									compacted_obj_outside_refs[**it].push_back((void **)*it);
								}
							}
						}
					}
					for (void **root : roots) {
						if (obj_block(*root)->flag != 0xffffffffffffffffull) {
							compacted_obj_outside_refs[*root].push_back(root);
						}
					}
					std::unordered_map<void *, void *> transfer_map;
					std::vector<block *> new_blocks;
					void *c_bump = nullptr, *c_bump_end = nullptr;
					new_blocks.push_back(alloc_block());
					new_blocks.back()->next_range(&c_bump, &c_bump_end);
					for (size_t i : to_compact) {
						block *b = blocks[i];
						for (void *o : to_compact_objs[b->flag]) {
							size_t sz = bytes_to_maxalings(size_fun(o))*max_align;
							if (static_cast<size_t>((uint8_t *)c_bump_end - (uint8_t *)c_bump) < sz) {
								new_blocks.push_back(alloc_block());
								new_blocks.back()->next_range(&c_bump, &c_bump_end);
							}
							transfer_map[o] = c_bump;
							std::memcpy(c_bump, o, sz);
							for (void **ref : compacted_obj_outside_refs[o]) {
								*ref = c_bump;
							}
							c_bump = (uint8_t *)c_bump + sz;
						}
					}
					for (size_t i : to_compact) {
						free_block(blocks[i]);
						blocks.erase(blocks.begin() + i);
					}
					for (const auto &[from, o] : transfer_map) {
						(void)from;
						for (auto it = begin_fun(o); it; it = next_fun(o, *it)) {
							auto tfi = transfer_map.find(**it);
							if (tfi != transfer_map.end()) {
								**it = reinterpret_cast<typename std::remove_reference<decltype(**it)>::type>(tfi->second);
							}
						}
					}
					blocks.insert(blocks.end(), new_blocks.begin(), new_blocks.end());
				}
			}
			for (block *b : blocks) {
				b->prepare();
				if (!b->is_full()) {
					free_blocks_list.push_back(b);
				}
			}
			bump_end = bump = nullptr;
			next_bump();
			object_count = alive.size();
		}
		inline void add_root(void **root) { roots.insert(root); }
		inline void remove_root(void **root) { roots.erase(root); }
		inline void move_root(void **from, void **to) { remove_root(from); add_root(to); }
		template<typename T> inline T *new_uninit() { return (T *)(alloc(sizeof(T))); }
		template<typename T, typename R> inline R *new_uninit_as() { return (R *)(alloc(sizeof(T))); }
		template<typename T, typename ...Ts> inline T *new_(Ts &&...args) {
			T *o = (T *)(alloc(sizeof(T)));
			new (o) T(std::forward<Ts>(args)...);
			return o;
		}
		template<typename T, typename R, typename ...Ts> inline R *new_as(Ts &&...args) {
			return (R *)new_<T>(std::forward<Ts>(args)...);
		}
		template<typename T> inline unique_root_type<T> make_unique_uninit() {
			return unique_root_type<T>(new_uninit<T>(), this);
		}
		template<typename T, typename ...Ts> inline unique_root_type<T> make_unique(Ts &&...args) {
			return unique_root_type<T>(new_<T>(std::forward<Ts>(args)...), this);
		}
		template<typename T, typename R> inline unique_root_type<R> make_unique_uninit_as() {
			return unique_root_type<R>(new_uninit_as<T, R>(), this);
		}
		template<typename T, typename R, typename ...Ts>
		inline unique_root_type<R> make_unique_as(Ts &&...args) {
			return unique_root_type<R>(new_as<T, R>(std::forward<Ts>(args)...), this);
		}
		inline uint64_t live_object_count() const { return object_count; }
		inline uint64_t block_count() const { return blocks.size(); }
		inline uint64_t big_object_count() const { return big_objects.size(); }
	private:
		std::vector<block *> blocks;
		std::vector<void *> big_objects;
		void *bump;
		void *bump_end;
		std::vector<block *> free_blocks_list;
		std::unordered_set<void **> roots;
		uint64_t object_count;
		size_t collet_counter;
		ObjSizeFun size_fun;
		PointerBeginFun begin_fun;
		NextPointerFun next_fun;

		inline void next_bump() {
			if (free_blocks_list.empty()) {
				bump_end = bump = nullptr;
				return;
			}
			free_blocks_list.back()->next_range(&bump, &bump_end);
			if (free_blocks_list.back()->is_full()) {
				free_blocks_list.pop_back();
			}
		}
		inline void *alloc_in_bump(size_t bytes) {
			const size_t bump_space = (uint8_t *)bump_end - (uint8_t *)bump;
			if (bytes < bump_space) {
				void *out = bump;
				bump = (uint8_t *)bump + bytes;
				return out;
			} else if (bump_space == bytes) {
				void *out = bump;
				next_bump();
				return out;
			}
			return nullptr;
		}
		inline void add_block() {
			blocks.push_back(alloc_block());
			blocks.back()->next_range(&bump, &bump_end);
		}
		inline void mark(void *obj, std::unordered_set<void *> &alive) {
			if (alive.contains(obj)) {
				return;
			}
			std::stack<void *> stack;
			stack.push(obj);
			while (!stack.empty()) {
				void *o = stack.top();
				stack.pop();
				if (alive.contains(o)) {
					continue;
				}
				alive.insert(o);
				size_t o_size = size_fun(o);
				if (o_size <= big_object_treshold) {
					block *b = obj_block(o);
					b->add_object(o, o_size);
				}
				for (auto it = begin_fun(o); it; it = next_fun(o, *it)) {
					stack.push(**it);
				}
			}
		}
	};
	template<typename IterType>
	using standard_gc = gc<std::function<size_t (void *)>,
		  std::function<std::optional<IterType> (void *)>,
		  std::function<std::optional<IterType> (void *, IterType)>>;
	template<typename T, typename IterType>
	using standard_gc_uroot = unique_root<T, std::function<size_t (void *)>,
		  std::function<std::optional<IterType> (void *)>,
		  std::function<std::optional<IterType> (void *, IterType)>>;
	using void_gc = standard_gc<void **>;
	template<typename T> using void_gc_uroot = standard_gc_uroot<T, void **>;
}

#endif

