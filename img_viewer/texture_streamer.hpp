#pragma once

#include <set>
#include <algorithm>

#include <vector>
#include <algorithm>

template <typename T, typename COMPARE=std::less<T> >
struct sorted_vector {
	typedef typename std::vector<T>::iterator       iterator;
	typedef typename std::vector<T>::const_iterator const_iterator;
	
	iterator		begin()       { return v.begin();	}
	iterator		end()         { return v.end();		}
	const_iterator	begin() const { return v.begin();	}
	const_iterator	end()   const { return v.end();		}
	
	std::vector<T>	v;
	COMPARE			cmp;
	
	sorted_vector (COMPARE const& c = COMPARE()): v(), cmp(c) {}

	template <typename ITERATOR>
	sorted_vector (ITERATOR first, ITERATOR last, COMPARE const& c = COMPARE()): v(first, last), cmp(c) {
		std::sort(v.begin(), v.end(), cmp);
	}

	
	size_t size () const {	return v.size(); }

	template <typename T>
	const_iterator find (T const& val) const {
		const_iterator i = std::lower_bound(v.begin(), v.end(), val, cmp);
		return i == v.end() || cmp(val, *i) ? v.end() : i;
	}
	template <typename T>
	iterator find (T const& val) {
		iterator i = std::lower_bound(v.begin(), v.end(), val, cmp);
		return i == v.end() || cmp(val, *i) ? v.end() : i;
	}

	template <typename T>
	bool contains (T const& val) {
		return find(val) != end();
	}

	iterator insert (T val) {
		iterator i = std::lower_bound(v.begin(), v.end(), val, cmp);
		if (i == v.end() || cmp(val, *i))
			return v.emplace(i, std::move(val));
		return v.end();
	}

	iterator find_or_insert (T val) {
		iterator i = std::lower_bound(v.begin(), v.end(), val, cmp);
		if (i == v.end() || cmp(val, *i))
			v.emplace(i, std::move(val));
		return i;
	}

	iterator erase (iterator it) {
		return v.erase(it);
	}

	template <typename U>
	bool try_erase (U const& val) {
		auto it = find(val);
		if (it == v.end())
			return false; // not found, not erased

		erase(it);
		return true; // found, erased
	}
	template <typename U>
	bool try_extract (U const& val, T* out) {
		auto it = find(val);
		if (it == v.end())
			return false; // not found, not erased

		*out = std::move(*it);
		erase(it);
		return true; // found, extracted, erased
	}
};


// Once this is somewhat reuseable, document this system (and make sure all includes are present)

struct Texture_Streamer {
	
	/* Current Approach:
		Find all mip desired count for each texture and have the threadpool always generate all needed mips (since they need to read the full size image anyway) (even if we had access to the mipmaps of a image directly (DDS) or had progressive jpgs or similar, this approach is always at least as fast as the one mipmap per job approach, but it could not allow what i want the system to be able to do)
		When desired mipmap cound gets higher all mips are reloaded and on completion of this job texture is deleted and new one is uploaded (mip images are stored)
		When desired mipmap cound gets lower texture is deleted and new one is generated with the current mips (stored mip is deleted)
		
		cached == uploaded
	*/

	template <typename F>
	static void find_mipmap_sizes_px (iv2 full_size_px, F foreach) {
		iv2 sz = full_size_px;
		for (int i=0;; ++i) {
			
			foreach(i, sz);

			if (all(sz == 1))
				break;

			sz = max(sz / 2, 1);
		}
	}
	static std::vector<Image2D> generate_mipmaps (Image2D&& full_size) { // mips in smallest to biggest order
		std::vector<Image2D> mips;
		find_mipmap_sizes_px(full_size.size, [&] (int i, iv2 size_px) {
				mips.emplace(mips.begin());
				mips.front().size = size_px;
			});

		mips[ mips.size() -1 ] = std::move( full_size );

		for (int i=(int)mips.size()-1 -1; i>=0; --i) { // second last to first
			mips[i] = Image2D::rescale_sample_bilinear(mips[i+1], mips[i].size);
			//mips[i] = Image2D::rescale_box_filter(mips[i+1], mips[i].size);
			//mips[i] = Image2D::rescale_sample_nearest(mips[i+1], mips[i].size);
		}

		return mips;
	}

	struct Cached_Texture {
		string					filepath;
		
		unique_ptr<Texture2D>	tex = nullptr; // gpu texture object, where we are trying to stream the texture into
		
		int						cached_mips = 0;
		int						desired_cached_mips = 0;

		flt						order_priority = +1; // [0,1]

		bool					was_queried = false; // so we only evict textures if none of their mips are cached anymore and they are not queried for one frame (this prevents textures being added and then removed every single frame)
		bool					threadpool_job_queued = false;

		struct Mipmap {
			iv2					size_px;
			unique_ptr<Image2D>	img = nullptr; // cpu copy of image data, since opengl does not allow evicting mipmaps (only whole texture via glDeleteTextures)
			flt					priority = +INF; // highest [0, +inf] lowest

			uptr get_memory_size () const {
				return (uptr)size_px.y * (uptr)size_px.x * sizeof(rgba8);
			}
		};
		std::vector<Mipmap>		mips;

		bool					debug_is_highlighted = false;

		void imgui () {
			if (tex)
				ImGui::Value("tex.gpu_handle", tex->get_gpu_handle());
			else
				ImGui::Text("<null>");

			ImGui::Value("cached_mips", cached_mips);
			ImGui::Value("desired_cached_mips", desired_cached_mips);

			ImGui::Value("order_priority", order_priority);

			ImGui::Value("was_queried", was_queried);
			ImGui::Value("threadpool_job_queried", threadpool_job_queued);
			
			if (ImGui::TreeNode(prints("mips[%d]###mips", (int)mips.size()).c_str())) {
				for (auto& m : mips) {
					
					ImGui::Text("%4d x %4d", m.size_px.x,m.size_px.y);

					ImGui::SameLine();
					ImGui::Text(m.img ? "cached":"null");

					ImGui::SameLine();
					ImGui::Text("%.3f", m.priority);

					ImGui::SameLine();
					ImGui::Value_Bytes("", m.get_memory_size());
				}
				ImGui::TreePop();
			}
		}

		flt get_displayable_pixel_density (iv2 onscreen_size_px) const {
			assert((cached_mips == 0) == (tex == nullptr));
			
			if (cached_mips == 0)
				return 0;

			v2 px_dens = (v2)mips[cached_mips -1].size_px / (v2)onscreen_size_px;
			
			return min(px_dens.x, px_dens.y);
		}
		bool all_mips_displayable () const {
			return cached_mips == (int)mips.size();
		}

	};
	struct Cached_Texture_Less { // for sorted_vector
		inline bool operator() (Cached_Texture const& l,	Cached_Texture const& r) const {	return std::less<string>()(l.filepath, r.filepath); }
		inline bool operator() (string const& l_filepath,	Cached_Texture const& r) const {	return std::less<string>()(l_filepath, r.filepath); }
		inline bool operator() (Cached_Texture const& l,	string const& r_filepath) const {	return std::less<string>()(l.filepath, r_filepath); }
	};

	void imgui_texture_info (string const& filepath) {
		auto* tex = find_texture(filepath);
		if (tex)
			tex->imgui();
	}

	sorted_vector<Cached_Texture, Cached_Texture_Less>	textures; // key: filepath

	uptr cache_memory_size_used = 0; // how many bytes of texture data we currently have cached (uploaded as textures or still cached in ram (waiting for upload), does not include temporary memory allocated by mip loader threads)
	uptr cache_memory_size_desired = 500 * 1024*1024; // how many bytes of texture data we want at max to have uploaded

	Cached_Texture* find_texture (string const& filepath) {
		auto it = textures.find(filepath);
		return it != textures.end() ? &*it : nullptr;
	}

	Cached_Texture* add_texture (string filepath, iv2 full_size_px) {
		
		Cached_Texture tmp;
		tmp.filepath = std::move(filepath);

		auto tex = textures.insert(std::move(tmp));
		assert(tex != textures.end());

		find_mipmap_sizes_px(full_size_px, [&] (int i, iv2 size_px) {
				tex->mips.emplace( tex->mips.begin() );
				tex->mips.front().size_px = size_px;
			});

		return &*tex;
	}

	// Update the texture object by replacing it with a new one and uploading the stored mipmap images to it
	void update_texture_object (Cached_Texture* tex) {

		tex->tex = nullptr;
		
		if (tex->cached_mips > 0) {

			tex->tex = make_unique<Texture2D>(std::move( Texture2D::generate() ));

			tex->tex->set_filtering_mipmapped();
			tex->tex->set_border_clamp();

			auto to_opengl_mip_index = [&] (int i) -> int { // !!! uses tex->cached_mips
				assert(i >= 0 && i < tex->cached_mips);
				return tex->cached_mips -1 -i;
			};

			for (int i=0; i<tex->cached_mips; ++i) {
				assert(tex->mips[i].img != nullptr);
				assert(all(tex->mips[i].img->size == tex->mips[i].size_px));

				tex->tex->upload_mipmap(to_opengl_mip_index(i), tex->mips[i].img->pixels, tex->mips[i].img->size);
			}

			tex->tex->set_active_mips(0, tex->cached_mips -1);
		}
	}

	// ONLY a helper function!! evict a singe mipmap from being cached by deleting the local mipmap image
	void evict_mip (Cached_Texture* tex, int mip_indx) { // !!! cached_mips not updated
		assert(mip_indx < tex->cached_mips);
		assert(tex->mips[mip_indx].img != nullptr);
		
		tex->mips[mip_indx].img = nullptr;
		cache_memory_size_used -= tex->mips[mip_indx].get_memory_size();
	}

	// evict all mips that do no longer count as desired_cached_mips
	void evict_undesired_mips (Cached_Texture* tex) {
		for (int i=tex->desired_cached_mips; i<tex->cached_mips; ++i) {
			evict_mip(tex, i);
		}
		tex->cached_mips = tex->desired_cached_mips;

		for (int i=tex->cached_mips; i<(int)tex->mips.size(); ++i) {
			assert(!tex->mips[i].img);
		}

		update_texture_object(tex);
	}

	// evicts all mips
	void evict_all_mips (Cached_Texture* tex) {
		for (int i=0; i<tex->cached_mips; ++i) {
			evict_mip(tex, i);
		}
		tex->cached_mips = 0;

		update_texture_object(tex);
	}

	// cache new mip data
	void cache_mips (Cached_Texture* tex, std::vector<Image2D> new_mips) {
		assert(tex->mips.size() == new_mips.size()); // image could have been resized while the app was running // TODO handle this later (simply update the list of mips each time we upload_mips() -> should be a good solution to images being updated while the app is running (update_mips() is basicly a full image update))
		assert(tex->desired_cached_mips >= 0 && tex->desired_cached_mips <= new_mips.size());

		evict_all_mips(tex);

		tex->cached_mips = min(tex->desired_cached_mips, (int)new_mips.size());
		
		for (int i=0; i<tex->cached_mips; ++i) {
			assert(tex->mips[i].img == nullptr);
			assert(all(tex->mips[i].size_px == new_mips[i].size)); // see above

			tex->mips[i].img = make_unique<Image2D>(std::move(new_mips[i]));
			cache_memory_size_used += tex->mips[i].get_memory_size();
		}

		update_texture_object(tex);
	}

	decltype(textures)::iterator remove_texture (decltype(textures)::iterator it) {
		evict_all_mips(&*it);
		return textures.erase(it);
	}

	void clear_cache () {
		for (auto t=textures.begin(); t!=textures.end();) {
			t = remove_texture(t);
		}

		img_loader_threadpool.jobs.cancel_all();

		assert(textures.size() == 0);
		assert(cache_memory_size_used == 0);
	}

	struct Threadpool_Job { // input is filepath to file to load
		string					filepath;
	};
	struct Threadpool_Result {
		string					filepath;
		std::vector<Image2D>	mip_images;
	};

	struct Threadpool_Processor {
		static Threadpool_Result process_job (Threadpool_Job&& job) {
			Threadpool_Result res;

			res.filepath = std::move(job.filepath);
			
			// load image from disk
			try {
				Image2D src = Image2D::load_from_file(res.filepath);
				
				res.mip_images = generate_mipmaps( std::move(src) );

			} catch (Expt_File_Load_Fail const& e) {
				// signifies that image was not loaded
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

	flt calc_priority (iv2 size_px, iv2 needed_size_px, flt order_priority) {
		v2 px_dens = (v2)size_px / (v2)needed_size_px;
		return min(px_dens.x, px_dens.y) * lerp(1, 1.25f, order_priority); // use pixel density as priority and bias by desired "order"
	}

	void queries_begin () { // reset priorities for all mips
		for (auto& t : textures) {
			t.order_priority = +INF;
			t.was_queried = false;
			for (auto& m : t.mips) {
				m.priority = +INF;
			}
		}
	}

	Cached_Texture* query (string const& filepath, iv2 onscreen_size_px, iv2 full_size_px, flt order_priority) { // priority_bias [0,1]

		auto* tex = find_texture(filepath);
		if (!tex)
			tex = add_texture(filepath, full_size_px);

		tex->order_priority = min(tex->order_priority, order_priority);
		tex->was_queried = true;

		for (auto& m : tex->mips) {
			m.priority = min(m.priority, calc_priority(m.size_px, onscreen_size_px, order_priority));
		}

		return tex;
	}

	void queries_end () {
		
		// Create list of all mipmaps
		struct Mip {
			Cached_Texture*					tex;
			int								mip_indx;
		};
		std::vector<Mip> mips_sorted;

		for (auto& t : textures) {
			
			t.desired_cached_mips = 0; // recalc this later by max'ing it
			
			for (int mip_indx=0; mip_indx<(int)t.mips.size(); ++mip_indx) {
				mips_sorted.push_back({&t, mip_indx});
			}

		}

		// sort it by the priority that was calculated when the textures were queried
		std::stable_sort(mips_sorted.begin(),mips_sorted.end(),
			[] (Mip const& l, Mip const& r) {
				return l.tex->mips[l.mip_indx].priority < r.tex->mips[r.mip_indx].priority;
			});

		// recalculate desired_cached_mips for each texture
		uptr memory_size_total = 0;

		for (auto& m : mips_sorted) {
			uptr mip_sz = m.tex->mips[m.mip_indx].get_memory_size();

			bool cached_desired = (memory_size_total +mip_sz) <= cache_memory_size_desired;

			if (cached_desired) {
				memory_size_total += mip_sz;

				m.tex->desired_cached_mips = max(m.tex->desired_cached_mips, m.mip_indx +1);
			}
		}

		// 
		flt upload_time;
		auto t_begin = glfwGetTime();

		sorted_vector<string> jobs_to_cancel;

		for (auto t=textures.begin(); t!=textures.end();) {
			
			bool texture_erased = false;
			
			if (t->desired_cached_mips == 0 && !t->was_queried) {
				// evict whole texture

				if (t->threadpool_job_queued) {
					jobs_to_cancel.insert(t->filepath);
				}

				t = remove_texture(t);
				texture_erased = true;
			} else if (t->desired_cached_mips == t->cached_mips) {
				
				// nothing to do

				if (t->threadpool_job_queued) {
					jobs_to_cancel.insert(t->filepath);
				}
				
			} else if (t->desired_cached_mips > t->cached_mips) {
				// recaching_desired
				
				if (t->threadpool_job_queued) {
					// job is already queued, nothing to do
				} else {
					img_loader_threadpool.jobs.push({ t->filepath });
					t->threadpool_job_queued = true;
				}

			} else if (t->desired_cached_mips < t->cached_mips) {
				// evicting_desired

				if (t->threadpool_job_queued) {
					jobs_to_cancel.insert(t->filepath);
				}

				evict_undesired_mips(&*t);
			}

			assert((sptr)cache_memory_size_used >= 0);

			if (!texture_erased)
				++t;
		}

		if (jobs_to_cancel.size() != 0) {
			img_loader_threadpool.jobs.cancel([&] (Threadpool_Job const& job) {
				bool cancel = jobs_to_cancel.contains(job.filepath);
				if (cancel) {
					auto* tex = find_texture(job.filepath);
					if (tex)
						tex->threadpool_job_queued = false;
				}
				return cancel;
			});
		}
		img_loader_threadpool.jobs.sort([&] (Threadpool_Job const& l, Threadpool_Job const& r) {
			auto* lt = find_texture(l.filepath);
			auto* rt = find_texture(r.filepath);

			// BUG: TODO: why do these sometimes trigger even though they should be impossible??
			//assert(lt); // since we cancelled the ones that dont exist anymore
			//assert(rt);

			return (lt ? lt->order_priority : +INF) < (rt ? rt->order_priority : +INF);
		});
		
		// 
		for (;;) {
		
			Threadpool_Result res;
			if (!img_loader_threadpool.results.try_pop(&res))
				break; // currently no images loaded async, stop polling
			
			auto* tex = find_texture(res.filepath);
			
			if (!tex) {
				// texture not cached anymore, was evicted, ignore result
			} else if (res.mip_images.size() == 0) {
				// image could not be loaded
				//assert(tex->threadpool_job_queued); // BUG: TODO: This triggers? threadpool_job_queued should be 100% reliable according to my logic, bug in threadsafe queue ??
				tex->threadpool_job_queued = false;
			} else {
				//assert(tex->threadpool_job_queued);
				tex->threadpool_job_queued = false;

				cache_mips(tex, std::move(res.mip_images));
			}
				
			assert((sptr)cache_memory_size_used >= 0);

			auto t_now = glfwGetTime();
			auto t_elapsed = t_now -t_begin;
			if (t_elapsed > 0.005f)
				break; // limit uploads

		}
		
		auto t_end = glfwGetTime();
		upload_time = (flt)(t_end -t_begin);


		if (ImGui::CollapsingHeader("Texture_Streamer", ImGuiTreeNodeFlags_DefaultOpen)) {
			
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

			flt tmp = (flt)cache_memory_size_desired / 1024 / 1024;
			ImGui::DragFloat("cache_memory_size_desired", &tmp, 1.0f/16, 0,+INF, "%.5f MB");
			cache_memory_size_desired = (uptr)roundf(tmp * 1024 * 1024);

			ImGui::Value_Bytes("cache_memory_size_desired", cache_memory_size_desired);

			ImGui::PushItemWidth(-1);
			ImGui::PlotLines("##cache_memory_size_used", sz_in_mb, ARRLEN(sz_in_mb), cur_val, "memory_size in MB", 0, (flt)cache_memory_size_desired/1024/1024 *1.2f, ImVec2(0,80));
			ImGui::PopItemWidth();

			static bool window_texture = false;
			ImGui::Checkbox("Texture Window", &window_texture);

			if (window_texture) {
				if (ImGui::Begin("Texture Window", &window_texture)) {
					
					static sorted_vector<string> keep_showing_evicted_textures;
					
					static ImGuiTextFilter filter;
					filter.Draw();

					for (auto& t : textures) {
						keep_showing_evicted_textures.find_or_insert(t.filepath);
					}

					for (auto& filepath : keep_showing_evicted_textures) {
						
						if (!filter.PassFilter(filepath.c_str()))
							continue;
						
						auto* tex = find_texture(filepath);
						bool texture_evicted = !tex;

						Cached_Texture dummy;

						if (texture_evicted) {
							tex = &dummy; // show dummy data if texture is not cached anymore
							ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
						}

						if (ImGui::TreeNode( prints("%-70s %2d %8.3f ###%s", filepath.c_str(), tex->desired_cached_mips, tex->order_priority, filepath.c_str()).c_str() )) {
							
							tex->imgui();

							ImGui::TreePop();
						}

						if (texture_evicted) {
							ImGui::PopStyleColor();
						}
					}

				}
				ImGui::End();
			}

			static bool window_threadpool_jobs = false;
			ImGui::Checkbox("Threadpool Jobs Window", &window_threadpool_jobs);

			if (window_threadpool_jobs) {
				if (ImGui::Begin("Threadpool Jobs Window", &window_threadpool_jobs)) {

					static ImGuiTextFilter filter;
					filter.Draw();

					auto foreach_job = [] (Threadpool_Job const& job) {
						if (!filter.PassFilter(job.filepath.c_str()))
							return;

						ImGui::Text(job.filepath.c_str());
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

			{
				static flt upload_times[256] = {};
				static int upload_times_cur = 0;

				upload_times[upload_times_cur++] = upload_time * 1000;
				upload_times_cur %= ARRLEN(upload_times);

				ImGui::PushItemWidth(-1);
				ImGui::PlotLines("##upload_times", upload_times, ARRLEN(upload_times), upload_times_cur, "upload_times in ms", 0, 16.0f*1.2f, ImVec2(0, 80));
				ImGui::PopItemWidth();
			}
		}
	}
};
