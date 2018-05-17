#pragma once

#include <string>
using std::string;

#include "basic_typedefs.hpp"
#include "simple_file_io.hpp"

bool write_blob_to_file (string const& name, void const* data, uptr sz) {
	return write_fixed_size_binary_file(name, data, sz);
}
bool load_blob_from_file (string const& name, void* data, uptr sz) {
	return load_fixed_size_binary_file(name, data, sz);
}
