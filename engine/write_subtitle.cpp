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

#include "write_subtitle.h"
#include "srt_parser.h"


void backup_file(const char* path) {
    std::string backup = std::string(path) + ".bak";
    if (std::filesystem::exists(backup)) return;
    std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
}

// How a timestamp gets moved. Either one line for the whole file, or a lookup per cue, and both together when the file drifts and was also cut about
struct Shift {
    double slope = 0;
    double intercept_s = 0;
    std::vector<int> offsets;
    std::vector<int> mapping;

    int apply(int ms, int cue) const {
        double stretched = ms * (1.0 + slope) + intercept_s * 1000.0;
        if (mapping.empty()) return (int)stretched;
        if (cue < 0 || cue >= (int)mapping.size()) return (int)stretched;
        int span = mapping[cue];
        if (span < 0 || span >= (int)offsets.size()) return (int)stretched;
        return (int)stretched + offsets[span];
    }
};

// goes back out the way it came in - utf-16 used to come back as utf-8
static Charset came_as = Charset::Legacy;

static std::string load_file(const char* path) {
    return load_text(path, &came_as);
}

// temp file then rename, so a throw halfway leaves the old file whole
static void save_file(const char* output_path, const std::string& text) {
    std::string temp_path = std::string(output_path) + ".tmp";
    std::ofstream out(temp_path, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write subtitle: " + std::string(output_path));

    std::string bytes = encode(text, came_as);
    out.write(bytes.data(), bytes.size());
    out.close();
    std::filesystem::rename(temp_path, output_path);
}

static std::string ms_to_ts(int ms, char ms_sep) {
    if (ms < 0) ms = 0;
    int h  = ms / 3600000; ms %= 3600000;
    int m  = ms / 60000;   ms %= 60000;
    int sc = ms / 1000;    ms %= 1000;
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d%c%03d", h, m, sc, ms_sep, ms);
    return buf;
}

static std::string ms_to_ass_ts(int ms) {
    if (ms < 0) ms = 0;
    int h  = ms / 3600000; ms %= 3600000;
    int m  = ms / 60000;   ms %= 60000;
    int sc = ms / 1000;    ms %= 1000;
    int cs = ms / 10;
    char buf[16];
    snprintf(buf, sizeof(buf), "%01d:%02d:%02d.%02d", h, m, sc, cs);
    return buf;
}

static std::string ms_to_frame(int ms, double slope) {
    if (ms < 0) ms = 0;
    int frame = ms / (1000 / ( ARB_FPS / (1 + slope)));
    std::string frame_str = std::to_string(frame);
    return frame_str;
}

// srt and vtt are the same file with a different character in front of the
// milliseconds, so they go through here together. We write to a temp file and
// move it into place at the end if something throws halfway the subtitle the user already had is still whole
static void write_cues(const char* input_path, const char* output_path, char ms_sep, const Shift& shift) {
    std::string text = load_file(input_path);
    bool ends_clean = !text.empty() && text.back() == '\n';
    std::istringstream ss(text);

    std::string out;

    std::string line;
    int cue = 0;
    const char* eol = "\n";
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            eol = "\r\n";
        }

        size_t arrow = line.find("-->");
        if (arrow != std::string::npos) {
            int start_ms = parse_timestamp(line, 0);
            int end_ms   = parse_timestamp(line, arrow + 3);

            if (start_ms >= 0 && end_ms >= 0) {
                // vtt hangs cue settings on the end of the line, keep them
                std::string tail;
                size_t after = line.find_first_not_of(" \t", arrow + 3);
                if (after != std::string::npos) {
                    size_t space = line.find_first_of(" \t", after);
                    if (space != std::string::npos) tail = line.substr(space);
                }
                line = ms_to_ts(shift.apply(start_ms, cue), ms_sep) + " --> " + ms_to_ts(shift.apply(end_ms, cue), ms_sep) + tail;
            }
            cue++;       // counted even when it did not parse, the reader did the same
        }

        out += line;
        if (ends_clean || !ss.eof()) out += eol;
    }

    save_file(output_path, out);
}

static void write_dialogue(const char* input_path, const char* output_path, const Shift& shift) {
    std::string text = load_file(input_path);
    bool ends_clean = !text.empty() && text.back() == '\n';
    std::istringstream ss(text);

    std::string out;

    std::string line;
    int cue = 0;
    int start_col = 1;
    int end_col = 2;
    const char* eol = "\n";

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            eol = "\r\n";
        }

        if (line.rfind("Format:", 0) == 0) {
            auto [s, e] = ass_time_columns(line);
            if (s >= 0 && e >= 0) { start_col = s; end_col = e; }

        } else if (line.rfind("Dialogue:", 0) == 0) {
            std::vector<size_t> commas = ass_commas(line);
            size_t sf, sl, ef, el;

            if (ass_field(line, commas, start_col, sf, sl) && ass_field(line, commas, end_col, ef, el)) {
                int start_ms = parse_timestamp(line.substr(sf, sl), 0);
                int end_ms   = parse_timestamp(line.substr(ef, el), 0);

                if (start_ms >= 0 && end_ms >= 0) {
                    std::string new_start = ms_to_ass_ts(shift.apply(start_ms, cue));
                    std::string new_end   = ms_to_ass_ts(shift.apply(end_ms, cue));

                    // Put the later field back first so the earlier one keeps
                    // the position we just looked up
                    if (sf < ef) {
                        line.replace(ef, el, new_end);
                        line.replace(sf, sl, new_start);
                    } else {
                        line.replace(sf, sl, new_start);
                        line.replace(ef, el, new_end);
                    }
                }
            }
            cue++;
        }

        out += line;
        if (ends_clean || !ss.eof()) out += eol;
    }

    save_file(output_path, out);
}

static void write_microdvd(const char* input_path, const char* output_path, const Shift& shift, double slope) {
    std::string text = load_file(input_path);
    bool ends_clean = !text.empty() && text.back() == '\n';
    std::istringstream ss(text);

    std::string out;

    std::string line;
    int cue = 0;
    const char* eol = "\n";
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
            eol = "\r\n";
        }  
        
        size_t separator = line.find("}{");
        if (separator != std::string::npos) {
          int start_ms = parse_frame(line) * (1000 / ARB_FPS);
          int end_ms = parse_frame(line, true) * (1000 / ARB_FPS);

            if (start_ms >= 0 && end_ms >= 0) {
                std::string tail;
                size_t after = line.find("}", separator + 2);
                if (after != std::string::npos) {
                    tail = line.substr(after);
                }
                line = "{" + ms_to_frame(shift.apply(start_ms, cue), slope) + "}{" + ms_to_frame(shift.apply(end_ms, cue), slope) + tail;
            }
            cue++;
        }

        out += line;
        if (ends_clean || !ss.eof()) out += eol;
    }

    save_file(output_path, out);
}

static Shift one_line(double slope, double intercept_s) {
    Shift shift;
    shift.slope = slope;
    shift.intercept_s = intercept_s;
    return shift;
}

static Shift per_cue(double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    Shift shift;
    shift.slope = slope;
    shift.offsets = offsets;
    shift.mapping = mapping;
    return shift;
}

void write_srt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_cues(input_path, output_path, ',', one_line(slope, intercept_s));
}

void write_vtt_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_cues(input_path, output_path, '.', one_line(slope, intercept_s));
}

void write_ass_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_dialogue(input_path, output_path, one_line(slope, intercept_s));
}

void write_microdvd_OLS(const char* input_path, const char* output_path, double slope, double intercept_s) {
    write_microdvd(input_path, output_path, one_line(slope, intercept_s), slope);
}

void write_srt_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_cues(input_path, output_path, ',', per_cue(slope, offsets, mapping));
}

void write_vtt_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_cues(input_path, output_path, '.', per_cue(slope, offsets, mapping));
}

void write_ass_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_dialogue(input_path, output_path, per_cue(slope, offsets, mapping));
}

void write_microdvd_split(const char* input_path, const char* output_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    write_microdvd(input_path, output_path, per_cue(slope, offsets, mapping), slope);
}
