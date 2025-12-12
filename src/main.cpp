#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "itgmania_adapter.h"

static void print_usage() {
    std::cerr << "Usage: itgmania-reference-harness <simfile> [steps-type] [difficulty]\n";
}

static void emit_json_stub(
    const std::string& simfile,
    const std::string& steps_type,
    const std::string& difficulty) {
    std::cout << "{\n";
    std::cout << "  \"status\": \"stub\",\n";
    std::cout << "  \"simfile\": \"" << simfile << "\",\n";
    std::cout << "  \"title\": \"\",\n";
    std::cout << "  \"subtitle\": \"\",\n";
    std::cout << "  \"artist\": \"\",\n";
    std::cout << "  \"steps_type\": \"" << steps_type << "\",\n";
    std::cout << "  \"difficulty\": \"" << difficulty << "\",\n";
    std::cout << "  \"meter\": null,\n";
    std::cout << "  \"bpms\": \"\",\n";
    std::cout << "  \"hash\": \"\",\n";
    std::cout << "  \"duration_seconds\": null,\n";
    std::cout << "  \"streams_breakdown\": \"\",\n";
    std::cout << "  \"streams_breakdown_level1\": \"\",\n";
    std::cout << "  \"streams_breakdown_level2\": \"\",\n";
    std::cout << "  \"streams_breakdown_level3\": \"\",\n";
    std::cout << "  \"total_stream_measures\": null,\n";
    std::cout << "  \"total_break_measures\": null,\n";
    std::cout << "  \"total_steps\": null,\n";
    std::cout << "  \"notes_per_measure\": [],\n";
    std::cout << "  \"nps_per_measure\": [],\n";
    std::cout << "  \"equally_spaced_per_measure\": [],\n";
    std::cout << "  \"peak_nps\": null,\n";
    std::cout << "  \"tech_counts\": {\n";
    std::cout << "    \"crossovers\": 0,\n";
    std::cout << "    \"footswitches\": 0,\n";
    std::cout << "    \"sideswitches\": 0,\n";
    std::cout << "    \"jacks\": 0,\n";
    std::cout << "    \"brackets\": 0,\n";
    std::cout << "    \"doublesteps\": 0\n";
    std::cout << "  }\n";
    std::cout << "}\n";
}

static void emit_chart_json(const ChartMetrics& m, const std::string& indent) {
    const std::string ind2 = indent + "  ";
    std::cout << indent << "{\n";
    std::cout << ind2 << "\"status\": \"ok\",\n";
    std::cout << ind2 << "\"simfile\": \"" << m.simfile << "\",\n";
    std::cout << ind2 << "\"title\": \"" << m.title << "\",\n";
    std::cout << ind2 << "\"subtitle\": \"" << m.subtitle << "\",\n";
    std::cout << ind2 << "\"artist\": \"" << m.artist << "\",\n";
    std::cout << ind2 << "\"steps_type\": \"" << m.steps_type << "\",\n";
    std::cout << ind2 << "\"difficulty\": \"" << m.difficulty << "\",\n";
    std::cout << ind2 << "\"meter\": " << m.meter << ",\n";
    std::cout << ind2 << "\"bpms\": \"" << m.bpms << "\",\n";
    std::cout << ind2 << "\"hash\": \"" << m.hash << "\",\n";
    std::cout << ind2 << "\"duration_seconds\": " << m.duration_seconds << ",\n";
    std::cout << ind2 << "\"streams_breakdown\": \"" << m.streams_breakdown << "\",\n";
    std::cout << ind2 << "\"streams_breakdown_level1\": \"" << m.streams_breakdown_level1 << "\",\n";
    std::cout << ind2 << "\"streams_breakdown_level2\": \"" << m.streams_breakdown_level2 << "\",\n";
    std::cout << ind2 << "\"streams_breakdown_level3\": \"" << m.streams_breakdown_level3 << "\",\n";
    std::cout << ind2 << "\"total_stream_measures\": " << m.total_stream_measures << ",\n";
    std::cout << ind2 << "\"total_break_measures\": " << m.total_break_measures << ",\n";
    std::cout << ind2 << "\"total_steps\": " << m.total_steps << ",\n";
    std::cout << ind2 << "\"notes_per_measure\": [";
    for (size_t i = 0; i < m.notes_per_measure.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << m.notes_per_measure[i];
    }
    std::cout << "],\n";
    std::cout << ind2 << "\"nps_per_measure\": [";
    for (size_t i = 0; i < m.nps_per_measure.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << m.nps_per_measure[i];
    }
    std::cout << "],\n";
    std::cout << ind2 << "\"equally_spaced_per_measure\": [";
    for (size_t i = 0; i < m.equally_spaced_per_measure.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << (m.equally_spaced_per_measure[i] ? "true" : "false");
    }
    std::cout << "],\n";
    std::cout << ind2 << "\"peak_nps\": " << m.peak_nps << ",\n";
    std::cout << ind2 << "\"jumps\": " << m.jumps << ",\n";
    std::cout << ind2 << "\"hands\": " << m.hands << ",\n";
    std::cout << ind2 << "\"quads\": " << m.quads << ",\n";
    std::cout << ind2 << "\"tech_counts\": {\n";
    std::cout << ind2 << "  \"crossovers\": " << m.tech.crossovers << ",\n";
    std::cout << ind2 << "  \"footswitches\": " << m.tech.footswitches << ",\n";
    std::cout << ind2 << "  \"sideswitches\": " << m.tech.sideswitches << ",\n";
    std::cout << ind2 << "  \"jacks\": " << m.tech.jacks << ",\n";
    std::cout << ind2 << "  \"brackets\": " << m.tech.brackets << ",\n";
    std::cout << ind2 << "  \"doublesteps\": " << m.tech.doublesteps << "\n";
    std::cout << ind2 << "}\n";
    std::cout << indent << "}";
}

static void emit_json(const ChartMetrics& m) {
    emit_chart_json(m, "");
    std::cout << "\n";
}

static void emit_json_array(const std::vector<ChartMetrics>& charts) {
    std::cout << "[\n";
    for (size_t i = 0; i < charts.size(); ++i) {
        emit_chart_json(charts[i], "  ");
        if (i + 1 < charts.size()) std::cout << ",";
        std::cout << "\n";
    }
    std::cout << "]\n";
}

int main(int argc, char** argv) {
    std::fprintf(stderr, "enter main\n");
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::string simfile = argv[1];
    const std::string steps_type = (argc >= 3) ? argv[2] : "";
    const std::string difficulty = (argc >= 4) ? argv[3] : "";

    init_itgmania_runtime(argc, argv);

    if (steps_type.empty() && difficulty.empty()) {
        auto charts = parse_all_charts_with_itgmania(simfile, "", "", "");
        if (!charts.empty()) {
            emit_json_array(charts);
            return 0;
        }
    }

    if (auto parsed = parse_chart_with_itgmania(simfile, steps_type, difficulty, "")) {
        emit_json(*parsed);
    } else {
        emit_json_stub(simfile, steps_type, difficulty);
    }

    return 0;
}
