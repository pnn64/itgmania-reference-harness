# itgmania-reference-harness

Small CLI that loads a simfile (e.g. `.sm` / `.ssc`) using ITGMania parsing code and prints per-chart metrics as JSON.

It also runs Simply Love’s chart parser Lua (`Themes/Simply Love/Scripts/SL-ChartParser*.lua`) **unchanged** to reproduce the same chart hash / stream analysis that Simply Love uses.

## Features

- Parse simfiles via ITGMania `Song`/`Steps` loaders
- Compute (per chart):
  - Metadata: title/subtitle/artist, step artist, steps type, difficulty, meter
  - BPMs: raw BPM string, actual min/max, display BPM string + min/max (Simply Love behavior)
  - Duration, notes-per-measure, NPS-per-measure, peak NPS
  - Simply Love stream breakdown strings + raw stream/break sequences
  - Tech counts (crossovers, footswitches, sideswitches, jacks, brackets, doublesteps)
  - Radar counts (taps/holds/notes/holds/mines/rolls/lifts/fakes/jumps/hands/quads)
  - Full timing segment tables (BPM/stop/delay/warp/time signature/etc.)

## Repo Layout

- `src/extern/itgmania/`: ITGMania submodule (required for parsing + Simply Love scripts)
- `src/main.cpp`: CLI entrypoint
- `src/itgmania_adapter.cpp`: ITGMania parsing + Simply Love Lua bridge

## Build

### Prerequisites

- CMake ≥ 3.20
- C++17 compiler
- Libraries (needed when not using a prebuilt ITGMania lib):
  - libtomcrypt
  - libtommath
  - libpcre
  - Lua 5.1
  - jsoncpp

On Ubuntu/Debian:

```bash
sudo apt-get install -y cmake g++ \
  libtomcrypt-dev libtommath-dev libpcre3-dev liblua5.1-0-dev libjsoncpp-dev
```

### Get the ITGMania submodule

```bash
git submodule update --init --recursive
```

### Configure + build

Recommended: compile a minimal subset of ITGMania sources needed for parsing:

```bash
cmake -S . -B build -DUSE_ITGMANIA_SOURCES=ON
cmake --build build
```

If you request chart parsing without `USE_ITGMANIA_SOURCES=ON` (or without linking a prebuilt ITGMania), the binary will build but will typically return `status: "stub"` because the full parsing/runtime pieces aren’t present.

## Usage

Parse *all* charts in a simfile (prints a JSON array):

```bash
./build/itgmania-reference-harness path/to/song.sm
```

Parse a specific chart:

```bash
./build/itgmania-reference-harness path/to/song.sm dance-single challenge
```

Parse all Edit charts for a steps type:

```bash
./build/itgmania-reference-harness path/to/song.sm dance-single edit
```

Parse a specific Edit chart (use `description` to disambiguate):

```bash
./build/itgmania-reference-harness path/to/song.sm dance-single edit "My Edit Description"
```

Notes:

- Difficulty names use ITGMania/StepMania’s enums (`beginner`, `easy`, `medium`, `hard`, `challenge`, `edit`).
- Edit charts can have multiple entries; use the `description` field to identify/select a specific one.
- If the requested chart isn’t found, the tool prints a JSON stub (`"status": "stub"`).
- JSON is written to stdout.

## Output Format

The CLI prints either:

- A JSON array of charts (when only `<simfile>` is provided), or
- A single JSON object (when `<simfile> <steps-type> <difficulty>` is provided)

### Stream sequences (Simply Love)

`stream_sequences` comes from Simply Love’s `GetStreamSequences(notes_per_measure, 16)`.

- Each element is `{ "stream_start": int, "stream_end": int, "is_break": bool }`
- Treat these as half-open “measure index” intervals from Simply Love; the length is `stream_end - stream_start`.

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

This repository is MIT licensed (see `LICENSE`).

ITGMania and Simply Love are included as a submodule and are licensed separately (see `src/extern/itgmania/`).
