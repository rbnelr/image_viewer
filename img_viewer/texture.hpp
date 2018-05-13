#pragma once

#include <memory>
using std::unique_ptr;
using std::make_unique;

#include <string>
typedef std::string str;
typedef std::string const& strcr;

#include "glad.h"
#include "glfw3.h"

#include "logging.hpp"
#include "vector_util.hpp"

#include "colors.hpp"

class Texture2D {
	friend void bind_texture (int tet_unit, Texture2D const& tex);
	friend void swap (Texture2D& l, Texture2D& r);
	
	GLuint	gpu_handle = 0;
	iv2		size_px = 0;


public:

	GLuint	get_gpu_handle () const {	return gpu_handle; }
	iv2		get_size_px () const {		return size_px; }

	Texture2D () {}

	Texture2D& operator= (Texture2D&& r) {
		swap(*this, r);
		return *this;
	}
	Texture2D (Texture2D&& r) {
		swap(*this, r);
	}

	//
	static Texture2D generate () {
		Texture2D tex;
		glGenTextures(1, &tex.gpu_handle);
		return std::move(tex);
	}
	~Texture2D () {
		glDeleteTextures(1, &gpu_handle);
	}

	void upload (rgba8* pixels, iv2 size_px) {
		this->size_px = size_px;
		
		glBindTexture(GL_TEXTURE_2D, gpu_handle);
		
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size_px.x,size_px.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

		// no mips
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		// nearest filtering
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

		glBindTexture(GL_TEXTURE_2D, 0);
	}
	void gen_mipmaps () {
		glBindTexture(GL_TEXTURE_2D, gpu_handle);
		glGenerateMipmap(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	void set_filtering_nearest () {
		glBindTexture(GL_TEXTURE_2D, gpu_handle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	void set_filtering_mipmapped () {
		glBindTexture(GL_TEXTURE_2D, gpu_handle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	void set_border_clamp () {
		glBindTexture(GL_TEXTURE_2D, gpu_handle);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}

};
void swap (Texture2D& l, Texture2D& r) {
	std::swap(l.gpu_handle, r.gpu_handle);
	std::swap(l.size_px, r.size_px);
}

void bind_texture (int tex_unit, Texture2D const& tex) {
	glActiveTexture(GL_TEXTURE0 +tex_unit);
	glBindTexture(GL_TEXTURE_2D, tex.gpu_handle);
}
