#pragma once

// Lookups the modules share: things a command names by number or by name.
//
//   --pattern 2           the 2nd pattern (numbers count from 1, as the lists show them)
//   --pattern "Bassline"  the pattern with that name (any letter case)

#include <juce_core/juce_core.h>

#include <cstdint>
#include <optional>

namespace lynx
{
struct Song;
}

namespace lynx::commands
{
// The pattern `text` names (index from 0), or -1 with `error` saying why (no such pattern, ambiguous name).
int findPattern (const Song& song, const juce::String& text, juce::String& error);

// The name a pattern shows: its own, or "Pattern N".
juce::String patternName (const Song& song, int pattern);

// The name a channel shows: its own, or "Channel N".
juce::String channelName (const Song& song, int channel);

// The channel `text` names: its number in the Channel Rack (from 1) or its name; -1 with `error` otherwise.
int findChannel (const Song& song, const juce::String& text, juce::String& error);

// A colour argument: "#d9534f", "d9534f" or "#abc" → 0xFFRRGGBB; "none" / "default" → 0 (the default colour);
// nullopt when it isn't one.
std::optional<std::uint32_t> parseColour (juce::String text);
// A stored colour as text: 0xFFRRGGBB → "#rrggbb"; 0 (the default) → null.
juce::var colourText (std::uint32_t argb);

// A file or folder argument: an absolute path, or one starting with ~/ (the home folder). Empty File otherwise
// (relative paths are refused: relative to what? the app's working folder).
juce::File pathFromArg (const juce::String& text);

// A key from a name in LYNX's (FL's) octave numbering, where middle C (MIDI 60) is C5 (Ableton calls it C3):
// "C5" → 60, "F#3" → 42, "Bb4" → 58, or a MIDI number "60". -1 with `error` when it isn't one (or is outside 0..127).
int parseKey (const juce::String& text, juce::String& error);
} // namespace lynx::commands
