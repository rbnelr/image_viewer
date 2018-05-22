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
		return msg.c_str(); // returned string will go out of scope when this Exception goes out of scope
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
	
	int get_pixel_size () const {
		return sizeof(*pixels);
	}
	uptr get_row_size () const {
		return (uptr)size.x * (uptr)get_pixel_size();
	}
	uptr calc_size () const {
		return (uptr)size.y * get_row_size();
	}

	rgba8& get_pixel (int x, int y) {					return pixels[y * size.x +x]; }
	rgba8 const& get_pixel (int x, int y) const {		return pixels[y * size.x +x]; }

	rgba8& get_pixel (iv2 p) {							return get_pixel(p.x,p.y); }
	rgba8 const& get_pixel (iv2 p) const {				return get_pixel(p.x,p.y); }

	rgba8& get_nearest_pixel_uv (v2 uv) {				return get_pixel((iv2)(uv * (v2)size)); }
	rgba8 const& get_nearest_pixel_uv (v2 uv) const {	return get_pixel((iv2)(uv * (v2)size)); }

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

	static Image2D rescale_nearest (Image2D const& src, iv2 new_size) {
		
		auto dst = Image2D::allocate(new_size);

		for (int y=0; y<new_size.y; ++y) {
			for (int x=0; x<new_size.x; ++x) {
				dst.get_pixel(x,y) = src.get_nearest_pixel_uv( ((v2)iv2(x,y) +0.5f) / (v2)new_size );
			}
		}

		return std::move(dst);
	}
	static Image2D rescale_box_filter (Image2D const& src, iv2 new_size) {
		
		auto dst = Image2D::allocate(new_size);
		
		v2 inv_scale_factor = (v2)src.size / (v2)new_size;

		for (int y=0; y<new_size.y; ++y) {
			for (int x=0; x<new_size.x; ++x) {
				// box low and high coords in old pixel coords
				v2 box_l = (v2)(iv2(x,y) +0) * inv_scale_factor;
				v2 box_h = (v2)(iv2(x,y) +1) * inv_scale_factor;
				// src pixels to consider
				iv2 box_l_px = (iv2)floor(box_l);
				iv2 box_h_px = min( (iv2)ceil(box_h), src.size ); // float imprecision, prevent invalid src pixel access
				//// how much the low and high pixel rows and columns count (box_l.x == 1.5 -> left column of pixel box considered only counts 0.5)
				//v2 box_l_factor = 1 -(box_l -(flt)box_l_px.x);
				//v2 box_h_factor = 1 -((flt)box_h_px.x -box_h);

				v4 accum = 0;

				for (int src_y=box_l_px.y; src_y<box_h_px.y; ++src_y) {
					for (int src_x=box_l_px.x; src_x<box_h_px.x; ++src_x) {
						v4 val = (v4)src.get_pixel(iv2(src_x,src_y)) / 255;
						
						val = v4(to_linear(val.xyz()), val.w);

						//if (src_x == box_l_px.x)
						//	val *= box_l_factor.x;
						//if (src_x == (box_h_px.x -1))
						//	val *= box_h_factor.x;

						accum += val;
					}
				}

				//accum /= inv_scale_factor.x * inv_scale_factor.y;
				accum /= (flt)(box_h_px -box_l_px).x * (flt)(box_h_px -box_l_px).y;

				accum = clamp(accum, 0, 1);

				accum = v4(to_srgb(accum.xyz()), accum.w);

				dst.get_pixel(x,y) = (rgba8)(accum * 255 +0.5f);
			}
		}
	
		return std::move(dst);
	}
};
void swap (Image2D& l, Image2D& r) {
	std::swap(l.size,	r.size);
	std::swap(l.pixels,	r.pixels);
}
