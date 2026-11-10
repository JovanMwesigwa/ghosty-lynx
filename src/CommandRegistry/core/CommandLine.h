#pragma once

// CommandLine: one typed line → a command name and its arguments, with the same syntax as the `lynx` CLI:
//
//   tempo get                              → "tempo.get"   {}
//   tempo.set --bpm 128                    → "tempo.set"   {"bpm": "128"}
//   project new --name "Night Ride" --bpm=140 --discard
//                                          → "project.new" {"name": "Night Ride", "bpm": "140", "discard": true}
//   tempo --help / tempo set --help        → help for the group / the command
//   tempo help / tempo set help / help tempo  → the same (the word "help" at either end)
//
// Values stay text: the registry converts them to the declared type (numbers, booleans) when it checks them.

#include <juce_core/juce_core.h>

namespace lynx::commands
{
struct CommandLine
{
    juce::String name;   // "tempo.set", a group alone ("tempo"), or empty
    juce::var args;      // a JSON object
    bool help = false;   // --help / -h was given
    juce::String error;  // the line couldn't be read (an unclosed quote)
};

CommandLine parseCommandLine (const juce::String& line);

// The words of a line, honouring "double" and 'single' quotes. False when a quote is left open.
bool splitWords (const juce::String& line, juce::StringArray& words);
} // namespace lynx::commands
