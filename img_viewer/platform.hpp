#pragma once

#include "glad_helper.hpp"

#include "windows.h"
#include "Shlobj.h"

#include "glfw3.h"

#define GLFW_EXPOSE_NATIVE_WIN32 1
#include "glfw3native.h"

#include "basic_typedefs.hpp"
#include "math.hpp"
#include "vector_util.hpp"

void glfw_error_proc (int err, cstr msg) {
	warning_log("GLFW Error! 0x%x '%s'\n", err, msg);
}

void APIENTRY ogl_debuproc (GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, cstr message, void const* userParam) {

	//if (source == GL_DEBUG_SOURCE_SHADER_COMPILER_ARB) return;

	// hiding irrelevant infos/warnings
	switch (id) {
		case 131185: // Buffer detailed info (where the memory lives which is supposed to depend on the usage hint)
		//case 1282: // using shader that was not compiled successfully
		//
		//case 2: // API_ID_RECOMPILE_FRAGMENT_SHADER performance warning has been generated. Fragment shader recompiled due to state change.
		//case 131218: // Program/shader state performance warning: Fragment shader in program 3 is being recompiled based on GL state.
		//
		//			 //case 131154: // Pixel transfer sync with rendering warning
		//
		//			 //case 1282: // Wierd error on notebook when trying to do texture streaming
		//			 //case 131222: // warning with unused shadow samplers ? (Program undefined behavior warning: Sampler object 0 is bound to non-depth texture 0, yet it is used with a program that uses a shadow sampler . This is undefined behavior.), This might just be unused shadow samplers, which should not be a problem
		//			 //case 131218: // performance warning, because of shader recompiling based on some 'key'
			return;
	}

	cstr src_str = "<unknown>";
	switch (source) {
		case GL_DEBUG_SOURCE_API_ARB:				src_str = "GL_DEBUG_SOURCE_API_ARB";				break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB:		src_str = "GL_DEBUG_SOURCE_WINDOW_SYSTEM_ARB";		break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER_ARB:	src_str = "GL_DEBUG_SOURCE_SHADER_COMPILER_ARB";	break;
		case GL_DEBUG_SOURCE_THIRD_PARTY_ARB:		src_str = "GL_DEBUG_SOURCE_THIRD_PARTY_ARB";		break;
		case GL_DEBUG_SOURCE_APPLICATION_ARB:		src_str = "GL_DEBUG_SOURCE_APPLICATION_ARB";		break;
		case GL_DEBUG_SOURCE_OTHER_ARB:				src_str = "GL_DEBUG_SOURCE_OTHER_ARB";				break;
	}

	cstr type_str = "<unknown>";
	switch (source) {
		case GL_DEBUG_TYPE_ERROR_ARB:				type_str = "GL_DEBUG_TYPE_ERROR_ARB";				break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB:	type_str = "GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR_ARB";	break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB:	type_str = "GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR_ARB";	break;
		case GL_DEBUG_TYPE_PORTABILITY_ARB:			type_str = "GL_DEBUG_TYPE_PORTABILITY_ARB";			break;
		case GL_DEBUG_TYPE_PERFORMANCE_ARB:			type_str = "GL_DEBUG_TYPE_PERFORMANCE_ARB";			break;
		case GL_DEBUG_TYPE_OTHER_ARB:				type_str = "GL_DEBUG_TYPE_OTHER_ARB";				break;
	}

	cstr severity_str = "<unknown>";
	switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH_ARB:			severity_str = "GL_DEBUG_SEVERITY_HIGH_ARB";		break;
		case GL_DEBUG_SEVERITY_MEDIUM_ARB:			severity_str = "GL_DEBUG_SEVERITY_MEDIUM_ARB";		break;
		case GL_DEBUG_SEVERITY_LOW_ARB:				severity_str = "GL_DEBUG_SEVERITY_LOW_ARB";			break;
	}

	warning_log("OpenGL debug proc: severity: %s src: %s type: %s id: %d  %s\n",
			severity_str, src_str, type_str, id, message);
}

bool real_lmb_down = false;
bool real_rmb_down = false;

int mouse_wheel_diff = 0;

void glfw_mouse_button_event (GLFWwindow* window, int button, int action, int mods) {
	bool went_down = action == GLFW_PRESS;

	switch (button) {
		case GLFW_MOUSE_BUTTON_LEFT:
			real_lmb_down = went_down;
			break;
		case GLFW_MOUSE_BUTTON_RIGHT:
			real_rmb_down = went_down;
			break;
	}
}
void glfw_mouse_scroll (GLFWwindow* window, double xoffset, double yoffset) {
	mouse_wheel_diff += (int)yoffset;
}
void glfw_key_event (GLFWwindow* window, int key, int scancode, int action, int mods);
void glfw_char_event (GLFWwindow* window, unsigned int codepoint, int mods);

struct Display {
	GLFWwindow*		window = nullptr;
	GLFWmonitor*	fullscreen_monitor = nullptr;

	bool			is_fullscreen;

	struct Window_Placement {
		iv2			size_px = iv2(1280, 720);
		iv2			pos_px = -1;

		void get_current (GLFWwindow* window) {
			glfwGetWindowSize(window, &size_px.x,&size_px.y);
			glfwGetWindowPos(window, &pos_px.x,&pos_px.y);
		}
	} windowed_placement; // just to be able to restore the window placement later when switching to fullscreen, does not actually store the current placement while in windowed mode

	iv2				framebuffer_size_px = 0;

	void toggle_fullscreen () {
		if (!is_fullscreen) {
			
			windowed_placement.get_current(window);

			GLFWvidmode const* mode = glfwGetVideoMode(fullscreen_monitor);
			glfwSetWindowMonitor(window, fullscreen_monitor, 0, 0, mode->width, mode->height, mode->refreshRate);

		} else {
			glfwSetWindowMonitor(window, NULL, windowed_placement.pos_px.x,windowed_placement.pos_px.y, windowed_placement.size_px.x,windowed_placement.size_px.y, 0);

		}

		is_fullscreen = !is_fullscreen;
	}

	void save_window_positioning () {
		
		if (is_fullscreen) {
			// windowed_placement currently stores the windowed_placement before switching to fullscreen, this is the one we want to restore on next app launch, since we always start in windowed mode
		} else {
			windowed_placement.get_current(window);
		}

		if (!write_fixed_size_binary_file("saves/window_placement.bin", &windowed_placement, sizeof(windowed_placement))) {
			fprintf(stderr, "Could not save window_placement to saves/window_placement.bin, window position and size won't be restored on the next launch of this app.");
		}
	}
	bool load_window_positioning () {
		return load_fixed_size_binary_file("saves/window_placement.bin", &windowed_placement, sizeof(windowed_placement));
	}
} disp;


void init_engine () {
	glfwSetErrorCallback(glfw_error_proc);

	assert_log(glfwInit() != 0, "glfwInit() failed");

	{
		int count;
		GLFWmonitor** monitors = glfwGetMonitors(&count);

		if (monitors)
			disp.fullscreen_monitor = monitors[0];
	}

	disp.load_window_positioning();
	
	disp.is_fullscreen = false; // always start in windowed mode

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	bool GL_VAOS_REQUIRED = true;

	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, 1);

	disp.window = glfwCreateWindow(disp.windowed_placement.size_px.x,disp.windowed_placement.size_px.y, u8"2D Game", NULL, NULL);

	if (all(disp.windowed_placement.pos_px > -1)) {
		glfwSetWindowPos(disp.window, disp.windowed_placement.pos_px.x,disp.windowed_placement.pos_px.y);
	}

	glfwSetKeyCallback(disp.window,			glfw_key_event);
	glfwSetCharModsCallback(disp.window,		glfw_char_event);
	glfwSetMouseButtonCallback(disp.window,	glfw_mouse_button_event);
	glfwSetScrollCallback(disp.window,			glfw_mouse_scroll);

	glfwMakeContextCurrent(disp.window);
	gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);

	if (GLAD_GL_ARB_debug_output) {
		glDebugMessageCallbackARB(ogl_debuproc, 0);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB);

		// without GL_DEBUG_OUTPUT_SYNCHRONOUS_ARB ogl_debuproc needs to be thread safe
	}

	GLuint vao; // one global vao for everything

	if (GL_VAOS_REQUIRED) {
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
	}

}
