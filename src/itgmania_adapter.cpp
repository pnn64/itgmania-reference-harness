#include "itgmania_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <numeric>
#include <filesystem>
#include <optional>

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
