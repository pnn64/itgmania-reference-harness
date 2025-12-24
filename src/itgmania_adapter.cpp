extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "itgmania_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <numeric>
#include <fstream>
#include <filesystem>
#include <optional>
#include <cstdio>
#include <cmath>
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
#include "StepParityCost.h"
#include "TechCounts.h"
#include "ThemeManager.h"
#include "TimingData.h"

static bool g_enable_step_parity_trace = false;

void set_step_parity_trace_enabled(bool enabled) {
    g_enable_step_parity_trace = enabled;
}

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

static void extract_sl_hash_bpms(lua_State* L,
                                 LuaStepsCtx* ctx,
                                 const std::string& steps_type,
                                 const std::string& difficulty,
                                 const std::string& description,
                                 std::string* out_hash_bpms) {
    if (!out_hash_bpms) return;
    out_hash_bpms->clear();

    const int top = lua_gettop(L);

    lua_getglobal(L, "ParseChartInfo");
    if (!lua_isfunction(L, -1)) {
        lua_settop(L, top);
        return;
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
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref_get_simfile_string);
    push_steps_userdata(L, ctx);
    if (lua_pcall(L, 1, 2, 0) != 0) {
        std::fprintf(stderr, "lua GetSimfileString error: %s\n", lua_tostring(L, -1));
        cleanup();
        return;
    }
    const std::string simfile_string = lua_tostring(L, -2) ? lua_tostring(L, -2) : "";
    const std::string file_type = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 2);

    if (simfile_string.empty() || file_type.empty()) {
        cleanup();
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref_get_simfile_chart_string);
    lua_pushlstring(L, simfile_string.data(), simfile_string.size());
    lua_pushstring(L, steps_type.c_str());
    lua_pushstring(L, difficulty.c_str());
    lua_pushstring(L, description.c_str());
    lua_pushstring(L, file_type.c_str());
    if (lua_pcall(L, 5, 2, 0) != 0) {
        std::fprintf(stderr, "lua GetSimfileChartString error: %s\n", lua_tostring(L, -1));
        cleanup();
        return;
    }

    if (lua_isstring(L, -1)) {
        *out_hash_bpms = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
    }
    lua_pop(L, 2);

    cleanup();
}

static std::string compute_hash_with_lua(const std::string& simfile_path,
                                        const std::string& steps_type,
                                        const std::string& difficulty,
                                        const std::string& description,
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
    extract_sl_hash_bpms(L, &ctx, steps_type, difficulty, description, out_hash_bpms);
    // Push an error handler to capture Lua stack traces.
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_remove(L, -2); // remove debug table, leave traceback on stack
    int errfunc = lua_gettop(L);

    lua_getglobal(L, "ParseChartInfo");
    push_steps_userdata(L, &ctx);
    lua_pushstring(L, "P1");
    if (lua_pcall(L, 2, 0, errfunc) != 0) {
        std::fprintf(stderr, "lua ParseChartInfo error: %s\n", lua_tostring(L, -1));
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
                std::fprintf(stderr, "lua GetStreamSequences error: %s\n", lua_tostring(L, -1));
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
            std::fprintf(stderr, "lua breakdown error: %s\n", lua_tostring(L, -1));
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
            std::fprintf(stderr, "lua stream totals error: %s\n", lua_tostring(L, -1));
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

constexpr float kJackCutoffSeconds = 0.176f;
constexpr float kFootswitchCutoffSeconds = 0.3f;
constexpr float kDoublestepCutoffSeconds = 0.235f;

static bool is_footswitch(int column, const StepParity::Row& current_row, const StepParity::Row& previous_row, float elapsed_time) {
    if (current_row.columns[column] == StepParity::NONE || previous_row.columns[column] == StepParity::NONE) {
        return false;
    }
    if (previous_row.columns[column] != current_row.columns[column]
        && StepParity::OTHER_PART_OF_FOOT[previous_row.columns[column]] != current_row.columns[column]
        && elapsed_time < kFootswitchCutoffSeconds) {
        return true;
    }
    return false;
}

static int count_notes_in_row(const StepParity::Row& row) {
    int count = 0;
    for (const auto& note : row.notes) {
        if (note.type != TapNoteType_Empty) {
            ++count;
        }
    }
    return count;
}

namespace {
template <typename T>
bool is_empty(const std::vector<T>& vec, int column_count) {
    for (int i = 0; i < column_count; ++i) {
        if (static_cast<int>(vec[i]) != 0) {
            return false;
        }
    }
    return true;
}

struct StepParityCostTraceCalculator {
    explicit StepParityCostTraceCalculator(const StepParity::StageLayout& layout_in) : layout(layout_in) {}

    float get_action_cost_breakdown(
        StepParity::State* initial_state,
        StepParity::State* result_state,
        std::vector<StepParity::Row>& rows,
        int row_index,
        float elapsed_time,
        std::vector<float>& out_costs) const {
        StepParity::Row& row = rows[row_index];
        const int column_count = row.columnCount;

        out_costs.assign(StepParity::NUM_Cost, 0.0f);
        float total = 0.0f;

        int left_heel = StepParity::INVALID_COLUMN;
        int left_toe = StepParity::INVALID_COLUMN;
        int right_heel = StepParity::INVALID_COLUMN;
        int right_toe = StepParity::INVALID_COLUMN;

        for (int i = 0; i < column_count; i++) {
            switch (result_state->columns[i]) {
                case StepParity::NONE:
                    break;
                case StepParity::LEFT_HEEL:
                    left_heel = i;
                    break;
                case StepParity::LEFT_TOE:
                    left_toe = i;
                    break;
                case StepParity::RIGHT_HEEL:
                    right_heel = i;
                    break;
                case StepParity::RIGHT_TOE:
                    right_toe = i;
                    break;
                default:
                    break;
            }
        }

        const bool moved_left =
            result_state->didTheFootMove[StepParity::LEFT_HEEL] ||
            result_state->didTheFootMove[StepParity::LEFT_TOE];

        const bool moved_right =
            result_state->didTheFootMove[StepParity::RIGHT_HEEL] ||
            result_state->didTheFootMove[StepParity::RIGHT_TOE];

        const bool did_jump =
            ((initial_state->didTheFootMove[StepParity::LEFT_HEEL] &&
              !initial_state->isTheFootHolding[StepParity::LEFT_HEEL]) ||
             (initial_state->didTheFootMove[StepParity::LEFT_TOE] &&
              !initial_state->isTheFootHolding[StepParity::LEFT_TOE])) &&
            ((initial_state->didTheFootMove[StepParity::RIGHT_HEEL] &&
              !initial_state->isTheFootHolding[StepParity::RIGHT_HEEL]) ||
             (initial_state->didTheFootMove[StepParity::RIGHT_TOE] &&
              !initial_state->isTheFootHolding[StepParity::RIGHT_TOE]));

        const bool jacked_left = did_jack_left(initial_state, result_state, left_heel, left_toe, moved_left, did_jump, column_count);
        const bool jacked_right = did_jack_right(initial_state, result_state, right_heel, right_toe, moved_right, did_jump, column_count);

        out_costs[StepParity::COST_MINE] = calc_mine_cost(initial_state, result_state, row, column_count);
        out_costs[StepParity::COST_HOLDSWITCH] = calc_hold_switch_cost(initial_state, result_state, row, column_count);
        out_costs[StepParity::COST_BRACKETTAP] = calc_bracket_tap_cost(initial_state, result_state, row, left_heel, left_toe, right_heel, right_toe, elapsed_time, column_count);
        out_costs[StepParity::COST_BRACKETJACK] = calc_bracket_jack_cost(initial_state, result_state, rows, row_index, moved_left, moved_right, jacked_left, jacked_right, did_jump, column_count);
        out_costs[StepParity::COST_DOUBLESTEP] = calc_doublestep_cost(initial_state, result_state, rows, row_index, moved_left, moved_right, jacked_left, jacked_right, did_jump, column_count);
        out_costs[StepParity::COST_SLOW_BRACKET] = calc_slow_bracket_cost(row, moved_left, moved_right, elapsed_time);
        out_costs[StepParity::COST_TWISTED_FOOT] = calc_twisted_foot_cost(result_state);
        out_costs[StepParity::COST_FACING] = calc_facing_costs(initial_state, result_state, column_count);
        out_costs[StepParity::COST_SPIN] = calc_spin_costs(initial_state, result_state, column_count);
        out_costs[StepParity::COST_FOOTSWITCH] = calc_footswitch_cost(initial_state, result_state, row, elapsed_time, column_count);
        out_costs[StepParity::COST_SIDESWITCH] = calc_sideswitch_cost(initial_state, result_state, column_count);
        out_costs[StepParity::COST_MISSED_FOOTSWITCH] = calc_missed_footswitch_cost(row, jacked_left, jacked_right, column_count);
        out_costs[StepParity::COST_JACK] = calc_jack_cost(moved_left, moved_right, jacked_left, jacked_right, elapsed_time, column_count);
        out_costs[StepParity::COST_DISTANCE] = calc_big_movements_quickly_cost(initial_state, result_state, elapsed_time, column_count);

        for (int i = 0; i < StepParity::NUM_Cost; i++) {
            if (i == StepParity::COST_TOTAL) {
                continue;
            }
            total += out_costs[i];
        }
        out_costs[StepParity::COST_TOTAL] = total;

        return total;
    }

  private:
    const StepParity::StageLayout& layout;

    float calc_mine_cost(StepParity::State* initial_state, StepParity::State* result_state, const StepParity::Row& row, int column_count) const {
        (void)initial_state;
        float cost = 0.0f;
        for (int i = 0; i < column_count; i++) {
            if (result_state->combinedColumns[i] != StepParity::NONE && row.mines[i] != 0.0f) {
                cost += StepParity::MINE;
                break;
            }
        }
        return cost;
    }

    float calc_hold_switch_cost(StepParity::State* initial_state, StepParity::State* result_state, const StepParity::Row& row, int column_count) const {
        float cost = 0.0f;
        for (int c = 0; c < column_count; c++) {
            if (row.holds[c].type == TapNoteType_Empty) {
                continue;
            }
            if (
                ((result_state->combinedColumns[c] == StepParity::LEFT_HEEL ||
                  result_state->combinedColumns[c] == StepParity::LEFT_TOE) &&
                 initial_state->combinedColumns[c] != StepParity::LEFT_TOE &&
                 initial_state->combinedColumns[c] != StepParity::LEFT_HEEL) ||
                ((result_state->combinedColumns[c] == StepParity::RIGHT_HEEL ||
                  result_state->combinedColumns[c] == StepParity::RIGHT_TOE) &&
                 initial_state->combinedColumns[c] != StepParity::RIGHT_TOE &&
                 initial_state->combinedColumns[c] != StepParity::RIGHT_HEEL)) {
                const int previous_foot = initial_state->whereTheFeetAre[result_state->combinedColumns[c]];
                cost += StepParity::HOLDSWITCH *
                        (previous_foot == StepParity::INVALID_COLUMN
                            ? 1.0f
                            : std::sqrt(layout.getDistanceSq(c, previous_foot)));
            }
        }
        return cost;
    }

    float calc_bracket_tap_cost(
        StepParity::State* initial_state,
        StepParity::State* result_state,
        const StepParity::Row& row,
        int left_heel,
        int left_toe,
        int right_heel,
        int right_toe,
        float elapsed_time,
        int column_count) const {
        (void)result_state;
        (void)column_count;
        float cost = 0.0f;
        if (left_heel != StepParity::INVALID_COLUMN && left_toe != StepParity::INVALID_COLUMN) {
            float jack_penalty = 1.0f;
            if (
                initial_state->didTheFootMove[StepParity::LEFT_HEEL] ||
                initial_state->didTheFootMove[StepParity::LEFT_TOE]) {
                jack_penalty = 1.0f / elapsed_time;
            }
            if (
                row.holds[left_heel].type != TapNoteType_Empty &&
                row.holds[left_toe].type == TapNoteType_Empty) {
                cost += StepParity::BRACKETTAP * jack_penalty;
            }
            if (
                row.holds[left_toe].type != TapNoteType_Empty &&
                row.holds[left_heel].type == TapNoteType_Empty) {
                cost += StepParity::BRACKETTAP * jack_penalty;
            }
        }

        if (right_heel != StepParity::INVALID_COLUMN && right_toe != StepParity::INVALID_COLUMN) {
            float jack_penalty = 1.0f;
            if (
                initial_state->didTheFootMove[StepParity::RIGHT_TOE] ||
                initial_state->didTheFootMove[StepParity::RIGHT_HEEL]) {
                jack_penalty = 1.0f / elapsed_time;
            }
            if (
                row.holds[right_heel].type != TapNoteType_Empty &&
                row.holds[right_toe].type == TapNoteType_Empty) {
                cost += StepParity::BRACKETTAP * jack_penalty;
            }
            if (
                row.holds[right_toe].type != TapNoteType_Empty &&
                row.holds[right_heel].type == TapNoteType_Empty) {
                cost += StepParity::BRACKETTAP * jack_penalty;
            }
        }
        return cost;
    }

    float calc_bracket_jack_cost(
        StepParity::State* initial_state,
        StepParity::State* result_state,
        std::vector<StepParity::Row>& rows,
        int row_index,
        bool moved_left,
        bool moved_right,
        bool jacked_left,
        bool jacked_right,
        bool did_jump,
        int column_count) const {
        (void)initial_state;
        (void)rows;
        (void)row_index;
        float cost = 0.0f;
        if (
            moved_left != moved_right &&
            (moved_left || moved_right) &&
            is_empty(result_state->holdFeet, column_count) &&
            !did_jump) {
            if (
                jacked_left &&
                result_state->didTheFootMove[StepParity::LEFT_HEEL] &&
                result_state->didTheFootMove[StepParity::LEFT_TOE]) {
                cost += StepParity::BRACKETJACK;
            }
            if (
                jacked_right &&
                result_state->didTheFootMove[StepParity::RIGHT_HEEL] &&
                result_state->didTheFootMove[StepParity::RIGHT_TOE]) {
                cost += StepParity::BRACKETJACK;
            }
        }
        return cost;
    }

    float calc_doublestep_cost(
        StepParity::State* initial_state,
        StepParity::State* result_state,
        std::vector<StepParity::Row>& rows,
        int row_index,
        bool moved_left,
        bool moved_right,
        bool jacked_left,
        bool jacked_right,
        bool did_jump,
        int column_count) const {
        float cost = 0.0f;
        if (
            moved_left != moved_right &&
            (moved_left || moved_right) &&
            is_empty(result_state->holdFeet, column_count) &&
            !did_jump) {
            const bool doublestepped = did_double_step(initial_state, result_state, rows, row_index, moved_left, jacked_left, moved_right, jacked_right, column_count);
            if (doublestepped) {
                cost += StepParity::DOUBLESTEP;
            }
        }
        return cost;
    }

    float calc_slow_bracket_cost(const StepParity::Row& row, bool moved_left, bool moved_right, float elapsed_time) const {
        float cost = 0.0f;
        if (elapsed_time > StepParity::SLOW_BRACKET_THRESHOLD && moved_left != moved_right &&
            std::count_if(row.notes.begin(), row.notes.end(), [](StepParity::IntermediateNoteData note) {
                return note.type != TapNoteType_Empty;
            }) >= 2) {
            const float timediff = elapsed_time - StepParity::SLOW_BRACKET_THRESHOLD;
            cost += timediff * StepParity::SLOW_BRACKET;
        }
        return cost;
    }

    float calc_twisted_foot_cost(StepParity::State* result_state) const {
        float cost = 0.0f;
        const int left_heel = result_state->whatNoteTheFootIsHitting[StepParity::LEFT_HEEL];
        const int left_toe = result_state->whatNoteTheFootIsHitting[StepParity::LEFT_TOE];
        const int right_heel = result_state->whatNoteTheFootIsHitting[StepParity::RIGHT_HEEL];
        const int right_toe = result_state->whatNoteTheFootIsHitting[StepParity::RIGHT_TOE];

        const StepParity::StagePoint left_pos = layout.averagePoint(left_heel, left_toe);
        const StepParity::StagePoint right_pos = layout.averagePoint(right_heel, right_toe);

        const bool crossed_over = right_pos.x < left_pos.x;
        const bool right_backwards = right_heel != StepParity::INVALID_COLUMN && right_toe != StepParity::INVALID_COLUMN
            ? layout.columns[right_toe].y < layout.columns[right_heel].y
            : false;
        const bool left_backwards = left_heel != StepParity::INVALID_COLUMN && left_toe != StepParity::INVALID_COLUMN
            ? layout.columns[left_toe].y < layout.columns[left_heel].y
            : false;

        if (!crossed_over && (right_backwards || left_backwards)) {
            cost += StepParity::TWISTED_FOOT;
        }
        return cost;
    }

    float calc_missed_footswitch_cost(const StepParity::Row& row, bool jacked_left, bool jacked_right, int column_count) const {
        (void)column_count;
        float cost = 0.0f;
        if ((jacked_left || jacked_right) &&
            (std::any_of(row.mines.begin(), row.mines.end(), [](float mine) { return mine != 0.0f; }) ||
             std::any_of(row.fakeMines.begin(), row.fakeMines.end(), [](float mine) { return mine != 0.0f; }))) {
            cost += StepParity::MISSED_FOOTSWITCH;
        }
        return cost;
    }

    float calc_facing_costs(StepParity::State* initial_state, StepParity::State* result_state, int column_count) const {
        float cost = 0.0f;

        int end_left_heel = StepParity::INVALID_COLUMN;
        int end_left_toe = StepParity::INVALID_COLUMN;
        int end_right_heel = StepParity::INVALID_COLUMN;
        int end_right_toe = StepParity::INVALID_COLUMN;

        for (int i = 0; i < column_count; i++) {
            switch (result_state->combinedColumns[i]) {
                case StepParity::NONE:
                    break;
                case StepParity::LEFT_HEEL:
                    end_left_heel = i;
                    break;
                case StepParity::LEFT_TOE:
                    end_left_toe = i;
                    break;
                case StepParity::RIGHT_HEEL:
                    end_right_heel = i;
                    break;
                case StepParity::RIGHT_TOE:
                    end_right_toe = i;
                default:
                    break;
            }
        }

        if (end_left_toe == StepParity::INVALID_COLUMN) end_left_toe = end_left_heel;
        if (end_right_toe == StepParity::INVALID_COLUMN) end_right_toe = end_right_heel;

        const float heel_facing =
            end_left_heel != StepParity::INVALID_COLUMN && end_right_heel != StepParity::INVALID_COLUMN
                ? layout.getXDifference(end_left_heel, end_right_heel)
                : 0.0f;
        const float toe_facing =
            end_left_toe != StepParity::INVALID_COLUMN && end_right_toe != StepParity::INVALID_COLUMN
                ? layout.getXDifference(end_left_toe, end_right_toe)
                : 0.0f;
        const float left_facing =
            end_left_heel != StepParity::INVALID_COLUMN && end_left_toe != StepParity::INVALID_COLUMN
                ? layout.getYDifference(end_left_heel, end_left_toe)
                : 0.0f;
        const float right_facing =
            end_right_heel != StepParity::INVALID_COLUMN && end_right_toe != StepParity::INVALID_COLUMN
                ? layout.getYDifference(end_right_heel, end_right_toe)
                : 0.0f;

        const float heel_facing_penalty = static_cast<float>(std::pow(-1.0f * std::min(heel_facing, 0.0f), 1.8)) * 100.0f;
        const float toes_facing_penalty = static_cast<float>(std::pow(-1.0f * std::min(toe_facing, 0.0f), 1.8)) * 100.0f;
        const float left_facing_penalty = static_cast<float>(std::pow(-1.0f * std::min(left_facing, 0.0f), 1.8)) * 100.0f;
        const float right_facing_penalty = static_cast<float>(std::pow(-1.0f * std::min(right_facing, 0.0f), 1.8)) * 100.0f;

        if (heel_facing_penalty > 0.0f) cost += heel_facing_penalty * StepParity::FACING;
        if (toes_facing_penalty > 0.0f) cost += toes_facing_penalty * StepParity::FACING;
        if (left_facing_penalty > 0.0f) cost += left_facing_penalty * StepParity::FACING;
        if (right_facing_penalty > 0.0f) cost += right_facing_penalty * StepParity::FACING;

        return cost;
    }

    float calc_spin_costs(StepParity::State* initial_state, StepParity::State* result_state, int column_count) const {
        float cost = 0.0f;

        int end_left_heel = StepParity::INVALID_COLUMN;
        int end_left_toe = StepParity::INVALID_COLUMN;
        int end_right_heel = StepParity::INVALID_COLUMN;
        int end_right_toe = StepParity::INVALID_COLUMN;

        for (int i = 0; i < column_count; i++) {
            switch (result_state->combinedColumns[i]) {
                case StepParity::NONE:
                    break;
                case StepParity::LEFT_HEEL:
                    end_left_heel = i;
                    break;
                case StepParity::LEFT_TOE:
                    end_left_toe = i;
                    break;
                case StepParity::RIGHT_HEEL:
                    end_right_heel = i;
                    break;
                case StepParity::RIGHT_TOE:
                    end_right_toe = i;
                default:
                    break;
            }
        }

        if (end_left_toe == StepParity::INVALID_COLUMN) end_left_toe = end_left_heel;
        if (end_right_toe == StepParity::INVALID_COLUMN) end_right_toe = end_right_heel;

        const StepParity::StagePoint previous_left_pos = layout.averagePoint(
            initial_state->whereTheFeetAre[StepParity::LEFT_HEEL],
            initial_state->whereTheFeetAre[StepParity::LEFT_TOE]);
        const StepParity::StagePoint previous_right_pos = layout.averagePoint(
            initial_state->whereTheFeetAre[StepParity::RIGHT_HEEL],
            initial_state->whereTheFeetAre[StepParity::RIGHT_TOE]);
        const StepParity::StagePoint left_pos = layout.averagePoint(end_left_heel, end_left_toe);
        const StepParity::StagePoint right_pos = layout.averagePoint(end_right_heel, end_right_toe);

        if (
            right_pos.x < left_pos.x &&
            previous_right_pos.x < previous_left_pos.x &&
            right_pos.y < left_pos.y &&
            previous_right_pos.y > previous_left_pos.y) {
            cost += StepParity::SPIN;
        }
        if (
            right_pos.x < left_pos.x &&
            previous_right_pos.x < previous_left_pos.x &&
            right_pos.y > left_pos.y &&
            previous_right_pos.y < previous_left_pos.y) {
            cost += StepParity::SPIN;
        }
        return cost;
    }

    float calc_footswitch_cost(
        StepParity::State* initial_state,
        StepParity::State* result_state,
        const StepParity::Row& row,
        float elapsed_time,
        int column_count) const {
        float cost = 0.0f;
        if (elapsed_time >= StepParity::SLOW_FOOTSWITCH_THRESHOLD && elapsed_time < StepParity::SLOW_FOOTSWITCH_IGNORE) {
            if (
                std::all_of(row.mines.begin(), row.mines.end(), [](float mine) { return mine == 0.0f; }) &&
                std::all_of(row.fakeMines.begin(), row.fakeMines.end(), [](float mine) { return mine == 0.0f; })) {
                const float time_scaled = elapsed_time - StepParity::SLOW_FOOTSWITCH_THRESHOLD;

                for (int i = 0; i < column_count; i++) {
                    if (
                        initial_state->combinedColumns[i] == StepParity::NONE ||
                        result_state->columns[i] == StepParity::NONE) {
                        continue;
                    }
                    if (
                        initial_state->combinedColumns[i] != result_state->columns[i] &&
                        initial_state->combinedColumns[i] != StepParity::OTHER_PART_OF_FOOT[result_state->columns[i]]) {
                        cost += (time_scaled / (StepParity::SLOW_FOOTSWITCH_THRESHOLD + time_scaled)) * StepParity::FOOTSWITCH;
                        break;
                    }
                }
            }
        }
        return cost;
    }

    float calc_sideswitch_cost(StepParity::State* initial_state, StepParity::State* result_state, int column_count) const {
        (void)column_count;
        float cost = 0.0f;
        for (int c : layout.sideArrows) {
            if (
                initial_state->combinedColumns[c] != result_state->columns[c] &&
                result_state->columns[c] != StepParity::NONE &&
                initial_state->combinedColumns[c] != StepParity::NONE &&
                !result_state->didTheFootMove[initial_state->combinedColumns[c]]) {
                cost += StepParity::SIDESWITCH;
            }
        }
        return cost;
    }

    float calc_jack_cost(bool moved_left, bool moved_right, bool jacked_left, bool jacked_right, float elapsed_time, int column_count) const {
        (void)column_count;
        float cost = 0.0f;
        if (elapsed_time < StepParity::JACK_THRESHOLD && moved_left != moved_right) {
            const float time_scaled = StepParity::JACK_THRESHOLD - elapsed_time;
            if (jacked_left || jacked_right) {
                cost += (1.0f / time_scaled - 1.0f / StepParity::JACK_THRESHOLD) * StepParity::JACK;
            }
        }
        return cost;
    }

    float calc_big_movements_quickly_cost(StepParity::State* initial_state, StepParity::State* result_state, float elapsed_time, int column_count) const {
        (void)column_count;
        float cost = 0.0f;
        for (StepParity::Foot foot : result_state->movedFeet) {
            if (foot == StepParity::NONE) {
                continue;
            }

            const int initial_position = initial_state->whereTheFeetAre[foot];
            if (initial_position == StepParity::INVALID_COLUMN) {
                continue;
            }

            const int result_position = result_state->whatNoteTheFootIsHitting[foot];

            const bool is_bracketing = result_state->whatNoteTheFootIsHitting[StepParity::OTHER_PART_OF_FOOT[foot]] != StepParity::INVALID_COLUMN;
            if (is_bracketing && result_state->whatNoteTheFootIsHitting[StepParity::OTHER_PART_OF_FOOT[foot]] == initial_position) {
                continue;
            }

            float dist = (std::sqrt(layout.getDistanceSq(initial_position, result_position)) * StepParity::DISTANCE) / elapsed_time;
            if (is_bracketing) {
                dist = dist * 0.2f;
            }
            cost += dist;
        }
        return cost;
    }

    bool did_double_step(
        StepParity::State* initial_state,
        StepParity::State* result_state,
        std::vector<StepParity::Row>& rows,
        int row_index,
        bool moved_left,
        bool jacked_left,
        bool moved_right,
        bool jacked_right,
        int column_count) const {
        (void)result_state;
        (void)column_count;
        StepParity::Row& row = rows[row_index];
        bool doublestepped = false;
        if (
            moved_left &&
            !jacked_left &&
            ((initial_state->didTheFootMove[StepParity::LEFT_HEEL] &&
              !initial_state->isTheFootHolding[StepParity::LEFT_HEEL]) ||
             (initial_state->didTheFootMove[StepParity::LEFT_TOE] &&
              !initial_state->isTheFootHolding[StepParity::LEFT_TOE]))) {
            doublestepped = true;
        }
        if (
            moved_right &&
            !jacked_right &&
            ((initial_state->didTheFootMove[StepParity::RIGHT_HEEL] &&
              !initial_state->isTheFootHolding[StepParity::RIGHT_HEEL]) ||
             (initial_state->didTheFootMove[StepParity::RIGHT_TOE] &&
              !initial_state->isTheFootHolding[StepParity::RIGHT_TOE]))) {
            doublestepped = true;
        }

        if (row_index - 1 > -1) {
            StepParity::Row& last_row = rows[row_index - 1];
            for (StepParity::IntermediateNoteData hold : last_row.holds) {
                if (hold.type == TapNoteType_Empty) continue;
                const float end_beat = row.beat;
                const float start_beat = last_row.beat;
                if (
                    hold.beat + hold.hold_length > start_beat &&
                    hold.beat + hold.hold_length < end_beat) {
                    doublestepped = false;
                }
                if (hold.beat + hold.hold_length >= end_beat) doublestepped = false;
            }
        }
        return doublestepped;
    }

    bool did_jack_left(
        StepParity::State* initial_state,
        StepParity::State* result_state,
        int left_heel,
        int left_toe,
        bool moved_left,
        bool did_jump,
        int column_count) const {
        (void)result_state;
        (void)column_count;
        bool jacked_left = false;
        if (!did_jump && moved_left) {
            if (left_heel > StepParity::INVALID_COLUMN &&
                initial_state->combinedColumns[left_heel] == StepParity::LEFT_HEEL &&
                !result_state->isTheFootHolding[StepParity::LEFT_HEEL] &&
                ((initial_state->didTheFootMove[StepParity::LEFT_HEEL] &&
                  !initial_state->isTheFootHolding[StepParity::LEFT_HEEL]) ||
                 (initial_state->didTheFootMove[StepParity::LEFT_TOE] &&
                  !initial_state->isTheFootHolding[StepParity::LEFT_TOE]))) {
                jacked_left = true;
            }
            if (
                left_toe > StepParity::INVALID_COLUMN &&
                initial_state->combinedColumns[left_toe] == StepParity::LEFT_TOE &&
                !result_state->isTheFootHolding[StepParity::LEFT_TOE] &&
                ((initial_state->didTheFootMove[StepParity::LEFT_HEEL] &&
                  !initial_state->isTheFootHolding[StepParity::LEFT_HEEL]) ||
                 (initial_state->didTheFootMove[StepParity::LEFT_TOE] &&
                  !initial_state->isTheFootHolding[StepParity::LEFT_TOE]))) {
                jacked_left = true;
            }
        }
        return jacked_left;
    }

    bool did_jack_right(
        StepParity::State* initial_state,
        StepParity::State* result_state,
        int right_heel,
        int right_toe,
        bool moved_right,
        bool did_jump,
        int column_count) const {
        (void)column_count;
        bool jacked_right = false;
        if (!did_jump && moved_right) {
            if (right_heel > StepParity::INVALID_COLUMN &&
                initial_state->combinedColumns[right_heel] == StepParity::RIGHT_HEEL &&
                !result_state->isTheFootHolding[StepParity::RIGHT_HEEL] &&
                ((initial_state->didTheFootMove[StepParity::RIGHT_HEEL] &&
                  !initial_state->isTheFootHolding[StepParity::RIGHT_HEEL]) ||
                 (initial_state->didTheFootMove[StepParity::RIGHT_TOE] &&
                  !initial_state->isTheFootHolding[StepParity::RIGHT_TOE]))) {
                jacked_right = true;
            }
            if (right_toe > StepParity::INVALID_COLUMN &&
                initial_state->combinedColumns[right_toe] == StepParity::RIGHT_TOE &&
                !result_state->isTheFootHolding[StepParity::RIGHT_TOE] &&
                ((initial_state->didTheFootMove[StepParity::RIGHT_HEEL] &&
                  !initial_state->isTheFootHolding[StepParity::RIGHT_HEEL]) ||
                 (initial_state->didTheFootMove[StepParity::RIGHT_TOE] &&
                  !initial_state->isTheFootHolding[StepParity::RIGHT_TOE]))) {
                jacked_right = true;
            }
        }
        return jacked_right;
    }
};
} // namespace

static StepParityTraceOut build_step_parity_trace(Steps* steps, TimingData* timing) {
    StepParityTraceOut out;
    out.status = "ok";
    out.foot_labels = {"none", "left_heel", "left_toe", "right_heel", "right_toe"};
    out.note_type_labels = {"empty", "tap", "hold_head", "hold_tail", "mine", "lift", "attack", "auto_keysound", "fake"};
    out.cost_labels.reserve(StepParity::NUM_Cost);
    for (int i = 0; i < StepParity::NUM_Cost; ++i) {
        out.cost_labels.push_back(StepParity::COST_LABELS[i]);
    }

    if (!steps || StepParity::Layouts.find(steps->m_StepsType) == StepParity::Layouts.end()) {
        out.status = "unsupported_steps_type";
        return out;
    }

    StepParity::StageLayout layout = StepParity::Layouts.at(steps->m_StepsType);
    out.layout.columns.reserve(layout.columns.size());
    for (const auto& point : layout.columns) {
        out.layout.columns.push_back({static_cast<double>(point.x), static_cast<double>(point.y)});
    }
    out.layout.up_arrows = layout.upArrows;
    out.layout.down_arrows = layout.downArrows;
    out.layout.side_arrows = layout.sideArrows;

    NoteData note_data;
    steps->GetNoteData(note_data);

    if (!GAMESTATE) {
        out.status = "no_gamestate";
        return out;
    }

    TimingData* previous_timing = GAMESTATE->GetProcessedTimingData();
    GAMESTATE->SetProcessedTimingData(timing);

    StepParity::StepParityGenerator gen(layout);
    const bool analyzed = gen.analyzeNoteData(note_data);

    GAMESTATE->SetProcessedTimingData(previous_timing);

    if (!analyzed) {
        out.status = gen.rows.empty() ? "no_rows" : "invalid_graph";
    }

    std::vector<int> note_counts;
    note_counts.reserve(gen.rows.size());
    out.rows.reserve(gen.rows.size());
    for (const auto& row : gen.rows) {
        StepParityRowOut row_out;
        row_out.row_index = row.rowIndex;
        row_out.beat = static_cast<double>(row.beat);
        row_out.second = static_cast<double>(row.second);
        row_out.note_types.reserve(row.notes.size());
        row_out.hold_types.reserve(row.holds.size());
        row_out.columns.reserve(row.columns.size());
        row_out.mines.reserve(row.mines.size());
        row_out.fake_mines.reserve(row.fakeMines.size());

        int note_count = 0;
        for (const auto& note : row.notes) {
            row_out.note_types.push_back(static_cast<int>(note.type));
            if (note.type != TapNoteType_Empty) {
                ++note_count;
            }
        }
        row_out.note_count = note_count;
        note_counts.push_back(note_count);

        for (const auto& hold : row.holds) {
            row_out.hold_types.push_back(static_cast<int>(hold.type));
        }
        for (auto foot : row.columns) {
            row_out.columns.push_back(static_cast<int>(foot));
        }
        row_out.where_feet = row.whereTheFeetAre;
        row_out.hold_tails.assign(row.holdTails.begin(), row.holdTails.end());
        for (float v : row.mines) {
            row_out.mines.push_back(static_cast<double>(v));
        }
        for (float v : row.fakeMines) {
            row_out.fake_mines.push_back(static_cast<double>(v));
        }
        out.rows.push_back(std::move(row_out));
    }

    if (gen.rows.size() >= 2) {
        out.tech_rows.reserve(gen.rows.size() - 1);
    }
    for (size_t i = 1; i < gen.rows.size(); ++i) {
        const StepParity::Row& current_row = gen.rows[i];
        const StepParity::Row& previous_row = gen.rows[i - 1];
        const float elapsed_time = current_row.second - previous_row.second;

        StepParityTechRowOut tech_out;
        tech_out.row_index = current_row.rowIndex;
        tech_out.elapsed = static_cast<double>(elapsed_time);

        if (note_counts[i] == 1 && note_counts[i - 1] == 1) {
            for (StepParity::Foot foot : StepParity::FEET) {
                if (current_row.whereTheFeetAre[foot] == StepParity::INVALID_COLUMN
                    || previous_row.whereTheFeetAre[foot] == StepParity::INVALID_COLUMN) {
                    continue;
                }
                if (previous_row.whereTheFeetAre[foot] == current_row.whereTheFeetAre[foot]) {
                    if (elapsed_time < kJackCutoffSeconds) {
                        tech_out.jacks += 1;
                    }
                } else {
                    if (elapsed_time < kDoublestepCutoffSeconds) {
                        tech_out.doublesteps += 1;
                    }
                }
            }
        }

        if (note_counts[i] >= 2) {
            if (current_row.whereTheFeetAre[StepParity::LEFT_HEEL] != StepParity::INVALID_COLUMN
                && current_row.whereTheFeetAre[StepParity::LEFT_TOE] != StepParity::INVALID_COLUMN) {
                tech_out.brackets += 1;
            }
            if (current_row.whereTheFeetAre[StepParity::RIGHT_HEEL] != StepParity::INVALID_COLUMN
                && current_row.whereTheFeetAre[StepParity::RIGHT_TOE] != StepParity::INVALID_COLUMN) {
                tech_out.brackets += 1;
            }
        }

        for (int c : layout.upArrows) {
            if (is_footswitch(c, current_row, previous_row, elapsed_time)) {
                tech_out.up_footswitches += 1;
                tech_out.footswitches += 1;
            }
        }
        for (int c : layout.downArrows) {
            if (is_footswitch(c, current_row, previous_row, elapsed_time)) {
                tech_out.down_footswitches += 1;
                tech_out.footswitches += 1;
            }
        }
        for (int c : layout.sideArrows) {
            if (is_footswitch(c, current_row, previous_row, elapsed_time)) {
                tech_out.sideswitches += 1;
            }
        }

        const int left_heel = current_row.whereTheFeetAre[StepParity::LEFT_HEEL];
        const int left_toe = current_row.whereTheFeetAre[StepParity::LEFT_TOE];
        const int right_heel = current_row.whereTheFeetAre[StepParity::RIGHT_HEEL];
        const int right_toe = current_row.whereTheFeetAre[StepParity::RIGHT_TOE];

        const int previous_left_heel = previous_row.whereTheFeetAre[StepParity::LEFT_HEEL];
        const int previous_left_toe = previous_row.whereTheFeetAre[StepParity::LEFT_TOE];
        const int previous_right_heel = previous_row.whereTheFeetAre[StepParity::RIGHT_HEEL];
        const int previous_right_toe = previous_row.whereTheFeetAre[StepParity::RIGHT_TOE];

        if (right_heel != StepParity::INVALID_COLUMN
            && previous_left_heel != StepParity::INVALID_COLUMN
            && previous_right_heel == StepParity::INVALID_COLUMN) {
            StepParity::StagePoint left_pos = layout.averagePoint(previous_left_heel, previous_left_toe);
            StepParity::StagePoint right_pos = layout.averagePoint(right_heel, right_toe);

            if (right_pos.x < left_pos.x) {
                if (i > 1) {
                    const StepParity::Row& previous_previous_row = gen.rows[i - 2];
                    const int previous_previous_right_heel = previous_previous_row.whereTheFeetAre[StepParity::RIGHT_HEEL];
                    if (previous_previous_right_heel != StepParity::INVALID_COLUMN
                        && previous_previous_right_heel != right_heel) {
                        StepParity::StagePoint previous_previous_right_pos = layout.columns[previous_previous_right_heel];
                        if (previous_previous_right_pos.x > left_pos.x) {
                            tech_out.full_crossovers += 1;
                        } else {
                            tech_out.half_crossovers += 1;
                        }
                        tech_out.crossovers += 1;
                    }
                } else {
                    tech_out.half_crossovers += 1;
                    tech_out.crossovers += 1;
                }
            }
        } else if (left_heel != StepParity::INVALID_COLUMN
            && previous_right_heel != StepParity::INVALID_COLUMN
            && previous_left_heel == StepParity::INVALID_COLUMN) {
            StepParity::StagePoint left_pos = layout.averagePoint(left_heel, left_toe);
            StepParity::StagePoint right_pos = layout.averagePoint(previous_right_heel, previous_right_toe);

            if (right_pos.x < left_pos.x) {
                if (i > 1) {
                    const StepParity::Row& previous_previous_row = gen.rows[i - 2];
                    const int previous_previous_left_heel = previous_previous_row.whereTheFeetAre[StepParity::LEFT_HEEL];
                    if (previous_previous_left_heel != StepParity::INVALID_COLUMN
                        && previous_previous_left_heel != left_heel) {
                        StepParity::StagePoint previous_previous_left_pos = layout.columns[previous_previous_left_heel];
                        if (right_pos.x > previous_previous_left_pos.x) {
                            tech_out.full_crossovers += 1;
                        } else {
                            tech_out.half_crossovers += 1;
                        }
                        tech_out.crossovers += 1;
                    }
                } else {
                    tech_out.half_crossovers += 1;
                    tech_out.crossovers += 1;
                }
            }
        }

        out.tech_rows.push_back(std::move(tech_out));
    }

    if (gen.nodes_for_rows.size() == gen.rows.size()) {
        StepParityCostTraceCalculator cost(layout);
        StepParity::State initial_state(layout.columnCount);
        std::vector<float> cost_breakdown;
        out.cost_rows.reserve(gen.rows.size());

        for (size_t i = 0; i < gen.rows.size(); ++i) {
            const int node_index = gen.nodes_for_rows[i];
            if (node_index < 0 || static_cast<size_t>(node_index) >= gen.nodes.size()) {
                continue;
            }

            StepParity::StepParityNode* current_node = gen.nodes[node_index];
            StepParity::State* initial_state_ptr = &initial_state;
            float elapsed = 1.0f;
            if (i > 0) {
                const int previous_index = gen.nodes_for_rows[i - 1];
                if (previous_index < 0 || static_cast<size_t>(previous_index) >= gen.nodes.size()) {
                    continue;
                }
                StepParity::StepParityNode* previous_node = gen.nodes[previous_index];
                initial_state_ptr = previous_node->state;
                elapsed = current_node->second - previous_node->second;
            }

            StepParityCostRowOut cost_row;
            cost_row.row_index = gen.rows[i].rowIndex;
            cost_row.elapsed = static_cast<double>(elapsed);
            const float total = cost.get_action_cost_breakdown(
                initial_state_ptr, current_node->state, gen.rows, static_cast<int>(i), elapsed, cost_breakdown);
            cost_row.total = static_cast<double>(total);
            cost_row.costs.reserve(cost_breakdown.size());
            for (float v : cost_breakdown) {
                cost_row.costs.push_back(static_cast<double>(v));
            }
            out.cost_rows.push_back(std::move(cost_row));
        }
    }

    return out;
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
    out.title = song.GetDisplayMainTitle();
    out.subtitle = song.GetDisplaySubTitle();
    out.artist = song.GetDisplayArtist();
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
    out.hash = compute_hash_with_lua(simfile_path, st_str, diff_str, steps->GetDescription(), td,
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
    if (g_enable_step_parity_trace) {
        out.step_parity_trace = build_step_parity_trace(steps, td);
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

void set_step_parity_trace_enabled(bool) {}
#endif
