#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>
#include <gclib/gc.hpp>
#include <gclib/util.hpp>

// Catch2 gives me linker errors that this is missing and I can't find anything about it...
// TODO: figure this thing out I guess
Catch::ITransientExpression::~ITransientExpression() { }

namespace example {
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
	static size_t bytes_of(void *obj) {
		switch (*(tag *)obj) {
		case tag_int: return sizeof(gcint);
		case tag_ivec_data: return gclib::bytes_vec_data<int>(obj);
		case tag_ivec: return sizeof(gcivec);
		}
		return sizeof(tag);
	}
	static std::optional<void **> ref_begin(void *obj) {
		switch (*(tag *)obj) {
		case tag_ivec: return ((gcivec *)obj)->data.data_ref();
		case tag_int:
		case tag_ivec_data:
			return std::nullopt;
		}
		return std::nullopt;
	}
	static std::optional<void **> ref_next(void *obj, void **prev) {
		(void)obj; (void)prev;
		return std::nullopt;
	}
}

TEST_CASE("example") {
	gclib::void_gc gc(example::bytes_of, example::ref_begin, example::ref_next);
	gclib::void_gc_uroot<example::gcivec> vector = gc.make_unique<example::gcivec>(&gc);
	for (int i = 0; i < 5; i++) {
		vector->data.push_back(i);
	}
	gclib::void_gc_uroot<example::gcint> factorial = gc.make_unique<example::gcint>(1);
	for (int i : vector->data) {
		factorial->data *= i;
	}
	vector = nullptr;
	std::vector<gclib::void_gc_uroot<example::tag>> object_list;
	object_list.push_back(gc.make_unique_as<example::gcint, example::tag>(42));
	object_list.push_back(gc.make_unique_as<example::gcivec, example::tag>(&gc));
	object_list[1].as<example::gcivec>()->data.push_back(factorial->data);
	object_list[1].as<example::gcivec>()->data.push_back(object_list[0].as<example::gcint>()->data);
	object_list.erase(object_list.begin());
	gc.collect(); // you can also trigger the collection manually
}

