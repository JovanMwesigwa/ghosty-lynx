#pragma once

// CommandModule: the interface every feature of the bridge implements (tempo/, project/, playlist/, ...).
//
// A module is one command group ("tempo") and declares its commands in define(), each as a
//   query    — reads the project, changes nothing       (tempo.get)
//   mutation — changes the project (one undo step)      (tempo.set)
//   action   — changes LYNX itself, not the project      (settings.audio.set: the audio device; no undo step)
// The framework does the rest: argument checking, help text, JSON, running on the UI thread, the HTTP rules for
// mutations (POST + JSON). A handler receives the Context (what it may use) and its checked Args:
//
//   class TempoModule final : public CommandModule
//   {
//   public:
//       TempoModule() : CommandModule ("tempo", "The project tempo") {}
//       void define() override
//       {
//           query ("get", "The project tempo in BPM", [] (Context& ctx, const Args&) {
//               return Result::success (makeObject ({ { "bpm", ctx.song().bpm } }));
//           });
//       }
//   };
//
// A new module is listed once, in CommandBridge.cpp.

#include "CommandRegistry/core/CommandRegistry.h"

#include <functional>
#include <vector>

namespace lynx
{
class AudioEngine;
class MainComponent;
struct Song;
} // namespace lynx

namespace lynx::commands
{
// What a handler may use. Handlers run on the UI thread, so all of it is safe to touch.
class Context
{
public:
    Context (AudioEngine& engine, std::function<MainComponent*()> mainUi);

    AudioEngine& engine;

    // The open project (read only).
    const Song& song() const;

    // Changes the project the way the UI does: one undo step, marked modified, sent to the audio thread, UI repainted.
    void edit (const std::function<void (Song&)>& change);

    // The main window, or nullptr in the moment before it exists at start-up (see windowNotReady).
    MainComponent* ui() const;
    static Result windowNotReady() { return Result::failure ("the main window isn't open yet"); }

private:
    std::function<MainComponent*()> mainUi;
};

using Handler = std::function<Result (Context& ctx, const Args& args)>;

class CommandModule
{
public:
    CommandModule (juce::String groupName, juce::String groupDescription);
    virtual ~CommandModule() = default;

    // Declare the commands here, with query() and mutation(). Called once, at start-up.
    virtual void define() = 0;

    // (the bridge) makes the group, then calls define()
    void install (CommandRegistry& registry, Context& context);

protected:
    void query (const juce::String& name, const juce::String& description, Handler run);
    void query (const juce::String& name, const juce::String& description, std::vector<Arg> args, Handler run);
    void mutation (const juce::String& name, const juce::String& description, std::vector<Arg> args, Handler run);
    void action (const juce::String& name, const juce::String& description, std::vector<Arg> args, Handler run);

    // A line to try for each command, as typed in the Console, shown in its help (call it after the commands):
    //   examples ({ { "get", "tempo get" }, { "set", "tempo set --bpm 128" } });
    void examples (std::initializer_list<std::pair<const char*, const char*>> commandAndExample);

private:
    void add (const juce::String& name, const juce::String& description, std::vector<Arg> args, bool changesProject, Handler run,
              bool changesSettings = false);

    juce::String groupName, groupDescription;
    CommandGroup* group = nullptr;
    Context* context = nullptr;
};
} // namespace lynx::commands
