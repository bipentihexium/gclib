#ifndef GCLIB_UTIL_HPP_
#define GCLIB_UTIL_HPP_
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <bit>

namespace gclib {
	/**
	 * as any gc object, this T must not have a destructor
	 *
	 * For tagged union system, just reserve one tag for vector data and add it as a header field. Since
	 * the capacity is stored right after the header, size_t header never wastes space because of
	 * alignment requirements.
	 *
	 * For a dynamic dispatch system, if you're fine with assuming a non-zero vtable pointer as a
	 * first field in a struct, then set Header to uintptr_t(0) and check it in gc callbacks - then
	 * it can be used more or less comfortably in a dynamic dispatch system, but it still relies on
	 * UB - so only for using on some specific platform which you know for sure does that. (Or you
	 * can even add an assertion in base object constructor that
	 * reinterpret_cast<uintptr_t>(*this) != 0
	 * to rely only on the fact that a virtual class takes up at least the space of one pointer)
	 */
	template<typename T, size_t Header, typename GC>
	class vector {
		size_t _size;
		void *_data;
		GC *gc;
	public:
		struct data_header {
			size_t header_magic;
			size_t capacity;
		};

		inline vector(GC *_gc) : _size(0), _data(nullptr), gc(_gc) { }
		inline vector(const vector<T, Header, GC> &o) : _size(o._size), gc(o.gc) {
			_data = alloc(o._size);
			std::copy(o.begin(), o.end(), begin());
		}
		inline vector<T, Header, GC> &operator=(const vector<T, Header, GC> &o) {
			gc = o.gc;
			_size = o._size;
			_data = alloc(o._size);
			std::copy(o.begin(), o.end(), begin());
		}
		inline T *data() {
			return reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(_data) + sizeof(data_header));
		}
		inline T *begin() {
			return reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(_data) + sizeof(data_header));
		}
		inline T *end() { return begin() + _size; }
		inline const T *cbegin() const {
			return reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(_data) + sizeof(data_header));
		}
		inline const T *cend() const { return cbegin() + _size; }
		inline size_t size() const { return _size; }
		inline size_t capacity() const { return _data ? cheader()->capacity : 0; }
		inline const data_header *cheader() const { return reinterpret_cast<const data_header *>(_data); }
		inline void **data_ref() { return &_data; }
		inline void push_back(const T &x) {
			if (capacity() <= _size) {
				if (_data == nullptr) {
					alloc(8);
				} else {
					realloc(capacity() + capacity() / 2);
				}
			}
			new (&data()[_size++]) T(x);
		}
		inline void push_back(T &&x) {
			if (capacity() <= _size) {
				if (_data == nullptr) {
					alloc(8);
				} else {
					realloc(capacity() + capacity() / 2);
				}
			}
			new (&data()[_size++]) T(std::forward<T>(x));
		}
		inline void pop_back() { _size--; }
		inline void reserve(size_t _capacity) {
			if (_data == nullptr) {
				alloc(_capacity);
			} else if (_capacity >= _size && _capacity != capacity()) {
				realloc(_capacity);
			}
		}
		inline void shrink_to_fit() { reserve(_size); }
		inline void clear() { _size = 0; }
		inline T &operator[](size_t i) { return data()[i]; }
		inline T &at(size_t i) { assert(i < _size); return data()[i]; }
		inline bool empty() const { return _size == 0; }
		inline bool operator==(const vector<T, Header, GC> &o) const {
			if (_size != o._size)
				return false;
			return std::equal(begin(), end(), o.begin(), o.end());
		}
		inline bool operator!=(const vector<T, Header, GC> &o) const {
			if (_size != o._size)
				return true;
			return !std::equal(begin(), end(), o.begin(), o.end());
		}
	private:
		inline void alloc(size_t _capacity) {
			_data = gc->alloc(sizeof(T) * _capacity + sizeof(data_header));
			header()->header_magic = Header;
			header()->capacity = _capacity;
		}
		inline void realloc(size_t new_capacity) {
			void *new_data = gc->alloc(sizeof(T) * new_capacity + sizeof(data_header));
			std::move(begin(), end(), reinterpret_cast<T *>(reinterpret_cast<uint8_t *>(new_data) + sizeof(data_header)));
			_data = new_data;
			header()->header_magic = Header;
			header()->capacity = new_capacity;
		}
		inline data_header *header() { return reinterpret_cast<data_header *>(_data); }
	};
	template<typename T>
	inline size_t bytes_vec_data(void *data) { return ((size_t *)data)[1] * sizeof(T) + 2 * sizeof(size_t); }
	template<typename T>
	constexpr inline size_t make_header(T marker) {
		static_assert(sizeof(T) <= sizeof(size_t), "marker size cannot be larger than size of size_t");
		if constexpr (std::endian::native == std::endian::little) {
			return static_cast<size_t>(marker);
		} else {
			// std::byteswap is c++23 :(
			size_t mark = static_cast<size_t>(marker);
			size_t out = 0;
			for (size_t i = 0; i < sizeof(size_t); i++) {
				out |= ((mark >> ((sizeof(size_t) - i - 1) * 8)) & 0xff) << (i * 8);
			}
			return out;
		}
	}
}

#endif

