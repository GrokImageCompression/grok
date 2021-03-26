/*
 *    Copyright (C) 2016-2021 Grok Image Compression Inc.
 *
 *    This source code is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This source code is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <FileProvider.h>


namespace grk {

FileProvider::FileProvider(std::string directoryPath) : dir(nullptr) {
	dir = opendir(directoryPath.c_str());
	if (!dir)
		spdlog::error("Could not open Folder {}", directoryPath);
}

FileProvider::~FileProvider() {
	if (dir)
		closedir(dir);
}
bool FileProvider::next(std::string &res){
	if (!dir)
		return false;

	dirent *content;
	while ((content = readdir(dir)) != nullptr) {
		if (strcmp(".", content->d_name) == 0
				|| strcmp("..", content->d_name) == 0)
			continue;
		res =  content->d_name;
		return true;
	}
	if (dir)
		closedir(dir);
	dir = nullptr;

	return false;
}

}
