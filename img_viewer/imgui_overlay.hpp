#pragma once

#include <string>
#include <vector>

#include "glfw3.h"
#include "imgui.hpp"
#include "imgui_draw.hpp"
#include "imgui_demo.hpp"

#include "image.hpp"
#include "texture.hpp"
#include "shader.hpp"

#include "colors.hpp"

#include "defer.hpp"

namespace ImGui {
	IMGUI_API void Value (const char* prefix, v2 v) {
		Text("%s: %.2f, %.2f", prefix, v.x,v.y);
	}
	IMGUI_API void Value (const char* prefix, iv2 v) {
		Text("%s: %d, %d", prefix, v.x,v.y);
	}

	IMGUI_API bool InputText_str (const char* label, std::string* s, ImGuiInputTextFlags flags = 0, ImGuiTextEditCallback callback = NULL, void* user_data = NULL) {
		int cur_length = (int)s->size();
		s->resize(1024);
		(*s)[ min(cur_length, (int)s->size()-1) ] = '\0'; // is this guaranteed to work?
		
		bool ret = InputText(label, &(*s)[0], (int)s->size(), flags, callback, user_data);

		s->resize( strlen(&(*s)[0]) );

		return ret;
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
};
