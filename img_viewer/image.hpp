#pragma once

#include <memory>
using std::unique_ptr;
using std::make_unique;

#include <string>
typedef std::string str;
typedef std::string const& strcr;

#include "stbi.hpp"

#include "logging.hpp"
#include "vector_util.hpp"
#include "colors.hpp"

class Expt_File_Load_Fail : std::exception {
public:
	Expt_File_Load_Fail (strcr filepath) {
		msg = prints("File \"%s\" could not be loaded!", filepath.c_str());
	}

	virtual cstr what () const {
		return msg.c_str(); // returned string will go out of scope when the Expt_File_Load_Fail object goes out of scope
	}

private:
	str		msg;
};

class Image2D {
	friend void swap (Image2D& l, Image2D& r);
public:
	iv2		size = 0;
	rgba8*	pixels = nullptr;

	Image2D () {}
	Image2D& operator= (Image2D&& r) {
		swap(*this, r);
		return *this;
	}
	Image2D (Image2D&& r) {
		swap(*this, r);
	}

	~Image2D () {
		free(pixels);
	}
	
	int get_pixel_size () {
		return sizeof(*pixels);
	}
	uptr get_row_size () {
		return (uptr)size.x * (uptr)get_pixel_size();
	}
	uptr calc_size () {
		return (uptr)size.y * get_row_size();
	}

	static Image2D allocate (iv2 size) {
		Image2D img;

		img.size = size;
		img.pixels = (rgba8*)malloc( img.calc_size() );

		return img;
	}
	static Image2D copy_from (rgba8* data, iv2 size) {
		auto img = allocate(size);
		memcpy(img.pixels, data, img.calc_size());
		return img;
	}

	static Image2D load_from_file (strcr filepath) {
		Image2D img;
		
		stbi_set_flip_vertically_on_load(true); // OpenGL has textues bottom-up

		int n;
		img.pixels = (rgba8*)stbi_load(filepath.c_str(), &img.size.x,&img.size.y, &n, 4);
		if (!img.pixels) throw Expt_File_Load_Fail(filepath);

		return img;
	}

	void flip_vertical () {
		auto* rows = (u8*)pixels;
		auto row_size = get_row_size();

		for (int row=0; row<size.y/2; ++row) {
			auto* row_a = pixels +size.x * row;
			auto* row_b = pixels +size.x * (size.y -1 -row);
			for (int i=0; i<size.x; ++i) {
				std::swap(row_a[i], row_b[i]);
			}
		}
	}
};
void swap (Image2D& l, Image2D& r) {
	std::swap(l.size,	r.size);
	std::swap(l.pixels,	r.pixels);
}
