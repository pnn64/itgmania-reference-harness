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

static std::string compute_hash_with_lua(const std::string& simfile_path,
                                        const std::string& steps_type,
                                        const std::string& difficulty,
                                        const std::string& description,
                                        TimingData* timing,
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
    if (luaL_dofile(L, parser_path.c_str()) != 0) {
        std::fprintf(stderr, "lua load error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return "";
    }
    const std::string helper_path = "src/extern/itgmania/Themes/Simply Love/Scripts/SL-ChartParserHelpers.lua";
    if (luaL_dofile(L, helper_path.c_str()) != 0) {
        std::fprintf(stderr, "lua helper load error: %s\n", lua_tostring(L, -1));
        lua_close(L);
        return "";
    }

    LuaStepsCtx ctx{simfile_path, steps_type, difficulty, description, timing};
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

    RadarValues radar = steps->GetRadarValues(PLAYER_1);
    int holds = static_cast<int>(radar[RadarCategory_Holds]);
    int mines = static_cast<int>(radar[RadarCategory_Mines]);
    int rolls = static_cast<int>(radar[RadarCategory_Rolls]);
    int taps_and_holds = static_cast<int>(radar[RadarCategory_TapsAndHolds]);
    int notes = static_cast<int>(radar[RadarCategory_Notes]);
    int lifts = static_cast<int>(radar[RadarCategory_Lifts]);
    int fakes = static_cast<int>(radar[RadarCategory_Fakes]);
    int jumps = static_cast<int>(radar[RadarCategory_Jumps]);
    int hands = static_cast<int>(radar[RadarCategory_Hands]);
    int quads = static_cast<int>(radar[RadarCategory_Hands]); // quads not separately tracked; reuse hands

    ChartMetrics out;
    out.simfile = simfile_path;
    out.title = song.GetDisplayMainTitle();
    out.subtitle = song.GetDisplaySubTitle();
    out.artist = song.GetDisplayArtist();
    out.step_artist = steps->GetCredit();
    std::vector<int> lua_notes_pm;
    std::vector<double> lua_nps_pm;
    std::vector<bool> lua_equally_spaced;
    double lua_peak_nps = 0.0;
    std::vector<std::string> breakdown_levels;
    int stream_measures = 0;
    int break_measures = 0;
    std::vector<StreamSequenceOut> stream_sequences;
    out.hash = compute_hash_with_lua(simfile_path, st_str, diff_str, steps->GetDescription(), steps->GetTimingData(),
                                     &out.streams_breakdown, &breakdown_levels, &stream_measures, &break_measures,
                                     &stream_sequences,
                                     &lua_notes_pm, &lua_nps_pm, &lua_equally_spaced, &lua_peak_nps);
    out.steps_type = st_str;
    out.difficulty = diff_str;
    out.meter = steps->GetMeter();
    out.bpms = bpm_string_from_timing(steps->GetTimingData());
    get_bpm_ranges_like_simply_love(steps, 1.0, out.bpm_min, out.bpm_max, out.display_bpm_min, out.display_bpm_max,
                                   out.display_bpm);
    NoteData nd;
    steps->GetNoteData(nd);
    float last_beat = nd.GetLastBeat();
    out.duration_seconds = steps->GetTimingData()->GetElapsedTimeFromBeat(last_beat);
    std::vector<int> notes_per_measure;
    std::vector<double> nps_per_measure;
    if (lua_notes_pm.empty()) {
        const std::vector<int>& npm = steps->GetNotesPerMeasure(PLAYER_1);
        notes_per_measure.assign(npm.begin(), npm.end());

        const std::vector<float>& nps = steps->GetNpsPerMeasure(PLAYER_1);
        nps_per_measure.reserve(nps.size());
        for (float v : nps) nps_per_measure.push_back(static_cast<double>(v));
    } else {
        notes_per_measure = std::move(lua_notes_pm);
        nps_per_measure = std::move(lua_nps_pm);
    }
    out.total_steps = static_cast<int>(std::accumulate(notes_per_measure.begin(), notes_per_measure.end(), 0));
    if (!lua_equally_spaced.empty()) {
        out.equally_spaced_per_measure = std::move(lua_equally_spaced);
        out.peak_nps = lua_peak_nps;
    } else {
        // Fallback: mark all measures as not guaranteed equally spaced and use computed peak.
        out.equally_spaced_per_measure.assign(notes_per_measure.size(), false);
        out.peak_nps = 0.0;
        for (double v : nps_per_measure) out.peak_nps = std::max(out.peak_nps, v);
    }
    out.stream_sequences = std::move(stream_sequences);
    out.holds = holds;
    out.mines = mines;
    out.rolls = rolls;
    out.taps_and_holds = taps_and_holds;
    out.notes = notes;
    out.lifts = lifts;
    out.fakes = fakes;
    if (breakdown_levels.size() == 4) {
        out.streams_breakdown_level1 = breakdown_levels[1];
        out.streams_breakdown_level2 = breakdown_levels[2];
        out.streams_breakdown_level3 = breakdown_levels[3];
    }
    out.total_stream_measures = stream_measures;
    out.total_break_measures = break_measures;
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

    TimingData* td = steps->GetTimingData();
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
