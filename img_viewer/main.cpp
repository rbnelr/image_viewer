
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

int	frame_i = 0;

struct Texture_Streamer {

	struct Cached_Texture {
		Texture2D	tex;

		iv2			size_px;
		bool		currently_async_loading; // loading async via threadpool

		int			not_queried_for_n_frames;
	};

	std::map< str, unique_ptr<Cached_Texture> >	textures; // key: filepath

	u64 memory_size = 0; // how many bytes of texture data we currently have uploaded

	struct Img_Loader_Threadpool_Job { // input is filepath to file to load
		string	filepath;
		iv2		size_px;
	};
	struct Img_Loader_Threadpool_Result {
		string	filepath;
		Image2D	src_img;
	};

	struct Img_Loader_Threadpool_Processor {
		static Img_Loader_Threadpool_Result process_job (Img_Loader_Threadpool_Job&& job) {
			Img_Loader_Threadpool_Result res;

			res.filepath = job.filepath;

			// load image from disk
			try {
				res.src_img = Image2D::load_from_file(job.filepath);

				res.src_img = Image2D::rescale_box_filter(res.src_img, job.size_px);

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

	u64 get_memory_size (Cached_Texture const& t) {
		auto sz = t.tex.get_size_px();
		return sz.y * sz.x * sizeof(rgba8);
	}

	Cached_Texture* find_texture (string const& filepath) {
		auto it = textures.find(filepath);
		return it != textures.end() ? it->second.get() : nullptr;
	}
	void evict_texture (decltype(textures)::iterator it) {
		memory_size -= get_memory_size(*it->second);
		textures.erase(it);
	}

	void begin_frame () {
		for (auto& t : textures) {
			t.second->not_queried_for_n_frames++;
		}
	}
	void end_frame () {
		for (auto it=textures.begin(), next_it=textures.begin(); it!=textures.end(); it=next_it) {
			next_it = it; ++next_it;
			if (it->second->not_queried_for_n_frames != 0)
				evict_texture(it);
		}
	}

	Texture2D* query_image_texture (strcr filepath, iv2 size_px) {
		
		auto* tex = find_texture(filepath);
		
		bool correct_size_cached = tex && all(tex->size_px == size_px);

		if (correct_size_cached) {
			tex->not_queried_for_n_frames = 0;
			return tex->currently_async_loading ? nullptr : &tex->tex;
		}

		if (tex && !correct_size_cached) {
			evict_texture(textures.find(filepath));
		}

		// not cached yet
		auto img = make_unique<Cached_Texture>();

		img_loader_threadpool.jobs.push({filepath, size_px});

		img->size_px = size_px;
		img->currently_async_loading = true;

		auto ret = textures.emplace(filepath, std::move(img));
		assert_log(ret.second);

		return nullptr; // just started loading async
	}

	void async_image_loading (int frame_i) {

		if (ImGui::CollapsingHeader("Texture_Streamer", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Value("threadpool threads", img_loader_threadpool.get_thread_count());

			ImGui::Value_Bytes("memory_size", memory_size);


			static f32 sz_in_mb[256] = {};
			static int cur_val = 0;

			if (frame_i % 1 == 0) {
				sz_in_mb[cur_val++] = (flt)memory_size / 1024 / 1024;
				cur_val %= ARRLEN(sz_in_mb);
			}

			static flt range = 16;
			ImGui::DragFloat("memory_size_plot_range", &range, 10);

			ImGui::PushItemWidth(-1);
			ImGui::PlotLines("##memory_size", sz_in_mb, ARRLEN(sz_in_mb), cur_val, "memory_size in MB", 0, range, ImVec2(0,80));
			ImGui::PopItemWidth();
			
			static bool show_image_loader_threadpool_jobs = false;
			show_image_loader_threadpool_jobs = ImGui::Button("show_image_loader_threadpool_jobs") || show_image_loader_threadpool_jobs;
			
			if (show_image_loader_threadpool_jobs) {
				ImGui::Begin("Texture_Streamer image loader threadpool jobs", &show_image_loader_threadpool_jobs);
				
				static ImGuiTextFilter filter;
				filter.Draw();

				auto foreach_job = [] (Img_Loader_Threadpool_Job const& job) {
					if (!filter.PassFilter(job.filepath.c_str()))
						return;

					ImGui::Text(job.filepath.c_str());
					ImGui::SameLine();
					ImGui::Value("sz", job.size_px);
				};

				static bool order = false;

				ImGui::SameLine();
				ImGui::Checkbox("Order: checked: top==next to be popped  unchecked: top==most recently pushed", &order);
				
				if (order) {
					img_loader_threadpool.jobs.iterate_queue_back_to_front(foreach_job);
				} else {
					img_loader_threadpool.jobs.iterate_queue_front_to_back(foreach_job);
				}

				ImGui::End();
			}
		}

		auto job_still_needed = [&] (Cached_Texture const* tex, iv2 job_tex_size_px) {
			if (!tex) { // texture is not in our list of textures (was evicted while still async loading?)
				return false;
			} else if (any(tex->size_px != job_tex_size_px)) { // texture is not the correct size (desired size changed while still async loading? -> both sizes are queued threadpool at the same time)
				return false;
			} else if (!tex->currently_async_loading) { // texture is the correct size but was already completed (i think this can happen when: query(sz:5x5), query(sz:6x6), query(sz:5x5) -> tex->size==5x5 but there are two versions of 5x5 in queue)
				return false;
			} else {
				return true;
			}
		};

		auto need_to_cancel_job = [&] (Img_Loader_Threadpool_Job const& job) {
			auto* tex = find_texture(job.filepath);
			return !job_still_needed(tex, job.size_px);
		};

		img_loader_threadpool.jobs.cancel(need_to_cancel_job);

		f64 uploading_begin = glfwGetTime();

		for (;;) {

			Img_Loader_Threadpool_Result res;
			if (!img_loader_threadpool.results.try_pop(&res))
				break; // currently no images loaded async, stop polling

			if (res.src_img.pixels == nullptr) {
				// image could not be loaded, simply pretend it never finished loading for development purposes
			} else {
				auto* tex = find_texture(res.filepath);
				if (!job_still_needed(tex, res.src_img.size)) {
					// Ignore result
				} else {
					// create texture from image by uploading

					tex->tex = Texture2D::generate();
					tex->tex.upload( res.src_img.pixels, res.src_img.size );

					//tex->tex.gen_mipmaps();
					tex->tex.set_filtering_mipmapped();
					tex->tex.set_border_clamp();
				
					memory_size += get_memory_size(*tex);

					tex->currently_async_loading = false;
				}
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
		textures.clear();

		memory_size = 0;
	}
};

struct App {
	//icol			bg_col =		irgb(41,49,52);
	
	MButton			lmb =	 {0,0,0};
	MButton			rmb =	 {0,0,0};

	unique_ptr<Texture2D>	tex_folder_icon;
	unique_ptr<Texture2D>	tex_loading_icon;
	unique_ptr<Texture2D>	tex_loading_file_icon;
	unique_ptr<Texture2D>	tex_file_icon;

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
			warning_log("%s\n", e.what());
			return std::move(create_null_texture());
		}

		auto tex = Texture2D::generate();
		tex.upload( img.pixels, img.size );

		tex.gen_mipmaps();
		tex.set_filtering_mipmapped();
		tex.set_border_clamp();

		return std::move(tex);
	}

	enum filetype_e {
		FT_IMAGE_FILE,
		FT_DIRECTORY,
		FT_NON_IMAGE_FILE,
	};

	struct Content {
		str		name;

		virtual filetype_e type () = 0;
		virtual ~Content () {};
	};

	struct File : Content {
		//str	name; // just the file name
		virtual ~File () {};
	};
	struct Non_Image_File : File {
		filetype_e type () { return FT_NON_IMAGE_FILE; };
	};
	struct Image_File : File {
		string	filepath; // the relative or absolute filepath needed to open the file
		
		std::vector<iv2> mip_sizes_px;

		void calc_mip_sizes (iv2 full_size_px) {
			mip_sizes_px.clear();

			iv2 sz = full_size_px;
			for (;;) {
				mip_sizes_px.push_back(sz);

				if (all(sz == 1))
					break;

				sz = max(sz / 2, 1);
			}
		}
		iv2 get_full_size_px () {
			return mip_sizes_px[0];
		}

		filetype_e type () { return FT_IMAGE_FILE; };
	};
	struct Directory_Tree : Content {
		//str		name; // for root: full or relative path of directory +'/',  for non-root: directory name +'/'

		std::vector< unique_ptr<Directory_Tree> >	subdirs;
		std::vector< unique_ptr<File> >				files;

		std::vector<Content*>						content;

		filetype_e type () { return FT_DIRECTORY; };
	};

	void _populate (Directory_Tree* tree, n_find_files::Directory_Tree const& dir, string const& path) {
		
		for (auto& d : dir.dirs) {
			auto subtree = make_unique<Directory_Tree>();

			subtree->name = d.name;
			_populate(subtree.get(), d, path+subtree->name);

			tree->content.emplace_back(subtree.get());

			tree->subdirs.emplace_back( std::move(subtree) );
		}
		for (auto& fn : dir.filenames) {
			string filepath = path + fn;
			
			iv2 size_px;
			bool is_image_file = stbi_info(filepath.c_str(), &size_px.x,&size_px.y, nullptr) != 0;
			
			unique_ptr<File> file;

			if (is_image_file) {
				auto img = make_unique<Image_File>();
				img->name = fn;
				img->filepath = std::move(filepath);

				img->calc_mip_sizes(size_px);

				file = unique_ptr<File>(std::move( img ));
			} else {
				file = unique_ptr<File>(std::move( make_unique<Non_Image_File>() ));
			}

			tree->content.emplace_back( file.get() );
			tree->files.emplace_back( std::move(file) );
		}
	}

	Texture_Streamer				tex_streamer;
	unique_ptr<Directory_Tree>	viewed_dir = nullptr;

	void init () {
		tex_loading_icon = make_unique<Texture2D>( simple_load_texture("assets_src/loading_icon.png") );
		tex_loading_file_icon = make_unique<Texture2D>( simple_load_texture("assets_src/loading_file_icon.png") );
		tex_folder_icon = make_unique<Texture2D>( simple_load_texture("assets_src/folder_icon.png") );
		tex_file_icon = make_unique<Texture2D>( simple_load_texture("assets_src/file_icon.png") );

		tex_streamer.init_thread_pool();
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
			
			tex_streamer.clear();
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

				_populate(viewed_dir.get(), new_dir, viewed_dir->name);

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

		static flt file_icon_sz = 0.70f;
		static flt file_icon_alpha = 0.12f;

		static flt loading_icon_sz = 0.25f;
		static flt loading_icon_alpha = 0.8f;
		
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

			ImGui::DragFloat("file_icon_sz", &file_icon_sz, 0.01f);
			ImGui::DragFloat("file_icon_alpha", &file_icon_alpha, 0.01f);

			ImGui::DragFloat("loading_icon_sz", &loading_icon_sz, 0.01f);
			ImGui::DragFloat("loading_icon_alpha", &loading_icon_alpha, 0.01f);
		}

		tex_streamer.begin_frame();

		static ImGuiTextFilter list_files_filter;
		
		bool list_files = ImGui::CollapsingHeader("file_list");
		if (list_files) {
			list_files_filter.Draw();
		}

		for (int content_i=0; content_i<(dir ? (int)dir->content.size() : 0); content_i++) {
			auto img_instance = [&] (v2 pos_center_rel, flt alpha, bool is_original_instance) {
				if (	pos_center_rel.y < -grid_sz_cells.y/2 -0.5f ||
						pos_center_rel.y > +grid_sz_cells.y/2 +0.5f)
					return;

				v2 pos_center_rel_px = pos_center_rel * cell_sz;

				auto* c = dir->content[content_i];

				auto get_texture_centered_in_cell_onscreen_size = [&] (iv2 img_size_px) { // img_size_px is just used to calc the image aspect ratio, so we can use the full image size to find the actual size onscreen
					v2 border_px = 5;

					auto img_full_size = (v2)img_size_px;
					v2 aspect = img_full_size / max(img_full_size.x, img_full_size.y);
					return (v2)cell_sz * aspect -border_px*2;
				};
				auto draw_texture_centered_in_cell = [&] (Texture2D const& tex, iv2 img_size_px, flt alpha) {
					v2 img_onscreen_sz_px = get_texture_centered_in_cell_onscreen_size(img_size_px);

					v2 offs_to_center_px = (cell_sz -img_onscreen_sz_px) / 2;

					v2 pos_px = view_center +pos_center_rel_px -cell_sz / 2;
					pos_px += offs_to_center_px;

					draw_textured_quad(pos_px, img_onscreen_sz_px, tex, rgba8(255,255,255, (u8)(alpha * 255 +0.5f)));
				};

				if (list_files)
					ImGui::PushID(content_i);

				switch (c->type()) {
					case FT_DIRECTORY: {

						Texture2D* tex = tex_folder_icon.get();
						draw_texture_centered_in_cell(*tex, tex->get_size_px(), alpha);

						if (list_files && is_original_instance && list_files_filter.PassFilter(c->name.c_str())) {
							ImGui::PushItemWidth(-100);
							ImGui::TextBox("##name", c->name);
							ImGui::PopItemWidth();
						}
					} break;

					case FT_NON_IMAGE_FILE: {
						
						Texture2D* tex = tex_file_icon.get();
						draw_texture_centered_in_cell(*tex, tex->get_size_px(), alpha * file_icon_alpha);

						if (list_files && is_original_instance && list_files_filter.PassFilter(c->name.c_str())) {
							ImGui::PushItemWidth(-100);
							ImGui::TextBox("##name", c->name);
							ImGui::PopItemWidth();
						}
					} break;

					case FT_IMAGE_FILE: {
						auto* img = (Image_File*)c;

						iv2 onscreen_size_px = get_texture_centered_in_cell_onscreen_size(img->get_full_size_px());
						
						int mip_level_to_use = 0;
						for (int i=0; i<(int)img->mip_sizes_px.size(); ++i) {
							if (any(img->mip_sizes_px[i] < onscreen_size_px)) {
								mip_level_to_use = i == 0 ? 0 : i -1;
								break;
							}
						}

						iv2 desired_size_px = img->mip_sizes_px[mip_level_to_use];

						Texture2D* tex = tex_streamer.query_image_texture(img->filepath, desired_size_px);

						if (tex) {
							draw_texture_centered_in_cell(*tex, img->get_full_size_px(), alpha);
						} else {
							Texture2D* tex = tex_loading_file_icon.get();
							draw_texture_centered_in_cell(*tex, tex->get_size_px(), alpha * file_icon_alpha);
						}

						if (list_files && is_original_instance && list_files_filter.PassFilter(c->name.c_str())) {
							ImGui::PushItemWidth(-100);
							ImGui::TextBox("##name", c->name);
							ImGui::PopItemWidth();

							ImGui::SameLine();
							auto& sz = img->get_full_size_px();
							ImGui::TextBox("##res", prints("%4d x %4d", sz.x,sz.y) );
						}
					} break;

					default:
						assert_log(false);
				}

				if (list_files)
					ImGui::PopID();
				
				//bool display_loading_icon = false;
				//if (display_loading_icon) {
				//	v2 pos_px = view_center +pos_center_rel_px +cell_sz * (-0.5f +(1 -loading_icon_sz));
				//	draw_textured_quad(pos_px, cell_sz * loading_icon_sz, *tex_loading_icon.get(), rgba8(255,255,255, (int)(alpha * loading_icon_alpha * 255.0f +0.5f)));
				//}
			};

			flt rel_indx = (flt)content_i -view_coord;

			flt quotient;
			flt remainder = mod_range(rel_indx, -grid_sz_cells.x/2, +grid_sz_cells.x/2, &quotient);

			flt out_of_bounds_l = max(-(remainder -0.5f +grid_sz_cells.x/2), 0.0f);
			flt out_of_bounds_r = max(  remainder +0.5f -grid_sz_cells.x/2 , 0.0f);
			
			v2 pos_center_rel = v2(remainder,quotient) -view_offs;

			assert_log((out_of_bounds_l +out_of_bounds_r) <= 1);
			img_instance(pos_center_rel, content_i == (int)roundf(view_coord) ? 1 : 1 -out_of_bounds_l -out_of_bounds_r, true);

			//ImGui::Value("content_i", content_i);
			//ImGui::Value("(int)roundf(view_coord)", (int)roundf(view_coord));

			if (out_of_bounds_l > 0) {
				img_instance((pos_center_rel -v2(-grid_sz_cells.x,1)), out_of_bounds_l, false);
			}
			if (out_of_bounds_r > 0) {
				img_instance((pos_center_rel +v2(-grid_sz_cells.x,1)), out_of_bounds_r, false);
			}

		}
	
		tex_streamer.end_frame();
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
		file_grid(viewed_dir.get(), imgui_left_bar_size.x);
		tex_streamer.async_image_loading(frame_i);

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

		++frame_i; // increment here instead of in mainloop because this we also draw frames on glfw_refresh_callback()
		return false;
	}
};

App	app;

bool glfw_refresh_callback_called_inside_frame_call = false;

void glfw_refresh_callback (GLFWwindow* window) { // refresh callback so window resizing does not freeze rendering
	if (glfw_refresh_callback_called_inside_frame_call)
		return; // prevent recursive calls of frame, this once happened when a failed assert tried to open a messagebox

	app.frame();
}

int main (int argc, char** argv) {
	
	init_engine();

	glfwSetWindowRefreshCallback(disp.window, glfw_refresh_callback);

	imgui_context.init();
	
	app.init();

	bool should_close = false;
	while (!should_close) {
		
		mouse_wheel_diff = 0;
		do_toggle_fullscreen = false;

		glfwPollEvents(); // calls async input callbacks and glfw_refresh_callback ()
		
		// see usage of glfw_refresh_callback_called_inside_frame_call in glfw_refresh_callback
		glfw_refresh_callback_called_inside_frame_call = true;
		
		should_close = app.frame();
		
		glfw_refresh_callback_called_inside_frame_call = false;
	}

	disp.save_window_positioning();

	glfwDestroyWindow(disp.window);
	glfwTerminate();

	return 0;
}
