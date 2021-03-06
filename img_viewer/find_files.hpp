#pragma once

#include <string>
using std::string;

#include <vector>

#include "windows.h"

namespace n_find_files {
	class Expt_Path_Not_Found : std::exception {
	public:
		Expt_Path_Not_Found (string path, string api_error): path{path}, api_error{api_error} {
			msg = prints("Path \"%s\" could not be found (file or directory does not exist)!", path.c_str());
		}

		virtual cstr what () const {
			return msg.c_str(); // returned string will go out of scope when this Exception goes out of scope
		}
		string const& get_path () const {
			return path;
		}
		string const& get_api_error () const {
			return api_error;
		}

	private:
		string	msg;
		string	path;
		string	api_error;
	};
	
	struct Directory {
		// contents
		std::vector<str>			dirnames;
		std::vector<str>			filenames;
	};
	struct Directory_Tree {
		str							name;
		// contents
		std::vector<Directory_Tree>	dirs;
		std::vector<str>			filenames;
	};

	void find_files (strcr dir_path, std::vector<str>* dirnames, std::vector<str>* filenames) {
		WIN32_FIND_DATA data;

		assert(dir_path.size() > 0 && dir_path.back() == '/');
		str search_str = dir_path +"*";

		HANDLE hFindFile = FindFirstFile(search_str.c_str(), &data);
		auto err = GetLastError();
		if (hFindFile == INVALID_HANDLE_VALUE) {
			if (err == ERROR_FILE_NOT_FOUND) {
				// no files found
			} else {
				throw Expt_Path_Not_Found(search_str, prints("FindFirstFile failed! [%x]", err));
			}
			FindClose(hFindFile);
			return;
		}

		for (;;) {
		
			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (	strcmp(data.cFileName, ".") == 0 ||
						strcmp(data.cFileName, "..") == 0 ) {
					// found directory represents the current directory or the parent directory, don't include this in the output
				} else {
					dirnames->emplace_back(std::move( str(data.cFileName) +'/' ));
				}
			} else {
				filenames->emplace_back(data.cFileName);
			}

			auto ret = FindNextFile(hFindFile, &data);
			auto err = GetLastError();
			if (ret == 0) {
				if (err == ERROR_NO_MORE_FILES) {
					break;
				} else {
					// TODO: in which cases would this happen?
					assert(false);//, prints("FindNextFile failed! [%x]", err).c_str());
					FindClose(hFindFile);
					return;
				}
			}
		}

		FindClose(hFindFile);

		return;
	}

	// 
	Directory find_files (strcr dir_path) {
		Directory dir;
		find_files(dir_path, &dir.dirnames, &dir.filenames);
		return dir;
	}

	Directory_Tree find_files_recursive (strcr dir_path, strcr dir_name) {
		Directory_Tree		dir;
		dir.name = dir_name;

		std::vector<str>	dirnames;

		assert(dir_path.size() == 0 ||	dir_path.back() == '/');
		assert(dir_name.size() > 1 &&	dir_name.back() == '/');

		str dir_full = dir_path+dir_name;

		find_files(dir_full, &dirnames, &dir.filenames);

		for (auto& d : dirnames) {
			dir.dirs.emplace_back( find_files_recursive(dir_full, d) );
		}
		return dir;
	}
	Directory_Tree find_files_recursive (strcr dir_name) {
		return find_files_recursive("", dir_name);
	}
}
