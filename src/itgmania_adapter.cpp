#include "itgmania_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <numeric>
#include <fstream>
#include <filesystem>
#include <optional>
#include <tomcrypt.h>

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

static std::string to_lower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

static std::string steps_type_string(StepsType st) {
    std::string s = StepsTypeToString(st);
    std::replace(s.begin(), s.end(), '_', '-');
    return to_lower(s);
}

static std::string diff_string(Difficulty d) {
    return to_lower(DifficultyToString(d));
}

static std::string bpm_string_from_timing(TimingData* td) {
    std::vector<TimingSegment*> segments = td->GetTimingSegments(SEGMENT_BPM);
    std::vector<RString> bpmStrings;
    bpmStrings.reserve(segments.size());
    for (TimingSegment* segment : segments) {
        BPMSegment* bpmSegment = ToBPM(segment);
        float beat = bpmSegment->GetBeat();
        float bpm = bpmSegment->GetBPM();
        RString segmentStr = ssprintf("%s=%s", NormalizeDecimal(beat).c_str(), NormalizeDecimal(bpm).c_str());
        bpmStrings.push_back(segmentStr);
    }
    return join(",", bpmStrings);
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
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* Linner) -> int {
        double beat = luaL_optnumber(Linner, 2, 0.0);
        lua_pushnumber(Linner, beat);
        return 1;
    });
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

    lua_pushcfunction(L, lua_steps_gettimingdata);
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

static std::string compute_hash_with_lua(const std::string& simfile_path,
                                         const std::string& steps_type,
                                         const std::string& difficulty,
                                         const std::string& description) {
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
    if (luaL_dofile(L, parser_path.c_str()) != 0) {
        std::fprintf(stderr, "lua load error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return "";
    }

    LuaStepsCtx ctx{simfile_path, steps_type, difficulty, description};
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
    lua_close(L);
    return result;
}
} // namespace

static ChartMetrics build_metrics_for_steps(const std::string& simfile_path, Steps* steps, const Song& song) {
    steps->GetTimingData()->TidyUpData(false);
    steps->CalculateStepStats(0.0f);
    steps->CalculateGrooveStatsHash();
    steps->CalculateTechCounts();
    steps->CalculateMeasureInfo();

    StepsType st = steps->m_StepsType;
    std::string st_str = steps_type_string(st);
    std::string diff_str = diff_string(steps->GetDifficulty());

    const TechCounts& tech = steps->GetTechCounts(PLAYER_1);

    std::vector<int> notes_per_measure;
    const std::vector<int>& npm = steps->GetNotesPerMeasure(PLAYER_1);
    notes_per_measure.assign(npm.begin(), npm.end());

    std::vector<double> nps_per_measure;
    const std::vector<float>& nps = steps->GetNpsPerMeasure(PLAYER_1);
    nps_per_measure.reserve(nps.size());
    for (float v : nps) nps_per_measure.push_back(static_cast<double>(v));

    RadarValues radar = steps->GetRadarValues(PLAYER_1);
    int jumps = static_cast<int>(radar[RadarCategory_Jumps]);
    int hands = static_cast<int>(radar[RadarCategory_Hands]);
    int quads = static_cast<int>(radar[RadarCategory_Hands]); // quads not separately tracked; reuse hands

    ChartMetrics out;
    out.simfile = simfile_path;
    out.title = song.GetDisplayMainTitle();
    out.subtitle = song.GetDisplaySubTitle();
    out.artist = song.GetDisplayArtist();
    out.hash = compute_hash_with_lua(simfile_path, st_str, diff_str, steps->GetDescription());
    out.steps_type = st_str;
    out.difficulty = diff_str;
    out.meter = steps->GetMeter();
    out.bpms = bpm_string_from_timing(steps->GetTimingData());
    out.total_steps = static_cast<int>(std::accumulate(notes_per_measure.begin(), notes_per_measure.end(), 0));
    out.notes_per_measure = std::move(notes_per_measure);
    out.nps_per_measure = std::move(nps_per_measure);
    out.jumps = jumps;
    out.hands = hands;
    out.quads = quads;
    out.tech.crossovers = static_cast<int>(tech[TechCountsCategory_Crossovers]);
    out.tech.footswitches = static_cast<int>(tech[TechCountsCategory_Footswitches]);
    out.tech.sideswitches = static_cast<int>(tech[TechCountsCategory_Sideswitches]);
    out.tech.jacks = static_cast<int>(tech[TechCountsCategory_Jacks]);
    out.tech.brackets = static_cast<int>(tech[TechCountsCategory_Brackets]);
    out.tech.doublesteps = static_cast<int>(tech[TechCountsCategory_Doublesteps]);
    return out;
}

static Steps* select_steps(
    const std::vector<Steps*>& steps,
    const std::string& steps_type_req,
    const std::string& difficulty_req,
    const std::string& description_req) {
    for (Steps* s : steps) {
        std::string st = steps_type_string(s->m_StepsType);
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
        std::string st_str = steps_type_string(steps->m_StepsType);
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
