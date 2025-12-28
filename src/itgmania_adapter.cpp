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
    out.title = song.GetMainTitle();
    out.subtitle = song.m_sSubTitle;
    out.artist = song.m_sArtist;
    out.title_translated = song.GetDisplayMainTitle();
    out.subtitle_translated = song.GetDisplaySubTitle();
    out.artist_translated = song.GetDisplayArtist();
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

namespace {
constexpr int kRowsPerBeat = 48;
constexpr float kMissingHoldLengthBeats = static_cast<float>(1u << 30) / static_cast<float>(kRowsPerBeat);

struct ParsedRow {
    std::vector<unsigned char> chars;
    int row = 0;
    float beat = 0.0f;
    float second = 0.0f;
};

enum class DumpTapNoteType {
    Empty,
    Tap,
    HoldHead,
    HoldTail,
    Mine,
    Fake
};

enum class DumpTapNoteSubType {
    Invalid,
    Hold,
    Roll
};

static const char* to_string(DumpTapNoteType value) {
    switch (value) {
        case DumpTapNoteType::Empty: return "Empty";
        case DumpTapNoteType::Tap: return "Tap";
        case DumpTapNoteType::HoldHead: return "HoldHead";
        case DumpTapNoteType::HoldTail: return "HoldTail";
        case DumpTapNoteType::Mine: return "Mine";
        case DumpTapNoteType::Fake: return "Fake";
    }
    return "Empty";
}

static const char* to_string(DumpTapNoteSubType value) {
    switch (value) {
        case DumpTapNoteSubType::Invalid: return "Invalid";
        case DumpTapNoteSubType::Hold: return "Hold";
        case DumpTapNoteSubType::Roll: return "Roll";
    }
    return "Invalid";
}

static const char* foot_label(StepParity::Foot foot) {
    switch (foot) {
        case StepParity::NONE: return "N";
        case StepParity::LEFT_HEEL: return "LH";
        case StepParity::LEFT_TOE: return "LT";
        case StepParity::RIGHT_HEEL: return "RH";
        case StepParity::RIGHT_TOE: return "RT";
        default: return "N";
    }
}

static std::string format_foot_vec(const StepParity::FootPlacement& feet) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < feet.size(); ++i) {
        if (i) {
            out << ",";
        }
        out << foot_label(feet[i]);
    }
    out << "]";
    return out.str();
}

static int foot_position(const std::vector<int>& positions, StepParity::Foot foot) {
    const size_t idx = static_cast<size_t>(foot);
    if (idx >= positions.size()) {
        return StepParity::INVALID_COLUMN;
    }
    return positions[idx];
}

static int foot_position(const int* positions, size_t size, StepParity::Foot foot) {
    const size_t idx = static_cast<size_t>(foot);
    if (idx >= size) {
        return StepParity::INVALID_COLUMN;
    }
    return positions[idx];
}

static std::string format_foot_positions(const std::vector<int>& positions) {
    std::ostringstream out;
    out << "lh=" << foot_position(positions, StepParity::LEFT_HEEL)
        << " lt=" << foot_position(positions, StepParity::LEFT_TOE)
        << " rh=" << foot_position(positions, StepParity::RIGHT_HEEL)
        << " rt=" << foot_position(positions, StepParity::RIGHT_TOE);
    return out.str();
}

static std::string format_foot_positions(const int* positions, size_t size) {
    std::ostringstream out;
    out << "lh=" << foot_position(positions, size, StepParity::LEFT_HEEL)
        << " lt=" << foot_position(positions, size, StepParity::LEFT_TOE)
        << " rh=" << foot_position(positions, size, StepParity::RIGHT_HEEL)
        << " rt=" << foot_position(positions, size, StepParity::RIGHT_TOE);
    return out.str();
}

static std::string format_foot_flags(const bool* flags, size_t size) {
    auto flag_value = [&](StepParity::Foot foot) -> int {
        const size_t idx = static_cast<size_t>(foot);
        if (idx >= size) {
            return 0;
        }
        return flags[idx] ? 1 : 0;
    };
    std::ostringstream out;
    out << "lh=" << flag_value(StepParity::LEFT_HEEL)
        << " lt=" << flag_value(StepParity::LEFT_TOE)
        << " rh=" << flag_value(StepParity::RIGHT_HEEL)
        << " rt=" << flag_value(StepParity::RIGHT_TOE);
    return out.str();
}

struct IdentityHasher {
    uint64_t value = 0;

    void write(const void* data, size_t len) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i) {
            value = value * 0x100000001b3ULL + bytes[i];
        }
    }

    uint64_t finish() const {
        return value;
    }
};

static void write_u32_le(IdentityHasher& hasher, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = static_cast<unsigned char>(value & 0xFF);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
    hasher.write(bytes, sizeof(bytes));
}

static void write_i32_le(IdentityHasher& hasher, int value) {
    write_u32_le(hasher, static_cast<uint32_t>(value));
}

static void write_f32_le(IdentityHasher& hasher, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32_le(hasher, bits);
}

static uint64_t hash_bytes(std::string_view bytes) {
    IdentityHasher hasher;
    if (!bytes.empty()) {
        hasher.write(bytes.data(), bytes.size());
    }
    return hasher.finish();
}

static uint64_t hash_rows(const std::vector<ParsedRow>& rows) {
    IdentityHasher hasher;
    for (const auto& row : rows) {
        if (!row.chars.empty()) {
            hasher.write(row.chars.data(), row.chars.size());
        }
        write_i32_le(hasher, row.row);
        write_f32_le(hasher, row.beat);
        write_f32_le(hasher, row.second);
    }
    return hasher.finish();
}

static bool is_ascii_whitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static std::string_view trim_ascii_whitespace(std::string_view line) {
    size_t start = 0;
    size_t end = line.size();
    while (start < end && is_ascii_whitespace(static_cast<unsigned char>(line[start]))) {
        ++start;
    }
    while (end > start && is_ascii_whitespace(static_cast<unsigned char>(line[end - 1]))) {
        --end;
    }
    return line.substr(start, end - start);
}

static int lrint_ties_even_f32(float value) {
    float floor_val = std::floor(value);
    float frac = value - floor_val;
    if (frac < 0.5f) {
        return static_cast<int>(floor_val);
    }
    if (frac > 0.5f) {
        return static_cast<int>(floor_val) + 1;
    }
    int floor_i = static_cast<int>(floor_val);
    return (floor_i & 1) == 0 ? floor_i : floor_i + 1;
}

static int beat_to_note_row_f32_exact(float beat) {
    return lrint_ties_even_f32(beat * static_cast<float>(kRowsPerBeat));
}

static void print_hex16(std::ostream& out, uint64_t value) {
    std::ios_base::fmtflags flags = out.flags();
    char fill = out.fill();
    out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    out.flags(flags);
    out.fill(fill);
}

static std::vector<ParsedRow> parse_chart_rows_with_timing(
    std::string_view note_data,
    TimingData* timing,
    int column_count,
    bool dump_rows,
    std::ostream& out) {
    std::vector<ParsedRow> rows;
    size_t measure_index = 0;
    if (column_count <= 0) {
        return rows;
    }

    if (dump_rows) {
        const uint64_t hash = hash_bytes(note_data);
        out << "STEP_PARITY_ROWS start hash=";
        print_hex16(out, hash);
        out << " columns=" << column_count << "\n";
    }

    size_t start = 0;
    const size_t len = note_data.size();
    while (start <= len) {
        size_t comma = note_data.find(',', start);
        if (comma == std::string_view::npos) {
            comma = len;
        }
        std::string_view measure = note_data.substr(start, comma - start);
        if (!measure.empty()) {
            std::vector<std::string_view> lines;
            size_t line_start = 0;
            const size_t measure_len = measure.size();
            while (line_start <= measure_len) {
                size_t nl = measure.find('\n', line_start);
                if (nl == std::string_view::npos) {
                    nl = measure_len;
                }
                std::string_view line = measure.substr(line_start, nl - line_start);
                line = trim_ascii_whitespace(line);
                if (!line.empty()) {
                    lines.push_back(line);
                }
                if (nl == measure_len) {
                    break;
                }
                line_start = nl + 1;
            }

            const size_t num_rows = lines.size();
            if (num_rows == 0) {
                measure_index += 1;
            } else {
                for (size_t i = 0; i < num_rows; ++i) {
                    const float percent = static_cast<float>(i) / static_cast<float>(num_rows);
                    const float beat = (static_cast<float>(measure_index) + percent) * 4.0f;
                    const int note_row = beat_to_note_row_f32_exact(beat);
                    const float quantized_beat = static_cast<float>(note_row) / static_cast<float>(kRowsPerBeat);
                    const float second = timing ? timing->GetElapsedTimeFromBeat(quantized_beat) : 0.0f;

                    std::vector<unsigned char> chars(static_cast<size_t>(column_count), static_cast<unsigned char>('0'));
                    const std::string_view line = lines[i];
                    const size_t copy_len = std::min(static_cast<size_t>(column_count), line.size());
                    for (size_t col = 0; col < copy_len; ++col) {
                        chars[col] = static_cast<unsigned char>(line[col]);
                    }

                    const size_t row_index = rows.size();
                    rows.push_back(ParsedRow{std::move(chars), note_row, quantized_beat, second});
                    if (dump_rows) {
                        const std::string row_text(rows[row_index].chars.begin(), rows[row_index].chars.end());
                        out << std::fixed << std::setprecision(6);
                        out << "STEP_PARITY_ROW idx=" << row_index
                            << " measure=" << measure_index
                            << " line=" << i << "/" << num_rows
                            << " row=" << note_row
                            << " beat=" << quantized_beat
                            << " second=" << second
                            << " data=" << row_text
                            << "\n";
                    }
                }
                measure_index += 1;
            }
        }

        if (comma == len) {
            break;
        }
        start = comma + 1;
    }

    if (dump_rows) {
        const uint64_t rows_hash = hash_rows(rows);
        out << "STEP_PARITY_ROWS end total=" << rows.size() << " rows_hash=";
        print_hex16(out, rows_hash);
        out << "\n";
    }

    return rows;
}

static size_t build_intermediate_notes_with_timing(
    const std::vector<ParsedRow>& rows,
    TimingData* timing,
    int column_count,
    bool dump_notes,
    std::ostream& out) {
    if (column_count <= 0 || rows.empty()) {
        return 0;
    }

    std::vector<std::optional<std::pair<size_t, float>>> hold_starts(static_cast<size_t>(column_count));
    std::unordered_map<uint64_t, float> hold_lengths;
    hold_lengths.reserve(rows.size());

    for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
        const auto& row = rows[row_idx];
        for (int col = 0; col < column_count; ++col) {
            const unsigned char ch = row.chars[static_cast<size_t>(col)];
            if (ch == '2' || ch == '4') {
                hold_starts[static_cast<size_t>(col)] = std::make_pair(row_idx, row.beat);
            } else if (ch == '3') {
                const auto& start = hold_starts[static_cast<size_t>(col)];
                if (start.has_value()) {
                    const size_t start_idx = start->first;
                    const float length = row.beat - start->second;
                    const uint64_t key = (static_cast<uint64_t>(start_idx) << 32) | static_cast<uint32_t>(col);
                    hold_lengths[key] = length;
                    hold_starts[static_cast<size_t>(col)] = std::nullopt;
                }
            }
        }
    }

    if (dump_notes) {
        const uint64_t rows_hash = hash_rows(rows);
        out << "STEP_PARITY_NOTES start rows=" << rows.size()
            << " columns=" << column_count
            << " rows_hash=";
        print_hex16(out, rows_hash);
        out << "\n";
    }

    size_t note_count = 0;
    for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
        const auto& row = rows[row_idx];
        const bool row_fake = timing ? timing->IsFakeAtBeat(row.beat) : false;
        for (int col = 0; col < column_count; ++col) {
            const unsigned char ch = row.chars[static_cast<size_t>(col)];
            DumpTapNoteType note_type = DumpTapNoteType::Empty;
            switch (ch) {
                case '0': note_type = DumpTapNoteType::Empty; break;
                case '1': note_type = DumpTapNoteType::Tap; break;
                case '2': note_type = DumpTapNoteType::HoldHead; break;
                case '4': note_type = DumpTapNoteType::HoldHead; break;
                case '3': note_type = DumpTapNoteType::HoldTail; break;
                case 'M': note_type = DumpTapNoteType::Mine; break;
                case 'K': note_type = DumpTapNoteType::Tap; break;
                case 'L': note_type = DumpTapNoteType::Tap; break;
                case 'F': note_type = DumpTapNoteType::Fake; break;
                default: note_type = DumpTapNoteType::Empty; break;
            }

            if (note_type == DumpTapNoteType::Empty || note_type == DumpTapNoteType::HoldTail) {
                continue;
            }

            DumpTapNoteSubType subtype = DumpTapNoteSubType::Invalid;
            if (ch == '4') {
                subtype = DumpTapNoteSubType::Roll;
            } else if (ch == '2') {
                subtype = DumpTapNoteSubType::Hold;
            }

            float hold_length = -1.0f;
            if (note_type == DumpTapNoteType::HoldHead) {
                const uint64_t key = (static_cast<uint64_t>(row_idx) << 32) | static_cast<uint32_t>(col);
                auto it = hold_lengths.find(key);
                hold_length = (it != hold_lengths.end()) ? it->second : kMissingHoldLengthBeats;
            }

            if (dump_notes) {
                out << std::fixed << std::setprecision(6);
                out << std::boolalpha;
                out << "STEP_PARITY_NOTE row_idx=" << row_idx
                    << " row=" << row.row
                    << " beat=" << row.beat
                    << " second=" << row.second
                    << " col=" << col
                    << " ch=" << static_cast<char>(ch)
                    << " type=" << to_string(note_type)
                    << " subtype=" << to_string(subtype)
                    << " fake=" << (note_type == DumpTapNoteType::Fake || row_fake)
                    << " hold_len=" << hold_length
                    << "\n";
                out << std::noboolalpha;
            }

            note_count += 1;
        }
    }

    if (dump_notes) {
        out << "STEP_PARITY_NOTES end total=" << note_count << "\n";
    }

    return note_count;
}

static bool emit_step_parity_path_dump(Steps* steps, std::ostream& out) {
    if (!steps) {
        out << "STEP_PARITY_PATH error=missing_steps\n";
        return false;
    }
    if (StepParity::Layouts.find(steps->m_StepsType) == StepParity::Layouts.end()) {
        out << "STEP_PARITY_PATH error=unsupported_steps_type\n";
        return false;
    }

    TimingData* timing = steps->GetTimingData();
    GAMESTATE->SetProcessedTimingData(timing);

    NoteData note_data;
    steps->GetNoteData(note_data);

    StepParity::StageLayout layout = StepParity::Layouts.at(steps->m_StepsType);
    StepParity::StepParityGenerator gen(layout);
    if (!gen.analyzeNoteData(note_data)) {
        GAMESTATE->SetProcessedTimingData(nullptr);
        out << "STEP_PARITY_PATH error=analyze_failed\n";
        return false;
    }

    const size_t node_count = gen.nodes.size();
    const int end_id = node_count ? gen.nodes.back()->id : -1;
    out << "STEP_PARITY_PATH start rows=" << gen.rows.size()
        << " nodes=" << node_count
        << " start=0 end=" << end_id
        << "\n";

    float total_cost = 0.0f;
    std::ios_base::fmtflags flags = out.flags();
    std::streamsize precision = out.precision();
    out << std::fixed << std::setprecision(6);

    for (size_t i = 0; i < gen.nodes_for_rows.size(); ++i) {
        const int node_id = gen.nodes_for_rows[i];
        const int prev_id = (i == 0) ? 0 : gen.nodes_for_rows[i - 1];
        StepParity::StepParityNode* prev_node = gen.nodes[prev_id];
        StepParity::StepParityNode* curr_node = gen.nodes[node_id];

        float edge_cost = -1.0f;
        auto it = prev_node->neighbors.find(curr_node);
        if (it != prev_node->neighbors.end()) {
            edge_cost = it->second;
        }
        total_cost += edge_cost;

        const StepParity::Row& row = gen.rows[i];
        const StepParity::State* state = curr_node->state;
        out << "STEP_PARITY_PATH row_idx=" << i
            << " node=" << node_id
            << " prev=" << prev_id
            << " edge_cost=" << edge_cost
            << " total_cost=" << total_cost
            << " beat=" << row.beat
            << " second=" << row.second
            << " note_count=" << row.noteCount
            << " columns=" << format_foot_vec(state->columns)
            << " combined=" << format_foot_vec(state->combinedColumns)
            << " moved=" << format_foot_vec(state->movedFeet)
            << " hold=" << format_foot_vec(state->holdFeet)
            << " row_feet=" << format_foot_positions(row.whereTheFeetAre)
            << " state_feet=" << format_foot_positions(state->whereTheFeetAre, StepParity::NUM_Foot)
            << " moved_flags=" << format_foot_flags(state->didTheFootMove, StepParity::NUM_Foot)
            << " hold_flags=" << format_foot_flags(state->isTheFootHolding, StepParity::NUM_Foot)
            << "\n";
    }

    const int last_id = gen.nodes_for_rows.empty() ? 0 : gen.nodes_for_rows.back();
    float end_cost = -1.0f;
    if (node_count && !gen.nodes_for_rows.empty()) {
        StepParity::StepParityNode* last_node = gen.nodes[last_id];
        StepParity::StepParityNode* end_node = gen.nodes.back();
        auto it = last_node->neighbors.find(end_node);
        if (it != last_node->neighbors.end()) {
            end_cost = it->second;
        }
    }
    total_cost += end_cost;
    out << "STEP_PARITY_PATH end last_node=" << last_id
        << " end_node=" << end_id
        << " edge_cost=" << end_cost
        << " total_cost=" << total_cost
        << "\n";

    out.flags(flags);
    out.precision(precision);

    GAMESTATE->SetProcessedTimingData(nullptr);
    return true;
}
} // namespace

bool emit_step_parity_dump(
    std::ostream& out,
    const std::string& simfile_path,
    const std::string& steps_type_req,
    const std::string& difficulty_req,
    const std::string& description_req,
    bool dump_rows,
    bool dump_notes,
    bool dump_path) {
    if (!dump_rows && !dump_notes && !dump_path) {
        return true;
    }

    init_singletons(0, nullptr);

    Song song;
    song.m_sSongFileName = simfile_path;
    song.SetSongDir(Dirname(simfile_path));

    const std::string ext = GetExtension(simfile_path).MakeLower();
    bool ok = false;
    if (ext == "ssc" || ext == "ats") {
        SSCLoader loader;
        ok = loader.LoadFromSimfile(simfile_path, song);
    } else if (ext == "sm" || ext == "sma") {
        SMLoader loader;
        ok = loader.LoadFromSimfile(simfile_path, song);
    } else {
        SSCLoader loader;
        ok = loader.LoadFromSimfile(simfile_path, song);
    }

    if (!ok) {
        out << "STEP_PARITY_DUMP error=failed_to_load_simfile\n";
        return false;
    }

    Steps* steps = select_steps(song.GetAllSteps(), steps_type_req, difficulty_req, description_req);
    if (!steps) {
        out << "STEP_PARITY_DUMP error=steps_not_found\n";
        return false;
    }

    if (!steps_supports_itgmania_notedata(steps)) {
        out << "STEP_PARITY_DUMP error=unsupported_steps_type\n";
        return false;
    }

    TimingData* timing = steps->GetTimingData();
    if (timing) {
        timing->TidyUpData(false);
    }

    RString note_data;
    steps->GetSMNoteData(note_data);

    int column_count = 0;
    if (GAMEMAN) {
        column_count = GAMEMAN->GetStepsTypeInfo(steps->m_StepsType).iNumTracks;
    }

    if (dump_rows || dump_notes) {
        const std::vector<ParsedRow> rows =
            parse_chart_rows_with_timing(note_data, timing, column_count, dump_rows, out);
        build_intermediate_notes_with_timing(rows, timing, column_count, dump_notes, out);
    }

    if (dump_path) {
        if (!emit_step_parity_path_dump(steps, out)) {
            return false;
        }
    }
    return true;
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

bool emit_step_parity_dump(
    std::ostream& out,
    const std::string& simfile_path,
    const std::string& steps_type,
    const std::string& difficulty,
    const std::string& description,
    bool dump_rows,
    bool dump_notes,
    bool dump_path) {
    (void)out;
    (void)simfile_path;
    (void)steps_type;
    (void)difficulty;
    (void)description;
    (void)dump_rows;
    (void)dump_notes;
    (void)dump_path;
    return false;
}
#endif
