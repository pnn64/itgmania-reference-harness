#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <iomanip>

#include "itgmania_adapter.h"

static constexpr std::string_view kVersion = "0.1.3";

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

template <typename T, typename EmitOneFn>
static void emit_inline_array(std::ostream& out, const std::vector<T>& values, const EmitOneFn& emit_one) {
    out << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        emit_one(out, values[i]);
    }
    out << "]";
}

static void emit_number_table(std::ostream& out, const std::vector<std::vector<double>>& table) {
    emit_inline_array(out, table, [](std::ostream& out, const std::vector<double>& row) {
        emit_inline_array(out, row, [](std::ostream& out, double v) { out << v; });
    });
}

static void emit_labels_table(std::ostream& out, const std::vector<TimingLabelOut>& labels) {
    emit_inline_array(out, labels, [](std::ostream& out, const TimingLabelOut& label) {
        out << "[" << label.beat << ", \"" << json_escape(label.label) << "\"]";
    });
}

static void emit_int_array(std::ostream& out, const std::vector<int>& values) {
    emit_inline_array(out, values, [](std::ostream& out, int v) { out << v; });
}

static void emit_double_array(std::ostream& out, const std::vector<double>& values) {
    emit_inline_array(out, values, [](std::ostream& out, double v) { out << v; });
}

static void emit_string_array(std::ostream& out, const std::vector<std::string>& values) {
    emit_inline_array(out, values, [](std::ostream& out, const std::string& v) {
        out << "\"" << json_escape(v) << "\"";
    });
}

static void print_usage() {
    std::cerr
        << "itgmania-reference-harness v" << kVersion << "\n"
        << "Usage:\n"
        << "  itgmania-reference-harness [--hash|-h] <simfile> [steps-type] [difficulty] [description]\n"
        << "\n"
        << "Options:\n"
        << "  --version, -v Print the version and exit\n"
        << "  --hash, -h   Print a hash list (one line per chart), no JSON\n"
        << "  --step-parity-trace Include StepParity per-row trace in JSON output\n"
        << "  --help       Show this help\n";
}

static void emit_json_stub_timing(std::ostream& out) {
    out << "  \"timing\": {\n";
    out << "    \"beat0_offset_seconds\": null,\n";
    out << "    \"beat0_group_offset_seconds\": null,\n";
    out << "    \"bpms\": [],\n";
    out << "    \"stops\": [],\n";
    out << "    \"delays\": [],\n";
    out << "    \"time_signatures\": [],\n";
    out << "    \"warps\": [],\n";
    out << "    \"labels\": [],\n";
    out << "    \"tickcounts\": [],\n";
    out << "    \"combos\": [],\n";
    out << "    \"speeds\": [],\n";
    out << "    \"scrolls\": [],\n";
    out << "    \"fakes\": []\n";
    out << "  },\n";
}

static void emit_json_stub_tech_counts(std::ostream& out) {
    out << "  \"tech_counts\": {\n";
    out << "    \"crossovers\": 0,\n";
    out << "    \"footswitches\": 0,\n";
    out << "    \"sideswitches\": 0,\n";
    out << "    \"jacks\": 0,\n";
    out << "    \"brackets\": 0,\n";
    out << "    \"doublesteps\": 0\n";
    out << "  }\n";
}

static void emit_json_stub(
    std::ostream& out,
    const std::string& simfile,
    const std::string& steps_type,
    const std::string& difficulty) {
    out << "{\n";
    out << "  \"status\": \"stub\",\n";
    out << "  \"simfile\": \"" << json_escape(simfile) << "\",\n";
    out << "  \"title\": \"\",\n";
    out << "  \"subtitle\": \"\",\n";
    out << "  \"artist\": \"\",\n";
    out << "  \"step_artist\": \"\",\n";
    out << "  \"description\": \"\",\n";
    out << "  \"steps_type\": \"" << json_escape(steps_type) << "\",\n";
    out << "  \"difficulty\": \"" << json_escape(difficulty) << "\",\n";
    out << "  \"meter\": null,\n";
    out << "  \"bpms\": \"\",\n";
    out << "  \"hash_bpms\": \"\",\n";
    out << "  \"bpm_min\": null,\n";
    out << "  \"bpm_max\": null,\n";
    out << "  \"display_bpm\": \"\",\n";
    out << "  \"display_bpm_min\": null,\n";
    out << "  \"display_bpm_max\": null,\n";
    out << "  \"hash\": \"\",\n";
    out << "  \"duration_seconds\": null,\n";
    out << "  \"streams_breakdown\": \"\",\n";
    out << "  \"streams_breakdown_level1\": \"\",\n";
    out << "  \"streams_breakdown_level2\": \"\",\n";
    out << "  \"streams_breakdown_level3\": \"\",\n";
    out << "  \"total_stream_measures\": null,\n";
    out << "  \"total_break_measures\": null,\n";
    out << "  \"total_steps\": null,\n";
    out << "  \"notes_per_measure\": [],\n";
    out << "  \"nps_per_measure\": [],\n";
    out << "  \"equally_spaced_per_measure\": [],\n";
    out << "  \"peak_nps\": null,\n";
    out << "  \"stream_sequences\": [],\n";
    out << "  \"holds\": null,\n";
    out << "  \"mines\": null,\n";
    out << "  \"rolls\": null,\n";
    out << "  \"taps_and_holds\": null,\n";
    out << "  \"notes\": null,\n";
    out << "  \"lifts\": null,\n";
    out << "  \"fakes\": null,\n";
    out << "  \"jumps\": null,\n";
    out << "  \"hands\": null,\n";
    out << "  \"quads\": null,\n";
    emit_json_stub_timing(out);
    emit_json_stub_tech_counts(out);
    out << "}\n";
}

static void emit_chart_json_header(std::ostream& out, const ChartMetrics& m, const std::string& ind2) {
    out << ind2 << "\"status\": \"" << json_escape(m.status) << "\",\n";
    out << ind2 << "\"simfile\": \"" << json_escape(m.simfile) << "\",\n";
    out << ind2 << "\"title\": \"" << json_escape(m.title) << "\",\n";
    out << ind2 << "\"subtitle\": \"" << json_escape(m.subtitle) << "\",\n";
    out << ind2 << "\"artist\": \"" << json_escape(m.artist) << "\",\n";
    out << ind2 << "\"step_artist\": \"" << json_escape(m.step_artist) << "\",\n";
    out << ind2 << "\"description\": \"" << json_escape(m.description) << "\",\n";
    out << ind2 << "\"steps_type\": \"" << json_escape(m.steps_type) << "\",\n";
    out << ind2 << "\"difficulty\": \"" << json_escape(m.difficulty) << "\",\n";
    out << ind2 << "\"meter\": " << m.meter << ",\n";
    out << ind2 << "\"bpms\": \"" << json_escape(m.bpms) << "\",\n";
    out << ind2 << "\"hash_bpms\": \"" << json_escape(m.hash_bpms) << "\",\n";
    out << ind2 << "\"bpm_min\": " << m.bpm_min << ",\n";
    out << ind2 << "\"bpm_max\": " << m.bpm_max << ",\n";
    out << ind2 << "\"display_bpm\": \"" << json_escape(m.display_bpm) << "\",\n";
    out << ind2 << "\"display_bpm_min\": " << m.display_bpm_min << ",\n";
    out << ind2 << "\"display_bpm_max\": " << m.display_bpm_max << ",\n";
    out << ind2 << "\"hash\": \"" << json_escape(m.hash) << "\",\n";
    out << ind2 << "\"duration_seconds\": " << m.duration_seconds << ",\n";
    out << ind2 << "\"streams_breakdown\": \"" << json_escape(m.streams_breakdown) << "\",\n";
    out << ind2 << "\"streams_breakdown_level1\": \"" << json_escape(m.streams_breakdown_level1) << "\",\n";
    out << ind2 << "\"streams_breakdown_level2\": \"" << json_escape(m.streams_breakdown_level2) << "\",\n";
    out << ind2 << "\"streams_breakdown_level3\": \"" << json_escape(m.streams_breakdown_level3) << "\",\n";
    out << ind2 << "\"total_stream_measures\": " << m.total_stream_measures << ",\n";
    out << ind2 << "\"total_break_measures\": " << m.total_break_measures << ",\n";
    out << ind2 << "\"total_steps\": " << m.total_steps << ",\n";
}

static void emit_chart_json_measure_data(std::ostream& out, const ChartMetrics& m, const std::string& ind2) {
    out << ind2 << "\"notes_per_measure\": ";
    emit_inline_array(out, m.notes_per_measure, [](std::ostream& out, int v) { out << v; });
    out << ",\n";

    out << ind2 << "\"nps_per_measure\": ";
    emit_inline_array(out, m.nps_per_measure, [](std::ostream& out, double v) { out << v; });
    out << ",\n";

    out << ind2 << "\"equally_spaced_per_measure\": ";
    emit_inline_array(out, m.equally_spaced_per_measure, [](std::ostream& out, bool v) { out << (v ? "true" : "false"); });
    out << ",\n";

    out << ind2 << "\"peak_nps\": " << m.peak_nps << ",\n";
    out << ind2 << "\"stream_sequences\": ";
    emit_inline_array(out, m.stream_sequences, [](std::ostream& out, const StreamSequenceOut& seq) {
        out << "{\"stream_start\": " << seq.stream_start << ", \"stream_end\": " << seq.stream_end
            << ", \"is_break\": " << (seq.is_break ? "true" : "false") << "}";
    });
    out << ",\n";

    out << ind2 << "\"holds\": " << m.holds << ",\n";
    out << ind2 << "\"mines\": " << m.mines << ",\n";
    out << ind2 << "\"rolls\": " << m.rolls << ",\n";
    out << ind2 << "\"taps_and_holds\": " << m.taps_and_holds << ",\n";
    out << ind2 << "\"notes\": " << m.notes << ",\n";
    out << ind2 << "\"lifts\": " << m.lifts << ",\n";
    out << ind2 << "\"fakes\": " << m.fakes << ",\n";
    out << ind2 << "\"jumps\": " << m.jumps << ",\n";
    out << ind2 << "\"hands\": " << m.hands << ",\n";
    out << ind2 << "\"quads\": " << m.quads << ",\n";
}

static void emit_chart_json_timing(std::ostream& out, const ChartMetrics& m, const std::string& ind2) {
    out << ind2 << "\"timing\": {\n";
    out << ind2 << "  \"beat0_offset_seconds\": " << m.beat0_offset_seconds << ",\n";
    out << ind2 << "  \"beat0_group_offset_seconds\": " << m.beat0_group_offset_seconds << ",\n";
    out << ind2 << "  \"bpms\": ";
    emit_number_table(out, m.timing_bpms);
    out << ",\n";
    out << ind2 << "  \"stops\": ";
    emit_number_table(out, m.timing_stops);
    out << ",\n";
    out << ind2 << "  \"delays\": ";
    emit_number_table(out, m.timing_delays);
    out << ",\n";
    out << ind2 << "  \"time_signatures\": ";
    emit_number_table(out, m.timing_time_signatures);
    out << ",\n";
    out << ind2 << "  \"warps\": ";
    emit_number_table(out, m.timing_warps);
    out << ",\n";
    out << ind2 << "  \"labels\": ";
    emit_labels_table(out, m.timing_labels);
    out << ",\n";
    out << ind2 << "  \"tickcounts\": ";
    emit_number_table(out, m.timing_tickcounts);
    out << ",\n";
    out << ind2 << "  \"combos\": ";
    emit_number_table(out, m.timing_combos);
    out << ",\n";
    out << ind2 << "  \"speeds\": ";
    emit_number_table(out, m.timing_speeds);
    out << ",\n";
    out << ind2 << "  \"scrolls\": ";
    emit_number_table(out, m.timing_scrolls);
    out << ",\n";
    out << ind2 << "  \"fakes\": ";
    emit_number_table(out, m.timing_fakes);
    out << "\n";
    out << ind2 << "},\n";
}

static void emit_chart_json_tech_counts(std::ostream& out, const ChartMetrics& m, const std::string& ind2, bool trailing_comma) {
    out << ind2 << "\"tech_counts\": {\n";
    out << ind2 << "  \"crossovers\": " << m.tech.crossovers << ",\n";
    out << ind2 << "  \"footswitches\": " << m.tech.footswitches << ",\n";
    out << ind2 << "  \"sideswitches\": " << m.tech.sideswitches << ",\n";
    out << ind2 << "  \"jacks\": " << m.tech.jacks << ",\n";
    out << ind2 << "  \"brackets\": " << m.tech.brackets << ",\n";
    out << ind2 << "  \"doublesteps\": " << m.tech.doublesteps << "\n";
    out << ind2 << "}";
    if (trailing_comma) {
        out << ",";
    }
    out << "\n";
}

static void emit_chart_json_step_parity_trace(std::ostream& out, const StepParityTraceOut& trace, const std::string& ind2) {
    const std::string ind3 = ind2 + "  ";
    const std::string ind4 = ind3 + "  ";
    const std::string ind5 = ind4 + "  ";

    out << ind2 << "\"step_parity_trace\": {\n";
    out << ind3 << "\"status\": \"" << json_escape(trace.status) << "\",\n";
    out << ind3 << "\"foot_labels\": ";
    emit_string_array(out, trace.foot_labels);
    out << ",\n";
    out << ind3 << "\"note_type_labels\": ";
    emit_string_array(out, trace.note_type_labels);
    out << ",\n";
    out << ind3 << "\"cost_labels\": ";
    emit_string_array(out, trace.cost_labels);
    out << ",\n";
    out << ind3 << "\"layout\": {\n";
    out << ind4 << "\"columns\": ";
    emit_number_table(out, trace.layout.columns);
    out << ",\n";
    out << ind4 << "\"up_arrows\": ";
    emit_int_array(out, trace.layout.up_arrows);
    out << ",\n";
    out << ind4 << "\"down_arrows\": ";
    emit_int_array(out, trace.layout.down_arrows);
    out << ",\n";
    out << ind4 << "\"side_arrows\": ";
    emit_int_array(out, trace.layout.side_arrows);
    out << "\n";
    out << ind3 << "},\n";
    out << ind3 << "\"rows\": [\n";
    for (size_t i = 0; i < trace.rows.size(); ++i) {
        const StepParityRowOut& row = trace.rows[i];
        out << ind4 << "{\n";
        out << ind5 << "\"row_index\": " << row.row_index << ",\n";
        out << ind5 << "\"beat\": " << row.beat << ",\n";
        out << ind5 << "\"second\": " << row.second << ",\n";
        out << ind5 << "\"note_count\": " << row.note_count << ",\n";
        out << ind5 << "\"note_types\": ";
        emit_int_array(out, row.note_types);
        out << ",\n";
        out << ind5 << "\"hold_types\": ";
        emit_int_array(out, row.hold_types);
        out << ",\n";
        out << ind5 << "\"columns\": ";
        emit_int_array(out, row.columns);
        out << ",\n";
        out << ind5 << "\"where_feet\": ";
        emit_int_array(out, row.where_feet);
        out << ",\n";
        out << ind5 << "\"hold_tails\": ";
        emit_int_array(out, row.hold_tails);
        out << ",\n";
        out << ind5 << "\"mines\": ";
        emit_double_array(out, row.mines);
        out << ",\n";
        out << ind5 << "\"fake_mines\": ";
        emit_double_array(out, row.fake_mines);
        out << "\n";
        out << ind4 << "}";
        if (i + 1 < trace.rows.size()) out << ",";
        out << "\n";
    }
    out << ind3 << "],\n";
    out << ind3 << "\"tech_rows\": [\n";
    for (size_t i = 0; i < trace.tech_rows.size(); ++i) {
        const StepParityTechRowOut& row = trace.tech_rows[i];
        out << ind4 << "{\n";
        out << ind5 << "\"row_index\": " << row.row_index << ",\n";
        out << ind5 << "\"elapsed\": " << row.elapsed << ",\n";
        out << ind5 << "\"jacks\": " << row.jacks << ",\n";
        out << ind5 << "\"doublesteps\": " << row.doublesteps << ",\n";
        out << ind5 << "\"brackets\": " << row.brackets << ",\n";
        out << ind5 << "\"footswitches\": " << row.footswitches << ",\n";
        out << ind5 << "\"up_footswitches\": " << row.up_footswitches << ",\n";
        out << ind5 << "\"down_footswitches\": " << row.down_footswitches << ",\n";
        out << ind5 << "\"sideswitches\": " << row.sideswitches << ",\n";
        out << ind5 << "\"crossovers\": " << row.crossovers << ",\n";
        out << ind5 << "\"half_crossovers\": " << row.half_crossovers << ",\n";
        out << ind5 << "\"full_crossovers\": " << row.full_crossovers << "\n";
        out << ind4 << "}";
        if (i + 1 < trace.tech_rows.size()) out << ",";
        out << "\n";
    }
    out << ind3 << "],\n";
    out << ind3 << "\"cost_rows\": [\n";
    for (size_t i = 0; i < trace.cost_rows.size(); ++i) {
        const StepParityCostRowOut& row = trace.cost_rows[i];
        out << ind4 << "{\n";
        out << ind5 << "\"row_index\": " << row.row_index << ",\n";
        out << ind5 << "\"elapsed\": " << row.elapsed << ",\n";
        out << ind5 << "\"total\": " << row.total << ",\n";
        out << ind5 << "\"costs\": ";
        emit_double_array(out, row.costs);
        out << "\n";
        out << ind4 << "}";
        if (i + 1 < trace.cost_rows.size()) out << ",";
        out << "\n";
    }
    out << ind3 << "]\n";
    out << ind2 << "}";
}

static void emit_chart_json(std::ostream& out, const ChartMetrics& m, const std::string& indent) {
    const std::string ind2 = indent + "  ";
    out << indent << "{\n";
    emit_chart_json_header(out, m, ind2);
    emit_chart_json_measure_data(out, m, ind2);
    emit_chart_json_timing(out, m, ind2);
    const bool has_step_parity_trace = m.step_parity_trace.has_value();
    emit_chart_json_tech_counts(out, m, ind2, has_step_parity_trace);
    if (has_step_parity_trace) {
        emit_chart_json_step_parity_trace(out, *m.step_parity_trace, ind2);
        out << "\n";
    }
    out << indent << "}";
}

static void emit_json(std::ostream& out, const ChartMetrics& m) {
    emit_chart_json(out, m, "");
    out << "\n";
}

static void emit_json_array(std::ostream& out, const std::vector<ChartMetrics>& charts) {
    out << "[\n";
    for (size_t i = 0; i < charts.size(); ++i) {
        emit_chart_json(out, charts[i], "  ");
        if (i + 1 < charts.size()) out << ",";
        out << "\n";
    }
    out << "]\n";
}

struct CliOpts {
    bool hash_mode = false;
    bool help = false;
    bool version = false;
    bool step_parity_trace = false;
    std::vector<std::string> positional;
};

static CliOpts parse_args(int argc, char** argv) {
    CliOpts o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];

        if (a == "--hash" || a == "-h") {
            o.hash_mode = true;
            continue;
        }
        if (a == "--version" || a == "-v") {
            o.version = true;
            continue;
        }
        if (a == "--step-parity-trace") {
            o.step_parity_trace = true;
            continue;
        }
        if (a == "--help") {
            o.help = true;
            continue;
        }
        if (a == "--") {
            for (++i; i < argc; ++i) o.positional.emplace_back(argv[i]);
            break;
        }
        if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << "\n";
            o.help = true;
            return o;
        }

        o.positional.push_back(std::move(a));
    }
    return o;
}

static int run_hash_mode(const std::string& simfile) {
    init_itgmania_runtime(0, nullptr);

    auto charts = parse_all_charts_with_itgmania(simfile, "", "", "");
    if (charts.empty()) {
        std::cerr << "No charts parsed for: " << simfile << "\n";
        return 2;
    }

    for (const auto& m : charts) {
        // No extra logic: print the parsed values directly.
        // Hash is already produced by ITGmania/Lua and should already be 16 chars in your setup.
        std::cout
            << std::left  << std::setw(20) << m.steps_type
            << std::right << std::setw(6)  << m.meter << "  "
            << std::left  << std::setw(18) << m.difficulty << "  "
            << m.hash
            << "\n";
    }

    return 0;
}

int main(int argc, char** argv) {
    const CliOpts opts = parse_args(argc, argv);

    if (opts.version) {
        std::cout << kVersion << "\n";
        return 0;
    }

    if (opts.help || opts.positional.empty()) {
        print_usage();
        return opts.help ? 0 : 1;
    }

    const std::string simfile = opts.positional[0];
    const std::string steps_type = (opts.positional.size() >= 2) ? opts.positional[1] : "";
    const std::string difficulty = (opts.positional.size() >= 3) ? opts.positional[2] : "";
    const std::string description = (opts.positional.size() >= 4) ? opts.positional[3] : "";

    if (opts.hash_mode) {
        return run_hash_mode(simfile);
    }

    set_step_parity_trace_enabled(opts.step_parity_trace);

    init_itgmania_runtime(argc, argv);

    if (steps_type.empty() && difficulty.empty()) {
        auto charts = parse_all_charts_with_itgmania(simfile, "", "", "");
        if (!charts.empty()) {
            emit_json_array(std::cout, charts);
            return 0;
        }
    }

    // Edit charts can have multiple entries. If no description is provided,
    // return all edit charts matching steps_type/difficulty (as a JSON array).
    if (!steps_type.empty() && difficulty == "edit" && description.empty()) {
        auto charts = parse_all_charts_with_itgmania(simfile, steps_type, difficulty, "");
        if (!charts.empty()) {
            emit_json_array(std::cout, charts);
            return 0;
        }
    }

    if (auto parsed = parse_chart_with_itgmania(simfile, steps_type, difficulty, description)) {
        emit_json(std::cout, *parsed);
    } else {
        emit_json_stub(std::cout, simfile, steps_type, difficulty);
    }

    return 0;
}
