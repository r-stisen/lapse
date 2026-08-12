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

#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <fstream>
#include <filesystem>
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif
#include "decoder.h"
#include "silero.h"
#include "srt_parser.h"
#include "cuetext.h"
#include "correlate.h"
#include "align.h"
#include "write_subtitle.h"
#include "log.h"

// The formats the parsers and the writers both handle. Callers can ask for the
// list with --formats so they know what is safe to hand us
const char* subtitle_formats[] = {".srt", ".ass", ".ssa", ".vtt", ".sub"};

bool is_subtitle(const std::string& path) {
    for (auto& ext : subtitle_formats)
        if (path.size() > strlen(ext) && path.substr(path.size() - strlen(ext)) == ext)
            return true;
    return false;
}

// The format follows the file we read, the destination is wherever the caller asked us to put it
void write_offsets(const std::string& in_path, const std::string& out_path, double slope, const std::vector<int>& offsets, const std::vector<int>& mapping) {
    if (in_path.ends_with(".srt"))
        write_srt_split(in_path.c_str(), out_path.c_str(), slope, offsets, mapping);
    else if (in_path.ends_with(".ass") || in_path.ends_with(".ssa"))
        write_ass_split(in_path.c_str(), out_path.c_str(), slope, offsets, mapping);
    else if (in_path.ends_with(".vtt"))
        write_vtt_split(in_path.c_str(), out_path.c_str(), slope, offsets, mapping);
    else if (in_path.ends_with(".sub"))
        write_microdvd_split(in_path.c_str(), out_path.c_str(), slope, offsets, mapping);
}

static bool number(const char* text, double& into) {
    char* stopped = nullptr;
    double got = std::strtod(text, &stopped);
    if (stopped == text || !stopped || *stopped) return false;
    into = got;
    return true;
}

// Listening to a film is the slow part by a mile, so the speech we found gets
// written down. Every other subtitle track for the same film then starts from
// the answer instead of decoding the whole thing again
static std::filesystem::path cache_dir() {
    if (const char* set = std::getenv("LAPSE_CACHE")) return set;
#ifdef _WIN32
    if (const char* local = std::getenv("LOCALAPPDATA")) return std::filesystem::path(local) / "lapse" / "cache";
    if (const char* profile = std::getenv("USERPROFILE")) return std::filesystem::path(profile) / ".cache" / "lapse";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) return std::filesystem::path(xdg) / "lapse";
    if (const char* home = std::getenv("HOME")) return std::filesystem::path(home) / ".cache" / "lapse";
#endif
    return std::filesystem::path(".") / ".cache" / "lapse";
}

static std::filesystem::path cache_path(const std::string& video, int audio_track) {
    std::error_code ec;
    uintmax_t size = std::filesystem::file_size(video, ec);
    if (ec) return {};
    auto written = std::filesystem::last_write_time(video, ec);
    if (ec) return {};

    std::string key = video + "|" + std::to_string((long long)size) + "|" + std::to_string((long long)written.time_since_epoch().count()) + "|" + std::to_string(audio_track);
    unsigned long long hash = 1469598103934665603ULL;
    for (char c : key) {
        hash ^= (unsigned char)c;
        hash *= 1099511628211ULL;
    }

    std::filesystem::path dir = cache_dir();
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};

    char name[32];
    snprintf(name, sizeof(name), "%016llx.spans", hash);
    return dir / name;
}

static bool load_spans(const std::filesystem::path& path, std::vector<std::pair<int,int>>& spans, std::vector<float>& weights, double& coverage) {
    std::ifstream in(path);
    if (!in) return false;

    std::string tag;
    in >> tag >> coverage;
    if (tag != "lapse3") return false;

    int start, end;
    float weight;
    while (in >> start >> end >> weight) {
        spans.push_back({start, end});
        weights.push_back(weight);
    }

    if (spans.size() < 20) {
        spans.clear();
        weights.clear();
        return false;
    }
    return true;
}

static void save_spans(const std::filesystem::path& path, const std::vector<std::pair<int,int>>& spans, const std::vector<float>& weights, double coverage) {
    std::filesystem::path partial = path;
    partial += ".tmp";

    std::ofstream out(partial);
    if (!out) return;

    out << "lapse3 " << coverage << '\n';
    for (int i = 0; i < (int)spans.size(); i++)
        out << spans[i].first << ' ' << spans[i].second << ' ' << (i < (int)weights.size() ? weights[i] : 1.0f) << '\n';

    out.close();
    std::error_code ec;
    std::filesystem::rename(partial, path, ec);
}


static double sure_sigma = 8.0;
static const double SOME_SIGMA = 3.5;

static const int AGREE_MS = 400;


static const int MIN_AGREEING = 3;

enum class Verdict { Solid, Unsure, Nothing };


static bool worth_splitting(const std::vector<int>& offsets, int base) {
    int runs = 0;
    size_t i = 0;
    while (i < offsets.size()) {
        size_t j = i;
        while (j + 1 < offsets.size() && offsets[j + 1] == offsets[i]) j++;
        if (j - i + 1 < 12) return false;
        if (std::abs(offsets[i] - base) > 60000) return false;
        runs++;
        i = j + 1;
    }
    if (runs < 2) return false;

    int low = offsets[0], high = offsets[0];
    for (int o : offsets) {
        low = std::min(low, o);
        high = std::max(high, o);
    }
    return high - low >= 120;
}

static int agree_count(const std::vector<Chunk>& chunks, const std::vector<double>& expected) {
    int agreed = 0;
    for (size_t i = 0; i < chunks.size() && i < expected.size(); i++)
        if (std::abs(chunks[i].offset - expected[i]) <= AGREE_MS) agreed++;
    return agreed;
}

static double agreement_of(const std::vector<Chunk>& chunks, const std::vector<double>& expected) {
    if (chunks.empty()) return 0.0;
    return (double)agree_count(chunks, expected) / chunks.size();
}


static Verdict judge(double sigma, double margin, int agreeing) {
    (void)margin;
    if (sigma < std::min(SOME_SIGMA, sure_sigma)) return Verdict::Nothing;
    if (sigma >= sure_sigma && agreeing >= MIN_AGREEING) return Verdict::Solid;
    return Verdict::Unsure;
}


static int clusters_of(std::vector<Chunk> chunks) {
    std::sort(chunks.begin(), chunks.end(), [](const Chunk& a, const Chunk& b) {
        return a.offset < b.offset;
    });

    int groups = 0;
    size_t i = 0;
    while (i < chunks.size()) {
        size_t j = i;
        while (j + 1 < chunks.size() && chunks[j + 1].offset - chunks[j].offset <= AGREE_MS) j++;
        if (j - i + 1 >= 2) groups++;
        i = j + 1;
    }
    return groups;
}


static std::string beside(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return path + ".lapse-unsure";
    return path.substr(0, dot) + ".lapse-unsure" + path.substr(dot);
}

static bool restore_backup(const std::string& path) {
    std::string backup = path + ".bak";
    std::error_code ec;
    if (!std::filesystem::exists(backup, ec)) {
        std::cerr << "No backup to restore: " << backup << '\n';
        return false;
    }

    std::filesystem::copy_file(backup, path, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        std::cerr << "Could not put " << backup << " back: " << ec.message() << '\n';
        return false;
    }
    std::filesystem::remove(backup, ec);
    say() << "Restored " << path << " from the backup\n";
    return true;
}

struct Report {
    std::string mode;
    std::string reference = "vad";
    int offset = 0;
    double ratio = 1.0;
    double confidence = 0;
    double margin = 0;
    double sigma = 0;
    double agreement = 0;
    double coverage = 1.0;
    int cues = 0;
    int ignored = 0;
    int parts = 1;
    bool written = false;
    std::string verdict = "nothing";
    std::string why;
    std::string output;
    std::vector<int> splits;
};

static bool as_json = false;

static std::string escaped(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if ((unsigned char)c < 0x20) continue;
        else out += c;
    }
    return out;
}

static void report(const Report& r) {
    if (!as_json) {
        say() << "Done (" << r.mode << "): offset=" << r.offset << "ms";
        if (r.ratio != 1.0) say() << " ratio=" << r.ratio;
        if (r.parts > 1) say() << " parts=" << r.parts;
        say() << " sigma=" << r.sigma << " agree=" << r.agreement << " confidence=" << r.confidence;
        say() << " [" << r.verdict << "]";
        if (!r.written) say() << " NOT WRITTEN (" << r.why << ")";
        else say() << " -> " << r.output;
        say() << '\n';
        return;
    }

    std::cout << "{\"mode\":\"" << r.mode << "\""
              << ",\"reference\":\"" << r.reference << "\""
              << ",\"offset_ms\":" << r.offset
              << ",\"ratio\":" << r.ratio
              << ",\"confidence\":" << r.confidence
              << ",\"margin\":" << r.margin
              << ",\"sigma\":" << r.sigma
              << ",\"agreement\":" << r.agreement
              << ",\"verdict\":\"" << r.verdict << "\""
              << ",\"coverage\":" << r.coverage
              << ",\"cues\":" << r.cues
              << ",\"ignored_cues\":" << r.ignored
              << ",\"parts\":" << r.parts
              << ",\"written\":" << (r.written ? "true" : "false");
    if (!r.why.empty()) std::cout << ",\"why\":\"" << escaped(r.why) << "\"";
    std::cout << ",\"output\":\"" << escaped(r.output) << "\"";

    std::cout << ",\"splits\":[";
    for (size_t i = 0; i < r.splits.size(); i++)
        std::cout << (i ? "," : "") << r.splits[i];
    std::cout << "]}\n";
}

void usage() {
    std::cerr << "Usage: lapse <video_or_subtitle> <subtitle> [auto|ols|nosplit|split] [penalty] [--output <path>] [--no-backup] [--no-sidecar] [--no-embedded] [--full-scan] [--no-cache] [--force] [--json] [--quiet] [--dry-run] [--strict] [--confidence N] [--audio-track N] [--sub-track N]\n";
    std::cerr << "       --confidence N   how far the answer has to stand out before the original is overwritten (default " << sure_sigma << ")\n";
    std::cerr << "       lapse --formats\n";
    std::cerr << "       lapse --vad\n";
    std::cerr << "       lapse --undo <subtitle>\n";
}

int run(int argc, const char *argv[]) {
    std::vector<std::string> args;
    std::string output_path;
    bool make_backup = true;
    bool use_embedded = true;
    bool full_scan = false;
    bool use_cache = true;
    bool force = false;
    bool dry_run = false;
    bool strict = false;
    bool no_sidecar = false;
    int audio_track = -1;
    int sub_track = -1;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--json") {
            as_json = true;
            quiet = true;
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "--strict") {
            strict = true;
        } else if (arg == "--audio-track") {
            double got;
            if (i + 1 >= argc || !number(argv[++i], got)) { usage(); return -1; }
            audio_track = (int)got;
        } else if (arg == "--sub-track") {
            double got;
            if (i + 1 >= argc || !number(argv[++i], got)) { usage(); return -1; }
            sub_track = (int)got;
        } else if (arg == "--confidence") {
            double got;
            if (i + 1 >= argc || !number(argv[++i], got) || got <= 0) {
                std::cerr << "--confidence wants a number above zero\n";
                return -1;
            }
            sure_sigma = got;
        } else if (arg == "--undo") {
            if (i + 1 >= argc) { usage(); return -1; }
            return restore_backup(argv[++i]) ? 0 : 1;
        } else if (arg == "--formats") {
            for (auto& ext : subtitle_formats)
                std::cout << ext << '\n';
            return 0;
        } else if (arg == "--vad") {
            bool silero = silero_open();
            silero_close();
            std::cout << (silero ? "silero\n" : "libfvad\n");
            return silero ? 0 : 1;
        } else if (arg == "--output") {
            if (i + 1 >= argc) { usage(); return -1; }
            output_path = argv[++i];
        } else if (arg == "--no-backup") {
            make_backup = false;
        } else if (arg == "--no-embedded") {
            use_embedded = false;
        } else if (arg == "--full-scan") {
            full_scan = true;
        } else if (arg == "--no-cache") {
            use_cache = false;
        } else if (arg == "--no-sidecar") {
            no_sidecar = true;
        } else if (arg == "--force") {
            force = true;
        } else {
            args.push_back(arg);
        }
    }

    if (args.size() < 2) {
        usage();
        return -1;
    }

    std::string ref_path   = args[0];
    std::string input_path = args[1];
    std::string mode       = (args.size() >= 3) ? args[2] : "auto";
    bool given_penalty     = (args.size() >= 4);

    double typed = 6.0;
    if (given_penalty && !number(args[3].c_str(), typed)) {
        std::cerr << "The penalty has to be a number, not " << args[3] << '\n';
        return -1;
    }
    float p = (float)typed;

    if (!is_subtitle(input_path)) {
        std::cerr << "Unsupported subtitle format: " << input_path << '\n';
        return 1;
    }

    // No --output means carry on overwriting the file we were given
    if (output_path.empty()) output_path = input_path;

    std::vector<int> reference_activity;
    std::vector<std::pair<int,int>> ref_spans;
    std::vector<float> ref_weights;
    double ref_coverage = 1.0;

    std::vector<std::pair<int,int>> timestamps = read_subtitle(input_path);

    // sounds and [door slams] are in the file but never in the audio. they still get written back out, they just do not get a vote
    int junk = 0;
    timestamps = drop_junk_cues(timestamps, read_cue_text(input_path), &junk);
    if (junk) say() << "Ignoring " << junk << " cues that are not dialogue\n";

    auto [spans, mapping] = process_spans(timestamps, mode != "split", mode != "split");
    if (spans.empty()) {
        std::cerr << "No timestamps found in: " << input_path << '\n';
        return 1;
    }

    // Half the scene groups ship an srt containing their own name and nothing
    // else. Four cues will sit on speech at whatever offset you fancy
    if ((int)spans.size() < MIN_CUES && !force) {
        std::cerr << "Only " << spans.size() << " cues in " << input_path
                  << ", that is not enough to go on. Pass --force to sync it anyway.\n";
        return 2;
    }

    // kept around so a thin answer can send us back for the whole film later
    AVFormatContext* AVC = nullptr;
    AVCodecContext* OAD = nullptr;
    int audio_stream_index = -1;
    std::filesystem::path cache;

    if (is_subtitle(ref_path)) {
        auto [rs, _] = process_spans(read_subtitle(ref_path));
        ref_spans = rs;
        reference_activity = activity(ref_spans);
    } else {
        AVC = open_file(ref_path.c_str());
        if (!AVC) return 1;


        if (use_embedded) {
            auto [rs, _] = process_spans(embedded_spans(AVC, sub_track));
            ref_spans = rs;
            reference_activity = activity(ref_spans);
        }

        cache = use_cache ? cache_path(ref_path, audio_track) : std::filesystem::path{};
        if (!cache.empty()) say() << "Cache: " << cache.string() << '\n';
        if (ref_spans.empty() && !cache.empty() && load_spans(cache, ref_spans, ref_weights, ref_coverage)) {
            if (full_scan && ref_coverage < 1.0) {
                ref_spans.clear();
                ref_weights.clear();
                ref_coverage = 1.0;
            } else {
                say() << "Reusing the speech we found last time (" << ref_spans.size() << " spans)\n";
                reference_activity = activity(ref_spans);
            }
        }

        if (ref_spans.empty()) {
            audio_stream_index = find_audio_stream(AVC, audio_track);
            OAD = open_audio_decoder(AVC, audio_stream_index);
            if (!OAD) {
                std::cerr << "No audio track we can decode in: " << ref_path << '\n';
                return 1;
            }

            std::vector<float> profile = speech_profile(AVC, OAD, audio_stream_index, full_scan ? 0 : 30, &ref_coverage);
            if (profile.empty()) {
                std::cerr << "Got no audio out of: " << ref_path << '\n';
                return 1;
            }

            auto [rs, w] = reference_spans(profile);
            ref_spans = rs;
            ref_weights = w;

            reference_activity.reserve(profile.size());
            for (float p : profile) reference_activity.push_back(p >= SPEECH_THRESHOLD ? 1 : 0);

            if (!cache.empty()) save_spans(cache, ref_spans, ref_weights, ref_coverage);
        }
    }

    if (ref_spans.empty()) {
        std::cerr << "No speech found in: " << ref_path << '\n';
        return 1;
    }
    say() << "Lining up " << spans.size() << " cues against " << ref_spans.size() << " stretches of speech\n";

    // A split has to pay for itself. How much it has to be worth grows with the size of the file, which is what the 6 was standing in for
    if (!given_penalty) p = (float)std::max(3.0, std::log((double)spans.size()));


    Report card;
    card.coverage = ref_coverage;
    card.cues = (int)spans.size();
    card.ignored = junk;
    card.output = output_path;
    if (is_subtitle(ref_path)) card.reference = "subtitle";
    else if (ref_weights.empty()) card.reference = "embedded";

    align_setup(ref_spans, ref_weights);

    std::vector<Chunk> slices = chunk_offsets(spans, ref_spans, ref_weights, 8, ref_coverage);

    // a cache written by a version that only sampled part of the film. listen to the rest of it, it costs seconds now
    if (OAD && ref_coverage < 1.0) {
        say() << "The saved profile only covers part of the film, listening to the rest\n";
        std::vector<float> profile = speech_profile(AVC, OAD, audio_stream_index, 0, &ref_coverage);

        auto [rs2, w2] = reference_spans(profile);
        ref_spans = rs2;
        ref_weights = w2;

        reference_activity.clear();
        reference_activity.reserve(profile.size());
        for (float p : profile) reference_activity.push_back(p >= SPEECH_THRESHOLD ? 1 : 0);

        if (!cache.empty()) save_spans(cache, ref_spans, ref_weights, ref_coverage);
        align_setup(ref_spans, ref_weights);
        slices = chunk_offsets(spans, ref_spans, ref_weights, 8, ref_coverage);
        card.coverage = ref_coverage;
    }

    //where each slice should have landed if the answer is right
    auto expected_at = [&](int offset, double ratio) {
        std::vector<double> want;
        for (auto& c : slices) want.push_back(c.time * (ratio - 1.0) + offset);
        return want;
    };

    // solid overwrites, anything short of that goes next to the original
    auto settle = [&](Verdict verdict) {
        if (force) verdict = Verdict::Solid;
        card.verdict = (verdict == Verdict::Solid) ? "solid"
                     : (verdict == Verdict::Unsure) ? "unsure" : "nothing";

        if (verdict == Verdict::Solid) return true;

        const char* because = (verdict == Verdict::Nothing)
            ? "the audio does not back this up, so it is a guess"
            : "not sure enough to touch the original";

        if (strict || no_sidecar) {
            card.why = because;
            return false;
        }
        if (output_path == input_path) {
            output_path = beside(input_path);
            make_backup = false;
        }
        card.why = because;
        card.output = output_path;
        return true;
    };

    auto save = [&](const std::vector<int>& offs, const std::vector<int>& map, Verdict verdict, double slope = 0.0) {
        card.ratio = 1.0 + slope;
        for (size_t i = 1; i < offs.size(); i++)
            if (offs[i] != offs[i - 1]) card.splits.push_back((int)i);
        card.parts = (int)card.splits.size() + 1;

        if (!settle(verdict)) { report(card); return 2; }
        card.written = true;
        if (!dry_run) {
            if (make_backup) backup_file(input_path.c_str());
            write_offsets(input_path, output_path, slope, offs, map);
        }
        report(card);
        return (verdict == Verdict::Unsure && !force) ? 3 : 0;
    };

    auto save_ols = [&](double slope, double intercept, Verdict verdict) {
        card.ratio = 1.0 + slope;
        card.offset = (int)(intercept * 1000.0);

        if (!settle(verdict)) { report(card); return 2; }
        card.written = true;
        if (!dry_run) {
            if (make_backup) backup_file(input_path.c_str());
            if (input_path.ends_with(".srt"))
                write_srt_OLS(input_path.c_str(), output_path.c_str(), slope, intercept);
            else if (input_path.ends_with(".ass") || input_path.ends_with(".ssa"))
                write_ass_OLS(input_path.c_str(), output_path.c_str(), slope, intercept);
            else if (input_path.ends_with(".vtt"))
                write_vtt_OLS(input_path.c_str(), output_path.c_str(), slope, intercept);
            else if (input_path.ends_with(".sub"))
                write_microdvd_OLS(input_path.c_str(), output_path.c_str(), slope, intercept);
        }
        report(card);
        return (verdict == Verdict::Unsure && !force) ? 3 : 0;
    };

    if (mode == "ols") {
        // Try the framerates people actually ship first. Only when none of them fit do we go back to measuring the drift chunk by chunk, which is the one thing that can still catch a stretch that isnt a standard ratio
        auto [ratio, offset, confidence, sigma] = best_framerate(spans, ref_spans, ref_weights, ref_coverage);
        double slope = ratio - 1.0;
        double intercept = offset / 1000.0;

        if (confidence < 0.5) {
            say() << "No framerate fit well, measuring the drift instead\n";
            std::vector<int> input_activity = activity(spans);
            auto [s, i] = fft_crosscorrelate(reference_activity, input_activity);
            slope = s;
            intercept = i;
        }

        card.mode = "ols";
        card.confidence = confidence;
        card.margin = 1.0;
        card.sigma = sigma;
        std::vector<double> want = expected_at((int)(intercept * 1000.0), 1.0 + slope);
        card.agreement = agreement_of(slices, want);
        return save_ols(slope, intercept, judge(sigma, 1.0, agree_count(slices, want)));

    } else if (mode == "nosplit") {
        auto [offset, confidence, margin, sigma] = best_offset(spans, ref_spans, ref_weights, ref_coverage);
        card.mode = "nosplit";
        card.offset = offset;
        card.confidence = confidence;
        card.margin = margin;
        card.sigma = sigma;
        std::vector<double> want = expected_at(offset, 1.0);
        card.agreement = agreement_of(slices, want);
        std::vector<int> offsets(spans.size(), offset);
        return save(offsets, mapping, judge(sigma, margin, agree_count(slices, want)));

    } else if (mode == "split") {
        // Lock onto the whole file first and let the split search work around that it only has to look at the offsets near the one we found
        auto [offset, confidence, margin, sigma] = best_offset(spans, ref_spans, ref_weights, ref_coverage);
        card.mode = "split";
        card.offset = offset;
        card.confidence = confidence;
        card.margin = margin;
        card.sigma = sigma;
        std::vector<double> want = expected_at(offset, 1.0);
        card.agreement = agreement_of(slices, want);
        std::vector<int> offsets = split_alignment(spans, ref_spans, ref_weights, p, offset);
        return save(offsets, mapping, judge(sigma, margin, agree_count(slices, want)));

    } else if (mode == "auto") {
        auto [order_spans, order_mapping] = process_spans(timestamps, false, false);
        std::vector<int> cuts = backward_jumps(order_spans);
        if (!cuts.empty()) {
            say() << "auto: the subtitle restarts " << cuts.size() << " time(s), so this is one file per part\n";

            double worst_sigma = 99.0;
            std::vector<int> joined = offsets_for_cuts(order_spans, ref_spans, ref_weights, cuts, ref_coverage, &worst_sigma);
            card.mode = "auto/restart";
            card.offset = joined.empty() ? 0 : joined[0];
            card.sigma = worst_sigma;
            card.margin = 1.0;
            int backing = (worst_sigma >= 4.0) ? MIN_AGREEING : 0;
            card.agreement = backing ? 1.0 : 0.0;
            return save(joined, order_mapping, judge(worst_sigma, 1.0, backing));
        }

        auto [offset, confidence, margin, sigma] = best_offset(spans, ref_spans, ref_weights, ref_coverage);

        int flat = agree_count(slices, expected_at(offset, 1.0));

        //  or sit on a line instead, which is what a wrong framerate looks like
        auto [drift, start] = robust_line(slices);
        int sloped = 0;
        for (auto& c : slices)
            if (std::abs(c.offset - (drift * c.time + start)) <= AGREE_MS) sloped++;

        int groups = clusters_of(slices);
        say() << "auto: " << flat << " of " << slices.size() << " slices back the offset, "
              << sloped << " sit on a line, " << groups << " group(s)\n";

        std::string choice;
        double ratio = 1.0;

        if (flat >= MIN_AGREEING) {
            choice = "shifted";
        } else if (sloped >= MIN_AGREEING + 1 && std::abs(drift) > 1e-5) {
            choice = "drifting";
            ratio = snap_ratio(1.0 + drift);
        } else if (groups >= 2) {
            choice = "recut";
        } else {

            say() << "auto: nothing agreed, trying the framerates\n";
            auto [r, shift, conf2, sigma2] = best_framerate(spans, ref_spans, ref_weights, ref_coverage);


            if (r != 1.0 && sigma2 >= sure_sigma && sigma2 > sigma + 2.0) {
                choice = "drifting";
                ratio = r;
            } else {
                choice = "shifted";
            }
        }

        card.offset = offset;
        card.confidence = confidence;
        card.margin = margin;
        card.sigma = sigma;

        if (choice == "recut") {
            auto [file_spans, file_mapping] = process_spans(timestamps, false, false);
            double worst_sigma = 99.0;
            std::vector<int> joined = concat_offsets(file_spans, ref_spans, ref_weights, ref_coverage, &worst_sigma);
            if (!joined.empty()) {
                card.mode = "auto/joined";
                card.offset = joined[0];
                card.sigma = worst_sigma;
                card.margin = 1.0;
                int backing = (worst_sigma >= 4.0) ? MIN_AGREEING : 0;
                card.agreement = backing ? 1.0 : 0.0;
                return save(joined, file_mapping, judge(worst_sigma, 1.0, backing));
            }
        }

        auto shifted = [&]() {
            card.mode = "auto/shifted";
            card.offset = offset;
            card.confidence = confidence;
            card.margin = margin;
            card.sigma = sigma;
            card.agreement = (double)flat / slices.size();
            Verdict verdict = judge(sigma, margin, flat);

            std::vector<int> offsets(spans.size(), offset);
            if (verdict == Verdict::Solid) {
                std::vector<int> tidied = split_alignment(spans, ref_spans, ref_weights, p, offset,
                REFINE_WINDOW_MS, REFINE_STEP_MS);
                if (worth_splitting(tidied, offset)) {
                    say() << "auto: one offset does not fit the whole file, splitting it\n";
                    offsets = tidied;
                    card.mode = "auto/shifted+split";
                }
            }
            return save(offsets, mapping, verdict);
        };

        if (choice == "shifted") return shifted();

        if (choice == "drifting") {
            std::vector<std::pair<int,int>> scaled;
            for (auto& s : spans) scaled.push_back({(int)(s.first * ratio), (int)(s.second * ratio)});

            auto [shift, conf2, margin2, sigma2] = best_offset(scaled, ref_spans, ref_weights, ref_coverage);


            if (sigma2 < sigma + 1.0) {
                say() << "auto: the stretch does not beat the single offset, dropping it\n";
                return shifted();
            }

            card.mode = "auto/drifting";
            card.offset = shift;
            card.confidence = conf2;
            card.margin = margin2;
            card.sigma = sigma2;


            std::vector<Chunk> after = chunk_offsets(scaled, ref_spans, ref_weights, 8, ref_coverage);
            std::vector<double> want(after.size(), shift);
            int backing = agree_count(after, want);
            card.agreement = after.empty() ? 0.0 : (double)backing / after.size();
            say() << "auto: " << backing << " of " << after.size() << " slices back the stretch\n";

            Verdict verdict = judge(sigma2, margin2, backing);


            if (verdict == Verdict::Solid) {
                std::vector<int> tidied = split_alignment(scaled, ref_spans, ref_weights, p, shift,
                                                          REFINE_WINDOW_MS, REFINE_STEP_MS);
                if (worth_splitting(tidied, shift)) {
                    say() << "auto: the stretched file still jumps about, splitting it as well\n";
                    card.mode = "auto/drifting+split";
                    return save(tidied, mapping, verdict, ratio - 1.0);
                }
            }

            return save_ols(ratio - 1.0, shift / 1000.0, verdict);
        }

        {
            auto [file_spans, file_mapping] = process_spans(timestamps, false, false);
            card.mode = "auto/recut";
            std::vector<int> offsets = split_alignment(file_spans, ref_spans, ref_weights, p, offset);

            std::vector<double> want;
            for (auto& c : slices) {
                int at = 0;
                while (at + 1 < (int)file_spans.size() && (file_spans[at].first + file_spans[at].second) / 2.0 < c.time) at++;
                want.push_back(offsets[at]);
            }
            card.agreement = agreement_of(slices, want);
            return save(offsets, file_mapping, judge(sigma, margin, agree_count(slices, want)));
        }

    } else {
        std::cerr << "Unknown mode: " << mode << ". Use auto, ols, nosplit or split.\n";
        return -1;
    }

    return 0;
}

int main(int argc, const char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        int result = run(argc, argv);
        silero_close();
        return result;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
