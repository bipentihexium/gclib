#include <gclib/gc.hpp>
#include <gclib/params.hpp>

namespace gclib {
	static_assert(big_object_treshold <= block_size);
}

