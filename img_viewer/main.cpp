
#define _CRT_SECURE_NO_WARNINGS 1

#include "stdio.h"

#include <memory>
using std::unique_ptr;
using std::make_unique;
using std::shared_ptr;
using std::make_shared;

#include <string>
using std::string;

#include <vector>

#include "glad_helper.hpp"
#include "glfw3.h"

#include "basic_typedefs.hpp"

#include "texture.hpp"

#include "vector_util.hpp"
#include "logging.hpp"
#include "simple_file_io.hpp"

#include "imgui_overlay.hpp"
#include "platform.hpp"

bool ctrl_down = false;
bool do_toggle_fullscreen = false;

void glfw_key_event (GLFWwindow* window, int key, int scancode, int action, int mods) {
	ImGuiIO& io = ImGui::GetIO();

	if (key >= 0 && key < ARRLEN(io.KeysDown))
		io.KeysDown[key] = action != GLFW_RELEASE;

	// from https://github.com/ocornut/imgui/blob/master/examples/opengl3_example/imgui_impl_glfw_gl3.cpp
	(void)mods; // Modifiers are not reliable across systems
	io.KeyCtrl = io.KeysDown[GLFW_KEY_LEFT_CONTROL] || io.KeysDown[GLFW_KEY_RIGHT_CONTROL];
	io.KeyShift = io.KeysDown[GLFW_KEY_LEFT_SHIFT] || io.KeysDown[GLFW_KEY_RIGHT_SHIFT];
	io.KeyAlt = io.KeysDown[GLFW_KEY_LEFT_ALT] || io.KeysDown[GLFW_KEY_RIGHT_ALT];
	io.KeySuper = io.KeysDown[GLFW_KEY_LEFT_SUPER] || io.KeysDown[GLFW_KEY_RIGHT_SUPER];

	ctrl_down = io.KeyCtrl;

	switch (key) {
		case GLFW_KEY_F11: {
			if (action == GLFW_PRESS)
				do_toggle_fullscreen = true;
		} break;
	}
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

#include "threadpool.hpp"

struct App {
	int				frame_i = 0;

	//icol			bg_col =		irgb(41,49,52);
	
	MButton			lmb =	 {0,0,0};
	MButton			rmb =	 {0,0,0};

	unique_ptr<Texture2D>	tex_folder_icon;
	unique_ptr<Texture2D>	tex_loading_icon;

	static Texture2D create_null_texture () {
		auto tex = Texture2D::generate();
		rgba8 col = rgba8(255,0,255,255);
		tex.upload(&col, 1);
		return std::move(tex);
	}
	static Texture2D simple_load_texture (strcr filepath) {
		Image2D img;

		// load image from disk
		try {
			img = Image2D::load_from_file(filepath);
		} catch (Expt_File_Load_Fail const& e) {
			assert_log(false, e.what());
			return std::move(create_null_texture());
		}

		auto tex = Texture2D::generate();
		tex.upload( img.pixels, img.size );

		tex.gen_mipmaps();
		tex.set_filtering_mipmapped();
		tex.set_border_clamp();

		return std::move(tex);
	}

	struct Image_Cache {

		struct Image {
			Image2D		full_src_img;
			Texture2D	full_tex;

			bool		currently_async_loading; // loading async via threadpool
		};

		std::map< str, unique_ptr<Image> >	images; // key: filepath

		u64 cpu_memory_size = 0;
		u64 gpu_memory_size = 0; // this might not be accurate, since we can't be sure of the gpu texel format

		struct Img_Loader_Threadpool_Job { // input is filepath to file to load
			string	filepath;
		};
		struct Img_Loader_Threadpool_Result {
			string	filepath;
			Image2D	full_src_img;
		};

		struct Img_Loader_Threadpool_Processor {
			static Img_Loader_Threadpool_Result process_job (Img_Loader_Threadpool_Job&& job) {
				Img_Loader_Threadpool_Result res;

				res.filepath = job.filepath;

				// load image from disk
				try {
					res.full_src_img = Image2D::load_from_file(job.filepath);

					//res.full_src_img = Image2D::dumb_fast_downsize(res.full_src_img, res.full_src_img.size / 4);

				} catch (Expt_File_Load_Fail const& e) {
					//assert_log(false, e.what()); // is not threadsafe!
					// res.full_src_img is still null (by which i mean default constructed) -> signifies that image was not loaded
				}

				return res;
			}
		};

		Threadpool<Img_Loader_Threadpool_Job, Img_Loader_Threadpool_Result, Img_Loader_Threadpool_Processor> img_loader_threadpool;

		void init_thread_pool () {
			int cpu_threads = (int)std::thread::hardware_concurrency();
			int threads = max(cpu_threads -1, 2);
			
			img_loader_threadpool.start_threads(threads);
		}

		Texture2D* query_image_texture (strcr filepath) {
			auto it = images.find(filepath);
			if (it != images.end()) {
				// already cached or just inserted
				Image& tmp = *it->second;
				return tmp.currently_async_loading ? nullptr : &tmp.full_tex;
			} else {
				// not cached yet
				auto img = make_unique<Image>();

				img_loader_threadpool.jobs.push({filepath});

				img->currently_async_loading = true;
			
				auto ret = images.emplace(filepath, std::move(img));
				assert_log(ret.second);

				return nullptr; // just started loading async
			}
		}

		void async_image_loading (int frame_i) {
			
			if (ImGui::CollapsingHeader("Image_Cache", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Value("threadpool threads", img_loader_threadpool.get_thread_count());
				
				ImGui::Value_Bytes("cpu_memory_size", cpu_memory_size);
				ImGui::Value_Bytes("gpu_memory_size", gpu_memory_size);


				static f32 sz_in_mb[256] = {};
				static int cur_val = 0;

				if (frame_i % 1 == 0) {
					sz_in_mb[cur_val++] = (flt)cpu_memory_size / 1024 / 1024;
					cur_val %= ARRLEN(sz_in_mb);
				}

				static flt range = 1024*6;
				ImGui::DragFloat("##cpu_memory_size_plot_range", &range, 10);

				ImGui::PushItemWidth(-1);
				ImGui::PlotLines("##cpu_memory_size", sz_in_mb, ARRLEN(sz_in_mb), cur_val, "cpu_memory_size in MB", 0, range, ImVec2(0,80));
				ImGui::PopItemWidth();

			}
			
			f64 uploading_begin = glfwGetTime();

			for (;;) {
				
				Img_Loader_Threadpool_Result res;
				if (!img_loader_threadpool.results.try_pop(&res))
					break; // currently no images loaded async, stop polling

				if (res.full_src_img.pixels == nullptr) {
					// image could not be loaded, simply pretend it never finished loading for development purposes
				} else {
					// create texture from image by uploading
					auto it = images.find(res.filepath);
					assert_log(it != images.end());
					
					auto& tmp = *it->second;
					tmp.full_src_img = std::move( res.full_src_img );

					cpu_memory_size += tmp.full_src_img.calc_size();

					tmp.full_tex = Texture2D::generate();
					tmp.full_tex.upload( tmp.full_src_img.pixels, tmp.full_src_img.size );

					tmp.full_tex.gen_mipmaps();
					tmp.full_tex.set_filtering_mipmapped();
					tmp.full_tex.set_border_clamp();

					gpu_memory_size += tmp.full_src_img.calc_size(); // same as cpu memory size for now

					tmp.currently_async_loading = false;
				}

				f32 elapsed = (f32)(glfwGetTime() -uploading_begin);

				if (elapsed > 0.005f)
					break; // stop uploading if uploading has already taken longer than timeout
			}

			f32 upload_elapsed = (f32)(glfwGetTime() -uploading_begin);
			
			static f32 max_upload_elapsed = -INF;
			max_upload_elapsed = max(max_upload_elapsed, upload_elapsed);
			
			ImGui::Value("max_upload_elapsed (ms)", max_upload_elapsed * 1000);
		}

		void clear () {
			images.clear();

			cpu_memory_size = 0;
			gpu_memory_size = 0;
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

	Image_Cache					img_cache;
	unique_ptr<Directory_Tree>	viewed_dir = nullptr;

	void init () {
		tex_loading_icon = make_unique<Texture2D>( simple_load_texture("assets_src/loading_icon.png") );
		tex_folder_icon = make_unique<Texture2D>( simple_load_texture("assets_src/folder_icon.png") );

		img_cache.init_thread_pool();
	}

	void gui () {
		static str viewed_dir_path_input_text = "P:/img_viewer_sample_files/"; // never touch string input, instead make a copy where we fix the escaping of backslashes etc.
		
		if (!ImGui::CollapsingHeader("viewed_dir_path", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		if (ImGui::Button("Directory selection dialog")) {
			char buf[MAX_PATH];
			
			BROWSEINFO	i = {};
			i.hwndOwner = glfwGetWin32Window(disp.window);
			i.pszDisplayName = buf;
			i.lpszTitle = "test";
			i.ulFlags = BIF_NEWDIALOGSTYLE;

			if (SHBrowseForFolder(&i) != NULL) {
				viewed_dir_path_input_text = string(i.lpszTitle);
			}
		}

		ImGui::PushItemWidth( ImGui::GetContentRegionAvailWidth() );
		ImGui::InputText_str("##viewed_dir_path_input_text", &viewed_dir_path_input_text);
		ImGui::PopItemWidth();
		
		bool trigger_load = ImGui::Button("Trigger Directory Load") || frame_i == 0;

		static string load_msg = "<not loaded yet>";
		static bool load_ok = false;

		if (trigger_load) {
			load_ok = false;
			
			img_cache.clear();
			viewed_dir = nullptr;
			
			try {
				auto fix_dir_path = [&] (string dir) -> string {
					for (char& c : dir) {
						if (c == '\\')
							c = '/';
					}
					if (dir.size() > 0 && dir.back() != '/') {
						dir.push_back('/');
					}
					return dir;
				};

				string viewed_dir_path = fix_dir_path(viewed_dir_path_input_text);

				auto new_dir = find_files_recursive(viewed_dir_path);
				
				viewed_dir = make_unique<Directory_Tree>();
				viewed_dir->name = viewed_dir_path;

				_populate(viewed_dir.get(), new_dir);

				load_ok = true;

			} catch (Expt_Path_Not_Found const& e) {
				load_msg = prints("Path not found! \"%s\"", e.get_path().c_str());
			}

		}

		static const ImVec4 col_ok =	ImVec4(0,1,0,1);
		static const ImVec4 col_err =	ImVec4(1,0,0,1);

		ImGui::SameLine();
		ImGui::TextColored(load_ok ? col_ok : col_err, load_ok ? "OK" : load_msg.c_str());

	}

	void file_grid (Directory_Tree* dir, int left_bar_size) {
		static flt zoom_multiplier_target = 1 ? 0.1f : 1;
		static flt zoom_multiplier = zoom_multiplier_target;

		flt zoom_delta = 0;
		zoom_delta = (flt)mouse_wheel_diff;

		static flt zoom_multiplier_anim_start;
		static int zoom_smoothing_frames_remain = 0;
		static int zoom_smoothing_frames = 4;
		
		static flt zoom_step = 0.1f;
		
		if (zoom_delta != 0) {
			flt zoom_level = log2f(zoom_multiplier_target);

			zoom_multiplier_anim_start = zoom_multiplier;
			zoom_multiplier_target = powf(2.0f, zoom_level +zoom_delta * zoom_step);

			zoom_smoothing_frames_remain = zoom_smoothing_frames -1; // start with anim t=1 frame instead of t=0 to reduce visual input lag
		}
		zoom_multiplier = lerp(zoom_multiplier_anim_start, zoom_multiplier_target, (flt)(zoom_smoothing_frames -zoom_smoothing_frames_remain) / zoom_smoothing_frames);

		if (zoom_smoothing_frames_remain != 0)
			zoom_smoothing_frames_remain--;

		v2 cell_sz = disp.framebuffer_size_px.y * zoom_multiplier;

		//
		v2 grid_sz_px = (v2)(disp.framebuffer_size_px -iv2(left_bar_size, 0));

		v2 view_center = v2((flt)left_bar_size, 0) +(v2)grid_sz_px / 2;

		static flt debug_view_size_multiplier = 0 ? 2 : 1;
		
		if (debug_view_size_multiplier != 1) {
			debug_view_size_multiplier = max(debug_view_size_multiplier, 0.0001f);

			emit_overlay_rect_outline(view_center -grid_sz_px/2/debug_view_size_multiplier, view_center +grid_sz_px/2/debug_view_size_multiplier, rgba8(255,0,0,255));

			grid_sz_px /= debug_view_size_multiplier;

			cell_sz /= debug_view_size_multiplier;
		}

		v2 grid_sz_cells = grid_sz_px / cell_sz;

		static flt view_coord = 48;//(flt)(dir ? (int)dir->content.size() : 0) / 2;
		static v2 view_offs = 0;

		static flt loading_icon_fullsz_sz = 0.70f;
		static flt loading_icon_fullsz_alpha = 0.12f;

		static flt loading_icon_overlay_sz = 0.25f;
		static flt loading_icon_overlay_alpha = 0.8f;
		
		if (ImGui::CollapsingHeader("file_grid", ImGuiTreeNodeFlags_DefaultOpen)) {
			
			// This allows me to save one of the following options shown through imgui to be saved to disk and automaticly loaded on the next start of the app (i love how crazy powerful coding like this can be!)
			#define IMGUI_SAVABLE_DRAGT(T, id, valptr, ...) do {											\
					if (frame_i == 0)																		\
						load_fixed_size_binary_file("saves/imgui/" id ".bin", valptr, sizeof(*valptr));		\
																											\
					bool save = ImGui::Button("##" id);														\
																											\
					ImGui::SameLine();																		\
					ImGui::T(id, valptr, __VA_ARGS__);														\
																											\
					if (save)																				\
						write_fixed_size_binary_file("saves/imgui/" id ".bin", valptr, sizeof(*valptr));	\
				} while(0)
			
			IMGUI_SAVABLE_DRAGT( DragInt,	"zoom_smoothing_frames", &zoom_smoothing_frames, 1.0f / 40);
			IMGUI_SAVABLE_DRAGT( DragFloat,	"zoom_step", &zoom_step, 0.01f / 40);
			
			IMGUI_SAVABLE_DRAGT( DragInt,	"zoom_anim_frames_remain", &zoom_smoothing_frames_remain);
			IMGUI_SAVABLE_DRAGT( DragFloat,	"zoom_multiplier_target", &zoom_multiplier_target);
			IMGUI_SAVABLE_DRAGT( DragFloat,	"zoom_multiplier", &zoom_multiplier);
			
			IMGUI_SAVABLE_DRAGT( DragFloat,	"debug_view_size_multiplier", &debug_view_size_multiplier, 1.0f/300, 0.01f);
			
			IMGUI_SAVABLE_DRAGT( DragFloat,	"view_coord", &view_coord, 1.0f / 50);
			IMGUI_SAVABLE_DRAGT( DragFloat2, "view_offs", &view_offs.x, 1.0f / 50);

			ImGui::DragFloat("loading_icon_fullsz_sz", &loading_icon_fullsz_sz, 0.01f);
			ImGui::DragFloat("loading_icon_fullsz_alpha", &loading_icon_fullsz_alpha, 0.01f);

			ImGui::DragFloat("loading_icon_overlay_sz", &loading_icon_overlay_sz, 0.01f);
			ImGui::DragFloat("loading_icon_overlay_alpha", &loading_icon_overlay_alpha, 0.01f);
		}


		for (int content_i=0; content_i<(dir ? (int)dir->content.size() : 0); content_i++) {
			auto img_instance = [&] (v2 pos_center_rel, flt alpha) {
				if (	pos_center_rel.y < -grid_sz_cells.y/2 -0.5f ||
						pos_center_rel.y > +grid_sz_cells.y/2 +0.5f)
					return;

				v2 pos_center_rel_px = pos_center_rel * cell_sz;

				auto* c = dir->content[content_i];
				Texture2D* tex = nullptr;

				if (dynamic_cast<Directory_Tree*>(c)) {

					tex = tex_folder_icon.get();

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
				} else {
					v2 pos_px = view_center +pos_center_rel_px +cell_sz * (-0.5f +(1 -loading_icon_fullsz_sz)/2);
					draw_textured_quad(pos_px, cell_sz * loading_icon_fullsz_sz, *tex_loading_icon.get(), rgba8(255,255,255, (int)(loading_icon_fullsz_alpha * 255.0f +0.5f)));
				}
			};

			flt rel_indx = (flt)content_i -view_coord;

			flt quotient;
			flt remainder = mod_range(rel_indx, -grid_sz_cells.x/2, +grid_sz_cells.x/2, &quotient);

			flt out_of_bounds_l = max(-(remainder -0.5f +grid_sz_cells.x/2), 0.0f);
			flt out_of_bounds_r = max(  remainder +0.5f -grid_sz_cells.x/2 , 0.0f);
			
			v2 pos_center_rel = v2(remainder,quotient) -view_offs;

			assert_log((out_of_bounds_l +out_of_bounds_r) <= 1);
			img_instance(pos_center_rel, content_i == (int)roundf(view_coord) ? 1 : 1 -out_of_bounds_l -out_of_bounds_r);

			//ImGui::Value("content_i", content_i);
			//ImGui::Value("(int)roundf(view_coord)", (int)roundf(view_coord));

			if (out_of_bounds_l > 0) {
				img_instance((pos_center_rel -v2(-grid_sz_cells.x,1)), out_of_bounds_l);
			}
			if (out_of_bounds_r > 0) {
				img_instance((pos_center_rel +v2(-grid_sz_cells.x,1)), out_of_bounds_r);
			}

		}
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
		glUniform2f(loc_screen_dim, (flt)disp.framebuffer_size_px.x,(flt)disp.framebuffer_size_px.y);
		
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
		glUniform2f(loc_screen_dim, (flt)disp.framebuffer_size_px.x,(flt)disp.framebuffer_size_px.y);

		glBufferData(GL_ARRAY_BUFFER, tri.size() * sizeof(Triangle), NULL, GL_STREAM_DRAW);
		glBufferData(GL_ARRAY_BUFFER, tri.size() * sizeof(Triangle), &tri[0], GL_STREAM_DRAW);

		glDrawArrays(GL_TRIANGLES, 0, (GLsizei)tri.size() * 3);

	}

	void render_all () {
		static iv2 imgui_left_bar_size = iv2(400, -1);

		ImGui::SetNextWindowBgAlpha(1);

		ImGui::SetNextWindowPos({0,0});
		
		if (imgui_left_bar_size.y != disp.framebuffer_size_px.y)
			ImGui::SetNextWindowSize(ImVec2((flt)imgui_left_bar_size.x, (flt)disp.framebuffer_size_px.y));

		ImGui::Begin("Protoype GUI", nullptr, ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoTitleBar);
		
		{
			bool fullscreen = disp.is_fullscreen;

			ImGui::Checkbox("app_fullscreen", &fullscreen);

			if (do_toggle_fullscreen)
				fullscreen = !fullscreen;
			
			if (fullscreen != disp.is_fullscreen) {
				disp.toggle_fullscreen();
			}
		}

		{
			auto tmp = ImGui::GetWindowSize();
			imgui_left_bar_size = (iv2)v2(tmp.x,tmp.y);
		}

		
		gui();
		img_cache.async_image_loading(frame_i);
		file_grid(viewed_dir.get(), imgui_left_bar_size.x);

		//gui_file_tree(viewed_dir.get());

		ImGui::Separator();

		{
			static bool show_demo_wnd = false;
			ImGui::Checkbox("ShowDemoWindow", &show_demo_wnd);
			if (show_demo_wnd) {
				ImGui::SetNextWindowBgAlpha(1);
				ImGui::ShowDemoWindow();
			}
		}

		ImGui::End();
	}
	
	bool frame () {
		
		overlay_tris.clear();

		iv2 mouse_pos_px;
		v2 mouse_pos_01_bottom_up;
		{ // get syncronous input
			// display
			glfwGetFramebufferSize(disp.window, &disp.framebuffer_size_px.x, &disp.framebuffer_size_px.y);
			// mouse
			f64 x, y;
			glfwGetCursorPos(disp.window, &x, &y);
			mouse_pos_px = iv2((int)x, (int)y);
			mouse_pos_01_bottom_up = ((v2)mouse_pos_px +0.5f) / (v2)disp.framebuffer_size_px;
			mouse_pos_01_bottom_up.y = 1 -mouse_pos_01_bottom_up.y;
		}

		if (glfwWindowShouldClose(disp.window))
			return true;

		imgui_context.begin_frame(disp.framebuffer_size_px, 1.0f/60, mouse_pos_px, real_lmb_down, real_rmb_down, mouse_wheel_diff);

		mouse_wheel_diff = ImGui::GetIO().WantCaptureMouse ? 0 : mouse_wheel_diff;
		lmb.new_state( ImGui::GetIO().WantCaptureMouse ? false : real_lmb_down ); // does this produce wrong edge cases?
		rmb.new_state( ImGui::GetIO().WantCaptureMouse ? false : real_rmb_down ); //

		// render gui
		glDisable(GL_SCISSOR_TEST);

		glViewport(0,0, disp.framebuffer_size_px.x,disp.framebuffer_size_px.y);

		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT);

		render_all();

		imgui_context.draw(disp.framebuffer_size_px);


		draw_triangles_solid(overlay_tris);

		// display to screen
		glfwSwapBuffers(disp.window);

		++frame_i;
		return false;
	}
};

App	app;

void glfw_refresh_callback (GLFWwindow* window) {
	app.frame();
}

int main (int argc, char** argv) {
	
	init_engine();

	glfwSetWindowRefreshCallback(disp.window, glfw_refresh_callback);

	imgui_context.init();
	
	app.init();

	// Controls
	for (;;) {
		
		mouse_wheel_diff = 0;
		do_toggle_fullscreen = false;

		glfwPollEvents(); // calls async input callbacks
		
		if (app.frame()) break;
	}

	disp.save_window_positioning();

	glfwDestroyWindow(disp.window);
	glfwTerminate();

	return 0;
}
