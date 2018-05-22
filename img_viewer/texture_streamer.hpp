#pragma once

#include <algorithm>

// priority defined as float in [0,+inf], lower is higher prio (+inf is a valid value)
struct Texture_Streamer {

	struct Mipmap {
		flt			priority;
		iv2			size_px;

		enum state_e { UNCACHED, CACHING, CACHED }
					state;

		Mipmap (flt p, iv2 sz, state_e s): priority{p}, size_px{sz}, state{s} {};
	};

	struct Cached_Texture {
		unique_ptr<Texture2D>				tex = nullptr; // gpu texture object, where we are trying to stream the texture into
		int									tex_mips_cached = 0; // how many mip levels are uploaded currently, lower mip levels are never missing (4x4 requiers 2x2 and 1x1 to be uploaded)

		std::vector< unique_ptr<Mipmap> >	lods;

		flt get_cached_pixel_density (iv2 onscreen_size_px) {
			assert((tex_mips_cached == 0) == (tex == nullptr));
			
			if (tex_mips_cached == 0)
				return 0;

			v2 px_dens = (v2)lods[tex_mips_cached -1]->size_px / (v2)onscreen_size_px;
			
			return min(px_dens.x, px_dens.y);
		}

		/*
		void cache_mip (int lod, Mipmap&& mip, u64* memory_size) {
			assert(!lods[lod].image, "mip level already cached");
			assert(resident_lod < mip_level, "mip level not cached yet, but somehow already uploaded");
			assert(all(lods[lod].size_px == mip.size_px), "mip level size does not match");

			memory_size += get_memory_size(*mip.image);

			lods[mip_level] = std::move(mip);

			bool can_upload_mips = true;
			for (int i=(lod -1); i>=resident_lod; --i) {
				if (!lods[i].image) {
					can_upload_mips = false;
					break;
				}
			}

			if (can_upload_mips) {
				for (int i=(lod -1); i>=resident_lod; --i) {
					upload_cached_mip(i);
				}

				resident_lod = lod +1;
			}
		}

		void upload_cached_mip (int mip_level) {
			// TODO: upload
		}
		void evict_mip (int mip_level, u64* memory_size) {
			// TODO: unupload_mip(mip_level); (modify resident_mips)

			memory_size -= get_memory_size(*lods[mip_level].image);

			lods[mip_level].image = nullptr;
		}

		static u64 get_memory_size (Image2D const& img) {
			return (u64)img.size.y * (u64)img.size.x * (u64)img.get_pixel_size();
		}
		*/
	};
	
	std::map< str, unique_ptr<Cached_Texture> >	textures; // key: filepath
	std::vector<Mipmap*>						lods_sorted; // lods sorted by priority

	u64 cache_memory_size_used = 0; // how many bytes of texture data we currently have cached (uploaded as textures or still cached in ram (waiting for upload), does not include temporary memory allocated by mip loader threads)
	u64 cache_memory_size_desired; // how many bytes of texture data we want at max to have uploaded

	struct Threadpool_Job { // input is filepath to file to load
		string				filepath;
		int					lod_indx;
		iv2					size_px;
	};
	struct Threadpool_Result {
		string				filepath;
		int					lod_indx;
		unique_ptr<Image2D>	image;
	};

	struct Threadpool_Processor {
		static Threadpool_Result process_job (Threadpool_Job&& job) {
			Threadpool_Result res;

			res.filepath = std::move(job.filepath);
			res.lod_indx = job.lod_indx;
			res.image = nullptr;

			// load image from disk
			try {
				Image2D src = Image2D::load_from_file(res.filepath);
				
				//Image2D scaled = Image2D::rescale_box_filter(src, job.size_px);
				Image2D scaled = Image2D::rescale_nearest(src, job.size_px);

				res.image = make_unique<Image2D>(std::move( scaled ));

			} catch (Expt_File_Load_Fail const& e) {
				//assert_log(false, e.what()); // is not threadsafe!
				// res.mip.image is still null -> signifies that image was not loaded
			}

			return res;
		}
	};

	Threadpool<Threadpool_Job, Threadpool_Result, Threadpool_Processor> img_loader_threadpool;

	void init_thread_pool () {
		int cpu_threads = (int)std::thread::hardware_concurrency();
		int threads = max(cpu_threads -1, 2);

		img_loader_threadpool.start_threads(threads);
	}

	Cached_Texture* find_texture (string const& filepath) {
		auto it = textures.find(filepath);
		return it != textures.end() ? it->second.get() : nullptr;
	}
	string const* find_texture_filepath_from_lod (Mipmap const* mip, int* lod_indx) { // dumb linear search
		for (auto& t : textures) {
			for (int li=0; li<(int)t.second->lods.size(); ++li) {
				if (t.second->lods[li].get() == mip) {
					*lod_indx = li;
					return &t.first;
				}
			}
		}
		*lod_indx = -1;
		return nullptr;
	}

	Cached_Texture* add_texture (string const& filepath, iv2 full_size_px) {
		
		auto tmp = make_unique<Cached_Texture>();
		auto* tex = tmp.get();
		
		auto ret = textures.emplace(filepath, std::move(tmp));
		assert(ret.second);

		{ // init lods
			iv2 sz = full_size_px;
			for (;;) {
				tex->lods.emplace_back( make_unique<Mipmap>(+INF, sz, Mipmap::UNCACHED) );

				lods_sorted.push_back( tex->lods.back().get() );

				if (all(sz == 1))
					break;

				sz = max(sz / 2, 1);
			}
		}

		return tex;
	}
	void remove_texture () {
		// TODO: remove lods from lods_sorted
	}

	flt calc_priority (iv2 size_px, iv2 needed_size_px) {
		v2 px_dens = (v2)size_px / (v2)needed_size_px;
		return min(px_dens.x, px_dens.y); // use pixel density as priority
	}

	void queries_begin () { // reset priorities for all mips
		for (auto& t : textures) {
			for (auto& l : t.second->lods) {
				l->priority = +INF;
			}
		}

		if (ImGui::CollapsingHeader("Texture_Streamer", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Value("threadpool threads", img_loader_threadpool.get_thread_count());

			ImGui::Value_Bytes("cache_memory_size_used", cache_memory_size_used);

			static f32 sz_in_mb[256] = {};
			static int cur_val = 0;

			if (frame_i % 12 == 0) {
				sz_in_mb[cur_val++] = (flt)cache_memory_size_used / 1024 / 1024;
				cur_val %= ARRLEN(sz_in_mb);
			}

			int tmp = (int)cache_memory_size_desired;
			ImGui::DragInt("cache_memory_size_desired", &tmp, 1024);
			cache_memory_size_desired = (u64)tmp;

			ImGui::PushItemWidth(-1);
			ImGui::PlotLines("##cache_memory_size_used", sz_in_mb, ARRLEN(sz_in_mb), cur_val, "memory_size in MB", 0, (flt)cache_memory_size_desired*1.2f, ImVec2(0,80));
			ImGui::PopItemWidth();

			static bool show_image_loader_threadpool_jobs = false;
			show_image_loader_threadpool_jobs = ImGui::Button("show_image_loader_threadpool_jobs") || show_image_loader_threadpool_jobs;

			if (show_image_loader_threadpool_jobs) {
				ImGui::Begin("Texture_Streamer image loader threadpool jobs", &show_image_loader_threadpool_jobs);

				static ImGuiTextFilter filter;
				filter.Draw();

				auto foreach_job = [] (Threadpool_Job const& job) {
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

	}

	Cached_Texture* query (string const& filepath, iv2 onscreen_size_px, iv2 full_size_px) { // flt priority

		auto* tex = find_texture(filepath);
		if (!tex)
			tex = add_texture(filepath, full_size_px);

		for (auto& l : tex->lods) {
			l->priority = min(l->priority, calc_priority(l->size_px, onscreen_size_px));
		}

		return tex;
	}

	void upload_lod (Cached_Texture* tex, int lod_indx, unique_ptr<Image2D> img) {
		//tex->lods[lod_indx]->state = Mipmap::CACHED;
	}

	void queries_end () {
		
		/*
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

		auto need_to_cancel_job = [&] (Threadpool_Job const& job) {
			auto* tex = find_texture(job.filepath);
			return !job_still_needed(tex, job.size_px);
		};

		img_loader_threadpool.jobs.cancel(need_to_cancel_job);
		*/

		std::stable_sort(lods_sorted.begin(),lods_sorted.end(),
			[] (Mipmap const* l, Mipmap const* r) {
				return l->priority < r->priority;
			});

		if (ImGui::TreeNode("lods_sorted")) {
			
			for (auto& l : lods_sorted) {
				ImGui::Text(prints("%f %d x %d %d", l->priority, l->size_px.x,l->size_px.y, (int)l->state).c_str());
			}

			ImGui::TreePop();
		}

		for (auto& l : lods_sorted) {
			if (l->state == l->UNCACHED) {
				
				int lod_indx;
				auto* tex_filepath = find_texture_filepath_from_lod(l, &lod_indx); // ugh!

				img_loader_threadpool.jobs.push({ *tex_filepath, lod_indx, l->size_px });

				l->state = l->CACHING;
			}
		}

		for (;;) {

			Threadpool_Result res;
			if (!img_loader_threadpool.results.try_pop(&res))
				break; // currently no images loaded async, stop polling

			if (res.image == nullptr) {
				// image could not be loaded, simply pretend it never finished loading for development purposes
			} else {
				auto* tex = find_texture(res.filepath);
				
				upload_lod(tex, res.lod_indx, std::move(res.image));
			}
		}
		
		/*
		for (;;) {

			Threadpool_Result res;
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
		*/
	}
};
