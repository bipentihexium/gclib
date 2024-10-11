#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>
#include <numeric>
#include <vector>
#include <gclib/gc.hpp>
#include <gclib/util.hpp>
#include "util.hpp"


enum tag : uint8_t {
	tag_int, tag_vec_data, tag_vec_int, tag_link_ilist, tag_big_object, tag_big_link_list
};
struct gcint {
	tag t;
	int data;
	gcint(int d) : t(tag_int), data(d) { }
};
struct gcbig_object {
	tag t;
	uint8_t data[gclib::big_object_treshold];
	gcbig_object() : t(tag_big_object) { }
};
constexpr size_t vec_tag = static_cast<size_t>(tag_vec_data) << ((sizeof(size_t) - 1) * 8) | tag_vec_data;
struct gcivec {
	tag t;
	gclib::vector<int, vec_tag, gclib::void_gc> _gc;
	gcivec(gclib::void_gc *gc) : t(tag_vec_int), _gc(gc) { }
};
struct link_ilist {
	tag t;
	int data;
	link_ilist *next;
	link_ilist(int d, link_ilist *n=nullptr) : t(tag_link_ilist), data(d), next(n) { }
};
struct big_link_list {
	tag t;
	uint8_t data[gclib::big_object_treshold];
	big_link_list *next;
	big_link_list(big_link_list *n=nullptr) : t(tag_big_link_list), next(n) { }
};

static size_t bytes_of(void *obj) {
	switch (*(tag *)obj) {
	case tag_int: return sizeof(gcint);
	case tag_vec_data: return gclib::bytes_vec_data<int>(obj);
	case tag_vec_int: return sizeof(gcivec);
	case tag_link_ilist: return sizeof(link_ilist);
	case tag_big_object: return sizeof(gcbig_object);
	case tag_big_link_list: return sizeof(big_link_list);
	}
	return sizeof(tag);
}
static std::optional<void **> ref_begin(void *obj) {
	switch (*(tag *)obj) {
	case tag_vec_int: return ((gcivec *)obj)->_gc.data_ref();
	case tag_link_ilist:{
		auto n = &((link_ilist *)obj)->next;
		return *n ? std::optional<void **>((void **)n) : std::nullopt;
	}
	case tag_big_link_list:{
		auto n = &((big_link_list *)obj)->next;
		return *n ? std::optional<void **>((void **)n) : std::nullopt;
	}
	case tag_int:
	case tag_vec_data:
	case tag_big_object:
		return std::nullopt;
	}
}
static std::optional<void **> ref_next(void *obj, void **prev) {
	(void)obj; (void)prev;
	return std::nullopt;
}

TEST_CASE("gc tag union tests 80k ints") {
	gclib::void_gc gc(bytes_of, ref_begin, ref_next);
	std::vector<gclib::void_gc_uroot<tag>> objs;
	for (int i = 0; i < 80'000; i++) {
		objs.push_back(gc.make_unique_as<gcint, tag>(i));
	}
	gc.collect();
	REQUIRE(gc.live_object_count() == 80'000);
	auto is_kept = GENERATE(randomArray<uint8_t, 80'000>(2, std::uniform_int_distribution<uint8_t>(0, 1)));
	size_t stay_count = std::accumulate(is_kept.begin(), is_kept.end(), 0);
	std::vector<gclib::void_gc_uroot<tag>> old;
	old.swap(objs);
	for (auto &&o : old) {
		if (is_kept[o.as<gcint>()->data]) {
			objs.push_back(std::move(o));
		}
	}
	old.clear();
	gc.collect();
	REQUIRE(gc.live_object_count() == stay_count);
	objs.clear();
	gc.collect();
	REQUIRE(gc.live_object_count() == 0);
}
TEST_CASE("gc tag union tests 100 big objects") {
	gclib::void_gc gc(bytes_of, ref_begin, ref_next);
	std::vector<gclib::void_gc_uroot<tag>> objs;
	for (int i = 0; i < 100; i++) {
		objs.push_back(gc.make_unique_as<gcbig_object, tag>());
	}
	gc.collect();
	REQUIRE(gc.live_object_count() == 100);
	objs.clear();
	gc.collect();
	REQUIRE(gc.live_object_count() == 0);
}
TEST_CASE("gc tag union tests 80k int ivec") {
	gclib::void_gc gc(bytes_of, ref_begin, ref_next);
	gclib::void_gc_uroot<gcivec> v = gc.make_unique<gcivec>(&gc);
	for (int i = 0; i < 80'000; i++) {
		v->_gc.push_back(i);
	}
	gc.collect();
	REQUIRE(gc.live_object_count() == 2);
	v = nullptr;
	gc.collect();
	REQUIRE(gc.live_object_count() == 0);
}
TEST_CASE("gc tag union tests 20 big link nodes in 3 lists") {
	gclib::void_gc gc(bytes_of, ref_begin, ref_next);
	auto list_sel = GENERATE(randomArray<uint8_t, 20>(1, std::uniform_int_distribution<uint8_t>(0, 2)));
	gclib::void_gc_uroot<big_link_list> list0 = gc.make_unique<big_link_list>();
	gclib::void_gc_uroot<big_link_list> list1 = gc.make_unique<big_link_list>();
	gclib::void_gc_uroot<big_link_list> list2 = gc.make_unique<big_link_list>();
	size_t list2_sz = 1;
	for (int i = 3; i < 20; i++) {
		auto &list = list_sel[i] == 0 ? list0 : (list_sel[i] == 1 ? list1 : list2);
		gclib::void_gc_uroot<big_link_list> new_node = gc.make_unique<big_link_list>(nullptr);
		new_node->next = list.get();
		list = std::move(new_node);
		if (list_sel[i] == 2) {
			list2_sz++;
		}
	}
	gc.collect();
	REQUIRE(gc.live_object_count() == 20);
	list2 = nullptr;
	gc.collect();
	REQUIRE(gc.live_object_count() == 20-list2_sz);
	for (size_t i = 0; i < list2_sz; i++) {
		gclib::void_gc_uroot<big_link_list> new_node = gc.make_unique<big_link_list>(nullptr);
		new_node->next = list2.get();
		list2 = std::move(new_node);
	}
	gc.collect();
	REQUIRE(gc.live_object_count() == 20);
	list0 = nullptr;
	list1 = nullptr;
	gc.collect();
	REQUIRE(gc.live_object_count() == list2_sz);
}
TEST_CASE("gc tag union tests 500k link nodes in 3 lists") {
	gclib::void_gc gc(bytes_of, ref_begin, ref_next);
	auto list_sel = GENERATE(randomArray<uint8_t, 500'000>(1, std::uniform_int_distribution<uint8_t>(0, 2)));
	gclib::void_gc_uroot<link_ilist> list0 = gc.make_unique<link_ilist>(0);
	gclib::void_gc_uroot<link_ilist> list1 = gc.make_unique<link_ilist>(0);
	gclib::void_gc_uroot<link_ilist> list2 = gc.make_unique<link_ilist>(0);
	size_t list2_sz = 1;
	for (int i = 3; i < 500'000; i++) {
		auto &list = list_sel[i] == 0 ? list0 : (list_sel[i] == 1 ? list1 : list2);
		gclib::void_gc_uroot<link_ilist> new_node = gc.make_unique<link_ilist>(int(i), nullptr);
		new_node->next = list.get();
		list = std::move(new_node);
		if (list_sel[i] == 2) {
			list2_sz++;
		}
	}
	gc.collect();
	REQUIRE(gc.live_object_count() == 500'000);
	list2 = nullptr;
	gc.collect();
	REQUIRE(gc.live_object_count() == 500'000-list2_sz);
	for (size_t i = 0; i < list2_sz; i++) {
		gclib::void_gc_uroot<link_ilist> new_node = gc.make_unique<link_ilist>(int(i), nullptr);
		new_node->next = list2.get();
		list2 = std::move(new_node);
	}
	gc.collect();
	REQUIRE(gc.live_object_count() == 500'000);
	list0 = nullptr;
	list1 = nullptr;
	gc.collect();
	REQUIRE(gc.live_object_count() == list2_sz);
}

