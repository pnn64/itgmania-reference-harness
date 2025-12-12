#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "itgmania_adapter.h"

static std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char uc : s) {
        switch (uc) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (uc < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(uc >> 4) & 0x0F]);
                    out.push_back(hex[uc & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(uc));
                }
        }
    }
    return out;
}

static void emit_number_table(const std::vector<std::vector<double>>& table) {
    std::cout << "[";
    for (size_t i = 0; i < table.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << "[";
        for (size_t j = 0; j < table[i].size(); ++j) {
            if (j) std::cout << ", ";
            std::cout << table[i][j];
        }
        std::cout << "]";
    }
    std::cout << "]";
}

static void emit_labels_table(const std::vector<TimingLabelOut>& labels) {
    std::cout << "[";
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << "[" << labels[i].beat << ", \"" << json_escape(labels[i].label) << "\"]";
    }
    std::cout << "]";
}

static void print_usage() {
    std::cerr << "Usage: itgmania-reference-harness <simfile> [steps-type] [difficulty] [description]\n";
}

static void emit_json_stub(
    const std::string& simfile,
    const std::string& steps_type,
    const std::string& difficulty) {
    std::cout << "{\n";
    std::cout << "  \"status\": \"stub\",\n";
    std::cout << "  \"simfile\": \"" << json_escape(simfile) << "\",\n";
    std::cout << "  \"title\": \"\",\n";
    std::cout << "  \"subtitle\": \"\",\n";
    std::cout << "  \"artist\": \"\",\n";
    std::cout << "  \"step_artist\": \"\",\n";
    std::cout << "  \"description\": \"\",\n";
    std::cout << "  \"steps_type\": \"" << json_escape(steps_type) << "\",\n";
    std::cout << "  \"difficulty\": \"" << json_escape(difficulty) << "\",\n";
    std::cout << "  \"meter\": null,\n";
    std::cout << "  \"bpms\": \"\",\n";
    std::cout << "  \"bpm_min\": null,\n";
    std::cout << "  \"bpm_max\": null,\n";
    std::cout << "  \"display_bpm\": \"\",\n";
    std::cout << "  \"display_bpm_min\": null,\n";
    std::cout << "  \"display_bpm_max\": null,\n";
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
    std::cout << "  \"stream_sequences\": [],\n";
    std::cout << "  \"holds\": null,\n";
    std::cout << "  \"mines\": null,\n";
    std::cout << "  \"rolls\": null,\n";
    std::cout << "  \"taps_and_holds\": null,\n";
    std::cout << "  \"notes\": null,\n";
    std::cout << "  \"lifts\": null,\n";
    std::cout << "  \"fakes\": null,\n";
    std::cout << "  \"jumps\": null,\n";
    std::cout << "  \"hands\": null,\n";
    std::cout << "  \"quads\": null,\n";
    std::cout << "  \"timing\": {\n";
    std::cout << "    \"beat0_offset_seconds\": null,\n";
    std::cout << "    \"beat0_group_offset_seconds\": null,\n";
    std::cout << "    \"bpms\": [],\n";
    std::cout << "    \"stops\": [],\n";
    std::cout << "    \"delays\": [],\n";
    std::cout << "    \"time_signatures\": [],\n";
    std::cout << "    \"warps\": [],\n";
    std::cout << "    \"labels\": [],\n";
    std::cout << "    \"tickcounts\": [],\n";
    std::cout << "    \"combos\": [],\n";
    std::cout << "    \"speeds\": [],\n";
    std::cout << "    \"scrolls\": [],\n";
    std::cout << "    \"fakes\": []\n";
    std::cout << "  },\n";
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
    std::cout << ind2 << "\"simfile\": \"" << json_escape(m.simfile) << "\",\n";
    std::cout << ind2 << "\"title\": \"" << json_escape(m.title) << "\",\n";
    std::cout << ind2 << "\"subtitle\": \"" << json_escape(m.subtitle) << "\",\n";
    std::cout << ind2 << "\"artist\": \"" << json_escape(m.artist) << "\",\n";
    std::cout << ind2 << "\"step_artist\": \"" << json_escape(m.step_artist) << "\",\n";
    std::cout << ind2 << "\"description\": \"" << json_escape(m.description) << "\",\n";
    std::cout << ind2 << "\"steps_type\": \"" << json_escape(m.steps_type) << "\",\n";
    std::cout << ind2 << "\"difficulty\": \"" << json_escape(m.difficulty) << "\",\n";
    std::cout << ind2 << "\"meter\": " << m.meter << ",\n";
    std::cout << ind2 << "\"bpms\": \"" << json_escape(m.bpms) << "\",\n";
    std::cout << ind2 << "\"bpm_min\": " << m.bpm_min << ",\n";
    std::cout << ind2 << "\"bpm_max\": " << m.bpm_max << ",\n";
    std::cout << ind2 << "\"display_bpm\": \"" << json_escape(m.display_bpm) << "\",\n";
    std::cout << ind2 << "\"display_bpm_min\": " << m.display_bpm_min << ",\n";
    std::cout << ind2 << "\"display_bpm_max\": " << m.display_bpm_max << ",\n";
    std::cout << ind2 << "\"hash\": \"" << json_escape(m.hash) << "\",\n";
    std::cout << ind2 << "\"duration_seconds\": " << m.duration_seconds << ",\n";
    std::cout << ind2 << "\"streams_breakdown\": \"" << json_escape(m.streams_breakdown) << "\",\n";
    std::cout << ind2 << "\"streams_breakdown_level1\": \"" << json_escape(m.streams_breakdown_level1) << "\",\n";
    std::cout << ind2 << "\"streams_breakdown_level2\": \"" << json_escape(m.streams_breakdown_level2) << "\",\n";
    std::cout << ind2 << "\"streams_breakdown_level3\": \"" << json_escape(m.streams_breakdown_level3) << "\",\n";
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
    std::cout << ind2 << "\"stream_sequences\": [";
    for (size_t i = 0; i < m.stream_sequences.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << "{\"stream_start\": " << m.stream_sequences[i].stream_start
                  << ", \"stream_end\": " << m.stream_sequences[i].stream_end
                  << ", \"is_break\": " << (m.stream_sequences[i].is_break ? "true" : "false") << "}";
    }
    std::cout << "],\n";
    std::cout << ind2 << "\"holds\": " << m.holds << ",\n";
    std::cout << ind2 << "\"mines\": " << m.mines << ",\n";
    std::cout << ind2 << "\"rolls\": " << m.rolls << ",\n";
    std::cout << ind2 << "\"taps_and_holds\": " << m.taps_and_holds << ",\n";
    std::cout << ind2 << "\"notes\": " << m.notes << ",\n";
    std::cout << ind2 << "\"lifts\": " << m.lifts << ",\n";
    std::cout << ind2 << "\"fakes\": " << m.fakes << ",\n";
    std::cout << ind2 << "\"jumps\": " << m.jumps << ",\n";
    std::cout << ind2 << "\"hands\": " << m.hands << ",\n";
    std::cout << ind2 << "\"quads\": " << m.quads << ",\n";
    std::cout << ind2 << "\"timing\": {\n";
    std::cout << ind2 << "  \"beat0_offset_seconds\": " << m.beat0_offset_seconds << ",\n";
    std::cout << ind2 << "  \"beat0_group_offset_seconds\": " << m.beat0_group_offset_seconds << ",\n";
    std::cout << ind2 << "  \"bpms\": ";
    emit_number_table(m.timing_bpms);
    std::cout << ",\n";
    std::cout << ind2 << "  \"stops\": ";
    emit_number_table(m.timing_stops);
    std::cout << ",\n";
    std::cout << ind2 << "  \"delays\": ";
    emit_number_table(m.timing_delays);
    std::cout << ",\n";
    std::cout << ind2 << "  \"time_signatures\": ";
    emit_number_table(m.timing_time_signatures);
    std::cout << ",\n";
    std::cout << ind2 << "  \"warps\": ";
    emit_number_table(m.timing_warps);
    std::cout << ",\n";
    std::cout << ind2 << "  \"labels\": ";
    emit_labels_table(m.timing_labels);
    std::cout << ",\n";
    std::cout << ind2 << "  \"tickcounts\": ";
    emit_number_table(m.timing_tickcounts);
    std::cout << ",\n";
    std::cout << ind2 << "  \"combos\": ";
    emit_number_table(m.timing_combos);
    std::cout << ",\n";
    std::cout << ind2 << "  \"speeds\": ";
    emit_number_table(m.timing_speeds);
    std::cout << ",\n";
    std::cout << ind2 << "  \"scrolls\": ";
    emit_number_table(m.timing_scrolls);
    std::cout << ",\n";
    std::cout << ind2 << "  \"fakes\": ";
    emit_number_table(m.timing_fakes);
    std::cout << "\n";
    std::cout << ind2 << "},\n";
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
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const std::string simfile = argv[1];
    const std::string steps_type = (argc >= 3) ? argv[2] : "";
    const std::string difficulty = (argc >= 4) ? argv[3] : "";
    const std::string description = (argc >= 5) ? argv[4] : "";

    init_itgmania_runtime(argc, argv);

    if (steps_type.empty() && difficulty.empty()) {
        auto charts = parse_all_charts_with_itgmania(simfile, "", "", "");
        if (!charts.empty()) {
            emit_json_array(charts);
            return 0;
        }
    }

    // Edit charts can have multiple entries. If no description is provided,
    // return all edit charts matching steps_type/difficulty (as a JSON array).
    if (!steps_type.empty() && difficulty == "edit" && description.empty()) {
        auto charts = parse_all_charts_with_itgmania(simfile, steps_type, difficulty, "");
        if (!charts.empty()) {
            emit_json_array(charts);
            return 0;
        }
    }

    if (auto parsed = parse_chart_with_itgmania(simfile, steps_type, difficulty, description)) {
        emit_json(*parsed);
    } else {
        emit_json_stub(simfile, steps_type, difficulty);
    }

    return 0;
}
