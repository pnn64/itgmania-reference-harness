#include "itgmania_adapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <set>
#include <vector>

#ifdef ITGMANIA_HARNESS
#include "global.h"
#include "GameConstantsAndTypes.h"
#include "GameManager.h"
#include "GameState.h"
#include "NoteData.h"
#include "NotesLoader.h"
#include "NotesLoaderDWI.h"
#include "NotesLoaderSM.h"
#include "NotesLoaderSSC.h"
#include "RageUtil.h"
#include "Song.h"
#include "Steps.h"
#include "StepParityGenerator.h"
#include "TimingData.h"

static bool steps_supports_itgmania_notedata(const Steps* steps) {
    if (!steps || !GAMEMAN) return false;
    return GAMEMAN->GetStepsTypeInfo(steps->m_StepsType).iNumTracks > 0;
}

static Steps* select_steps(
    const std::vector<Steps*>& steps,
    const std::string& steps_type_req,
    const std::string& difficulty_req,
    const std::string& description_req) {
    for (Steps* s : steps) {
        std::string st = StepsTypeToString(s->m_StepsType);
        std::replace(st.begin(), st.end(), '_', '-');
        st = std::string(st.c_str());
        std::transform(st.begin(), st.end(), st.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        std::string diff = std::string(DifficultyToString(s->GetDifficulty()).c_str());
        std::transform(diff.begin(), diff.end(), diff.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

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

namespace {
constexpr int kRowsPerBeat = 48;
constexpr float kMissingHoldLengthBeats = static_cast<float>(1u << 30) / static_cast<float>(kRowsPerBeat);

struct ParsedRow {
    std::vector<unsigned char> chars;
    int row = 0;
    float beat = 0.0f;
    float second = 0.0f;
};

enum class DumpTapNoteType {
    Empty,
    Tap,
    HoldHead,
    HoldTail,
    Mine,
    Fake
};

enum class DumpTapNoteSubType {
    Invalid,
    Hold,
    Roll
};

static const char* to_string(DumpTapNoteType value) {
    switch (value) {
        case DumpTapNoteType::Empty: return "Empty";
        case DumpTapNoteType::Tap: return "Tap";
        case DumpTapNoteType::HoldHead: return "HoldHead";
        case DumpTapNoteType::HoldTail: return "HoldTail";
        case DumpTapNoteType::Mine: return "Mine";
        case DumpTapNoteType::Fake: return "Fake";
    }
    return "Empty";
}

static const char* to_string(DumpTapNoteSubType value) {
    switch (value) {
        case DumpTapNoteSubType::Invalid: return "Invalid";
        case DumpTapNoteSubType::Hold: return "Hold";
        case DumpTapNoteSubType::Roll: return "Roll";
    }
    return "Invalid";
}

static const char* foot_label(StepParity::Foot foot) {
    switch (foot) {
        case StepParity::NONE: return "N";
        case StepParity::LEFT_HEEL: return "LH";
        case StepParity::LEFT_TOE: return "LT";
        case StepParity::RIGHT_HEEL: return "RH";
        case StepParity::RIGHT_TOE: return "RT";
        default: return "N";
    }
}

static std::string format_foot_vec(const StepParity::FootPlacement& feet) {
    std::ostringstream out;
    out << "[";
    for (size_t i = 0; i < feet.size(); ++i) {
        if (i) {
            out << ",";
        }
        out << foot_label(feet[i]);
    }
    out << "]";
    return out.str();
}

static int foot_position(const std::vector<int>& positions, StepParity::Foot foot) {
    const size_t idx = static_cast<size_t>(foot);
    if (idx >= positions.size()) {
        return StepParity::INVALID_COLUMN;
    }
    return positions[idx];
}

static int foot_position(const int* positions, size_t size, StepParity::Foot foot) {
    const size_t idx = static_cast<size_t>(foot);
    if (idx >= size) {
        return StepParity::INVALID_COLUMN;
    }
    return positions[idx];
}

static std::string format_foot_positions(const std::vector<int>& positions) {
    std::ostringstream out;
    out << "lh=" << foot_position(positions, StepParity::LEFT_HEEL)
        << " lt=" << foot_position(positions, StepParity::LEFT_TOE)
        << " rh=" << foot_position(positions, StepParity::RIGHT_HEEL)
        << " rt=" << foot_position(positions, StepParity::RIGHT_TOE);
    return out.str();
}

static std::string format_foot_positions(const int* positions, size_t size) {
    std::ostringstream out;
    out << "lh=" << foot_position(positions, size, StepParity::LEFT_HEEL)
        << " lt=" << foot_position(positions, size, StepParity::LEFT_TOE)
        << " rh=" << foot_position(positions, size, StepParity::RIGHT_HEEL)
        << " rt=" << foot_position(positions, size, StepParity::RIGHT_TOE);
    return out.str();
}

static std::string format_foot_flags(const bool* flags, size_t size) {
    auto flag_value = [&](StepParity::Foot foot) -> int {
        const size_t idx = static_cast<size_t>(foot);
        if (idx >= size) {
            return 0;
        }
        return flags[idx] ? 1 : 0;
    };
    std::ostringstream out;
    out << "lh=" << flag_value(StepParity::LEFT_HEEL)
        << " lt=" << flag_value(StepParity::LEFT_TOE)
        << " rh=" << flag_value(StepParity::RIGHT_HEEL)
        << " rt=" << flag_value(StepParity::RIGHT_TOE);
    return out.str();
}

struct IdentityHasher {
    uint64_t value = 0;

    void write(const void* data, size_t len) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < len; ++i) {
            value = value * 0x100000001b3ULL + bytes[i];
        }
    }

    uint64_t finish() const {
        return value;
    }
};

static void write_u32_le(IdentityHasher& hasher, uint32_t value) {
    unsigned char bytes[4];
    bytes[0] = static_cast<unsigned char>(value & 0xFF);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
    hasher.write(bytes, sizeof(bytes));
}

static void write_i32_le(IdentityHasher& hasher, int value) {
    write_u32_le(hasher, static_cast<uint32_t>(value));
}

static void write_f32_le(IdentityHasher& hasher, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    write_u32_le(hasher, bits);
}

static uint64_t hash_bytes(std::string_view bytes) {
    IdentityHasher hasher;
    if (!bytes.empty()) {
        hasher.write(bytes.data(), bytes.size());
    }
    return hasher.finish();
}

static uint64_t hash_rows(const std::vector<ParsedRow>& rows) {
    IdentityHasher hasher;
    for (const auto& row : rows) {
        if (!row.chars.empty()) {
            hasher.write(row.chars.data(), row.chars.size());
        }
        write_i32_le(hasher, row.row);
        write_f32_le(hasher, row.beat);
        write_f32_le(hasher, row.second);
    }
    return hasher.finish();
}

static bool is_ascii_whitespace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static std::string_view trim_ascii_whitespace(std::string_view line) {
    size_t start = 0;
    size_t end = line.size();
    while (start < end && is_ascii_whitespace(static_cast<unsigned char>(line[start]))) {
        ++start;
    }
    while (end > start && is_ascii_whitespace(static_cast<unsigned char>(line[end - 1]))) {
        --end;
    }
    return line.substr(start, end - start);
}

static int lrint_ties_even_f32(float value) {
    float floor_val = std::floor(value);
    float frac = value - floor_val;
    if (frac < 0.5f) {
        return static_cast<int>(floor_val);
    }
    if (frac > 0.5f) {
        return static_cast<int>(floor_val) + 1;
    }
    int floor_i = static_cast<int>(floor_val);
    return (floor_i & 1) == 0 ? floor_i : floor_i + 1;
}

static int beat_to_note_row_f32_exact(float beat) {
    return lrint_ties_even_f32(beat * static_cast<float>(kRowsPerBeat));
}

static void print_hex16(std::ostream& out, uint64_t value) {
    std::ios_base::fmtflags flags = out.flags();
    char fill = out.fill();
    out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
    out.flags(flags);
    out.fill(fill);
}

static std::vector<ParsedRow> parse_chart_rows_with_timing(
    std::string_view note_data,
    TimingData* timing,
    int column_count,
    bool dump_rows,
    std::ostream& out) {
    std::vector<ParsedRow> rows;
    size_t measure_index = 0;
    if (column_count <= 0) {
        return rows;
    }

    if (dump_rows) {
        const uint64_t hash = hash_bytes(note_data);
        out << "STEP_PARITY_ROWS start hash=";
        print_hex16(out, hash);
        out << " columns=" << column_count << "\n";
    }

    size_t start = 0;
    const size_t len = note_data.size();
    while (start <= len) {
        size_t comma = note_data.find(',', start);
        if (comma == std::string_view::npos) {
            comma = len;
        }
        std::string_view measure = note_data.substr(start, comma - start);
        if (!measure.empty()) {
            std::vector<std::string_view> lines;
            size_t line_start = 0;
            const size_t measure_len = measure.size();
            while (line_start <= measure_len) {
                size_t nl = measure.find('\n', line_start);
                if (nl == std::string_view::npos) {
                    nl = measure_len;
                }
                std::string_view line = measure.substr(line_start, nl - line_start);
                line = trim_ascii_whitespace(line);
                if (!line.empty()) {
                    lines.push_back(line);
                }
                if (nl == measure_len) {
                    break;
                }
                line_start = nl + 1;
            }

            const size_t num_rows = lines.size();
            if (num_rows == 0) {
                measure_index += 1;
            } else {
                for (size_t i = 0; i < num_rows; ++i) {
                    const float percent = static_cast<float>(i) / static_cast<float>(num_rows);
                    const float beat = (static_cast<float>(measure_index) + percent) * 4.0f;
                    const int note_row = beat_to_note_row_f32_exact(beat);
                    const float quantized_beat = static_cast<float>(note_row) / static_cast<float>(kRowsPerBeat);
                    const float second = timing ? timing->GetElapsedTimeFromBeat(quantized_beat) : 0.0f;

                    std::vector<unsigned char> chars(static_cast<size_t>(column_count), static_cast<unsigned char>('0'));
                    const std::string_view line = lines[i];
                    const size_t copy_len = std::min(static_cast<size_t>(column_count), line.size());
                    for (size_t col = 0; col < copy_len; ++col) {
                        chars[col] = static_cast<unsigned char>(line[col]);
                    }

                    const size_t row_index = rows.size();
                    rows.push_back(ParsedRow{std::move(chars), note_row, quantized_beat, second});
                    if (dump_rows) {
                        const std::string row_text(rows[row_index].chars.begin(), rows[row_index].chars.end());
                        out << std::fixed << std::setprecision(6);
                        out << "STEP_PARITY_ROW idx=" << row_index
                            << " measure=" << measure_index
                            << " line=" << i << "/" << num_rows
                            << " row=" << note_row
                            << " beat=" << quantized_beat
                            << " second=" << second
                            << " data=" << row_text
                            << "\n";
                    }
                }
                measure_index += 1;
            }
        }

        if (comma == len) {
            break;
        }
        start = comma + 1;
    }

    if (dump_rows) {
        const uint64_t rows_hash = hash_rows(rows);
        out << "STEP_PARITY_ROWS end total=" << rows.size() << " rows_hash=";
        print_hex16(out, rows_hash);
        out << "\n";
    }

    return rows;
}

static size_t build_intermediate_notes_with_timing(
    const std::vector<ParsedRow>& rows,
    TimingData* timing,
    int column_count,
    bool dump_notes,
    std::ostream& out) {
    if (column_count <= 0 || rows.empty()) {
        return 0;
    }

    std::vector<std::optional<std::pair<size_t, float>>> hold_starts(static_cast<size_t>(column_count));
    std::unordered_map<uint64_t, float> hold_lengths;
    hold_lengths.reserve(rows.size());

    for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
        const auto& row = rows[row_idx];
        for (int col = 0; col < column_count; ++col) {
            const unsigned char ch = row.chars[static_cast<size_t>(col)];
            if (ch == '2' || ch == '4') {
                hold_starts[static_cast<size_t>(col)] = std::make_pair(row_idx, row.beat);
            } else if (ch == '3') {
                const auto& start = hold_starts[static_cast<size_t>(col)];
                if (start.has_value()) {
                    const size_t start_idx = start->first;
                    const float length = row.beat - start->second;
                    const uint64_t key = (static_cast<uint64_t>(start_idx) << 32) | static_cast<uint32_t>(col);
                    hold_lengths[key] = length;
                    hold_starts[static_cast<size_t>(col)] = std::nullopt;
                }
            }
        }
    }

    if (dump_notes) {
        const uint64_t rows_hash = hash_rows(rows);
        out << "STEP_PARITY_NOTES start rows=" << rows.size()
            << " columns=" << column_count
            << " rows_hash=";
        print_hex16(out, rows_hash);
        out << "\n";
    }

    size_t note_count = 0;
    for (size_t row_idx = 0; row_idx < rows.size(); ++row_idx) {
        const auto& row = rows[row_idx];
        const bool row_fake = timing ? timing->IsFakeAtBeat(row.beat) : false;
        for (int col = 0; col < column_count; ++col) {
            const unsigned char ch = row.chars[static_cast<size_t>(col)];
            DumpTapNoteType note_type = DumpTapNoteType::Empty;
            switch (ch) {
                case '0': note_type = DumpTapNoteType::Empty; break;
                case '1': note_type = DumpTapNoteType::Tap; break;
                case '2': note_type = DumpTapNoteType::HoldHead; break;
                case '4': note_type = DumpTapNoteType::HoldHead; break;
                case '3': note_type = DumpTapNoteType::HoldTail; break;
                case 'M': note_type = DumpTapNoteType::Mine; break;
                case 'K': note_type = DumpTapNoteType::Tap; break;
                case 'L': note_type = DumpTapNoteType::Tap; break;
                case 'F': note_type = DumpTapNoteType::Fake; break;
                default: note_type = DumpTapNoteType::Empty; break;
            }

            if (note_type == DumpTapNoteType::Empty || note_type == DumpTapNoteType::HoldTail) {
                continue;
            }

            DumpTapNoteSubType subtype = DumpTapNoteSubType::Invalid;
            if (ch == '4') {
                subtype = DumpTapNoteSubType::Roll;
            } else if (ch == '2') {
                subtype = DumpTapNoteSubType::Hold;
            }

            float hold_length = -1.0f;
            if (note_type == DumpTapNoteType::HoldHead) {
                const uint64_t key = (static_cast<uint64_t>(row_idx) << 32) | static_cast<uint32_t>(col);
                auto it = hold_lengths.find(key);
                hold_length = (it != hold_lengths.end()) ? it->second : kMissingHoldLengthBeats;
            }

            if (dump_notes) {
                out << std::fixed << std::setprecision(6);
                out << std::boolalpha;
                out << "STEP_PARITY_NOTE row_idx=" << row_idx
                    << " row=" << row.row
                    << " beat=" << row.beat
                    << " second=" << row.second
                    << " col=" << col
                    << " ch=" << static_cast<char>(ch)
                    << " type=" << to_string(note_type)
                    << " subtype=" << to_string(subtype)
                    << " fake=" << (note_type == DumpTapNoteType::Fake || row_fake)
                    << " hold_len=" << hold_length
                    << "\n";
                out << std::noboolalpha;
            }

            note_count += 1;
        }
    }

    if (dump_notes) {
        out << "STEP_PARITY_NOTES end total=" << note_count << "\n";
    }

    return note_count;
}

static bool emit_step_parity_path_dump(Steps* steps, std::ostream& out) {
    if (!steps) {
        out << "STEP_PARITY_PATH error=missing_steps\n";
        return false;
    }
    if (StepParity::Layouts.find(steps->m_StepsType) == StepParity::Layouts.end()) {
        out << "STEP_PARITY_PATH error=unsupported_steps_type\n";
        return false;
    }

    TimingData* timing = steps->GetTimingData();
    GAMESTATE->SetProcessedTimingData(timing);

    NoteData note_data;
    steps->GetNoteData(note_data);

    StepParity::StageLayout layout = StepParity::Layouts.at(steps->m_StepsType);
    StepParity::StepParityGenerator gen(layout);
    if (!gen.analyzeNoteData(note_data)) {
        GAMESTATE->SetProcessedTimingData(nullptr);
        out << "STEP_PARITY_PATH error=analyze_failed\n";
        return false;
    }

    const size_t node_count = gen.nodes.size();
    const int end_id = node_count ? gen.nodes.back()->id : -1;
    out << "STEP_PARITY_PATH start rows=" << gen.rows.size()
        << " nodes=" << node_count
        << " start=0 end=" << end_id
        << "\n";

    float total_cost = 0.0f;
    std::ios_base::fmtflags flags = out.flags();
    std::streamsize precision = out.precision();
    out << std::fixed << std::setprecision(6);

    for (size_t i = 0; i < gen.nodes_for_rows.size(); ++i) {
        const int node_id = gen.nodes_for_rows[i];
        const int prev_id = (i == 0) ? 0 : gen.nodes_for_rows[i - 1];
        StepParity::StepParityNode* prev_node = gen.nodes[prev_id];
        StepParity::StepParityNode* curr_node = gen.nodes[node_id];

        float edge_cost = -1.0f;
        auto it = prev_node->neighbors.find(curr_node);
        if (it != prev_node->neighbors.end()) {
            edge_cost = it->second;
        }
        total_cost += edge_cost;

        const StepParity::Row& row = gen.rows[i];
        const StepParity::State* state = curr_node->state;
        out << "STEP_PARITY_PATH row_idx=" << i
            << " node=" << node_id
            << " prev=" << prev_id
            << " edge_cost=" << edge_cost
            << " total_cost=" << total_cost
            << " beat=" << row.beat
            << " second=" << row.second
            << " note_count=" << row.noteCount
            << " columns=" << format_foot_vec(state->columns)
            << " combined=" << format_foot_vec(state->combinedColumns)
            << " moved=" << format_foot_vec(state->movedFeet)
            << " hold=" << format_foot_vec(state->holdFeet)
            << " row_feet=" << format_foot_positions(row.whereTheFeetAre)
            << " state_feet=" << format_foot_positions(state->whereTheFeetAre, StepParity::NUM_Foot)
            << " moved_flags=" << format_foot_flags(state->didTheFootMove, StepParity::NUM_Foot)
            << " hold_flags=" << format_foot_flags(state->isTheFootHolding, StepParity::NUM_Foot)
            << "\n";
    }

    const int last_id = gen.nodes_for_rows.empty() ? 0 : gen.nodes_for_rows.back();
    float end_cost = -1.0f;
    if (node_count && !gen.nodes_for_rows.empty()) {
        StepParity::StepParityNode* last_node = gen.nodes[last_id];
        StepParity::StepParityNode* end_node = gen.nodes.back();
        auto it = last_node->neighbors.find(end_node);
        if (it != last_node->neighbors.end()) {
            end_cost = it->second;
        }
    }
    total_cost += end_cost;
    out << "STEP_PARITY_PATH end last_node=" << last_id
        << " end_node=" << end_id
        << " edge_cost=" << end_cost
        << " total_cost=" << total_cost
        << "\n";

    out.flags(flags);
    out.precision(precision);

    GAMESTATE->SetProcessedTimingData(nullptr);
    return true;
}
} // namespace

bool emit_step_parity_dump(
    std::ostream& out,
    const std::string& simfile_path,
    const std::string& steps_type_req,
    const std::string& difficulty_req,
    const std::string& description_req,
    bool dump_rows,
    bool dump_notes,
    bool dump_path) {
    if (!dump_rows && !dump_notes && !dump_path) {
        return true;
    }

    init_itgmania_runtime(0, nullptr);

    Song song;
    song.m_sSongFileName = simfile_path;
    song.SetSongDir(Dirname(simfile_path));

    const std::string ext = GetExtension(simfile_path).MakeLower();
    bool ok = false;
    if (ext == "ssc" || ext == "ats") {
        SSCLoader loader;
        ok = loader.LoadFromSimfile(simfile_path, song);
    } else if (ext == "sm" || ext == "sma") {
        SMLoader loader;
        ok = loader.LoadFromSimfile(simfile_path, song);
    } else if (ext == "dwi") {
        std::set<RString> blacklisted_images;
        ok = DWILoader::LoadFromDir(Dirname(simfile_path), song, blacklisted_images);
    } else {
        SSCLoader loader;
        ok = loader.LoadFromSimfile(simfile_path, song);
    }

    if (!ok) {
        out << "STEP_PARITY_DUMP error=failed_to_load_simfile\n";
        return false;
    }

    Steps* steps = select_steps(song.GetAllSteps(), steps_type_req, difficulty_req, description_req);
    if (!steps) {
        out << "STEP_PARITY_DUMP error=steps_not_found\n";
        return false;
    }

    if (!steps_supports_itgmania_notedata(steps)) {
        out << "STEP_PARITY_DUMP error=unsupported_steps_type\n";
        return false;
    }

    TimingData* timing = steps->GetTimingData();
    if (timing) {
        timing->TidyUpData(false);
    }

    RString note_data;
    steps->GetSMNoteData(note_data);

    int column_count = 0;
    if (GAMEMAN) {
        column_count = GAMEMAN->GetStepsTypeInfo(steps->m_StepsType).iNumTracks;
    }

    if (dump_rows || dump_notes) {
        const std::vector<ParsedRow> rows =
            parse_chart_rows_with_timing(note_data, timing, column_count, dump_rows, out);
        build_intermediate_notes_with_timing(rows, timing, column_count, dump_notes, out);
    }

    if (dump_path) {
        if (!emit_step_parity_path_dump(steps, out)) {
            return false;
        }
    }
    return true;
}
#else
bool emit_step_parity_dump(
    std::ostream& out,
    const std::string& simfile_path,
    const std::string& steps_type,
    const std::string& difficulty,
    const std::string& description,
    bool dump_rows,
    bool dump_notes,
    bool dump_path) {
    (void)out;
    (void)simfile_path;
    (void)steps_type;
    (void)difficulty;
    (void)description;
    (void)dump_rows;
    (void)dump_notes;
    (void)dump_path;
    return false;
}
#endif
