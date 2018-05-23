#pragma once

#include <string>
#include <vector>
#include <unordered_map>

#include "glfw3.h"
#include "imgui.cpp"
#include "imgui_draw.hpp"
#include "imgui_demo.hpp"

#include "image.hpp"
#include "texture.hpp"
#include "shader.hpp"

#include "colors.hpp"

#include "defer.hpp"

// Show a slim button that saves a value to disk which is then automaticly loaded on the next start of the app
void imgui_saveable (cstr id, void* pval, uptr val_size) {
	
	ImGui::PushID(id);

	auto imgui_id = ImGui::GetID("##");

	#if 1
	auto* state = ImGui::GetStateStorage();
	bool first_call_with_this_id = state->GetBool(imgui_id, false) == false;
	if (first_call_with_this_id) {
		state->SetBool(imgui_id, true);
	}
	#else // c++ hashmap is constant time (imgui StateStorage does a linear search on GetBool and SetBool), but using imgui seems appropriate
	struct ImGuiID_Wrapper {
		ImGuiID	val;

		bool operator== (const ImGuiID_Wrapper& r) const { return val == r.val; }
	};
	struct Hasher {
		std::size_t operator()(const ImGuiID_Wrapper& id) const { return (std::size_t)id.val; } // ImGuiID is already a hash
	};

	static std::unordered_map<ImGuiID_Wrapper, bool, Hasher> called_before;

	auto it = called_before.find(ImGuiID_Wrapper{imgui_id});
	bool first_call_with_this_id = it == called_before.end();
	if (first_call_with_this_id) {
		called_before.emplace(ImGuiID_Wrapper{imgui_id}, true);
	}
	#endif

	if (first_call_with_this_id)
		load_fixed_size_binary_file(prints("saves/imgui/%s.bin", id), pval, val_size); // the path generated here does not respect previous ImGui::PushID() calls, so you need to make sure the id string itself is unique the save restore will break

	bool save = ImGui::Button("##");

	ImGui::PopID();

	if (save)
		write_fixed_size_binary_file(prints("saves/imgui/%s.bin", id), pval, val_size);

	ImGui::SameLine();
};
#define IMGUI_SAVEABLE(id, pval) imgui_saveable(id, pval, sizeof(*(pval)))

namespace ImGui {
	IMGUI_API void Value (const char* prefix, v2 v) {
		Text("%s: %.2f, %.2f", prefix, v.x,v.y);
	}
	IMGUI_API void Value (const char* prefix, iv2 v) {
		Text("%s: %d, %d", prefix, v.x,v.y);
	}
	IMGUI_API void Value (const char* prefix, u64 i) {
		Text("%s: %lld", prefix, i);
	}
	IMGUI_API void Value_Bytes (const char* prefix, u64 i) {
		cstr unit;
		f32 val = (f32)i;
		if (		i < ((u64)1024) ) {
			val /= (u64)1;
			unit = "B";
		} else if (	i < ((u64)1024*1024) ) {
			val /= (u64)1024;
			unit = "KB";
		} else if (	i < ((u64)1024*1024*1024) ) {
			val /= (u64)1024*1024;
			unit = "MB";
		} else if (	i < ((u64)1024*1024*1024*1024) ) {
			val /= (u64)1024*1024*1024;
			unit = "GB";
		} else if (	i < ((u64)1024*1024*1024*1024*1024) ) {
			val /= (u64)1024*1024*1024*1024;
			unit = "TB";
		} else {
			val /= (u64)1024*1024*1024*1024*1024;
			unit = "PB";
		}
		Text("%s: %.2f %s", prefix, val, unit);
	}

	IMGUI_API bool InputText_str (const char* label, std::string* s, ImGuiInputTextFlags flags = 0, ImGuiTextEditCallback callback = NULL, void* user_data = NULL) {
		int cur_length = (int)s->size();
		s->resize(1024);
		(*s)[ min(cur_length, (int)s->size()-1) ] = '\0'; // is this guaranteed to work?
		
		bool ret = InputText(label, &(*s)[0], (int)s->size(), flags, callback, user_data);

		s->resize( strlen(&(*s)[0]) );

		return ret;
	}

	IMGUI_API void TextBox (const char* label, std::string s) { // (pseudo) read-only text box, (can still be copied out of, which is nice, this also allows proper layouting)
		InputText_str(label, &s);
	}
}

struct Imgui_Context {
	
	struct Vertex {
		v2		pos_screen;
		v2		uv;
		rgba8	col;
	};

	void bind_vbos () {
		glBindBuffer(GL_ARRAY_BUFFER, vbo_vert);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo_indx);

		GLint loc_pos =		glGetAttribLocation(shad.prog, "attr_pos_screen");
		GLint loc_uv =		glGetAttribLocation(shad.prog, "attr_uv");
		GLint loc_col =		glGetAttribLocation(shad.prog, "attr_col");

		glEnableVertexAttribArray(loc_pos);
		glVertexAttribPointer(loc_pos, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos_screen));
		
		glEnableVertexAttribArray(loc_uv);
		glVertexAttribPointer(loc_uv, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
		
		glEnableVertexAttribArray(loc_col);
		glVertexAttribPointer(loc_col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, col));
	}

	Texture2D	tex;
	Shader		shad;

	GLint tex_unit = 0;

	GLuint vbo_vert;
	GLuint vbo_indx;

	std::vector<Vertex> vbo_data;

	void init () {
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		{
			io.KeyMap[ ImGuiKey_Tab			 ] = GLFW_KEY_TAB;
			io.KeyMap[ ImGuiKey_LeftArrow	 ] = GLFW_KEY_LEFT;
			io.KeyMap[ ImGuiKey_RightArrow	 ] = GLFW_KEY_RIGHT;
			io.KeyMap[ ImGuiKey_UpArrow		 ] = GLFW_KEY_UP;
			io.KeyMap[ ImGuiKey_DownArrow	 ] = GLFW_KEY_DOWN;
			io.KeyMap[ ImGuiKey_PageUp		 ] = GLFW_KEY_PAGE_UP;
			io.KeyMap[ ImGuiKey_PageDown	 ] = GLFW_KEY_PAGE_DOWN;
			io.KeyMap[ ImGuiKey_Home		 ] = GLFW_KEY_HOME;
			io.KeyMap[ ImGuiKey_End			 ] = GLFW_KEY_END;
			io.KeyMap[ ImGuiKey_Insert		 ] = GLFW_KEY_INSERT;
			io.KeyMap[ ImGuiKey_Delete		 ] = GLFW_KEY_DELETE;
			io.KeyMap[ ImGuiKey_Backspace	 ] = GLFW_KEY_BACKSPACE;
			io.KeyMap[ ImGuiKey_Space		 ] = GLFW_KEY_SPACE;
			io.KeyMap[ ImGuiKey_Enter		 ] = GLFW_KEY_ENTER;
			io.KeyMap[ ImGuiKey_Escape		 ] = GLFW_KEY_ESCAPE;
			io.KeyMap[ ImGuiKey_A			 ] = GLFW_KEY_A;
			io.KeyMap[ ImGuiKey_C			 ] = GLFW_KEY_C;
			io.KeyMap[ ImGuiKey_V			 ] = GLFW_KEY_V;
			io.KeyMap[ ImGuiKey_X			 ] = GLFW_KEY_X;
			io.KeyMap[ ImGuiKey_Y			 ] = GLFW_KEY_Y;
			io.KeyMap[ ImGuiKey_Z			 ] = GLFW_KEY_Z;
		}

		shad = Shader("shad_imgui");
		shad.vert_filename = "shaders/textured.vert";
		shad.frag_filename = "shaders/textured.frag";
		shad.load_program();

		glUseProgram(shad.prog); // bind shader for glUniforms

		GLint tex_loc = glGetUniformLocation(shad.prog, "tex");
		glUniform1i(tex_loc, tex_unit);

		glGenBuffers(1, &vbo_vert);
		glGenBuffers(1, &vbo_indx);

		{
			iv2	 size;
			u8*	 pixels; // Imgui mallocs and frees the pixels
			io.Fonts->GetTexDataAsRGBA32(&pixels, &size.x,&size.y);

			auto img = Image2D::copy_from((rgba8*)pixels, size);
			//img.flip_vertical();

			tex = Texture2D::generate();
			tex.upload(img.pixels, img.size);
			tex.set_filtering_nearest();

			io.Fonts->TexID = (void*)&tex;
		}
	}
	~Imgui_Context () {
		ImGui::DestroyContext();
	}

	void begin_frame (iv2 disp_dim, flt dt, iv2 mcursor_pos_px, bool lmb_down, bool rmb_down, int mouse_wheel_diff) {
		ImGuiIO& io = ImGui::GetIO();

		io.DisplaySize.x = (flt)disp_dim.x;
		io.DisplaySize.y = (flt)disp_dim.y;
		io.DeltaTime = dt;
		io.MousePos.x = (flt)mcursor_pos_px.x;
		io.MousePos.y = (flt)mcursor_pos_px.y;
		io.MouseDown[0] = lmb_down;
		io.MouseDown[1] = rmb_down;
		io.MouseWheel = (flt)mouse_wheel_diff;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ImGui::NewFrame();
	}

	void draw (iv2 disp_dim) {
		//ImGui::ShowDemoWindow();
		
		ImGui::Render();

		glViewport(0,0, disp_dim.x,disp_dim.y);
		
		//
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glDisable(GL_DEPTH_TEST);

		glDisable(GL_CULL_FACE);

		glEnable(GL_SCISSOR_TEST); // to render to sub area of window to enable having black bars around game screen

		//
		glUseProgram(shad.prog);

		GLint loc_screen_dim = glGetUniformLocation(shad.prog, "screen_dim");
		glUniform2f(loc_screen_dim, (flt)disp_dim.x,(flt)disp_dim.y);

		static GLint draw_wireframe_loc = glGetUniformLocation(shad.prog, "draw_wireframe");
		glUniform1i(draw_wireframe_loc, false);

		ImDrawData* draw_data = ImGui::GetDrawData();

		bind_vbos();

		for (int n = 0; n < draw_data->CmdListsCount; n++) {
			auto* cmd_list = draw_data->CmdLists[n];

			const ImDrawVert* vtx_buffer = cmd_list->VtxBuffer.Data;  // vertex buffer generated by ImGui
			const ImDrawIdx* idx_buffer = cmd_list->IdxBuffer.Data;   // index buffer generated by ImGui

			auto vertex_size = cmd_list->VtxBuffer.size() * sizeof(ImDrawVert);
			auto index_size = cmd_list->IdxBuffer.size() * sizeof(ImDrawIdx);

			// steam (with orphan) the contents of each command list into the vbos
			glBufferData(GL_ARRAY_BUFFER, vertex_size, NULL, GL_STREAM_DRAW);
			glBufferData(GL_ARRAY_BUFFER, vertex_size, vtx_buffer, GL_STREAM_DRAW);

			glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_size, NULL, GL_STREAM_DRAW);
			glBufferData(GL_ELEMENT_ARRAY_BUFFER, index_size, idx_buffer, GL_STREAM_DRAW);

			const ImDrawIdx* cur_idx_buffer = idx_buffer;

			for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++) {
				const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
				if (pcmd->UserCallback) {
					pcmd->UserCallback(cmd_list, pcmd);
				} else {
					bind_texture(tex_unit, tex);

					flt y0 = (flt)disp_dim.y -pcmd->ClipRect.w;
					flt y1 = (flt)disp_dim.y -pcmd->ClipRect.y;

					glScissor((int)pcmd->ClipRect.x, (int)y0, (int)(pcmd->ClipRect.z -pcmd->ClipRect.x), (int)(y1 -y0));

					// Render 'pcmd->ElemCount/3' indexed triangles.
					// By default the indices ImDrawIdx are 16-bits, you can change them to 32-bits if your engine doesn't support 16-bits indices.
					glDrawElements(GL_TRIANGLES, pcmd->ElemCount, GL_UNSIGNED_SHORT,
						(GLvoid const*)((u8 const*)cur_idx_buffer -(u8 const*)idx_buffer));
				}
				cur_idx_buffer += pcmd->ElemCount;
			}
		}
	}
} imgui_context;
