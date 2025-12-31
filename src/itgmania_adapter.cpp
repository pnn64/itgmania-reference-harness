extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "itgmania_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <numeric>
#include <fstream>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <cstdio>
#include <tomcrypt.h>

#include "embedded_lua.h"

#ifdef ITGMANIA_HARNESS
#include "global.h"
#include "arch/ArchHooks/ArchHooks.h"
#include "CryptManager.h"
#include "Difficulty.h"
#include "EnumHelper.h"
#include "Game.h"
#include "GameManager.h"
#include "GameState.h"
#include "GameConstantsAndTypes.h"
#include "JsonUtil.h"
#include "LocalizedString.h"
#include "LuaManager.h"
#include "MsdFile.h"
#include "MessageManager.h"
#include "NoteData.h"
#include "NoteDataUtil.h"
#include "NoteSkinManager.h"
#include "NotesLoader.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSSC.h"
#include "NoteTypes.h"
#include "PrefsManager.h"
#include "RadarValues.h"
#include "RageFileManager.h"
#include "RageLog.h"
#include "RageUtil.h"
#include "Song.h"
#include "Steps.h"
#include "Style.h"
#include "StepParityGenerator.h"
#include "TechCounts.h"
#include "ThemeManager.h"
#include "TimingData.h"

static void init_singletons(int argc, char** argv) {
    static bool initialized = false;
    static int stored_argc = 0;
    static char** stored_argv = nullptr;
    static char default_prog[] = "itgmania-reference-harness";
    static char* default_argv[] = {default_prog, nullptr};

    if (initialized) return;

    if (argv != nullptr) {
        stored_argc = argc;
        stored_argv = argv;
    }
    if (stored_argv == nullptr) {
        stored_argc = 1;
        stored_argv = default_argv;
    }

    SetCommandlineArguments(stored_argc, stored_argv);

    if (!LOG) {
        LOG = new RageLog;
        LOG->SetLogToDisk(false);
        LOG->SetInfoToDisk(false);
        LOG->SetUserLogToDisk(false);
        LOG->SetShowLogOutput(false);
    }

    if (!PREFSMAN) {
#ifdef ITGMANIA_HARNESS_SOURCE
        // Harness provides a minimal PREFSMAN in stubs.
    (void)0;
#else
        PREFSMAN = new PrefsManager;
        PREFSMAN->m_bLogToDisk.Set(false);
        PREFSMAN->m_bForceLogFlush.Set(false);
#endif
    }

    if (!MESSAGEMAN) {
        MESSAGEMAN = new MessageManager;
    }

    if (!GAMEMAN) {
        GAMEMAN = new GameManager;
    }

    if (!GAMESTATE) {
        GAMESTATE = new GameState;
    }

    initialized = true;
}

void init_itgmania_runtime(int argc, char** argv) {
    init_singletons(argc, argv);
}

static bool load_lua_chunk(lua_State* L, const std::string& path, std::string_view embedded_src, const char* label) {
    if (std::filesystem::exists(path)) {
        if (luaL_dofile(L, path.c_str()) == 0) return true;

        std::string err = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);

        if (embedded_src.empty()) {
            std::fprintf(stderr, "lua load error (%s): %s\n", label, err.c_str());
            return false;
        }

        std::fprintf(stderr, "lua load error (%s): %s; using embedded copy\n", label, err.c_str());
    } else if (embedded_src.empty()) {
        std::fprintf(stderr, "lua load error (%s): missing %s and no embedded copy\n", label, path.c_str());
        return false;
    }

    if (luaL_loadbuffer(L, embedded_src.data(), embedded_src.size(), label) != 0) {
        std::fprintf(stderr, "embedded lua compile error (%s): %s\n", label,
            lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
        lua_pop(L, 1);
        return false;
    }

    if (lua_pcall(L, 0, 0, 0) != 0) {
        std::fprintf(stderr, "embedded lua runtime error (%s): %s\n", label,
            lua_tostring(L, -1) ? lua_tostring(L, -1) : "");
        lua_pop(L, 1);
        return false;
    }

    return true;
}

static std::string to_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        // std::tolower returns int; cast back to char to avoid narrowing warning on MSVC.
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

static std::string normalize_steps_type_string(std::string s) {
    std::replace(s.begin(), s.end(), '_', '-');
    return to_lower(s);
}

static std::string steps_type_string(StepsType st) {
    const int sti = static_cast<int>(st);
    if (sti < 0 || sti >= static_cast<int>(NUM_StepsType)) return "invalid";
    return normalize_steps_type_string(StepsTypeToString(st));
}

static std::string steps_type_string(const Steps* steps) {
    if (!steps) return "invalid";
    if (steps->m_StepsType == StepsType_Invalid && !steps->m_StepsTypeStr.empty()) {
        return normalize_steps_type_string(steps->m_StepsTypeStr);
    }
    return steps_type_string(steps->m_StepsType);
}

static std::string diff_string(Difficulty d) {
    return to_lower(DifficultyToString(d));
}

struct RawSimfileMetadataTags {
    bool has_title = false;
    bool has_subtitle = false;
    bool has_artist = false;
    std::string title;
    std::string subtitle;
    std::string artist;
};

static std::string ascii_upper(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c >= 'a' && c <= 'z') {
            out.push_back(static_cast<char>(c - ('a' - 'A')));
        } else {
            out.push_back(c);
        }
    }
    return out;
}

static void trim_ascii(std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    if (start == 0 && end == text.size()) {
        return;
    }
    text.assign(text.data() + start, end - start);
}

static bool extract_tag_value(
    const std::string& data,
    const std::string& data_upper,
    const char* tag,
    std::string& out) {
    const size_t tag_len = std::strlen(tag);
    size_t search_pos = 0;
    bool found = false;
    std::string value;

    while ((search_pos = data_upper.find(tag, search_pos)) != std::string::npos) {
        value.clear();
        bool escaped = false;
        size_t i = search_pos + tag_len;
        for (; i < data.size(); ++i) {
            char c = data[i];
            if (escaped) {
                value.push_back(c);
                escaped = false;
                continue;
            }
            if (c == '\\') {
                escaped = true;
                continue;
            }
            if (c == ';') {
                break;
            }
            value.push_back(c);
        }

        out = value;
        found = true;

        if (i >= data.size()) {
            break;
        }
        search_pos = i + 1;
    }

    if (found) {
        trim_ascii(out);
    }

    return found;
}

static RawSimfileMetadataTags read_simfile_metadata_tags(const std::string& simfile_path) {
    RawSimfileMetadataTags out;
    std::ifstream in(simfile_path, std::ios::binary);
    if (!in) {
        return out;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string data = ss.str();
    if (data.empty()) {
        return out;
    }

    const std::string data_upper = ascii_upper(data);
    out.has_title = extract_tag_value(data, data_upper, "#TITLE:", out.title);
    out.has_subtitle = extract_tag_value(data, data_upper, "#SUBTITLE:", out.subtitle);
    out.has_artist = extract_tag_value(data, data_upper, "#ARTIST:", out.artist);
    return out;
}

static void apply_song_metadata_fallback(
    const Song& song,
    const std::string& simfile_path,
    std::string& title,
    std::string& subtitle,
    std::string& artist) {
    RString main_title = song.m_sMainTitle;
    RString sub_title = song.m_sSubTitle;
    RString artist_name = song.m_sArtist;
    bool used_folder_fallback = false;

    Trim(main_title);
    Trim(sub_title);
    Trim(artist_name);

    if (main_title.empty()) {
        NotesLoader::GetMainAndSubTitlesFromFullTitle(
            Basename(song.GetSongDir()), main_title, sub_title);
        used_folder_fallback = true;
    }

    if (artist_name.empty()) {
        artist_name = "Unknown artist";
    }

    if (used_folder_fallback) {
        const RawSimfileMetadataTags raw = read_simfile_metadata_tags(simfile_path);
        if (raw.has_title) {
            main_title = raw.title.c_str();
            if (raw.has_subtitle) {
                sub_title = raw.subtitle.c_str();
            } else {
                sub_title = "";
            }
            if (raw.has_artist) {
                artist_name = raw.artist.c_str();
            }
        }
    }

    title = main_title.c_str();
    subtitle = sub_title.c_str();
    artist = artist_name.c_str();
}

static void compute_display_metadata(
    const Song& song,
    const std::string& title,
    const std::string& subtitle,
    const std::string& artist,
    std::string& title_out,
    std::string& subtitle_out,
    std::string& artist_out) {
    bool show_native = true;
    if (PREFSMAN) {
        show_native = PREFSMAN->m_bShowNativeLanguage;
    }

    if (!show_native) {
        title_out = song.m_sMainTitleTranslit.empty()
            ? title
            : song.m_sMainTitleTranslit.c_str();
        subtitle_out = song.m_sSubTitleTranslit.empty()
            ? subtitle
            : song.m_sSubTitleTranslit.c_str();
        artist_out = song.m_sArtistTranslit.empty()
            ? artist
            : song.m_sArtistTranslit.c_str();
        return;
    }

    title_out = title;
    subtitle_out = subtitle;
    artist_out = artist;
}

static std::string bpm_string_from_timing(TimingData* td) {
    const std::vector<TimingSegment*>& segments = td->GetTimingSegments(SEGMENT_BPM);
    std::vector<RString> bpm_strings;
    bpm_strings.reserve(segments.size());
    for (TimingSegment* segment : segments) {
        const BPMSegment* bpm_segment = ToBPM(segment);
        const float beat = bpm_segment->GetBeat();
        const float bpm = bpm_segment->GetBPM();
        bpm_strings.push_back(ssprintf("%s=%s", NormalizeDecimal(beat).c_str(), NormalizeDecimal(bpm).c_str()));
    }
    return join(",", bpm_strings);
}

static std::vector<std::vector<double>> timing_segments_to_number_table(TimingData* td, TimingSegmentType tst) {
    const std::vector<TimingSegment*>& segs = td->GetTimingSegments(tst);
    std::vector<std::vector<double>> out;
    out.reserve(segs.size());
    for (TimingSegment* seg : segs) {
        std::vector<float> values = seg->GetValues();
        std::vector<double> row;
        row.reserve(values.size() + 1);
        row.push_back(static_cast<double>(seg->GetBeat()));
        for (float v : values) row.push_back(static_cast<double>(v));
        out.push_back(std::move(row));
    }
    return out;
}

static std::vector<TimingLabelOut> timing_labels_to_table(TimingData* td) {
    const std::vector<TimingSegment*>& segs = td->GetTimingSegments(SEGMENT_LABEL);
    std::vector<TimingLabelOut> out;
    out.reserve(segs.size());
    for (TimingSegment* seg : segs) {
        TimingLabelOut row;
        row.beat = static_cast<double>(seg->GetBeat());
        row.label = ToLabel(seg)->GetLabel();
        out.push_back(std::move(row));
    }
    return out;
}

static std::string format_bpm_like_simply_love(double bpm, double music_rate) {
    if (music_rate == 1.0) {
        return ssprintf("%.0f", bpm);
    }
    std::string s = ssprintf("%.1f", bpm);
    if (s.size() >= 2 && s.compare(s.size() - 2, 2, ".0") == 0) {
        s.resize(s.size() - 2);
    }
    return s;
}

static std::string stringify_display_bpms_like_simply_love(double bpm_min, double bpm_max, double music_rate) {
    const std::string lo = format_bpm_like_simply_love(bpm_min, music_rate);
    const std::string hi = format_bpm_like_simply_love(bpm_max, music_rate);
    if (bpm_min == bpm_max) return lo;
    return lo + " - " + hi;
}

static void get_bpm_ranges_like_simply_love(
    Steps* steps,
    double music_rate,
    double& out_actual_min,
    double& out_actual_max,
    double& out_display_min,
    double& out_display_max,
    std::string& out_display_str) {
    float actual_min = 0.0f;
    float actual_max = 0.0f;
    steps->GetTimingData()->GetActualBPM(actual_min, actual_max);

    DisplayBpms disp;
    steps->GetDisplayBpms(disp);
    float display_min = disp.GetMin();
    float display_max = disp.GetMax();

    // Match Simply Love: if DISPLAYBPM values are <= 0, use actual BPMs instead.
    if (display_min <= 0.0f || display_max <= 0.0f) {
        display_min = actual_min;
        display_max = actual_max;
    }

    out_actual_min = static_cast<double>(actual_min);
    out_actual_max = static_cast<double>(actual_max);
    out_display_min = static_cast<double>(display_min) * music_rate;
    out_display_max = static_cast<double>(display_max) * music_rate;
    out_display_str = stringify_display_bpms_like_simply_love(out_display_min, out_display_max, music_rate);
}

static bool load_song(const std::string& simfile_path, Song& song) {
    RString ext = GetExtension(simfile_path);
    ext.MakeLower();
    if (ext == "ssc" || ext == "ats") {
        SSCLoader loader;
        return loader.LoadFromSimfile(simfile_path, song, false);
    }
    if (ext == "sm" || ext == "sma") {
        SMLoader loader;
        return loader.LoadFromSimfile(simfile_path, song, false);
    }
    return false;
}

static std::string raw_bpms_from_msd(const std::string& simfile_path,
                                     const std::string& steps_type,
                                     const std::string& difficulty,
                                     const std::string& description) {
    MsdFile msd;
    if (!msd.ReadFile(simfile_path, true)) {
        return {};
    }

    RString ext = GetExtension(simfile_path);
    ext.MakeLower();

    auto normalize_steps = [&](const RString& value) -> std::string {
        RString out = value;
        Trim(out);
        return normalize_steps_type_string(out.c_str());
    };
    auto normalize_diff = [&](const RString& value) -> std::string {
        RString out = value;
        Trim(out);
        return to_lower(out.c_str());
    };
    auto normalize_desc = [&](const RString& value) -> std::string {
        RString out = value;
        Trim(out);
        return out.c_str();
    };

    if (ext != "ssc" && ext != "ats") {
        const unsigned values = msd.GetNumValues();
        for (unsigned i = 0; i < values; ++i) {
            const MsdFile::value_t& params = msd.GetValue(i);
            RString tag = params[0];
            tag.MakeUpper();
            if (tag == "BPMS") {
                return params[1];
            }
        }
        return {};
    }

    RString top_bpms;
    bool in_steps = false;
    RString step_type_raw;
    RString diff_raw;
    RString desc_raw;
    RString chart_bpms;

    const unsigned values = msd.GetNumValues();
    for (unsigned i = 0; i < values; ++i) {
        const MsdFile::value_t& params = msd.GetValue(i);
        RString tag = params[0];
        tag.MakeUpper();

        if (!in_steps) {
            if (tag == "BPMS") {
                top_bpms = params[1];
            } else if (tag == "NOTEDATA") {
                in_steps = true;
                step_type_raw = "";
                diff_raw = "";
                desc_raw = "";
                chart_bpms = "";
            }
            continue;
        }

        if (tag == "STEPSTYPE") {
            step_type_raw = params[1];
        } else if (tag == "DIFFICULTY") {
            diff_raw = params[1];
        } else if (tag == "DESCRIPTION") {
            desc_raw = params[1];
        } else if (tag == "BPMS") {
            chart_bpms = params[1];
        } else if (tag == "NOTES" || tag == "NOTES2" || tag == "STEPFILENAME") {
            const std::string step_type_norm = normalize_steps(step_type_raw);
            const std::string diff_norm = normalize_diff(diff_raw);
            const std::string desc_norm = normalize_desc(desc_raw);

            bool match = (steps_type.empty() || step_type_norm == steps_type) &&
                (difficulty.empty() || diff_norm == difficulty);
            if (match && diff_norm == "edit" && !description.empty()) {
                match = desc_norm == description;
            }

            if (match) {
                if (!chart_bpms.empty()) return chart_bpms;
                if (!top_bpms.empty()) return top_bpms;
                return {};
            }
            in_steps = false;
        }
    }

    if (!top_bpms.empty()) return top_bpms;
    return {};
}

struct FallbackBpmOverride {
    std::string bpms;
};

static int lua_normalize_float_digits_override(lua_State* L) {
    auto* ov = static_cast<FallbackBpmOverride*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!ov) {
        lua_pushstring(L, "");
        return 1;
    }
    lua_pushlstring(L, ov->bpms.data(), ov->bpms.size());
    return 1;
}

static bool install_normalize_bpms_override(lua_State* L, FallbackBpmOverride* ov) {
    lua_getglobal(L, "ParseChartInfo");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    bool replaced = false;
    for (int i = 1;; ++i) {
        const char* upname = lua_getupvalue(L, -1, i);
        if (!upname) break;
        if (std::string_view(upname) == "GetSimfileChartString") {
            for (int j = 1;; ++j) {
                const char* inner = lua_getupvalue(L, -1, j);
                if (!inner) break;
                if (std::string_view(inner) == "NormalizeFloatDigits") {
                    lua_pop(L, 1);
                    lua_pushlightuserdata(L, ov);
                    lua_pushcclosure(L, lua_normalize_float_digits_override, 1);
                    lua_setupvalue(L, -2, j);
                    replaced = true;
                    break;
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
            break;
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
    return replaced;
}

// ---------------------------------------------------------------------------
// Lua-driven hash using Simply Love's chart parser (no Lua file modifications).
namespace {
struct LuaStepsCtx {
    std::string filename;
    std::string steps_type;
    std::string difficulty;
    std::string description;
    TimingData* timing = nullptr;
};

static int lua_steps_getfilename(lua_State* L) {
    auto* ctx = static_cast<LuaStepsCtx*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushstring(L, ctx->filename.c_str());
    return 1;
}

static int lua_steps_getstepstype(lua_State* L) {
    auto* ctx = static_cast<LuaStepsCtx*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushstring(L, ctx->steps_type.c_str());
    return 1;
}

static int lua_steps_getdifficulty(lua_State* L) {
    auto* ctx = static_cast<LuaStepsCtx*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushstring(L, ctx->difficulty.c_str());
    return 1;
}

static int lua_steps_getdescription(lua_State* L) {
    auto* ctx = static_cast<LuaStepsCtx*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_pushstring(L, ctx->description.c_str());
    return 1;
}

static int lua_steps_gettimingdata(lua_State* L) {
    auto* ctx = static_cast<LuaStepsCtx*>(lua_touserdata(L, lua_upvalueindex(1)));
    lua_newtable(L);
    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, [](lua_State* Linner) -> int {
        auto* innerCtx = static_cast<LuaStepsCtx*>(lua_touserdata(Linner, lua_upvalueindex(1)));
        double beat = luaL_optnumber(Linner, 2, 0.0);
        double seconds = innerCtx && innerCtx->timing ? innerCtx->timing->GetElapsedTimeFromBeat(static_cast<float>(beat)) : beat;
        lua_pushnumber(Linner, seconds);
        return 1;
    }, 1);
    lua_setfield(L, -2, "GetElapsedTimeFromBeat");
    return 1;
}

static int lua_steps_calculatetechcounts(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* Linner) -> int {
        lua_pushnumber(Linner, 0);
        return 1;
    });
    lua_setfield(L, -2, "GetValue");
    return 1;
}

static void push_steps_userdata(lua_State* L, LuaStepsCtx* ctx) {
    lua_newtable(L);

    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, lua_steps_getfilename, 1);
    lua_setfield(L, -2, "GetFilename");

    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, lua_steps_getstepstype, 1);
    lua_setfield(L, -2, "GetStepsType");

    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, lua_steps_getdifficulty, 1);
    lua_setfield(L, -2, "GetDifficulty");

    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, lua_steps_getdescription, 1);
    lua_setfield(L, -2, "GetDescription");

    lua_pushlightuserdata(L, ctx);
    lua_pushcclosure(L, lua_steps_gettimingdata, 1);
    lua_setfield(L, -2, "GetTimingData");

    lua_pushcfunction(L, lua_steps_calculatetechcounts);
    lua_setfield(L, -2, "CalculateTechCounts");
}

static int lua_ragefile_open(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    const char* path = luaL_checkstring(L, 2);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    lua_pushstring(L, ss.str().c_str());
    lua_setfield(L, 1, "_contents");
    lua_pushboolean(L, 1);
    return 1;
}

static int lua_ragefile_create(lua_State* L) {
    lua_newtable(L);
    lua_pushcfunction(L, lua_ragefile_open);
    lua_setfield(L, -2, "Open");

    lua_pushcfunction(L, [](lua_State* Linner) -> int {
        luaL_checktype(Linner, 1, LUA_TTABLE);
        lua_getfield(Linner, 1, "_contents");
        return 1;
    });
    lua_setfield(L, -2, "Read");

    lua_pushcfunction(L, [](lua_State*) -> int { return 0; });
    lua_setfield(L, -2, "destroy");
    return 1;
}

static int lua_cryptman_sha1(lua_State* L) {
    size_t len = 0;
    const char* data = luaL_checklstring(L, 2, &len);
    unsigned char out[20];
    hash_state hs;
    sha1_init(&hs);
    sha1_process(&hs, reinterpret_cast<const unsigned char*>(data), static_cast<unsigned long>(len));
    sha1_done(&hs, out);
    lua_pushlstring(L, reinterpret_cast<const char*>(out), sizeof(out));
    return 1;
}

static int lua_binary_to_hex(lua_State* L) {
    size_t len = 0;
    const unsigned char* data = reinterpret_cast<const unsigned char*>(luaL_checklstring(L, 1, &len));
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out.push_back(hex[data[i] >> 4]);
        out.push_back(hex[data[i] & 0x0F]);
    }
    lua_pushlstring(L, out.c_str(), out.size());
    return 1;
}

static int lua_toenumshort(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    lua_pushstring(L, s);
    return 1;
}

static int lua_oldstyle_diff(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    lua_pushstring(L, s);
    return 1;
}

static int lua_ivalues_iter(lua_State* L) {
    lua_Integer idx = lua_tointeger(L, lua_upvalueindex(2)) + 1;
    lua_pushinteger(L, idx);
    lua_replace(L, lua_upvalueindex(2));

    lua_pushvalue(L, lua_upvalueindex(1)); // table
    lua_pushinteger(L, idx);
    lua_gettable(L, -2);
    lua_remove(L, -2); // remove table
    if (lua_isnil(L, -1)) return 0;
    return 1;
}

static int lua_ivalues(lua_State* L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_pushvalue(L, 1);     // upvalue 1: table
    lua_pushinteger(L, 0);   // upvalue 2: current index
    lua_pushcclosure(L, lua_ivalues_iter, 2);
    return 1;
}

static bool extract_sl_hash_bpms(lua_State* L,
                                 LuaStepsCtx* ctx,
                                 const std::string& steps_type,
                                 const std::string& difficulty,
                                 const std::string& description,
                                 std::string* out_hash_bpms) {
    if (!out_hash_bpms) return false;
    out_hash_bpms->clear();

    const int top = lua_gettop(L);

    lua_getglobal(L, "ParseChartInfo");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return false;
    }
    const int parse_idx = lua_gettop(L);

    int ref_get_simfile_string = LUA_NOREF;
    int ref_get_simfile_chart_string = LUA_NOREF;

    for (int i = 1;; ++i) {
        const char* upname = lua_getupvalue(L, parse_idx, i);
        if (!upname) break;

        const std::string_view name_sv{upname};
        if (name_sv == "GetSimfileString") {
            ref_get_simfile_string = luaL_ref(L, LUA_REGISTRYINDEX);
            continue;
        }
        if (name_sv == "GetSimfileChartString") {
            ref_get_simfile_chart_string = luaL_ref(L, LUA_REGISTRYINDEX);
            continue;
        }
        lua_pop(L, 1);
    }

    lua_pop(L, 1); // pop ParseChartInfo

    auto cleanup = [&]() {
        if (ref_get_simfile_string != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref_get_simfile_string);
        }
        if (ref_get_simfile_chart_string != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref_get_simfile_chart_string);
        }
        lua_settop(L, top);
    };

    if (ref_get_simfile_string == LUA_NOREF || ref_get_simfile_chart_string == LUA_NOREF) {
        cleanup();
        return false;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref_get_simfile_string);
    push_steps_userdata(L, ctx);
    if (lua_pcall(L, 1, 2, 0) != 0) {
        cleanup();
        return false;
    }
    const std::string simfile_string = lua_tostring(L, -2) ? lua_tostring(L, -2) : "";
    const std::string file_type = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 2);

    if (simfile_string.empty() || file_type.empty()) {
        cleanup();
        return false;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref_get_simfile_chart_string);
    lua_pushlstring(L, simfile_string.data(), simfile_string.size());
    lua_pushstring(L, steps_type.c_str());
    lua_pushstring(L, difficulty.c_str());
    lua_pushstring(L, description.c_str());
    lua_pushstring(L, file_type.c_str());
    if (lua_pcall(L, 5, 2, 0) != 0) {
        cleanup();
        return false;
    }

    if (lua_isstring(L, -1)) {
        *out_hash_bpms = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    }
    lua_pop(L, 2);

    cleanup();
    return !out_hash_bpms->empty();
}

static std::string build_ssc_stub_simfile(std::string_view steps_type,
                                          std::string_view description,
                                          std::string_view difficulty,
                                          int meter,
                                          std::string_view bpms,
                                          std::string_view note_data) {
    std::string out;
    out.reserve(bpms.size() + note_data.size() + steps_type.size() + description.size() + difficulty.size() + 160);
    out.append("#BPMS:");
    out.append(bpms);
    out.append(";\n#NOTEDATA:\n");
    out.append("#STEPSTYPE:");
    out.append(steps_type);
    out.append(";\n#DESCRIPTION:");
    out.append(description);
    out.append(";\n#DIFFICULTY:");
    out.append(difficulty);
    out.append(";\n#METER:");
    out.append(std::to_string(meter));
    out.append(";\n#NOTES:\n");
    out.append(note_data);
    if (note_data.empty() || note_data.back() != '\n') {
        out.push_back('\n');
    }
    out.append(";\n");
    return out;
}

struct FallbackSimfileOverride {
    std::string simfile_string;
    std::string file_type;
};

struct ParseUpvalueOverride {
    int index = 0;
    int original_ref = LUA_NOREF;
};

static int lua_get_simfile_string_override(lua_State* L) {
    auto* ov = static_cast<FallbackSimfileOverride*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!ov) return 0;
    lua_pushlstring(L, ov->simfile_string.data(), ov->simfile_string.size());
    lua_pushlstring(L, ov->file_type.data(), ov->file_type.size());
    return 2;
}

static ParseUpvalueOverride install_parsechartinfo_upvalue_override(
    lua_State* L,
    std::string_view upvalue_name,
    lua_CFunction replacement,
    void* replacement_ctx) {
    ParseUpvalueOverride out;
    const int top = lua_gettop(L);

    lua_getglobal(L, "ParseChartInfo");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return out;
    }

    for (int i = 1;; ++i) {
        const char* name = lua_getupvalue(L, -1, i);
        if (!name) break;
        if (std::string_view(name) == upvalue_name) {
            out.index = i;
            out.original_ref = luaL_ref(L, LUA_REGISTRYINDEX);
            lua_pushlightuserdata(L, replacement_ctx);
            lua_pushcclosure(L, replacement, 1);
            lua_setupvalue(L, -2, i);
            lua_settop(L, top);
            return out;
        }
        lua_pop(L, 1);
    }

    lua_settop(L, top);
    return out;
}

static void restore_parsechartinfo_upvalue_override(lua_State* L, const ParseUpvalueOverride& ov) {
    if (ov.index <= 0 || ov.original_ref == LUA_NOREF) return;

    const int top = lua_gettop(L);
    lua_getglobal(L, "ParseChartInfo");
    if (lua_isfunction(L, -1)) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, ov.original_ref);
        lua_setupvalue(L, -2, ov.index);
    } else {
        lua_pop(L, 1);
    }
    luaL_unref(L, LUA_REGISTRYINDEX, ov.original_ref);
    lua_settop(L, top);
}

static void clear_stream_cache(lua_State* L, const char* pn) {
    const int top = lua_gettop(L);
    lua_getglobal(L, "SL");
    if (!lua_istable(L, -1)) {
        lua_settop(L, top);
        return;
    }
    lua_getfield(L, -1, pn);
    lua_getfield(L, -1, "Streams");
    if (lua_istable(L, -1)) {
        lua_pushstring(L, "");
        lua_setfield(L, -2, "Filename");
        lua_pushstring(L, "");
        lua_setfield(L, -2, "StepsType");
        lua_pushstring(L, "");
        lua_setfield(L, -2, "Difficulty");
        lua_pushstring(L, "");
        lua_setfield(L, -2, "Description");
    }
    lua_settop(L, top);
}

static int get_simfile_chart_string_ref(lua_State* L) {
    const int top = lua_gettop(L);
    lua_getglobal(L, "ParseChartInfo");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return LUA_NOREF;
    }

    int ref = LUA_NOREF;
    for (int i = 1;; ++i) {
        const char* upname = lua_getupvalue(L, -1, i);
        if (!upname) break;
        if (std::string_view(upname) == "GetSimfileChartString") {
            ref = luaL_ref(L, LUA_REGISTRYINDEX);
            break;
        }
        lua_pop(L, 1);
    }

    lua_settop(L, top);
    return ref;
}

static bool call_get_simfile_chart_string(lua_State* L,
                                          int func_ref,
                                          const std::string& simfile_string,
                                          const std::string& steps_type,
                                          const std::string& difficulty,
                                          const std::string& description,
                                          const std::string& file_type,
                                          std::string* out_chart_string,
                                          std::string* out_bpms) {
    if (func_ref == LUA_NOREF) return false;

    const int top = lua_gettop(L);
    lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);
    lua_pushlstring(L, simfile_string.data(), simfile_string.size());
    lua_pushstring(L, steps_type.c_str());
    lua_pushstring(L, difficulty.c_str());
    lua_pushstring(L, description.c_str());
    lua_pushstring(L, file_type.c_str());
    if (lua_pcall(L, 5, 2, 0) != 0) {
        lua_settop(L, top);
        return false;
    }

    if (out_chart_string) {
        *out_chart_string = lua_tostring(L, -2) ? lua_tostring(L, -2) : "";
    }
    if (out_bpms) {
        *out_bpms = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    }
    lua_settop(L, top);
    return true;
}

static std::string compute_sl_hash(lua_State* L, std::string_view chart_string, std::string_view bpms) {
    if (chart_string.empty() || bpms.empty()) return "";

    std::string data;
    data.reserve(chart_string.size() + bpms.size());
    data.append(chart_string);
    data.append(bpms);

    const int top = lua_gettop(L);
    lua_getglobal(L, "CRYPTMAN");
    lua_getfield(L, -1, "SHA1String");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return "";
    }
    lua_pushvalue(L, -2);
    lua_pushlstring(L, data.data(), data.size());
    if (lua_pcall(L, 2, 1, 0) != 0) {
        lua_settop(L, top);
        return "";
    }
    lua_getglobal(L, "BinaryToHex");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return "";
    }
    lua_pushvalue(L, -2);
    if (lua_pcall(L, 1, 1, 0) != 0) {
        lua_settop(L, top);
        return "";
    }

    const char* hex = lua_tostring(L, -1);
    std::string out = hex ? hex : "";
    if (out.size() > 16) out.resize(16);
    lua_settop(L, top);
    return out;
}

static std::string fallback_hash_from_notes(lua_State* L,
                                            const Steps* steps,
                                            const std::string& steps_type,
                                            const std::string& difficulty,
                                            const std::string& description,
                                            const std::string& hash_bpms) {
    if (!L || !steps || hash_bpms.empty()) return "";

    RString note_data_raw;
    steps->GetSMNoteData(note_data_raw);
    if (note_data_raw.empty()) return "";

    const std::string simfile_stub =
        build_ssc_stub_simfile(steps_type, description, difficulty, steps->GetMeter(), hash_bpms,
                               std::string_view(note_data_raw.data(), note_data_raw.size()));

    const int chart_ref = get_simfile_chart_string_ref(L);
    if (chart_ref == LUA_NOREF) return "";

    std::string chart_string;
    (void)call_get_simfile_chart_string(L, chart_ref, simfile_stub, steps_type, difficulty, description, "ssc",
                                        &chart_string, nullptr);
    luaL_unref(L, LUA_REGISTRYINDEX, chart_ref);

    if (chart_string.empty()) return "";
    return compute_sl_hash(L, chart_string, hash_bpms);
}

static bool fallback_parse_from_notes(lua_State* L,
                                      LuaStepsCtx* ctx,
                                      const Steps* steps,
                                      const std::string& steps_type,
                                      const std::string& difficulty,
                                      const std::string& description,
                                      const std::string& hash_bpms) {
    if (!L || !ctx || !steps || hash_bpms.empty()) return false;

    RString note_data_raw;
    steps->GetSMNoteData(note_data_raw);
    if (note_data_raw.empty()) return false;

    FallbackSimfileOverride simfile_override;
    simfile_override.simfile_string = build_ssc_stub_simfile(
        steps_type, description, difficulty, steps->GetMeter(), hash_bpms,
        std::string_view(note_data_raw.data(), note_data_raw.size()));
    simfile_override.file_type = "ssc";

    const ParseUpvalueOverride upvalue = install_parsechartinfo_upvalue_override(
        L, "GetSimfileString", lua_get_simfile_string_override, &simfile_override);
    if (upvalue.index == 0) return false;

    clear_stream_cache(L, "P1");

    const int top = lua_gettop(L);
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2);
    int errfunc = lua_gettop(L);

    lua_getglobal(L, "ParseChartInfo");
    push_steps_userdata(L, ctx);
    lua_pushstring(L, "P1");
    bool ok = true;
    if (lua_pcall(L, 2, 0, errfunc) != 0) {
        lua_pop(L, 1);
        ok = false;
    }
    lua_pop(L, 1);

    restore_parsechartinfo_upvalue_override(L, upvalue);
    lua_settop(L, top);
    return ok;
}

static std::string compute_hash_with_lua(const std::string& simfile_path,
                                         const std::string& steps_type,
                                         const std::string& difficulty,
                                         const std::string& description,
                                         const Steps* steps,
                                         TimingData* timing,
                                         std::string* out_hash_bpms,
                                         std::string* breakdown_text,
                                         std::vector<std::string>* breakdown_levels,
                                         int* stream_measures,
                                         int* break_measures,
                                         std::vector<StreamSequenceOut>* stream_sequences,
                                         std::vector<int>* lua_notes_per_measure,
                                         std::vector<double>* lua_nps_per_measure,
                                         std::vector<bool>* lua_equally_spaced,
                                         double* lua_peak_nps) {
    lua_State* L = luaL_newstate();
    if (!L) return "";
    luaL_openlibs(L);

    lua_newtable(L);
    lua_pushcfunction(L, lua_ragefile_create);
    lua_setfield(L, -2, "CreateRageFile");
    lua_setglobal(L, "RageFileUtil");

    lua_newtable(L);
    lua_pushcfunction(L, lua_cryptman_sha1);
    lua_setfield(L, -2, "SHA1String");
    lua_setglobal(L, "CRYPTMAN");

    lua_pushcfunction(L, lua_binary_to_hex);
    lua_setglobal(L, "BinaryToHex");
    lua_pushcfunction(L, lua_toenumshort);
    lua_setglobal(L, "ToEnumShortString");
    lua_pushcfunction(L, lua_oldstyle_diff);
    lua_setglobal(L, "OldStyleStringToDifficulty");
    lua_pushcfunction(L, lua_ivalues);
    lua_setglobal(L, "ivalues");
    lua_pushcfunction(L, [](lua_State* Linner) -> int {
        if (!lua_istable(Linner, 1)) { lua_pushboolean(Linner, 0); return 1; }
        lua_pushnil(Linner);
        bool has = lua_next(Linner, 1) != 0;
        if (has) lua_pop(Linner, 2); // pop value + key
        lua_pushboolean(Linner, has);
        return 1;
    });
    lua_setglobal(L, "TableContainsData");

    lua_newtable(L);
    lua_newtable(L);
    lua_pushnumber(L, 0);
    lua_setfield(L, -2, "ColumnCueMinTime");
    lua_setfield(L, -2, "Global");
    lua_newtable(L); lua_newtable(L); lua_setfield(L, -2, "Streams"); lua_setfield(L, -2, "P1");
    lua_newtable(L); lua_newtable(L); lua_setfield(L, -2, "Streams"); lua_setfield(L, -2, "P2");
    lua_setglobal(L, "SL");

    lua_pushstring(L, "P1");
    lua_setglobal(L, "player");

    const std::string parser_path = "src/extern/itgmania/Themes/Simply Love/Scripts/SL-ChartParser.lua";
    if (!load_lua_chunk(L, parser_path, embedded_lua::kSLChartParserLua, "@SL-ChartParser.lua")) {
        lua_close(L);
        return "";
    }
    const std::string helper_path = "src/extern/itgmania/Themes/Simply Love/Scripts/SL-ChartParserHelpers.lua";
    if (!load_lua_chunk(L, helper_path, embedded_lua::kSLChartParserHelpersLua, "@SL-ChartParserHelpers.lua")) {
        lua_close(L);
        return "";
    }

    LuaStepsCtx ctx{simfile_path, steps_type, difficulty, description, timing};
    const bool has_hash_bpms = extract_sl_hash_bpms(L, &ctx, steps_type, difficulty, description, out_hash_bpms);
    FallbackBpmOverride bpm_override;
    if (!has_hash_bpms) {
        std::string fallback_bpms;
        if (timing) {
            fallback_bpms = bpm_string_from_timing(timing);
        }
        if (fallback_bpms.empty()) {
            fallback_bpms = raw_bpms_from_msd(simfile_path, steps_type, difficulty, description);
        }
        if (!fallback_bpms.empty()) {
            if (out_hash_bpms) {
                *out_hash_bpms = fallback_bpms;
            }
            bpm_override.bpms = fallback_bpms;
            install_normalize_bpms_override(L, &bpm_override);
        }
    }
    // Push an error handler to capture Lua stack traces.
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2); // remove debug table, leave traceback on stack
    int errfunc = lua_gettop(L);

    lua_getglobal(L, "ParseChartInfo");
    push_steps_userdata(L, &ctx);
    lua_pushstring(L, "P1");
    if (lua_pcall(L, 2, 0, errfunc) != 0) {
        lua_pop(L, 1);
        lua_close(L);
        return "";
    }
    lua_pop(L, 1); // pop traceback handler

    lua_getglobal(L, "SL");
    lua_getfield(L, -1, "P1");
    lua_getfield(L, -1, "Streams");
    lua_getfield(L, -1, "Hash");
    std::string result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    if (result.empty() && steps && out_hash_bpms && !out_hash_bpms->empty()) {
        // Fallback: re-run SL parser with ITGmania note data to populate streams/hashes.
        if (fallback_parse_from_notes(L, &ctx, steps, steps_type, difficulty, description, *out_hash_bpms)) {
            lua_getfield(L, -1, "Hash");
            result = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
            lua_pop(L, 1);
        }
        if (result.empty()) {
            const std::string fallback = fallback_hash_from_notes(
                L, steps, steps_type, difficulty, description, *out_hash_bpms);
            if (!fallback.empty()) {
                result = fallback;
            }
        }
    }

    auto load_int_table = [&](const char* name, std::vector<int>& out) {
        lua_getfield(L, -1, name);
        if (lua_istable(L, -1)) {
            size_t len = lua_objlen(L, -1);
            out.resize(len);
            for (size_t i = 0; i < len; ++i) {
                lua_rawgeti(L, -1, static_cast<int>(i + 1));
                out[i] = static_cast<int>(lua_tointeger(L, -1));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    };
    auto load_double_table = [&](const char* name, std::vector<double>& out) {
        lua_getfield(L, -1, name);
        if (lua_istable(L, -1)) {
            size_t len = lua_objlen(L, -1);
            out.resize(len);
            for (size_t i = 0; i < len; ++i) {
                lua_rawgeti(L, -1, static_cast<int>(i + 1));
                out[i] = lua_tonumber(L, -1);
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    };
    auto load_bool_table = [&](const char* name, std::vector<bool>& out) {
        lua_getfield(L, -1, name);
        if (lua_istable(L, -1)) {
            size_t len = lua_objlen(L, -1);
            out.resize(len);
            for (size_t i = 0; i < len; ++i) {
                lua_rawgeti(L, -1, static_cast<int>(i + 1));
                out[i] = lua_toboolean(L, -1) != 0;
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    };

    if (lua_notes_per_measure) load_int_table("NotesPerMeasure", *lua_notes_per_measure);
    if (lua_nps_per_measure) load_double_table("NPSperMeasure", *lua_nps_per_measure);
    if (lua_equally_spaced) load_bool_table("EquallySpacedPerMeasure", *lua_equally_spaced);
    if (lua_peak_nps) {
        lua_getfield(L, -1, "PeakNPS");
        *lua_peak_nps = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    if (stream_sequences) {
        stream_sequences->clear();
        lua_getglobal(L, "GetStreamSequences");
        lua_getfield(L, -2, "NotesPerMeasure");
        if (!lua_istable(L, -1)) {
            lua_pop(L, 2); // pop non-table + function
        } else {
            lua_pushinteger(L, 16);
            if (lua_pcall(L, 2, 1, 0) != 0) {
                lua_pop(L, 1);
            } else if (lua_istable(L, -1)) {
                size_t len = lua_objlen(L, -1);
                stream_sequences->reserve(len);
                for (size_t i = 0; i < len; ++i) {
                    lua_rawgeti(L, -1, static_cast<int>(i + 1));
                    if (lua_istable(L, -1)) {
                        StreamSequenceOut seg{};
                        lua_getfield(L, -1, "streamStart");
                        seg.stream_start = static_cast<int>(lua_tointeger(L, -1));
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "streamEnd");
                        seg.stream_end = static_cast<int>(lua_tointeger(L, -1));
                        lua_pop(L, 1);
                        lua_getfield(L, -1, "isBreak");
                        seg.is_break = lua_toboolean(L, -1) != 0;
                        lua_pop(L, 1);
                        stream_sequences->push_back(seg);
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1); // pop result table
            } else {
                lua_pop(L, 1);
            }
        }
    }
    auto call_breakdown = [&](int level, std::string& out) {
        lua_getglobal(L, "GenerateBreakdownText");
        lua_pushstring(L, "P1");
        lua_pushinteger(L, level);
        if (lua_pcall(L, 2, 1, 0) != 0) {
            lua_pop(L, 1);
            return;
        }
        if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
        lua_pop(L, 1);
    };
    if (breakdown_text) call_breakdown(0, *breakdown_text);
    if (breakdown_levels) {
        breakdown_levels->resize(4);
        for (int i = 0; i < 4; ++i) call_breakdown(i, (*breakdown_levels)[i]);
    }
    if (stream_measures && break_measures) {
        lua_getglobal(L, "GetTotalStreamAndBreakMeasures");
        lua_pushstring(L, "P1");
        if (lua_pcall(L, 1, 2, 0) != 0) {
            lua_pop(L, 1);
        } else {
            *stream_measures = static_cast<int>(lua_tointeger(L, -2));
            *break_measures = static_cast<int>(lua_tointeger(L, -1));
            lua_pop(L, 2);
        }
    }

    lua_close(L);
    return result;
}
} // namespace

struct RadarCountsOut {
    int holds = 0;
    int mines = 0;
    int rolls = 0;
    int taps_and_holds = 0;
    int notes = 0;
    int lifts = 0;
    int fakes = 0;
    int jumps = 0;
    int hands = 0;
    int quads = 0;
};

static RadarCountsOut get_radar_counts(const RadarValues& radar) {
    RadarCountsOut out;
    out.holds = static_cast<int>(radar[RadarCategory_Holds]);
    out.mines = static_cast<int>(radar[RadarCategory_Mines]);
    out.rolls = static_cast<int>(radar[RadarCategory_Rolls]);
    out.taps_and_holds = static_cast<int>(radar[RadarCategory_TapsAndHolds]);
    out.notes = static_cast<int>(radar[RadarCategory_Notes]);
    out.lifts = static_cast<int>(radar[RadarCategory_Lifts]);
    out.fakes = static_cast<int>(radar[RadarCategory_Fakes]);
    out.jumps = static_cast<int>(radar[RadarCategory_Jumps]);
    out.hands = static_cast<int>(radar[RadarCategory_Hands]);
    out.quads = static_cast<int>(radar[RadarCategory_Hands]); // quads not separately tracked; reuse hands
    return out;
}

static bool steps_supports_itgmania_notedata(const Steps* steps) {
    if (!steps || !GAMEMAN) return false;
    return GAMEMAN->GetStepsTypeInfo(steps->m_StepsType).iNumTracks > 0;
}

static void prepare_steps_for_metrics(Steps* steps, TimingData* timing) {
    (void)timing;
    steps->CalculateStepStats(0.0f);
    steps->CalculateGrooveStatsHash();
    steps->CalculateTechCounts();
    steps->CalculateMeasureInfo();
}

struct MeasureStatsOut {
    std::vector<int> notes_per_measure;
    std::vector<double> nps_per_measure;
    std::vector<bool> equally_spaced_per_measure;
    double peak_nps = 0.0;
    int total_steps = 0;
};

static MeasureStatsOut get_measure_stats(
    Steps* steps,
    std::vector<int> lua_notes_pm,
    std::vector<double> lua_nps_pm,
    std::vector<bool> lua_equally_spaced,
    double lua_peak_nps,
    bool allow_steps_fallback) {
    MeasureStatsOut out;
    if (lua_notes_pm.empty()) {
        if (!allow_steps_fallback || !steps) {
            return out;
        }
        const std::vector<int>& npm = steps->GetNotesPerMeasure(PLAYER_1);
        out.notes_per_measure.assign(npm.begin(), npm.end());

        const std::vector<float>& nps = steps->GetNpsPerMeasure(PLAYER_1);
        out.nps_per_measure.reserve(nps.size());
        for (float v : nps) out.nps_per_measure.push_back(static_cast<double>(v));
    } else {
        out.notes_per_measure = std::move(lua_notes_pm);
        out.nps_per_measure = std::move(lua_nps_pm);
    }

    out.total_steps = static_cast<int>(std::accumulate(out.notes_per_measure.begin(), out.notes_per_measure.end(), 0));
    if (!lua_equally_spaced.empty()) {
        out.equally_spaced_per_measure = std::move(lua_equally_spaced);
        out.peak_nps = lua_peak_nps;
    } else {
        // Fallback: mark all measures as not guaranteed equally spaced and use computed peak.
        out.equally_spaced_per_measure.assign(out.notes_per_measure.size(), false);
        out.peak_nps = 0.0;
        for (double v : out.nps_per_measure) out.peak_nps = std::max(out.peak_nps, v);
    }
    return out;
}

static double get_duration_seconds(Steps* steps, TimingData* timing) {
    NoteData nd;
    steps->GetNoteData(nd);
    if (nd.IsEmpty()) {
        return 0.0;
    }
    const float last_beat = nd.GetLastBeat();
    return timing->GetElapsedTimeFromBeat(last_beat);
}

static double get_duration_seconds_from_measure_count(TimingData* timing, size_t measure_count) {
    if (!timing || measure_count == 0) return 0.0;
    const float end_beat = static_cast<float>(measure_count) * 4.0f;
    return timing->GetElapsedTimeFromBeat(end_beat);
}

static void fill_timing_tables(ChartMetrics& out, TimingData* td) {
    out.beat0_offset_seconds = static_cast<double>(td->m_fBeat0OffsetInSeconds);
    out.beat0_group_offset_seconds = static_cast<double>(td->m_fBeat0GroupOffsetInSeconds);
    out.timing_bpms = timing_segments_to_number_table(td, SEGMENT_BPM);
    out.timing_stops = timing_segments_to_number_table(td, SEGMENT_STOP);
    out.timing_delays = timing_segments_to_number_table(td, SEGMENT_DELAY);
    out.timing_time_signatures = timing_segments_to_number_table(td, SEGMENT_TIME_SIG);
    out.timing_warps = timing_segments_to_number_table(td, SEGMENT_WARP);
    out.timing_labels = timing_labels_to_table(td);
    out.timing_tickcounts = timing_segments_to_number_table(td, SEGMENT_TICKCOUNT);
    out.timing_combos = timing_segments_to_number_table(td, SEGMENT_COMBO);
    out.timing_speeds = timing_segments_to_number_table(td, SEGMENT_SPEED);
    out.timing_scrolls = timing_segments_to_number_table(td, SEGMENT_SCROLL);
    out.timing_fakes = timing_segments_to_number_table(td, SEGMENT_FAKE);
}

static void fill_tech_counts(ChartMetrics& out, const TechCounts& tech) {
    out.tech.crossovers = static_cast<int>(tech[TechCountsCategory_Crossovers]);
    out.tech.footswitches = static_cast<int>(tech[TechCountsCategory_Footswitches]);
    out.tech.sideswitches = static_cast<int>(tech[TechCountsCategory_Sideswitches]);
    out.tech.jacks = static_cast<int>(tech[TechCountsCategory_Jacks]);
    out.tech.brackets = static_cast<int>(tech[TechCountsCategory_Brackets]);
    out.tech.doublesteps = static_cast<int>(tech[TechCountsCategory_Doublesteps]);
}

static ChartMetrics build_metrics_for_steps(const std::string& simfile_path, Steps* steps, const Song& song) {
    TimingData* const td = steps->GetTimingData();
    td->TidyUpData(false);

    const std::string st_str = steps_type_string(steps);
    const std::string diff_str = diff_string(steps->GetDifficulty());

    const bool can_compute_notedata_metrics = steps_supports_itgmania_notedata(steps);
    if (can_compute_notedata_metrics) {
        prepare_steps_for_metrics(steps, td);
    }

    ChartMetrics out;
    out.status = can_compute_notedata_metrics ? "ok" : "unsupported_steps_type";
    out.simfile = simfile_path;
    apply_song_metadata_fallback(song, simfile_path, out.title, out.subtitle, out.artist);
    compute_display_metadata(
        song,
        out.title,
        out.subtitle,
        out.artist,
        out.title_translated,
        out.subtitle_translated,
        out.artist_translated);
    out.step_artist = steps->GetCredit();
    out.description = steps->GetDescription();
    std::vector<int> lua_notes_pm;
    std::vector<double> lua_nps_pm;
    std::vector<bool> lua_equally_spaced;
    double lua_peak_nps = 0.0;
    std::vector<std::string> breakdown_levels;
    int stream_measures = 0;
    int break_measures = 0;
    std::vector<StreamSequenceOut> stream_sequences;
    out.hash = compute_hash_with_lua(simfile_path, st_str, diff_str, steps->GetDescription(), steps, td,
                                     &out.hash_bpms,
                                     &out.streams_breakdown, &breakdown_levels, &stream_measures, &break_measures,
                                     &stream_sequences,
                                     &lua_notes_pm, &lua_nps_pm, &lua_equally_spaced, &lua_peak_nps);
    out.steps_type = st_str;
    out.difficulty = diff_str;
    out.meter = steps->GetMeter();
    out.bpms = bpm_string_from_timing(td);
    get_bpm_ranges_like_simply_love(steps, 1.0, out.bpm_min, out.bpm_max, out.display_bpm_min, out.display_bpm_max,
                                   out.display_bpm);

    const MeasureStatsOut measures = get_measure_stats(
        steps, std::move(lua_notes_pm), std::move(lua_nps_pm), std::move(lua_equally_spaced), lua_peak_nps,
        can_compute_notedata_metrics);
    out.total_steps = measures.total_steps;
    out.notes_per_measure = measures.notes_per_measure;
    out.nps_per_measure = measures.nps_per_measure;
    out.equally_spaced_per_measure = measures.equally_spaced_per_measure;
    out.peak_nps = measures.peak_nps;

    if (can_compute_notedata_metrics) {
        out.duration_seconds = get_duration_seconds(steps, td);
    } else {
        out.duration_seconds = get_duration_seconds_from_measure_count(td, out.notes_per_measure.size());
    }

    out.stream_sequences = std::move(stream_sequences);
    if (breakdown_levels.size() == 4) {
        out.streams_breakdown_level1 = breakdown_levels[1];
        out.streams_breakdown_level2 = breakdown_levels[2];
        out.streams_breakdown_level3 = breakdown_levels[3];
    }
    out.total_stream_measures = stream_measures;
    out.total_break_measures = break_measures;

    if (can_compute_notedata_metrics) {
        const TechCounts& tech = steps->GetTechCounts(PLAYER_1);
        const RadarValues radar = steps->GetRadarValues(PLAYER_1);
        const RadarCountsOut radar_counts = get_radar_counts(radar);

        out.holds = radar_counts.holds;
        out.mines = radar_counts.mines;
        out.rolls = radar_counts.rolls;
        out.taps_and_holds = radar_counts.taps_and_holds;
        out.notes = radar_counts.notes;
        out.lifts = radar_counts.lifts;
        out.fakes = radar_counts.fakes;
        out.jumps = radar_counts.jumps;
        out.hands = radar_counts.hands;
        out.quads = radar_counts.quads;

        fill_tech_counts(out, tech);
    }
    fill_timing_tables(out, td);
    return out;
}

static Steps* select_steps(
    const std::vector<Steps*>& steps,
    const std::string& steps_type_req,
    const std::string& difficulty_req,
    const std::string& description_req) {
    for (Steps* s : steps) {
        std::string st = steps_type_string(s);
        std::string diff = diff_string(s->GetDifficulty());
        std::string desc = s->GetDescription();
        if (!steps_type_req.empty() && st != steps_type_req) continue;
        if (!difficulty_req.empty() && diff != difficulty_req) continue;
        if (!description_req.empty() && desc != description_req) continue;
        return s;
    }
    if (steps_type_req.empty() && difficulty_req.empty() && description_req.empty() && !steps.empty()) {
        return steps.front();
    }
    return nullptr;
}

std::optional<ChartMetrics> parse_chart_with_itgmania(
    const std::string& simfile_path,
    const std::string& steps_type_req,
    const std::string& difficulty_req,
    const std::string& description_req) {
    // Ensure the engine singletons exist.
    init_singletons(0, nullptr);

    Song song;
    song.m_sSongFileName = simfile_path;
    song.SetSongDir(std::filesystem::path(simfile_path).parent_path().string().c_str());

    if (!load_song(simfile_path, song)) {
        std::fprintf(stderr, "LoadFromSimfile failed for %s\n", simfile_path.c_str());
        return std::nullopt;
    }

    Steps* steps = select_steps(song.GetAllSteps(), steps_type_req, difficulty_req, description_req);
    if (!steps) {
        std::fprintf(stderr, "No matching steps for %s\n", simfile_path.c_str());
        return std::nullopt;
    }

    return build_metrics_for_steps(simfile_path, steps, song);
}

std::vector<ChartMetrics> parse_all_charts_with_itgmania(
    const std::string& simfile_path,
    const std::string& steps_type_req,
    const std::string& difficulty_req,
    const std::string& description_req) {
    init_singletons(0, nullptr);

    Song song;
    song.m_sSongFileName = simfile_path;
    song.SetSongDir(std::filesystem::path(simfile_path).parent_path().string().c_str());

    std::vector<ChartMetrics> out;

    if (!load_song(simfile_path, song)) {
        std::fprintf(stderr, "LoadFromSimfile failed for %s\n", simfile_path.c_str());
        return out;
    }

    const auto& all_steps = song.GetAllSteps();
    for (Steps* steps : all_steps) {
        std::string st_str = steps_type_string(steps);
        std::string diff_str = diff_string(steps->GetDifficulty());
        if (!steps_type_req.empty() && st_str != steps_type_req) continue;
        if (!difficulty_req.empty() && diff_str != difficulty_req) continue;
        if (steps->GetDifficulty() == Difficulty_Edit && !description_req.empty() && steps->GetDescription() != description_req) continue;

        out.push_back(build_metrics_for_steps(simfile_path, steps, song));
    }

    return out;
}

#else
std::optional<ChartMetrics> parse_chart_with_itgmania(
    const std::string& simfile_path,
    const std::string& steps_type,
    const std::string& difficulty,
    const std::string& description) {
    (void)simfile_path;
    (void)steps_type;
    (void)difficulty;
    (void)description;
    return std::nullopt;
}
std::vector<ChartMetrics> parse_all_charts_with_itgmania(
    const std::string& simfile_path,
    const std::string& steps_type,
    const std::string& difficulty,
    const std::string& description) {
    (void)simfile_path;
    (void)steps_type;
    (void)difficulty;
    (void)description;
    return {};
}

#endif
