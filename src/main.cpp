#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <iomanip>

#include "itgmania_adapter.h"

static constexpr std::string_view kVersion = "0.1.10";

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

static void print_usage() {
    std::cerr
        << "itgmania-reference-harness v" << kVersion << "\n"
        << "Usage:\n"
        << "  itgmania-reference-harness [--hash|-h] <simfile> [steps-type] [difficulty] [description]\n"
        << "\n"
        << "Options:\n"
        << "  --version, -v Print the version and exit\n"
        << "  --hash, -h   Print a hash list (one line per chart), no JSON\n"
        << "  --omit-tech  Omit tech_counts from JSON output\n"
        << "  --dump-rows  Emit step parity row dumps to stderr\n"
        << "  --dump-notes Emit step parity note dumps to stderr\n"
        << "  --dump-path  Emit step parity path dumps to stderr\n"
        << "  --help       Show this help\n";
}

static void emit_json_stub_timing(std::ostream& out, bool trailing_comma) {
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
    out << "  }";
    if (trailing_comma) {
        out << ",\n";
    } else {
        out << "\n";
    }
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
    const std::string& difficulty,
    bool include_tech_counts) {
    out << "{\n";
    out << "  \"status\": \"stub\",\n";
    out << "  \"simfile\": \"" << json_escape(simfile) << "\",\n";
    out << "  \"title\": \"\",\n";
    out << "  \"subtitle\": \"\",\n";
    out << "  \"artist\": \"\",\n";
    out << "  \"title_translated\": \"\",\n";
    out << "  \"subtitle_translated\": \"\",\n";
    out << "  \"artist_translated\": \"\",\n";
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
    emit_json_stub_timing(out, include_tech_counts);
    if (include_tech_counts) {
        emit_json_stub_tech_counts(out);
    }
    out << "}\n";
}

static void emit_chart_json_header(std::ostream& out, const ChartMetrics& m, const std::string& ind2) {
    out << ind2 << "\"status\": \"" << json_escape(m.status) << "\",\n";
    out << ind2 << "\"simfile\": \"" << json_escape(m.simfile) << "\",\n";
    out << ind2 << "\"title\": \"" << json_escape(m.title) << "\",\n";
    out << ind2 << "\"subtitle\": \"" << json_escape(m.subtitle) << "\",\n";
    out << ind2 << "\"artist\": \"" << json_escape(m.artist) << "\",\n";
    out << ind2 << "\"title_translated\": \"" << json_escape(m.title_translated) << "\",\n";
    out << ind2 << "\"subtitle_translated\": \"" << json_escape(m.subtitle_translated) << "\",\n";
    out << ind2 << "\"artist_translated\": \"" << json_escape(m.artist_translated) << "\",\n";
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

static void emit_chart_json_timing(
    std::ostream& out,
    const ChartMetrics& m,
    const std::string& ind2,
    bool trailing_comma) {
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
    out << ind2 << "}";
    if (trailing_comma) {
        out << ",\n";
    } else {
        out << "\n";
    }
}

static void emit_chart_json_tech_counts(std::ostream& out, const ChartMetrics& m, const std::string& indent, const std::string& ind2) {
    out << ind2 << "\"tech_counts\": {\n";
    out << ind2 << "  \"crossovers\": " << m.tech.crossovers << ",\n";
    out << ind2 << "  \"footswitches\": " << m.tech.footswitches << ",\n";
    out << ind2 << "  \"sideswitches\": " << m.tech.sideswitches << ",\n";
    out << ind2 << "  \"jacks\": " << m.tech.jacks << ",\n";
    out << ind2 << "  \"brackets\": " << m.tech.brackets << ",\n";
    out << ind2 << "  \"doublesteps\": " << m.tech.doublesteps << "\n";
    out << ind2 << "}\n";
    out << indent << "}";
}

static void emit_chart_json(
    std::ostream& out,
    const ChartMetrics& m,
    const std::string& indent,
    bool include_tech_counts) {
    const std::string ind2 = indent + "  ";
    out << indent << "{\n";
    emit_chart_json_header(out, m, ind2);
    emit_chart_json_measure_data(out, m, ind2);
    emit_chart_json_timing(out, m, ind2, include_tech_counts);
    if (include_tech_counts) {
        emit_chart_json_tech_counts(out, m, indent, ind2);
    } else {
        out << indent << "}";
    }
}

static void emit_json(std::ostream& out, const ChartMetrics& m, bool include_tech_counts) {
    emit_chart_json(out, m, "", include_tech_counts);
    out << "\n";
}

static void emit_json_array(
    std::ostream& out,
    const std::vector<ChartMetrics>& charts,
    bool include_tech_counts) {
    out << "[\n";
    for (size_t i = 0; i < charts.size(); ++i) {
        emit_chart_json(out, charts[i], "  ", include_tech_counts);
        if (i + 1 < charts.size()) out << ",";
        out << "\n";
    }
    out << "]\n";
}

struct CliOpts {
    bool hash_mode = false;
    bool help = false;
    bool version = false;
    bool omit_tech = false;
    bool dump_rows = false;
    bool dump_notes = false;
    bool dump_path = false;
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
        if (a == "--omit-tech") {
            o.omit_tech = true;
            continue;
        }
        if (a == "--dump-rows") {
            o.dump_rows = true;
            continue;
        }
        if (a == "--dump-notes") {
            o.dump_notes = true;
            continue;
        }
        if (a == "--dump-path") {
            o.dump_path = true;
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
    const bool include_tech_counts = !opts.omit_tech;
    const bool wants_dump = opts.dump_rows || opts.dump_notes || opts.dump_path;

    if (opts.hash_mode) {
        if (wants_dump) {
            std::cerr << "--dump-rows/--dump-notes/--dump-path are not available with --hash\n";
            return 1;
        }
        return run_hash_mode(simfile);
    }

    init_itgmania_runtime(argc, argv);

    if (wants_dump) {
        if (steps_type.empty() || difficulty.empty()) {
            std::cerr << "--dump-rows/--dump-notes/--dump-path require steps-type and difficulty\n";
            return 1;
        }
        if (difficulty == "edit" && description.empty()) {
            std::cerr << "--dump-rows/--dump-notes/--dump-path require description for edit charts\n";
            return 1;
        }
        if (!emit_step_parity_dump(
                std::cerr,
                simfile,
                steps_type,
                difficulty,
                description,
                opts.dump_rows,
                opts.dump_notes,
                opts.dump_path)) {
            std::cerr << "Failed to emit step parity dump\n";
            return 1;
        }
    }

    if (steps_type.empty() && difficulty.empty()) {
        auto charts = parse_all_charts_with_itgmania(simfile, "", "", "");
        if (!charts.empty()) {
            emit_json_array(std::cout, charts, include_tech_counts);
            return 0;
        }
    }

    // Edit charts can have multiple entries. If no description is provided,
    // return all edit charts matching steps_type/difficulty (as a JSON array).
    if (!steps_type.empty() && difficulty == "edit" && description.empty()) {
        auto charts = parse_all_charts_with_itgmania(simfile, steps_type, difficulty, "");
        if (!charts.empty()) {
            emit_json_array(std::cout, charts, include_tech_counts);
            return 0;
        }
    }

    if (auto parsed = parse_chart_with_itgmania(simfile, steps_type, difficulty, description)) {
        emit_json(std::cout, *parsed, include_tech_counts);
    } else {
        emit_json_stub(std::cout, simfile, steps_type, difficulty, include_tech_counts);
    }

    return 0;
}
