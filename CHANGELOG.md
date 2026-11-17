# Changelog

All notable changes to Retune. Versions follow the git tags; the format is
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Changed

- Help opens the help page in a window of the plugin's own, not in the web browser. Nothing is
  written to the temp folder, and on Linux no web server runs in the host any more.

## [0.9.0] - 2026-11-14

First public preview.

### Added

- MIDI note effect that transposes every note by whole octaves plus steps of 1/N octave, for N from
  5 to 96.
- The fraction kept as a MIDI 2.0 Pitch 7.9 attribute, or in the CLAP note dialect for hosts such as
  Bitwig; MIDI 1.0 rounded to the nearest semitone.
- No stuck notes: each note remembers where it went when the shift changes.
- CLAP plugin for Linux (x86-64) and macOS 26 or later (Apple Silicon).

### Known gaps

- See the README's Known gaps section.
