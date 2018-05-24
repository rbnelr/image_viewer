#pragma once

#include <algorithm>

// priority defined as float in [0,+inf], lower is higher prio (+inf is a valid value)
struct Texture_Streamer {
	
	struct Cached_Texture;

	struct Mipmap {
		enum state_e { UNCACHED=0, CACHING, CACHED }; // UNCACHED = not loaded at all,  CACHING = in job queue or in processing or in results queue,  CACHED = uplaoded to texture
		static constexpr cstr state_e_str[] = { "UNCACHED", "CACHING", "CACHED" };

		flt			priority;
		iv2			size_px;

		state_e		state;
		state_e		desired_state; // only for displaying in imgui for debugging purposes

		// these are needed because we work with 'std::vector<Mipmap*> mips_sorted' and finding the texture that each Mipmap belongs to was a performance issue in debug mode
		int				mip_indx;
		std::map< str, unique_ptr<Cached_Texture> >::value_type*	tex; // texture this mipmap belongs to

		u64 get_memory_size () const {
			return (u64)size_px.y * (u64)size_px.x * sizeof(rgba8);
		}
	};

	struct Cached_Texture {
		unique_ptr<Texture2D>	tex = nullptr; // gpu texture object, where we are trying to stream the texture into
		int						tex_displayable_mips = 0; // how many mip levels are displayable (mip and all mips below it are uploaded, since 4x4 can complete and be uploaded before 2x2 and 1x1 are, which means we can actually diplay no mips at all)

		std::vector<Mipmap>		mips; // stored in smaller -> bigger order (whereas opengl has mip indexes from biggest to smallest) to make code simpler

		bool					debug_is_highlighted = false;

		int to_opengl_mip_index (int my_indx) {
			assert(my_indx >= 0 && my_indx < (int)mips.size());
			return (int)mips.size() -my_indx -1;
		}

		void imgui () {
			if (tex)
				ImGui::Value("tex.gpu_handle", tex->get_gpu_handle());
			else
				ImGui::Text("<null>");

			ImGui::Value("tex_displayable_mips", tex_displayable_mips);
				
			if (ImGui::TreeNode(prints("mips[%d]###mips", (int)mips.size()).c_str())) {
				for (auto& m : mips) {
						
					ImGui::Text("%.5f", m.priority);

					ImGui::SameLine();
					ImGui::Text("%4d x %4d", m.size_px.x,m.size_px.y);

					ImGui::SameLine();
					ImGui::Text(Mipmap::state_e_str[m.state]);

					ImGui::SameLine();
					ImGui::Text(Mipmap::state_e_str[m.desired_state]);
				}
				ImGui::TreePop();
			}
		}

		flt get_displayable_pixel_density (iv2 onscreen_size_px) const {
			if (tex_displayable_mips > 0) assert(tex != nullptr);
			
			if (tex_displayable_mips == 0)
				return 0;

			v2 px_dens = (v2)mips[tex_displayable_mips -1].size_px / (v2)onscreen_size_px;
			
			return min(px_dens.x, px_dens.y);
		}
		bool all_mips_displayable () const {
			return tex_displayable_mips == (int)mips.size();
		}

		unique_ptr<Texture2D> gen_texture () {
			auto tex = make_unique<Texture2D>(std::move( Texture2D::generate() ));

			tex->set_filtering_mipmapped();
			tex->set_border_clamp();

			tex->set_active_mips(to_opengl_mip_index(0), to_opengl_mip_index(0));

			return tex;
		}

		void update_displayable_mips () {
			int counter = 0;
			for (auto& m : mips) {
				if (m.state != Mipmap::CACHED) break;
				counter++;
			}

			tex_displayable_mips = counter;
			
			tex->set_active_mips(
				to_opengl_mip_index(max(tex_displayable_mips -1, 0)), // guard against tex_displayable_mips==0, the 1x1 mipmap will still be shown when rendering the texture, but the user should not actually draw the texture since get_displayable_pixel_density() will return 0
				to_opengl_mip_index(0));
		}

		void upload_mip (int mip_indx, unique_ptr<Image2D> img, u64* cache_memory_size_used) {
			assert(mips[mip_indx].state == Mipmap::CACHING);
			assert(tex_displayable_mips <= mip_indx);
			if (tex_displayable_mips == mip_indx) {
				for (int i=0; i<mip_indx; ++i) {
					assert(mips[i].state == Mipmap::CACHED);
				}
			}

			if (!tex)
				tex = gen_texture();
			
			if (0) {
				static rgba8 colors[] = {
					rgba8(255,  0,  0, 255),
					rgba8(  0,255,  0, 255),
					rgba8(  0,  0,255, 255),
					rgba8(  0,255,255, 255),
					rgba8(255,  0,255, 255),
					rgba8(255,255,  0, 255),
					rgba8(127,127,127, 255),
					rgba8(255,127,127, 255),
					rgba8(127,255,127, 255),
					rgba8(127,127,255, 255),
				};

				Image2D mip_visualize_img = Image2D::allocate(img->size);

				for (int y=0; y<mip_visualize_img.size.y; ++y) {
					for (int x=0; x<mip_visualize_img.size.x; ++x) {
						mip_visualize_img.get_pixel(x,y) = colors[ mip_indx % ARRLEN(colors) ];
					}
				}

				img = make_unique<Image2D>(std::move(mip_visualize_img));
			}

			tex->upload_mipmap(to_opengl_mip_index(mip_indx), img->pixels, img->size);

			mips[mip_indx].state = Mipmap::CACHED;

			update_displayable_mips();

			*cache_memory_size_used += mips[mip_indx].get_memory_size();

		}

		void evict_mip (int mip_indx, u64* cache_memory_size_used) {
			#if 0
			assert(mips[mip_indx].state == Mipmap::CACHED);
			assert(tex);

			{ // create a new texture, copy all mipmaps except the one we are deleting, and replace the texture with the new one
				auto new_tex = gen_texture();

				/*
				struct Mipmap_Copier {
					GLuint	fbo;
				
					Mipmap_Copier () {
						glGenFramebuffers(1, &fbo);
					}
					~Mipmap_Copier () {
						glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
						glDeleteFramebuffers(1, &fbo);
					}

					void copy_mipmap (GLuint dst_tex, GLuint src_tex, int mip, iv2 size_px) {

						glBindTexture(GL_TEXTURE_2D, src_tex);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, mip);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip);
						
						glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
						glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, src_tex, mip);

						GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
						assert(status == GL_FRAMEBUFFER_COMPLETE);

						glReadBuffer(GL_COLOR_ATTACHMENT0);

						glBindTexture(GL_TEXTURE_2D, dst_tex);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, mip);
						glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mip);

						glTexImage2D(GL_TEXTURE_2D, mip, GL_RGBA8, size_px.x,size_px.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
						glCopyTexSubImage2D(GL_TEXTURE_2D, mip, 0,0, 0,0, size_px.x,size_px.y);

						glBindTexture(GL_TEXTURE_2D, 0);
					}
				};

				Mipmap_Copier mc;
			
				for (int i=0; i<(int)mips.size(); ++i) {
					if (i != mip_indx && mips[i].state == Mipmap::CACHED)
						mc.copy_mipmap(new_tex->get_gpu_handle(), tex->get_gpu_handle(), to_opengl_mip_index(mip_indx), mips[mip_indx].size_px);
				}
				*/


			}

			mips[mip_indx].state = Mipmap::UNCACHED;

			if (tex_displayable_mips > mip_indx) {
				tex_displayable_mips = mip_indx;
			}
			
			// need to set the active mips always, since this is a new texture
			tex->set_active_mips(
				to_opengl_mip_index(max(tex_displayable_mips -1, 0)), // guard against tex_displayable_mips==0, the 1x1 mipmap will still be shown when rendering the texture, but the user should not actually draw the texture since get_displayable_pixel_density() will return 0
				to_opengl_mip_index(0));
			#else
			assert(mips[mip_indx].state == Mipmap::CACHED);

			auto sz = mips[mip_indx].size_px;

			// it seems you just can't evict a mipmap that you already uploaded, i'm getting horrible garbage textures and the debugger just gives up and displays no mipmaps at all most of the time
			//glBindTexture(GL_TEXTURE_2D, tex->get_gpu_handle());
			//glTexImage2D(GL_TEXTURE_2D, to_opengl_mip_index(mip_indx), GL_RGBA8, sz.x,sz.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
			//glBindTexture(GL_TEXTURE_2D, 0);

			mips[mip_indx].state = Mipmap::UNCACHED;

			update_displayable_mips();

			*cache_memory_size_used -= mips[mip_indx].get_memory_size();
			#endif
		}
	};
	void imgui_texture_info (string const& filepath) {
		auto* tex = find_texture(filepath);
		if (tex)
			tex->imgui();
	}

	std::map< str, unique_ptr<Cached_Texture> >	textures; // key: filepath
	std::vector<Mipmap*>						mips_sorted; // mips sorted by priority

	u64 cache_memory_size_used = 0; // how many bytes of texture data we currently have cached (uploaded as textures or still cached in ram (waiting for upload), does not include temporary memory allocated by mip loader threads)
	u64 cache_memory_size_desired = 8 * 1024*1024; // how many bytes of texture data we want at max to have uploaded

	struct Threadpool_Job { // input is filepath to file to load
		string				filepath;
		int					mip_indx;
		iv2					size_px;
	};
	struct Threadpool_Result {
		string				filepath;
		int					mip_indx;
		unique_ptr<Image2D>	image;
	};

	struct Threadpool_Processor {
		static Threadpool_Result process_job (Threadpool_Job&& job) {
			Threadpool_Result res;

			res.filepath = std::move(job.filepath);
			res.mip_indx = job.mip_indx;
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
		
		if (cpu_threads == 12) {
			cpu_threads = 12 -4;
		} else if (cpu_threads == 4) {
			cpu_threads = 3;
		} else {
			cpu_threads -= 1;
		}

		int threads = max(cpu_threads, 2);

		img_loader_threadpool.start_threads(threads);
	}

	Cached_Texture* find_texture (string const& filepath) {
		auto it = textures.find(filepath);
		return it != textures.end() ? it->second.get() : nullptr;
	}

	Cached_Texture* add_texture (string const& filepath, iv2 full_size_px) {
		
		auto tmp = make_unique<Cached_Texture>();
		auto* tex = tmp.get();
		
		auto& ret = textures.emplace(filepath, std::move(tmp));
		assert(ret.second);

		{ // init mips
			iv2 sz = full_size_px;
			for (;;) {
				Mipmap mip;
				mip.priority = +INF;
				mip.size_px = sz;
				mip.state = Mipmap::UNCACHED;
				//mip.mip_indx in loop below
				mip.tex = &*ret.first;
				
				tex->mips.insert(tex->mips.begin(), mip); // build list of mips to go from smallest (1x1) to biggest

				if (all(sz == 1))
					break;

				sz = max(sz / 2, 1);
			}
			// std::vector of mips will not change after this point
			for (int i=0; i<(int)tex->mips.size(); ++i) {
				tex->mips[i].mip_indx = i;
				mips_sorted.push_back( &tex->mips[i] ); // safe to take pointer
			}
		}

		return tex;
	}
	void remove_texture () {
		// TODO: remove mips from mips_sorted
	}

	void clear_cache () {
		
	}

	flt calc_priority (iv2 size_px, iv2 needed_size_px) {
		v2 px_dens = (v2)size_px / (v2)needed_size_px;
		return min(px_dens.x, px_dens.y); // use pixel density as priority
	}

	bool imgui_header_open;
	void queries_begin () { // reset priorities for all mips
		for (auto& t : textures) {
			for (auto& l : t.second->mips) {
				l.priority = +INF;
			}
		}

		imgui_header_open = ImGui::CollapsingHeader("Texture_Streamer", ImGuiTreeNodeFlags_DefaultOpen);
		if (imgui_header_open) {
			if (ImGui::Button("Clear cache"))
				clear_cache();
			
			ImGui::Value("threadpool threads", img_loader_threadpool.get_thread_count());

			ImGui::Value_Bytes("cache_memory_size_used", cache_memory_size_used);

			static f32 sz_in_mb[256] = {};
			static int cur_val = 0;

			if (frame_i % 12 == 0) {
				sz_in_mb[cur_val++] = (flt)cache_memory_size_used / 1024 / 1024;
				cur_val %= ARRLEN(sz_in_mb);
			}

			flt tmp = (flt)cache_memory_size_desired / 1024 /1024;
			ImGui::DragFloat("cache_memory_size_desired", &tmp, 1.0f/16, 0,0, "%.2f MB");
			cache_memory_size_desired = (u64)roundf(tmp * 1024 * 1024);

			ImGui::PushItemWidth(-1);
			ImGui::PlotLines("##cache_memory_size_used", sz_in_mb, ARRLEN(sz_in_mb), cur_val, "memory_size in MB", 0, (flt)cache_memory_size_desired*1.2f, ImVec2(0,80));
			ImGui::PopItemWidth();

			static bool show_image_loader_threadpool_jobs = false;
			ImGui::Checkbox("show_image_loader_threadpool_jobs", &show_image_loader_threadpool_jobs);

			if (show_image_loader_threadpool_jobs) {
				if (ImGui::Begin("Texture_Streamer image loader threadpool jobs", &show_image_loader_threadpool_jobs)) {

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
				}
				ImGui::End();
			}
		}

	}

	Cached_Texture* query (string const& filepath, iv2 onscreen_size_px, iv2 full_size_px) { // flt priority

		auto* tex = find_texture(filepath);
		if (!tex)
			tex = add_texture(filepath, full_size_px);

		for (auto& m : tex->mips) {
			m.priority = min(m.priority, calc_priority(m.size_px, onscreen_size_px));
		}

		return tex;
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

		if (ImGui::TreeNode("textures")) {

			for (auto& t : textures) {
				bool open = ImGui::TreeNode(t.first.c_str());
				t.second->debug_is_highlighted = ImGui::IsItemHovered();

				if (open) {
					t.second->imgui();
					ImGui::TreePop();
				}
			}

			ImGui::TreePop();
		}

		img_loader_threadpool.jobs.cancel_all_and_call_foreach([&] (Threadpool_Job const& job) { // cancel all jobs, so we can re-push them (which mips should be loaded and in which order)
			auto* tex = find_texture(job.filepath);
			
			assert(tex->mips[job.mip_indx].state == Mipmap::CACHING);
			tex->mips[job.mip_indx].state = Mipmap::UNCACHED;
		});

		std::stable_sort(mips_sorted.begin(),mips_sorted.end(),
			[] (Mipmap const* l, Mipmap const* r) {
				return l->priority < r->priority;
			});

		bool mips_sorted_open = imgui_header_open && ImGui::TreeNode("mips_sorted");
		if (mips_sorted_open) {

			ImGui::Columns(5, NULL, false);
			ImGui::Text("filepath");
			ImGui::NextColumn();

			ImGui::Text("priority");
			ImGui::NextColumn();

			ImGui::Text("size_px");
			ImGui::NextColumn();

			ImGui::Text("state");
			ImGui::NextColumn();

			ImGui::Text("desired state");
			ImGui::NextColumn();
		}

		u64 memory_size_total = 0;

		for (auto& m : mips_sorted) {
			u64 mip_sz = m->get_memory_size();

			m->desired_state = (memory_size_total +mip_sz) <= cache_memory_size_desired ? Mipmap::CACHED : Mipmap::UNCACHED;

			if (m->desired_state == Mipmap::CACHED)
				memory_size_total += mip_sz;

			if (m->desired_state == Mipmap::CACHED) {
				switch(m->state) {
					case Mipmap::UNCACHED: {
						img_loader_threadpool.jobs.push({ m->tex->first, m->mip_indx, m->size_px });

						m->state = m->CACHING;
					} break;
					case Mipmap::CACHING:	break; // Already caching, wait for it to complete
					case Mipmap::CACHED:	break; // Already cached, nothing to do
					default: assert(false);
				}
			} else {
				switch(m->state) {
					case Mipmap::UNCACHED:	break; // Not cached yet, nothing to do
					case Mipmap::CACHING: {
						// TODO: cancel or ignore when it completes  OR  just go through the normal process of uploading when it completed (it becomes CACHED) so it gets evicted one frame later
					} break;
					case Mipmap::CACHED: {
						m->tex->second->evict_mip(m->mip_indx, &cache_memory_size_used);
					} break;
					default: assert(false);
				}
			}

			if (mips_sorted_open) {
				ImGui::Text("%10s", m->tex->first.c_str());
				ImGui::NextColumn();

				ImGui::Text("%.5f", m->priority);
				ImGui::NextColumn();

				ImGui::Text("%4d x %4d", m->size_px.x,m->size_px.y);
				ImGui::NextColumn();

				ImGui::Text(Mipmap::state_e_str[m->state]);
				ImGui::NextColumn();

				ImGui::Text(Mipmap::state_e_str[m->desired_state]);
				ImGui::NextColumn();
			}
			
		}

		if (mips_sorted_open) {

			ImGui::Columns(1);

			ImGui::TreePop();
		}

		auto t_begin = glfwGetTime();

		for (;;) {
		
			Threadpool_Result res;
			if (!img_loader_threadpool.results.try_pop(&res))
				break; // currently no images loaded async, stop polling
			
			if (res.image == nullptr) {
				// image could not be loaded, simply pretend it never finished loading for development purposes
			} else {
				auto* tex = find_texture(res.filepath);
				
				tex->upload_mip(res.mip_indx, std::move(res.image), &cache_memory_size_used);
			}

			auto t_now = glfwGetTime();
			auto t_elapsed = t_now -t_begin;
			if (t_elapsed > 0.005f)
				break; // limit uploads

		}
		
		auto t_end = glfwGetTime();

		flt upload_time = (flt)(t_end -t_begin);
		
		if (imgui_header_open) {
			/*
			struct imgui_exponential_moving_average {
				flt	 value;
				flt	 alpha;
				
				static imgui_exponential_moving_average from_initial_value (flt val, flt alpha=0.5f) {
					return {val, alpha};
				}

				void update (cstr id, flt current_val) {
					value = lerp(value, current_val, alpha);

					ImGui::Value(id, value * 1000, "%.3f ms");
				}
			};
			static auto upload_time_avg = imgui_exponential_moving_average::from_initial_value(upload_time);
			upload_time_avg.update("upload_time_avg", upload_time);

			*/

			static flt upload_times[256] = {};
			static int upload_times_cur = 0;

			upload_times[upload_times_cur++] = upload_time * 1000;
			upload_times_cur %= ARRLEN(upload_times);

			ImGui::PushItemWidth(-1);
			ImGui::PlotLines("##upload_times", upload_times, ARRLEN(upload_times), upload_times_cur, "upload_times in ms", 0, 16.0f*1.2f, ImVec2(0, 80));
			ImGui::PopItemWidth();
		}
	}
};
