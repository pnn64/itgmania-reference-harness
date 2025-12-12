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
    int total_steps = 0;
    std::vector<int> notes_per_measure;
    std::vector<double> nps_per_measure;
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
