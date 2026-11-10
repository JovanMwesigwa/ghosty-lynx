#include "CommandRegistry/core/CommandModule.h"

#include "engine/AudioEngine.h"

namespace lynx::commands
{
Context::Context (AudioEngine& e, std::function<MainComponent*()> ui) : engine (e), mainUi (std::move (ui)) {}

const Song& Context::song() const
{
    return engine.getSong();
}

void Context::edit (const std::function<void (Song&)>& change)
{
    engine.editSong (change);
}

MainComponent* Context::ui() const
{
    return mainUi != nullptr ? mainUi() : nullptr;
}

CommandModule::CommandModule (juce::String name, juce::String description)
    : groupName (std::move (name)), groupDescription (std::move (description))
{
}

void CommandModule::install (CommandRegistry& registry, Context& ctx)
{
    group = &registry.group (groupName, groupDescription);
    context = &ctx;
    define();
}

void CommandModule::query (const juce::String& name, const juce::String& description, Handler run)
{
    add (name, description, {}, false, std::move (run));
}

void CommandModule::query (const juce::String& name, const juce::String& description, std::vector<Arg> args, Handler run)
{
    add (name, description, std::move (args), false, std::move (run));
}

void CommandModule::mutation (const juce::String& name, const juce::String& description, std::vector<Arg> args, Handler run)
{
    add (name, description, std::move (args), true, std::move (run));
}

void CommandModule::examples (std::initializer_list<std::pair<const char*, const char*>> commandAndExample)
{
    jassert (group != nullptr); // (in define())
    for (const auto& [name, example] : commandAndExample)
    {
        const auto found = group->commands.find (name);
        jassert (found != group->commands.end()); // an example for a command this module doesn't have
        if (found != group->commands.end())
            found->second.example = juce::String::fromUTF8 (example);
    }
}

void CommandModule::action (const juce::String& name, const juce::String& description, std::vector<Arg> args, Handler run)
{
    add (name, description, std::move (args), false, std::move (run), true);
}

void CommandModule::add (const juce::String& name, const juce::String& description, std::vector<Arg> args, bool changesProject, Handler run,
                         bool changesSettings)
{
    jassert (group != nullptr && context != nullptr); // query() / mutation() belong in define()
    group->add ({ .name = name,
                  .description = description,
                  .args = std::move (args),
                  .changesProject = changesProject,
                  .changesSettings = changesSettings,
                  .run = [ctx = context, run = std::move (run)] (const Args& a) { return run (*ctx, a); } });
}
} // namespace lynx::commands
