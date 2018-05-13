#pragma once

#include <string>
typedef std::string str;
typedef std::string const& strcr;

#include "stdio.h"

static bool load_text_file (strcr filepath, str* text) {

	FILE* f = fopen(filepath.c_str(), "rb"); // we don't want "\r\n" to "\n" conversion, because it interferes with our file size calculation
	if (!f) return false;

	fseek(f, 0, SEEK_END);
	int filesize = ftell(f);
	fseek(f, 0, SEEK_SET);

	text->resize(filesize);

	size_t ret = fread(&(*text)[0], 1,text->size(), f);
	if (ret != (size_t)filesize) return false;

	return true;
}
