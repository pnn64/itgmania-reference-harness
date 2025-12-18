# itgmania-reference-harness

CLI that loads a simfile (e.g. `.sm` / `.ssc`) using ITGMania parsing code and prints per-chart metrics as JSON. It also runs Simply Love's chart parser Lua (`Themes/Simply Love/Scripts/SL-ChartParser*.lua`) unchanged; embedded copies are bundled so the tool works even when those scripts aren't on disk.

## Features

- Parse simfiles via ITGMania `Song`/`Steps` loaders
- Compute per-chart metrics:
  - Metadata: title/subtitle/artist, step artist, steps type, difficulty, meter
  - BPMs: ITGMania timing BPMS string, `hash_bpms` as parsed by Simply Love for hashing, actual min/max, display BPM string + min/max (Simply Love behavior)
  - Duration, notes-per-measure, NPS-per-measure, peak NPS
  - Simply Love stream breakdown strings + raw stream/break sequences
  - Tech counts (crossovers, footswitches, sideswitches, jacks, brackets, doublesteps)
  - Radar counts (taps/holds/notes/holds/mines/rolls/lifts/fakes/jumps/hands/quads)
  - Full timing segment tables (BPM/stop/delay/warp/time signature/etc.)

## Repo layout

- `src/extern/itgmania/`: ITGMania submodule (parsing + Simply Love scripts)
- `src/main.cpp`: CLI entrypoint
- `src/itgmania_adapter.cpp`: ITGMania parsing + Simply Love Lua bridge

## Build

### Prerequisites

- CMake >= 3.20
- C++17 compiler
- Dependencies for ITGMania and the harness (example for Ubuntu/Debian):

```bash
sudo apt-get update && sudo apt-get install -y \
  zstd cmake build-essential \
  libtomcrypt-dev libtommath-dev libpcre3-dev liblua5.1-0-dev libjsoncpp-dev \
  nasm libgtk-3-dev libasound2-dev libpulse-dev pkg-config libglu1-mesa-dev libudev-dev
```

### Clone the harness and ITGMania + submodules

```bash
git clone --recurse-submodules https://github.com/pnn64/itgmania-reference-harness.git
```

### Prepare ITGMania build directory (configure only)

The harness needs ITGMania's generated headers/config from a configure step, but a full ITGMania compile is not required.

```bash
cd src/extern/itgmania/Build
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release .. && cmake ..
```

### Configure and build the harness

From the repo root after configuring ITGMania:

```bash
cmake -S . -B build
cmake --build build
```

If you request chart parsing without `USE_ITGMANIA_SOURCES=ON` (or without linking a prebuilt ITGMania), the binary builds but typically returns `status: "stub"` because the full parsing/runtime pieces aren't present.

## Usage

### Parse all charts (JSON array)

```bash
./build/itgmania-reference-harness path/to/song.sm
```

### Parse a specific chart (single JSON object)

```bash
./build/itgmania-reference-harness path/to/song.sm dance-single challenge
```

### Parse Edit charts for a steps type

```bash
./build/itgmania-reference-harness path/to/song.sm dance-single edit
```

### Parse a specific Edit chart by description

```bash
./build/itgmania-reference-harness path/to/song.sm dance-single edit "My Edit Description"
```

### Hash-only mode (no JSON)

Print one line per chart with steps type, meter, difficulty, and the Simply Love chart hash:

```bash
./build/itgmania-reference-harness --hash path/to/song.ssc
# or
./build/itgmania-reference-harness -h path/to/song.ssc
```

### Flags

- `--hash` / `-h`: hash-only mode
- `--help`: show usage
- `--version` / `-v`: print the harness version

### Notes

- Difficulty names use ITGMania/StepMania enums (`beginner`, `easy`, `medium`, `hard`, `challenge`, `edit`).
- Edit charts can have multiple entries; use the `description` field to identify/select one.
- If the requested chart isn't found, the tool prints a JSON stub (`"status": "stub"`).
- `hash_bpms` is the BPMS string used by Simply Love when computing `hash`.
- JSON is written to stdout.

## Output format

The CLI prints either a JSON array of charts (when only `<simfile>` is provided) or a single JSON object (when `<simfile> <steps-type> <difficulty>` is provided).

### Stream sequences (Simply Love)

`stream_sequences` comes from Simply Love's `GetStreamSequences(notes_per_measure, 16)`.

- Each element is `{ "stream_start": int, "stream_end": int, "is_break": bool }`
- Treat these as half-open "measure index" intervals; length is `stream_end - stream_start`.

### Timing data

`timing` exports ITGMania timing segments in a Lua-table-like numeric format:

- Most segment lists are arrays of arrays: `[beat, ...values]`
- `labels` is `[beat, label]`
- Values correspond to ITGMania `TimingSegment::GetValues()`:

| Key | Entry format |
| --- | --- |
| `bpms` | `[beat, bpm]` |
| `stops` | `[beat, seconds]` |
| `delays` | `[beat, seconds]` |
| `time_signatures` | `[beat, numerator, denominator]` |
| `warps` | `[beat, length_beats]` |
| `tickcounts` | `[beat, ticks_per_beat]` |
| `combos` | `[beat, combo, miss_combo]` |
| `speeds` | `[beat, ratio, delay, unit]` (`unit`: `0=beats`, `1=seconds`) |
| `scrolls` | `[beat, ratio]` |
| `fakes` | `[beat, length_beats]` |

## License

ITGMania and Simply Love are included as a submodule and are licensed separately (see `src/extern/itgmania/`). If embedded Lua is used, it is the same source from that submodule.
