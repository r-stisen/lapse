// LAPSE - Language-Agnostic subtitle synchronization engine
// Copyright (C) 2026 Rasmus Stisen Jensen (rs-jensen)
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#pragma once
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <filesystem>

void backup_file(const char* path);
void write_srt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s);
void write_ass_OLS(const char* input_path, const char* output_path, double slope, double intercept_s);
void write_vtt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s);
void write_microdvd_OLS(const char* input_path, const char* output_path, double slope, double intercept_s);
void write_srt_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping);
void write_ass_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping);
void write_vtt_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping);
void write_microdvd_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping);
