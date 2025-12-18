#pragma once

#include <optional>
#include <string>
#include <vector>

struct TechCountsOut {
    int crossovers = 0;
    int footswitches = 0;
    int sideswitches = 0;
    int jacks = 0;
    int brackets = 0;
    int doublesteps = 0;
};

struct TimingLabelOut {
    double beat = 0.0;
    std::string label;
};

struct StreamSequenceOut {
    int stream_start = 0;
    int stream_end = 0;
    bool is_break = false;
};

struct ChartMetrics {
    std::string status = "ok";
    std::string simfile;
    std::string hash;
    std::string title;
    std::string subtitle;
    std::string artist;
    std::string step_artist;
    std::string description;
    std::string steps_type;
    std::string difficulty;
    int meter = -1;
    std::string bpms;
    std::string hash_bpms;
    double bpm_min = 0.0;
    double bpm_max = 0.0;
    std::string display_bpm;
    double display_bpm_min = 0.0;
    double display_bpm_max = 0.0;
    double duration_seconds = 0.0;
    std::string streams_breakdown;
    std::string streams_breakdown_level1;
    std::string streams_breakdown_level2;
    std::string streams_breakdown_level3;
    int total_stream_measures = 0;
    int total_break_measures = 0;
    int total_steps = 0;
    std::vector<int> notes_per_measure;
    std::vector<double> nps_per_measure;
    std::vector<bool> equally_spaced_per_measure;
    double peak_nps = 0.0;
    std::vector<StreamSequenceOut> stream_sequences;
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
    TechCountsOut tech;
    double beat0_offset_seconds = 0.0;
    double beat0_group_offset_seconds = 0.0;
    std::vector<std::vector<double>> timing_bpms;
    std::vector<std::vector<double>> timing_stops;
    std::vector<std::vector<double>> timing_delays;
    std::vector<std::vector<double>> timing_time_signatures;
    std::vector<std::vector<double>> timing_warps;
    std::vector<TimingLabelOut> timing_labels;
    std::vector<std::vector<double>> timing_tickcounts;
    std::vector<std::vector<double>> timing_combos;
    std::vector<std::vector<double>> timing_speeds;
    std::vector<std::vector<double>> timing_scrolls;
    std::vector<std::vector<double>> timing_fakes;
};

std::optional<ChartMetrics> parse_chart_with_itgmania(
    const std::string& simfile_path,
    const std::string& steps_type,
    const std::string& difficulty,
    const std::string& description);
std::vector<ChartMetrics> parse_all_charts_with_itgmania(
    const std::string& simfile_path,
    const std::string& steps_type,
    const std::string& difficulty,
    const std::string& description);

void init_itgmania_runtime(int argc, char** argv);
