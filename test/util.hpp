#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>
#include <algorithm>
#include <random>

template<typename T, typename Dist>
struct ArrayGenerator : Catch::Generators::IGenerator<T> {
	T arr;
	size_t repeats;
	Dist dist;
    std::default_random_engine rng;
    inline explicit ArrayGenerator(size_t _repeats, Dist &&_dist) : repeats(_repeats), dist(_dist) { gen(); }
	inline void gen() { std::generate(std::begin(arr), std::end(arr), [&]() { return dist(rng); }); if (repeats) repeats--; }
	inline bool next() override { gen(); return repeats; }
	inline const T &get() const override { return arr; }
};
template<typename T, size_t N, typename Dist>
inline Catch::Generators::GeneratorWrapper<std::array<T, N>> randomArray(size_t repeats, Dist &&dist) {
    return Catch::Generators::GeneratorWrapper<std::array<T, N>>(
		Catch::Detail::make_unique<ArrayGenerator<std::array<T, N>, std::decay_t<Dist>>>(repeats,
			std::forward<Dist>(dist)));
}

