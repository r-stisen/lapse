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
#include "charset.h"

int parse_timestamp(const std::string& line, size_t from);
std::string load_text(const std::string& path, Charset* was = nullptr);
std::string trim(const std::string& s);

std::vector<size_t> ass_commas(const std::string& line);
bool ass_field(const std::string& line, const std::vector<size_t>& commas, int index, size_t& from, size_t& len);
std::pair<int,int> ass_time_columns(const std::string& format_line);

std::vector<std::pair<int,int>> read_subtitle(const std::string& path);
std::vector<std::pair<int, int>> read_srt(const char* filename);
std::vector<std::pair<int, int>> read_ass(const char* filename);
std::vector<std::pair<int, int>> read_microdvd(const char* filename);
std::vector<std::pair<int,int>> read_vtt(const char* filename);
std::pair<std::vector<std::pair<int,int>>, std::vector<int>> process_spans(const std::vector<std::pair<int, int>>& timestamps, bool merge = true, bool sort_by_time = true);
const float SPEECH_THRESHOLD = 0.25f;
const int MIN_CUES = 10;
const int MIN_SPEECH_MS = 200;
const int MAX_CUE_MS = 10000;

const int MAX_TIME_MS = 24 * 3600 * 1000;
const int MAX_CUES = 100000;
const size_t MAX_SUBTITLE_BYTES = 64u * 1024 * 1024;

std::vector<int> activity(const std::vector<std::pair<int, int>>& spans);
std::pair<std::vector<std::pair<int, int>>, std::vector<float>> reference_spans(const std::vector<float>& probability);
