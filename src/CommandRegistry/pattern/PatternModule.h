#pragma once

// pattern: the musical content (FL's patterns; closest to Ableton's MIDI clips, but one pattern can hold notes for
// several channels). The Playlist plays patterns through pattern clips.
//
//   pattern.list                              every pattern: number, name, notes, channels used, length
//   pattern.new [--name "Bassline"] [--bars 4]
//                                             a new empty pattern, made current (what the Channel Rack and Piano Roll
//                                             show); without --bars its length follows its notes (whole bars)
//   pattern.notes [--pattern "Bassline"|2]    the notes in a pattern (default: the current one)
//   pattern.note.add --channel Bass --key C4 [--bar 1 --beat 1 --length 1 --velocity 0.8 --slide]
//                                             one note (keys: C5 = middle C, LYNX/FL naming; or a MIDI number)
//   pattern.steps --channel Kick --steps "x...x...x...x..." [--bar 1]
//                                             a channel's 16th steps from a grid (x hit, . rest), as the Channel Rack
//
// Every command that takes --pattern works on the current pattern when it's left out.

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class PatternModule final : public CommandModule
{
public:
    PatternModule() : CommandModule ("pattern", "Patterns: the musical content the Playlist's clips play") {}
    void define() override;
};
} // namespace lynx::commands
