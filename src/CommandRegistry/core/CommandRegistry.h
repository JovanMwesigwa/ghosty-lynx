#pragma once

// CommandRegistry: named commands that drive the running app without clicking through the UI.
//
// Commands live in groups, one group per feature and one file per group (TempoCommands.cpp, ...):
//
//     tempo            "The project tempo"
//       .get           → {"bpm": 124}
//       .set --bpm N   → changes it (one undo step)
//
// A command is declared, not hand-parsed: its arguments (name, type, range) are listed once, and the registry checks
// them before the handler runs (missing, wrong type, out of range, unknown flag) and turns them into help text. So a
// handler only does its job:
//
//     auto& tempo = registry.group ("tempo", "The project tempo");
//     tempo.add ({ .name = "set",
//                  .description = "Set the project tempo",
//                  .args = { Arg::number ("bpm", "the new tempo", 10, 522) },
//                  .changesProject = true,
//                  .run = [&] (const Args& a) { ... a.number ("bpm") ... } });
//
// The registry knows nothing about how a command arrives (HTTP, a terminal, MIDI, a script): transports such as
// CommandServer call run() with the full name ("tempo.set") and a JSON object of arguments.
//
// Threading: run() calls the handler on the calling thread. Handlers touch the Song, which only the UI (message)
// thread may do (docs/ARCHITECTURE.md), so transports must call run() on the message thread.

#include <juce_core/juce_core.h>

#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace lynx::commands
{
// What a command returns: a value that can be written as JSON, or an error message.
struct Result
{
    bool ok = true;
    juce::var value;
    juce::String error;

    static Result success (juce::var value = {}) { return { true, std::move (value), {} }; }
    static Result failure (juce::String message) { return { false, {}, std::move (message) }; }
};

// {"bpm": 124, "previous": 130} in one line: makeObject ({ { "bpm", 124.0 }, { "previous", 130.0 } })
juce::var makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties);

// One argument a command takes (a --flag on the command line, a key in the JSON body).
struct Arg
{
    enum class Type { number, text, boolean };

    juce::String name;
    juce::String description;
    Type type = Type::number;
    bool required = true;
    double min = std::numeric_limits<double>::lowest();
    double max = std::numeric_limits<double>::max();

    static Arg number (juce::String name, juce::String description, double min, double max)
    {
        return { std::move (name), std::move (description), Type::number, true, min, max };
    }
    static Arg text (juce::String name, juce::String description) { return { std::move (name), std::move (description), Type::text }; }
    static Arg boolean (juce::String name, juce::String description) { return { std::move (name), std::move (description), Type::boolean }; }

    Arg optional() const
    {
        auto copy = *this;
        copy.required = false;
        return copy;
    }
};

// A command's arguments after checking: every required one is there with the declared type and range.
class Args
{
public:
    explicit Args (juce::var values) : values (std::move (values)) {}

    bool has (const juce::Identifier& name) const { return values.hasProperty (name); }
    double number (const juce::Identifier& name, double fallback = 0.0) const { return has (name) ? (double) values[name] : fallback; }
    juce::String text (const juce::Identifier& name, const juce::String& fallback = {}) const { return has (name) ? values[name].toString() : fallback; }
    bool boolean (const juce::Identifier& name, bool fallback = false) const { return has (name) ? (bool) values[name] : fallback; }

private:
    juce::var values;
};

struct Command
{
    juce::String name;          // within its group: "get" (the full name is "tempo.get"); dots for a sub-resource: "track.set"
    juce::String description;   // one line, shown in the help
    juce::String example;       // a line to try, as typed in the Console ("tempo set --bpm 128"); shown in the help
    std::vector<Arg> args;
    bool changesProject = false; // edits the project: transports make sure the caller meant it (CommandServer: POST + JSON)
    bool changesSettings = false; // changes LYNX itself (the audio device, ...), not the project: no undo step, POST + JSON too
    std::function<Result (const Args&)> run;
};

// A feature's commands. Get one with CommandRegistry::group() and add() its commands.
struct CommandGroup
{
    juce::String name;
    juce::String description;
    std::map<std::string, Command> commands; // by name, sorted

    CommandGroup& add (Command command);
};

class CommandRegistry
{
public:
    CommandRegistry(); // registers the built-in "commands.list"

    // The group with this name, made on first use (the description is kept from the first call).
    CommandGroup& group (const juce::String& name, const juce::String& description = {});

    // "tempo.get" → its command, or nullptr.
    const Command* find (const juce::String& fullName) const;

    // A command ("tempo.get") or a group ("tempo") by that name.
    bool exists (const juce::String& name) const;

    // Runs "group.command" after checking its arguments. A group's name alone returns that group's help, and so does
    // "<group>.help"; "<group>.<command>.help" returns that group's help with only that command.
    // Unknown names, bad arguments and exceptions thrown by a handler come back as failures.
    Result run (const juce::String& name, const juce::var& args) const;

    // Help as data: every group (or one) with its commands and their arguments.
    juce::var describe (const juce::String& onlyGroup = {}) const;

private:
    std::map<std::string, CommandGroup> groups;

    const CommandGroup* findGroup (const juce::String& name) const;
};
} // namespace lynx::commands
