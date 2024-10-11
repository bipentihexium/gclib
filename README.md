# GC library

A simple C++ single-threaded garbage collector library based of the immix GC intended to be used with tagged union types. Though it somewhat works with polymorphic types as well.

## Usage

Headers are in `./include/`, source files are in `./src/`, and this project can be built with CMake:

```sh
git clone https://github.com/bipentihexium/gclib.git
cd gclib
cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release
# tests are off by default, you can enable them like this:
#cmake -S. -Bbuild -DCMAKE_BUILD_TYPE=Release -DGCLIB_BUILD_TESTS=ON
cmake --build build
```

and then you can find the library in `./build/`. You can run tests with

```sh
ctest --test-dir build
```

You can use the library like this:

```cpp
#include <gclib/gc.hpp>
#include <gclib/util.hpp>

enum tag : uint8_t {
	tag_int, tag_ivec_data, tag_ivec
};
struct gcint {
	tag t;
	int data;
	gcint(int d) : t(tag_int), data(d) { }
};
constexpr size_t ivec_tag = gclib::make_header(tag_ivec_data);
struct gcivec {
	tag t;
	gclib::vector<int, ivec_tag, gclib::void_gc> data;
	gcivec(gclib::void_gc *gc) : t(tag_ivec), data(gc) { }
};
size_t bytes_of(void *obj) {
	switch (*(tag *)obj) {
	case tag_int: return sizeof(gcint);
	case tag_ivec_data: return gclib::bytes_vec_data<int>(obj);
	case tag_ivec: return sizeof(gcivec);
	}
	return sizeof(tag);
}
std::optional<void **> ref_begin(void *obj) {
	switch (*(tag *)obj) {
	case tag_ivec: return ((gcivec *)obj)->data.data_ref();
	case tag_int:
	case tag_ivec_data:
		return std::nullopt;
	}
	return std::nullopt;
}
std::optional<void **> ref_next(void *obj, void **prev) {
	(void)obj; (void)prev;
	return std::nullopt;
}
void fun() {
	gclib::void_gc gc(bytes_of, ref_begin, ref_next);
	gclib::void_gc_uroot<gcivec> vector = gc.make_unique<gcivec>(&gc);
	for (int i = 0; i < 5; i++) {
		vector->data.push_back(i);
	}
	gclib::void_gc_uroot<gcint> factorial = gc.make_unique<gcint>(1);
	for (int i : vector->data) {
		factorial->data *= i;
	}
	vector = nullptr; // v will now get collected at some point in the future;
	std::vector<gclib::void_gc_uroot<tag>> object_list;
	object_list.push_back(gc.make_unique_as<gcint, tag>(42));
	object_list.push_back(gc.make_unique_as<gcivec, tag>(&gc));
	object_list[1].as<gcivec>()->data.push_back(factorial->data);
	object_list[1].as<gcivec>()->data.push_back(object_list[0].as<gcint>()->data);
	object_list.erase(object_list.begin());
	gc.collect(); // you can also trigger the collection manually
}
```

