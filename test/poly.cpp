#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>
#include <numeric>
#include <vector>
#include <gclib/gc.hpp>
#include "util.hpp"

class object {
public:
	virtual ~object();
	template<typename T> T *as() { return reinterpret_cast<T *>(this); }
	virtual size_t bytes() = 0;
	virtual std::optional<object **> refs_begin() { return std::nullopt; }
	virtual std::optional<object **> refs_next(object **) { return std::nullopt; }
};
object::~object() { }

class int_object : public object {
public:
	int data;
	int_object(int v) : data(v) { }
	size_t bytes() override;
};
size_t int_object::bytes() { return sizeof(*this); }

template<typename T>
class linked_list_node : public object {
public:
	T data;
	::linked_list_node<T> *next;
	linked_list_node(T v, linked_list_node<T> *n=nullptr) : data(v), next(n) { }
	virtual std::optional<object **> refs_begin() override {
		return next == nullptr ? std::nullopt : std::optional<object **>((object **)&next);
	}
	size_t bytes() override { return sizeof(*this); }
	::linked_list_node<T> &operator[](size_t i) {
		if (i)
			return (*next)[i-1];
		return *this;
	}
	::linked_list_node<T> &at(size_t i) { return (*this)[i]; }
};
template<typename T>
class doubly_linked_list_node : public object {
public:
	T data;
	::doubly_linked_list_node<T> *prev;
	::doubly_linked_list_node<T> *next;
	doubly_linked_list_node(T v, doubly_linked_list_node<T> *p=nullptr, doubly_linked_list_node<T> *n=nullptr) :
		data(v), prev(p), next(n) { }
	virtual std::optional<object **> refs_begin() override {
		return next == nullptr ? std::nullopt : std::optional<object **>((object **)&next);
	}
	virtual std::optional<object **> refs_next(object **pi) override {
		return prev == nullptr || (void **)&prev == (void **)pi ? std::nullopt : std::optional<object **>((object **)&prev);
	}
	size_t bytes() override { return sizeof(*this); }
	::doubly_linked_list_node<T> &operator[](int64_t i) {
		if (i > 0)
			return (*next)[i-1];
		if (i < 0)
			return (*prev)[i+1];
		return *this;
	}
	::doubly_linked_list_node<T> &at(size_t i) { return (*this)[i]; }
};

using poly_gc_t = gclib::standard_gc<object **>;
template<typename T> using poly_gc_uroot = gclib::standard_gc_uroot<T, object **>;

TEST_CASE("gc polymorphic tests") {
	poly_gc_t gc(
		[](void *obj) { return static_cast<object *>(obj)->bytes(); },
		[](void *obj) { return static_cast<object *>(obj)->refs_begin(); },
		[](void *obj, object **it) { return static_cast<object *>(obj)->refs_next(it); }
	);
	std::vector<poly_gc_uroot<object>> objects;
	SECTION("push 10 ints") {
		for (int i = 0; i < 10; i++) {
			objects.push_back(gc.make_unique_as<int_object, object>(i));
		}
		gc.collect();
		REQUIRE(gc.live_object_count() == 10);
		objects.clear();
		gc.collect();
		REQUIRE(gc.live_object_count() == 0);
	}
	SECTION("push 80k ints and drop random") {
		for (int i = 0; i < 80'000; i++) {
			objects.push_back(gc.make_unique_as<int_object, object>(i));
		}
		gc.collect();
		REQUIRE(gc.live_object_count() == 80'000);
		auto is_kept = GENERATE(randomArray<uint8_t, 80'000>(2,
			std::uniform_int_distribution<uint8_t>(0, 1)));
		size_t stay_count = std::accumulate(is_kept.begin(), is_kept.end(), 0);
		std::vector<poly_gc_uroot<object>> old_objects;
		old_objects.swap(objects);
		for (auto &&obj : old_objects) {
			if (is_kept[obj->as<int_object>()->data]) {
				objects.push_back(std::move(obj));
			}
		}
		old_objects.clear();
		gc.collect();
		REQUIRE(gc.live_object_count() == stay_count);
		objects.clear();
		gc.collect();
		REQUIRE(gc.live_object_count() == 0);
	}
	SECTION("linked list") {
		poly_gc_uroot<linked_list_node<int>> head = gc.make_unique<linked_list_node<int>>(0, nullptr);
		for (int i = 1; i < 10; i++) {
			poly_gc_uroot<linked_list_node<int>> new_head = gc.make_unique<linked_list_node<int>>(i, nullptr);
			new_head->next = head.get();
			head = std::move(new_head);
		}
		gc.collect();
		REQUIRE(gc.live_object_count() == 10);
		head->at(4).next = &head->at(8);
		gc.collect();
		REQUIRE(gc.live_object_count() == 7);
		head = nullptr;
		gc.collect();
		REQUIRE(gc.live_object_count() == 0);
	}
	SECTION("doubly linked list") {
		poly_gc_uroot<doubly_linked_list_node<int>> root = gc.make_unique<doubly_linked_list_node<int>>(0);
		root->next = &*root;
		root->prev = &*root;
		auto random_stuff = GENERATE(randomArray<int32_t, 100>(3,
			std::uniform_int_distribution<int32_t>(-50, 50)));
		for (int i = 0; i < 100; i++) {
			poly_gc_uroot<doubly_linked_list_node<int>> node(&root->at(random_stuff[i]), &gc);
			auto res = gc.make_unique<doubly_linked_list_node<int>>(i, nullptr);
			res->next = node->next;
			res->prev = node.get();
			res->next->prev = res.get();
			res->prev->next = res.get();
		}
		gc.collect();
		REQUIRE(gc.live_object_count() == 101);
		root->next = &*root;
		root->prev = &*root;
		gc.collect();
		REQUIRE(gc.live_object_count() == 1);
		root = nullptr;
		gc.collect();
		REQUIRE(gc.live_object_count() == 0);
	}
}


