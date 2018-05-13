#pragma once

#include <memory>
using std::unique_ptr;
using std::make_unique;

#include <string>
typedef std::string str;
typedef std::string const& strcr;

#include "glad.h"
#include "glfw3.h"

#include "file_io.hpp"
#include "logging.hpp"

struct Shader {
	str		name = "<unnamed Texture2D>";

	str								vert_filename = "";
	str								frag_filename = "";

	GLuint							prog = 0;

	Shader (str n) {
		name = n;
		printf("Shader(): %s\n", name.c_str());
	}
	Shader () {
		printf("Shader(): %s\n", name.c_str());
	}

	~Shader () {
		if (prog != 0) glDeleteProgram(prog);

		printf("~Shader(): %s\n", name.c_str());
	}

	static bool get_shader_compile_log (GLuint shad, str* log) {
		GLsizei log_len;
		{
			GLint temp = 0;
			glGetShaderiv(shad, GL_INFO_LOG_LENGTH, &temp);
			log_len = (GLsizei)temp;
		}

		if (log_len <= 1) {
			return false; // no log available
		} else {
			// GL_INFO_LOG_LENGTH includes the null terminator, but it is not allowed to write the null terminator in str, so we have to allocate one additional char and then resize it away at the end

			log->resize(log_len);

			GLsizei written_len = 0;
			glGetShaderInfoLog(shad, log_len, &written_len, &(*log)[0]);
			assert_log(written_len == (log_len -1));

			log->resize(written_len);

			return true;
		}
	}
	static bool get_program_link_log (GLuint prog, str* log) {
		GLsizei log_len;
		{
			GLint temp = 0;
			glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &temp);
			log_len = (GLsizei)temp;
		}

		if (log_len <= 1) {
			return false; // no log available
		} else {
			// GL_INFO_LOG_LENGTH includes the null terminator, but it is not allowed to write the null terminator in str, so we have to allocate one additional char and then resize it away at the end

			log->resize(log_len);

			GLsizei written_len = 0;
			glGetProgramInfoLog(prog, log_len, &written_len, &(*log)[0]);
			assert_log(written_len == (log_len -1));

			log->resize(written_len);

			return true;
		}
	}

	static bool load_shader (GLenum type, strcr filename, GLuint* shad) {
		*shad = glCreateShader(type);

		str source;
		if (!load_text_file(filename, &source)) {
			warning_log("Could not load shader source");
			return false;
		}

		{
			cstr ptr = source.c_str();
			glShaderSource(*shad, 1, &ptr, NULL);
		}

		glCompileShader(*shad);

		bool success;
		{
			GLint status;
			glGetShaderiv(*shad, GL_COMPILE_STATUS, &status);

			str log_str;
			bool log_avail = get_shader_compile_log(*shad, &log_str);

			success = status == GL_TRUE;
			if (!success) {
				// compilation failed
				assert_log(false, "OpenGL error in shader compilation \"%s\"!\n>>>\n%s\n<<<\n", filename.c_str(), log_avail ? log_str.c_str() : "<no log available>");
			} else {
				// compilation success
				if (log_avail) {
					assert_log(false, "OpenGL shader compilation log \"%s\":\n>>>\n%s\n<<<\n", filename.c_str(), log_str.c_str());
				}
			}
		}

		return success;
	}
	bool load_program () {
		if (prog != 0) glDeleteProgram(prog);

		prog = glCreateProgram();

		GLuint vert;
		GLuint frag;

		bool compile_success = true;

		bool vert_success = load_shader(GL_VERTEX_SHADER,		vert_filename, &vert);
		bool frag_success = load_shader(GL_FRAGMENT_SHADER,		frag_filename, &frag);

		if (!(vert_success && frag_success)) {
			glDeleteProgram(prog);
			prog = 0;
			return false;
		}

		glAttachShader(prog, vert);
		glAttachShader(prog, frag);

		glLinkProgram(prog);

		bool success;
		{
			GLint status;
			glGetProgramiv(prog, GL_LINK_STATUS, &status);

			str log_str;
			bool log_avail = get_program_link_log(prog, &log_str);

			success = status == GL_TRUE;
			if (!success) {
				// linking failed
				assert_log(false, "OpenGL error in shader linkage \"%s\"|\"%s\"!\n>>>\n%s\n<<<\n", vert_filename.c_str(), frag_filename.c_str(), log_avail ? log_str.c_str() : "<no log available>");
			} else {
				// linking success
				if (log_avail) {
					assert_log(false, "OpenGL shader linkage log \"%s\"|\"%s\":\n>>>\n%s\n<<<\n", vert_filename.c_str(), frag_filename.c_str(), log_str.c_str());
				}
			}
		}

		glDetachShader(prog, vert);
		glDetachShader(prog, frag);

		glDeleteShader(vert);
		glDeleteShader(frag);

		return success;
	}

};
