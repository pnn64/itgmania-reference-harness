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

struct ChartMetrics {
    std::string simfile;
    std::string hash;
    std::string title;
    std::string subtitle;
    std::string artist;
    std::string steps_type;
    std::string difficulty;
    int meter = -1;
    std::string bpms;
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
    int jumps = 0;
    int hands = 0;
    int quads = 0;
    TechCountsOut tech;
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
