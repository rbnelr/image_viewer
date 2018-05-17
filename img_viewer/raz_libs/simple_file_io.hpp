#pragma once

#include <string>
using std::string;

#include "stdio.h"

#include "basic_typedefs.hpp"

bool load_text_file (string const& filepath, string* text) {

	FILE* f = fopen(filepath.c_str(), "rb"); // we don't want "\r\n" to "\n" conversion, because it interferes with our file size calculation
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	int filesize = ftell(f);
	fseek(f, 0, SEEK_SET);

	text->resize(filesize);

	uptr ret = fread(&(*text)[0], 1,text->size(), f);
	if (ret != (uptr)filesize) return false;

	return true;
}

bool load_fixed_size_binary_file (string const& filepath, void* data, uptr sz) {

	FILE* f = fopen(filepath.c_str(), "rb");
	if (!f)
		return false;

	fseek(f, 0, SEEK_END);
	int filesize = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (filesize != sz)
		return false;

	uptr ret = fread(data, 1,sz, f);
	if (ret != sz)
		return false;

	return true;
}

bool write_fixed_size_binary_file (string const& filepath, void const* data, uptr sz) {

	FILE* f = fopen(filepath.c_str(), "wb");
	if (!f)
		return false;

	uptr ret = fwrite(data, 1,sz, f);
	if (ret != sz)
		return false;

	return true;
}
