
#define _CRT_SECURE_NO_WARNINGS 1

#include "stdio.h"

#include <memory>
using std::unique_ptr;
using std::make_unique;
using std::shared_ptr;
using std::make_shared;

#include <string>
typedef std::string str;
typedef std::string const& strcr;

#include <vector>

#include "glad_helper.hpp"
#include "glfw3.h"

#include "basic_typedefs.hpp"

#include "texture.hpp"

#include "vector_util.hpp"
#include "logging.hpp"
#include "file_io.hpp"

#include "imgui_overlay.hpp"
#include "platform.hpp"

bool ctrl_down = false;

void glfw_key_event (GLFWwindow* window, int key, int scancode, int action, int mods) {
	ImGuiIO& io = ImGui::GetIO();

	assert_log(key >= 0 && key < ARRLEN(io.KeysDown));
	io.KeysDown[key] = action != GLFW_RELEASE;

	// from https://github.com/ocornut/imgui/blob/master/examples/opengl3_example/imgui_impl_glfw_gl3.cpp
	(void)mods; // Modifiers are not reliable across systems
	io.KeyCtrl = io.KeysDown[GLFW_KEY_LEFT_CONTROL] || io.KeysDown[GLFW_KEY_RIGHT_CONTROL];
	io.KeyShift = io.KeysDown[GLFW_KEY_LEFT_SHIFT] || io.KeysDown[GLFW_KEY_RIGHT_SHIFT];
	io.KeyAlt = io.KeysDown[GLFW_KEY_LEFT_ALT] || io.KeysDown[GLFW_KEY_RIGHT_ALT];
	io.KeySuper = io.KeysDown[GLFW_KEY_LEFT_SUPER] || io.KeysDown[GLFW_KEY_RIGHT_SUPER];

	ctrl_down = io.KeyCtrl;
}
void glfw_char_event (GLFWwindow* window, unsigned int codepoint, int mods) {
	ImGuiIO& io = ImGui::GetIO();
	io.AddInputCharacter((char)codepoint);
}

#include "find_files.hpp"
using namespace n_find_files;

struct MButton {
	bool	down		: 1;
	bool	went_down	: 1;
	bool	went_up		: 1;

	void new_state (bool new_down) {
		went_down =	 new_down && !down;
		went_up =	!new_down &&  down;
		down = new_down;
	}
};

#include <map>

struct App {
	Display			disp;
	Imgui_Context	imgui_ctx;

	int				frame_i = 0;

	//icol			bg_col =		irgb(41,49,52);
	
	MButton			lmb =	 {0,0,0};
	MButton			rmb =	 {0,0,0};

	struct Image_Cache {
		
		struct Image {
			Image2D		full_src_img;
			Texture2D	full_tex;
		};

		std::map< str, unique_ptr<Image> >	images; // key: filepath

		Texture2D* query_image_texture (strcr filepath) {
			auto it = images.find(filepath);
			if (it != images.end()) // already cached
				return &it->second->full_tex;
			// not cached yet

			auto img = make_unique<Image>();

			try {
				img->full_src_img = Image2D::load_from_file(filepath);
			} catch (Expt_File_Load_Fail const& e) {
				assert_log(false, e.what());
				return nullptr;
			}
			
			img->full_tex = Texture2D::generate();
			img->full_tex.upload( img->full_src_img.pixels, img->full_src_img.size );

			img->full_tex.gen_mipmaps();
			img->full_tex.set_filtering_mipmapped();
			img->full_tex.set_border_clamp();

			auto ret = images.emplace(filepath, std::move(img));
			assert_log(ret.second);
			
			return &ret.first->second->full_tex;
		}
	};

	struct Content {
		str		name;

		virtual void enable_dynamic_cast() {};
	};

	struct File : Content {
		
	};
	struct Directory_Tree : Content {
		//str		name; // for root: full or relative path of directory +'/',  for non-root: directory name +'/'

		std::vector< unique_ptr<Directory_Tree> >	subdirs;
		std::vector< unique_ptr<File> >				files;

		std::vector<Content*>						content;
	};

	void _populate (Directory_Tree* tree, n_find_files::Directory_Tree const& dir) {
		
		for (auto& d : dir.dirs) {
			auto subtree = make_unique<Directory_Tree>();

			subtree->name = d.name;
			_populate(subtree.get(), d);

			tree->content.emplace_back(subtree.get());

			tree->subdirs.emplace_back( std::move(subtree) );
		}
		for (auto& fn : dir.filenames) {
			auto file = make_unique<File>();
			file->name = fn;

			tree->content.emplace_back(file.get());

			tree->files.emplace_back( std::move(file) );
		}
	}

	Image_Cache		img_cache;
	Directory_Tree	test_dir;

	void init () {
		test_dir.name = "P:/img_viewer_sample_files/";

		_populate(&test_dir, find_files_recursive(test_dir.name));
	}

	void gui_file_tree (Directory_Tree* dir) {
		using namespace ImGui;
		
		if (TreeNodeEx(dir->name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
			
			if (dir->content.size() == 0) {
				TextDisabled("<empty>");
			} else {
				for (auto* c : dir->content) {
					if (dynamic_cast<Directory_Tree*>(c)) {

						gui_file_tree((Directory_Tree*)c);

					} else if (dynamic_cast<File*>(c)) {

						Text(c->name.c_str());

					} else {
						assert_log(false);
					}
				}
			}

			TreePop();
		}
	}
	void gui_file_grid (Directory_Tree* dir, int left_bar_size) {
		static flt zoom_multiplier_target = 1;
		static flt zoom_multiplier = zoom_multiplier_target;

		flt zoom_delta = 0;
		zoom_delta = (flt)mouse_wheel_diff;

		static flt zoom_multiplier_anim_start;
		static int zoom_anim_frames_remain = 0;
		static int anim_frames = 4;
		ImGui::DragInt("anim_frames", &anim_frames);

		static flt zoom_step = 0.1f;
		ImGui::DragFloat("zoom_step", &zoom_step);

		if (zoom_delta != 0) {
			flt zoom_level = log2f(zoom_multiplier_target);

			zoom_multiplier_anim_start = zoom_multiplier;
			zoom_multiplier_target = powf(2.0f, zoom_level +zoom_delta * zoom_step);

			zoom_anim_frames_remain = anim_frames -1; // start with anim t=1 frame instead of t=0 to reduce visual input lag
		}

		zoom_multiplier = lerp(zoom_multiplier_anim_start, zoom_multiplier_target, (flt)(anim_frames -zoom_anim_frames_remain) / anim_frames);

		ImGui::DragInt("zoom_anim_frames_remain", &zoom_anim_frames_remain);
		ImGui::DragFloat("zoom_multiplier_target", &zoom_multiplier_target);
		ImGui::DragFloat("zoom_multiplier", &zoom_multiplier);

		if (zoom_anim_frames_remain != 0)
			zoom_anim_frames_remain--;

		v2 cell_sz = 180 * zoom_multiplier;

		//
		v2 grid_sz_px = (v2)(disp.dim -iv2(left_bar_size, 0));

		v2 view_center = v2((flt)left_bar_size, 0) +(v2)grid_sz_px / 2;

		static flt debug_view_size_multiplier = 1;
		ImGui::DragFloat("debug_view_size_multiplier", &debug_view_size_multiplier, 1.0f/300, 0.01f);
		if (debug_view_size_multiplier != 1) {
			debug_view_size_multiplier = max(debug_view_size_multiplier, 0.0001f);
			
			emit_overlay_rect_outline(view_center -grid_sz_px/2/debug_view_size_multiplier, view_center +grid_sz_px/2/debug_view_size_multiplier, rgba8(255,0,0,255));

			grid_sz_px /= debug_view_size_multiplier;

			cell_sz /= debug_view_size_multiplier;
		}

		v2 grid_sz_cells = grid_sz_px / cell_sz;

		static flt focus = (flt)dir->content.size() / 2;
		
		ImGui::DragFloat("focus", &focus, 1.0f / 50, -1, (flt)dir->content.size() +1);

		for (int content_i=0; content_i<(int)dir->content.size(); content_i++) {
			auto img_instance = [&] (v2 pos_center_rel, flt alpha) {
				if (	pos_center_rel.y < -grid_sz_cells.y/2 -0.5f ||
						pos_center_rel.y > +grid_sz_cells.y/2 +0.5f)
					return;

				v2 pos_center_rel_px = pos_center_rel * cell_sz;

				auto* c = dir->content[content_i];
				Texture2D* tex = nullptr;

				if (dynamic_cast<Directory_Tree*>(c)) {

					tex = img_cache.query_image_texture("assets_src/folder_icon.png");

				} else if (dynamic_cast<File*>(c)) {

					auto* file = (File*)c;

					tex = img_cache.query_image_texture(dir->name+file->name);

				} else {
					assert_log(false);
				}

				if (tex) {
					auto img_size = (v2)tex->get_size_px();

					v2 aspect = img_size / max(img_size.x, img_size.y);

					v2 sz_px = (v2)cell_sz * aspect -5*2;
					v2 offs_to_center_px = (cell_sz -sz_px) / 2;

					v2 pos_px = view_center +pos_center_rel_px -cell_sz / 2;
					pos_px += offs_to_center_px;

					draw_textured_quad(pos_px, sz_px, *tex, rgba8(255,255,255, (u8)(alpha * 255 +0.5f)));
				}
			};

			flt rel_indx = (flt)content_i -focus;

			flt quotient;
			flt remainder = mod_range(rel_indx, -grid_sz_cells.x/2, +grid_sz_cells.x/2, &quotient);

			flt out_of_bounds_l = max(-(remainder -0.5f +grid_sz_cells.x/2), 0.0f);
			flt out_of_bounds_r = max(  remainder +0.5f -grid_sz_cells.x/2 , 0.0f);
			
			v2 pos_center_rel = v2(remainder,quotient);

			assert_log((out_of_bounds_l +out_of_bounds_r) <= 1);
			img_instance(pos_center_rel, content_i == (int)roundf(focus) ? 1 : 1 -out_of_bounds_l -out_of_bounds_r);

			if (out_of_bounds_l > 0) {
				img_instance((pos_center_rel -v2(-grid_sz_cells.x,1)), out_of_bounds_l);
			}
			if (out_of_bounds_r > 0) {
				img_instance((pos_center_rel +v2(-grid_sz_cells.x,1)), out_of_bounds_r);
			}

		}

		#if 0
		static flt zoom_multiplier_target = 1;
		static flt zoom_multiplier = zoom_multiplier_target;

		flt zoom_delta = 0;
		flt scroll_delta = 0;
		(ctrl_down ? zoom_delta : scroll_delta) = (flt)mouse_wheel_diff;

		static flt zoom_multiplier_anim_start;
		static int zoom_anim_frames_remain = 0;
		static int anim_frames = 4;
		ImGui::InputInt("anim_frames", &anim_frames);

		if (zoom_delta != 0) {
			flt zoom_level = log2f(zoom_multiplier_target);
			
			zoom_multiplier_anim_start = zoom_multiplier;
			zoom_multiplier_target = powf(2.0f, zoom_level +zoom_delta / 10);
			
			zoom_anim_frames_remain = anim_frames -1; // start with anim t=1 frame instead of t=0 to reduce visual input lag
		}

		zoom_multiplier = lerp(zoom_multiplier_anim_start, zoom_multiplier_target, (flt)(anim_frames -zoom_anim_frames_remain) / anim_frames);

		if (zoom_anim_frames_remain != 0)
			zoom_anim_frames_remain--;

		v2 cell_sz = 300 * zoom_multiplier;

		flt scroll_px = (flt)-0 * 150;

		iv2 grid_sz = disp.dim -iv2(left_bar_size, 0);

		int columns = (int)floor((flt)grid_sz.x / cell_sz.x);

		iv2 cell = 0;

		for (auto* c : dir->content) {
			Texture2D* tex = nullptr;
			
			if (dynamic_cast<Directory_Tree*>(c)) {

				tex = img_cache.query_image_texture("assets_src/folder_icon.png");

			} else if (dynamic_cast<File*>(c)) {
				
				auto* file = (File*)c;

				tex = img_cache.query_image_texture(dir->name+file->name);
				
			} else {
				assert_log(false);
			}

			if (tex) {

				auto img_size = (v2)tex->get_size_px();

				v2 aspect = img_size / max(img_size.x, img_size.y);

				v2 sz_px = (v2)cell_sz * aspect;
				v2 offs_to_center_px = (cell_sz -sz_px) / 2;

				v2 pos_px = v2((flt)left_bar_size, -scroll_px) +cell_sz * (v2)cell;
				pos_px += offs_to_center_px;

				draw_textured_quad(pos_px, sz_px, *tex);
			}

			if (++cell.x == columns) {
				cell.x = 0;
				cell.y++;
			}
		}
		#endif
	}

	struct Solid_Col_Vertex {
		v2		pos_screen;
		rgba8	col;
	};
	struct Triangle {
		Solid_Col_Vertex a, b, c;
	};

	std::vector<Triangle> overlay_tris;

	void emit_overlay_rect_outline (v2 A, v2 B, rgba8 col) {
		v2 a = A;
		v2 b = v2(B.x,A.y);
		v2 c = B;
		v2 d = v2(A.x,B.y);
		emit_overlay_line(a,b, col);
		emit_overlay_line(b,c, col);
		emit_overlay_line(c,d, col);
		emit_overlay_line(d,a, col);
	}
	void emit_overlay_line (v2 A, v2 B, rgba8 col) {

		v2 line_dir = B -A;

		v2 line_normal = normalize( rotate2_90(line_dir) );

		v2 a = A -line_normal/2;
		v2 b = B -line_normal/2;
		v2 c = B +line_normal/2;
		v2 d = A +line_normal/2;

		emit_overlay_quad({a,col}, {b,col}, {c,col}, {d,col});
	}
	void emit_overlay_quad (Solid_Col_Vertex const& a, Solid_Col_Vertex const& b, Solid_Col_Vertex const& c, Solid_Col_Vertex const& d) {
		overlay_tris.push_back({b,c,a});
		overlay_tris.push_back({a,c,d});
	}

	void draw_textured_quad (v2 pos_px, v2 sz_px, Texture2D const& tex, rgba8 col=rgba8(255)) {

		struct Textured_Vertex {
			v2		pos_screen;
			v2		uv;
			rgba8	col;

			Textured_Vertex (v2 p, v2 uv, rgba8 c): pos_screen{p}, uv{uv}, col{c} {}
		};
		
		static std::vector<Textured_Vertex> vbo_data;
		
		vbo_data.clear();
		
		for (v2 p : { v2(1,0),v2(1,1),v2(0,0), v2(0,0),v2(1,1),v2(0,1) })
			vbo_data.emplace_back( pos_px +sz_px * p, v2(p.x, 1 -p.y), col ); // flip uv, since we send positions as top-down in the gui code
		
		static GLuint vbo = [] () {
			GLuint vbo;
			glGenBuffers(1, &vbo);
			return vbo;
		} ();
		
		auto bind_vbos = [&] (Shader* shad, GLuint vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
		
			GLint loc_pos =		glGetAttribLocation(shad->prog, "attr_pos_screen");
			GLint loc_uv =		glGetAttribLocation(shad->prog, "attr_uv");
			GLint loc_col =		glGetAttribLocation(shad->prog, "attr_col");
		
			glEnableVertexAttribArray(loc_pos);
			glVertexAttribPointer(loc_pos, 2, GL_FLOAT, GL_FALSE, sizeof(Textured_Vertex), (void*)offsetof(Textured_Vertex, pos_screen));
		
			glEnableVertexAttribArray(loc_uv);
			glVertexAttribPointer(loc_uv, 2, GL_FLOAT, GL_FALSE, sizeof(Textured_Vertex), (void*)offsetof(Textured_Vertex, uv));
		
			glEnableVertexAttribArray(loc_col);
			glVertexAttribPointer(loc_col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Textured_Vertex), (void*)offsetof(Textured_Vertex, col));
		};
		
		static auto shad = [] () {
			auto shad = make_unique<Shader>("shad_textured");
			shad->vert_filename = "shaders/textured.vert";
			shad->frag_filename = "shaders/textured.frag";
			shad->load_program();
			return shad;
		} ();
		
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);
		
		glUseProgram(shad->prog);
		
		bind_vbos(shad.get(), vbo);
		
		static GLint loc_screen_dim = glGetUniformLocation(shad->prog, "screen_dim");
		glUniform2f(loc_screen_dim, (flt)disp.dim.x,(flt)disp.dim.y);
		
		glBufferData(GL_ARRAY_BUFFER, vbo_data.size() * sizeof(Textured_Vertex), NULL, GL_STREAM_DRAW);
		glBufferData(GL_ARRAY_BUFFER, vbo_data.size() * sizeof(Textured_Vertex), &vbo_data[0], GL_STREAM_DRAW);
		
		static GLint tex_unit = 0;

		static GLint tex_loc = glGetUniformLocation(shad->prog, "tex");
		glUniform1i(tex_loc, tex_unit);

		bind_texture(tex_unit, tex);
		
		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vbo_data.size());
		
	}
	void draw_triangles_solid (std::vector<Triangle> tri) {
		if (tri.size() == 0) return;

		static GLuint vbo = [] () {
			GLuint vbo;
			glGenBuffers(1, &vbo);
			return vbo;
		} ();

		auto bind_vbos = [&] (Shader* shad, GLuint vbo) {
			glBindBuffer(GL_ARRAY_BUFFER, vbo);

			GLint loc_pos =		glGetAttribLocation(shad->prog, "attr_pos_screen");
			GLint loc_col =		glGetAttribLocation(shad->prog, "attr_col");

			glEnableVertexAttribArray(loc_pos);
			glVertexAttribPointer(loc_pos, 2, GL_FLOAT, GL_FALSE, sizeof(Solid_Col_Vertex), (void*)offsetof(Solid_Col_Vertex, pos_screen));

			glEnableVertexAttribArray(loc_col);
			glVertexAttribPointer(loc_col, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Solid_Col_Vertex), (void*)offsetof(Solid_Col_Vertex, col));
		};

		static auto shad = [] () {
			auto shad = make_unique<Shader>("shad_solid_col");
			shad->vert_filename = "shaders/solid_col.vert";
			shad->frag_filename = "shaders/solid_col.frag";
			shad->load_program();
			return shad;
		} ();

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_SCISSOR_TEST);

		glUseProgram(shad->prog);

		bind_vbos(shad.get(), vbo);

		static GLint loc_screen_dim = glGetUniformLocation(shad->prog, "screen_dim");
		glUniform2f(loc_screen_dim, (flt)disp.dim.x,(flt)disp.dim.y);

		glBufferData(GL_ARRAY_BUFFER, tri.size() * sizeof(Triangle), NULL, GL_STREAM_DRAW);
		glBufferData(GL_ARRAY_BUFFER, tri.size() * sizeof(Triangle), &tri[0], GL_STREAM_DRAW);

		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)tri.size() * 3);

	}

	void gui () {
		using namespace ImGui;

		SetNextWindowBgAlpha(1);

		SetNextWindowPos({0,0});

		// doen't work, window is not resizable
		//SetNextWindowSizeConstraints({-1,-1}, {-1,-1},
		//	[] (ImGuiSizeCallbackData* data) {
		//		data->DesiredSize.x = data->DesiredSize.y;
		//	});

		Begin("Protoype GUI", nullptr, ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar);
		auto wnd_size = GetWindowSize();
		int left_bar_size = (int)wnd_size.x;

		static bool directories_always_first = false;
		Checkbox("directories_always_first", &directories_always_first);

		gui_file_grid(&test_dir, left_bar_size);

		gui_file_tree(&test_dir);


		End();

		SetNextWindowBgAlpha(1);
		ShowDemoWindow();
	}
	
	bool frame () {
		
		overlay_tris.clear();

		iv2 mouse_pos_px;
		v2 mouse_pos_01_bottom_up;
		{ // get syncronous input
			// display
			glfwGetFramebufferSize(disp.wnd, &disp.dim.x, &disp.dim.y);
			// mouse
			f64 x, y;
			glfwGetCursorPos(disp.wnd, &x, &y);
			mouse_pos_px = iv2((int)x, (int)y);
			mouse_pos_01_bottom_up = ((v2)mouse_pos_px +0.5f) / (v2)disp.dim;
			mouse_pos_01_bottom_up.y = 1 -mouse_pos_01_bottom_up.y;
		}

		if (glfwWindowShouldClose(disp.wnd))
			return true;

		imgui_ctx.begin_frame(disp.dim, 1.0f/60, mouse_pos_px, real_lmb_down, real_rmb_down, mouse_wheel_diff);

		mouse_wheel_diff = ImGui::GetIO().WantCaptureMouse ? 0 : mouse_wheel_diff;
		lmb.new_state( ImGui::GetIO().WantCaptureMouse ? false : real_lmb_down ); // does this produce wrong edge cases?
		rmb.new_state( ImGui::GetIO().WantCaptureMouse ? false : real_rmb_down ); //

		// render gui
		glDisable(GL_SCISSOR_TEST);

		glViewport(0,0, disp.dim.x,disp.dim.y);

		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT);

		gui();

		imgui_ctx.draw(disp.dim);


		draw_triangles_solid(overlay_tris);

		// display to screen
		glfwSwapBuffers(disp.wnd);

		++frame_i;
		return false;
	}
};

App	app;

void glfw_refresh_callback (GLFWwindow* window) {
	app.frame();
}

int main (int argc, char** argv) {
	
	app.disp = init_engine();

	glfwSetWindowRefreshCallback(app.disp.wnd, glfw_refresh_callback);

	app.imgui_ctx.init();

	app.init();

	// Controls
	for (;;) {
		
		mouse_wheel_diff = 0;

		glfwPollEvents(); // calls async input callbacks
		
		if (app.frame()) break;
	}

	glfwDestroyWindow(app.disp.wnd);
	glfwTerminate();

	return 0;
}
