# Calling lapse from another tool

The Docker watcher finds new files on its own, either right away through file
events or later during a full rescan. That works well for a library that
mostly sits still. It is not the fastest option when another tool already
manages your media and knows the exact moment a file is ready, because now
lapse has to notice it too instead of just being told.

Most tools that manage a media library can run a command of your choosing
when something happens, a subtitle finishing download, a video finishing
import, and so on. Since lapse is a normal command line program, it can be
called directly from that hook instead of waiting for the watcher.

This page lists what the CLI can do. It does not tell you which parts to
use, since that depends on what your setup already handles and what you
want lapse to be responsible for.

## Why the CLI fits this at all

Every lapse release ships a single binary with FFmpeg, libfvad and FFTW3
already built in. Nothing else needs to be installed next to it, which
matters if the tool calling lapse runs in its own container. A copy of the
binary on a shared volume is enough.

## What lapse needs

Two file paths and, optionally, a mode:

```bash
lapse <reference> <subtitle> [auto|ols|nosplit|split] [penalty]
```

The reference can be a video file or another subtitle file. When it is a
subtitle, lapse compares timing directly instead of listening to audio,
which is faster and does not need FFmpeg to decode anything. A common shape
in a pipeline is the first subtitle for a release syncing against the
video, and every other language for that same release syncing against the
first one instead of the video again.

```bash
lapse video.mkv subtitles.srt
lapse already-synced.srt new-language.srt
```

## Modes

| Mode | What it does |
|---|---|
| `auto` (default) | Works out on its own whether the file needs a shift, a stretch, splitting, or some mix, and picks accordingly |
| `ols` | Fits one gradual stretch across the whole file, for framerate drift |
| `nosplit` | Applies a single offset to the whole file, no cuts |
| `split` | Allows the offset to change partway through, for director's cuts, ad breaks, or a rejoined two part release |

`split` takes an optional penalty as a fourth argument, default `6`. Higher
values produce fewer splits.

## Selecting a track

```
--audio-track N     use the Nth audio track instead of the default one
--sub-track N        use the Nth embedded subtitle track as the reference
--no-embedded         ignore subtitle tracks inside the video and use the audio
```

If the video has more than one audio or subtitle track and the tool calling
lapse knows which one is relevant, these pick it directly instead of lapse
using its default choice.

## Output handling

```
--output <path>     write the corrected subtitle to <path> instead of overwriting the input
--no-backup          do not create the .bak file
--no-sidecar          write nothing at all rather than an unsure file beside the original
```

`--undo <subtitle>` puts the `.bak` back and removes it.

| Result | Flags |
|---|---|
| Overwrite, keep a backup | *neither flag* (default) |
| Overwrite, no backup | `--no-backup` |
| Write a separate file, leave the original alone | `--output out.srt --no-backup` |
| Write a separate file, back up the original too | `--output out.srt` |

## Verdicts and exit codes

Every run reaches a verdict, and the verdict decides both what happens to
the file and the exit code:

| Verdict | What it means | What happens | Exit |
|---|---|---|---|
| `solid` | the answer clears the confidence bar | the subtitle is overwritten, `.bak` kept | `0` |
| `unsure` | there is an answer, it is just not proven | original untouched, answer written to `name.lapse-unsure.srt` | `3` |
| `nothing` | the audio does not support any offset | original untouched, best guess written to `name.lapse-unsure.srt` | `3` |

`--strict` or `--no-sidecar` refuse to write anything for `unsure` or
`nothing` instead of the sidecar file, and exit `2`. `--confidence N` moves
where the bar for `solid` sits, default `8`. `--force` treats every result
as `solid` and always overwrites.

A caller can read either the exit code or the `verdict` field in the JSON
output to decide what to do with the result.

## Automation flags

```
--json      one machine readable line on stdout, everything else on stderr
--quiet      say nothing but errors, implied by --json
--dry-run     work out the answer without writing anything
--no-cache    do not read or write the saved speech profile
```

The JSON line includes the offset, confidence, verdict, coverage, how many
cues were found and ignored, and how many parts the file was split into:

```json
{"mode":"auto/shifted","reference":"vad","offset_ms":22,"ratio":1,"confidence":0.455,
 "margin":0.12,"sigma":12.3,"agreement":0.75,"verdict":"solid","coverage":1,
 "cues":1578,"ignored_cues":1,"parts":1,"written":true,"output":"...","splits":[]}
```

`mode` reports what lapse actually did. `ols`, `nosplit` and `split` show up
when that mode was requested directly. Everything under `auto` is what
lapse decided on its own:

| `mode` | What it found |
|---|---|
| `auto/shifted` | one offset fits the whole file |
| `auto/shifted+split` | one offset nearly fits, with cuts in it |
| `auto/drifting` | the file drifts, usually a framerate mismatch |
| `auto/drifting+split` | it drifts and was cut about as well |
| `auto/recut` | the offset changes throughout |
| `auto/joined` | two parts in one video |
| `auto/restart` | the subtitle starts over partway through |

## Flags that answer a question and exit

```
--version    prints the version number
--formats     lists the subtitle extensions the engine reads and writes
--vad          prints silero or libfvad, depending on which detector this copy can reach
```

None of these sync anything.

## Running it in a different container

If the tool calling lapse is not the same container lapse itself runs in, a
copy of the lapse binary needs to be reachable by both, for example through
a shared volume. Nothing else needs to be shared, since the binary carries
its own dependencies.

## Using this alongside the Docker watcher

A hook like this only catches files as they pass through whichever tool
triggers it. Anything added outside that path, copied in by hand, restored
from a backup, and so on, will not be caught by it. The Docker watcher can
keep running for that case, independently of whatever calls the CLI
directly.
