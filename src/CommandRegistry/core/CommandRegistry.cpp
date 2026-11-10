#include "CommandRegistry/core/CommandRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>

namespace lynx::commands
{
namespace
{
const char* typeName (Arg::Type type)
{
    switch (type)
    {
        case Arg::Type::number:  return "number";
        case Arg::Type::text:    return "text";
        case Arg::Type::boolean: return "boolean";
    }
    return "?";
}

juce::String argList (const Command& command)
{
    juce::StringArray names;
    for (const auto& arg : command.args)
        names.add ("--" + arg.name);
    return names.isEmpty() ? "no arguments" : names.joinIntoString (", ");
}

// One argument, checked against its declaration: the converted value, or the reason it was refused.
std::pair<juce::var, juce::String> checkArg (const juce::String& fullName, const Arg& arg, const juce::var& given)
{
    const auto prefix = fullName + " --" + arg.name + ": ";
    switch (arg.type)
    {
        case Arg::Type::number:
        {
            double value = 0.0;
            if (given.isInt() || given.isInt64() || given.isDouble())
            {
                value = (double) given;
            }
            else // text such as "128" or "128.5" (a query string, a command-line flag): the whole text must be a number
            {
                const auto text = given.toString().trim().toStdString();
                char* end = nullptr;
                value = std::strtod (text.c_str(), &end);
                if (text.empty() || end == nullptr || *end != '\0' || ! std::isfinite (value))
                    return { {}, prefix + "expected a number, got \"" + given.toString() + "\"" };
            }

            if (value < arg.min || value > arg.max)
                return { {}, prefix + juce::String (value) + " is out of range (" + juce::String (arg.min) + " to " + juce::String (arg.max) + ")" };
            return { value, {} };
        }
        case Arg::Type::boolean:
        {
            if (given.isBool())
                return { given, {} };
            const auto text = given.toString().trim().toLowerCase();
            if (text == "true" || text == "1" || text == "yes" || text == "on")
                return { true, {} };
            if (text == "false" || text == "0" || text == "no" || text == "off")
                return { false, {} };
            return { {}, prefix + "expected true or false, got \"" + given.toString() + "\"" };
        }
        case Arg::Type::text:
            return { given.toString(), {} };
    }
    return { {}, prefix + "unknown argument type" };
}

juce::var describeCommand (const CommandGroup& group, const Command& command)
{
    juce::Array<juce::var> args;
    for (const auto& arg : command.args)
    {
        auto entry = makeObject ({ { "name", arg.name }, { "description", arg.description }, { "type", typeName (arg.type) }, { "required", arg.required } });
        if (arg.type == Arg::Type::number)
        {
            entry.getDynamicObject()->setProperty ("min", arg.min);
            entry.getDynamicObject()->setProperty ("max", arg.max);
        }
        args.add (entry);
    }
    auto out = makeObject ({ { "name", group.name + "." + command.name },
                             { "description", command.description },
                             { "changesProject", command.changesProject },
                             { "changesSettings", command.changesSettings },
                             { "args", args } });
    if (command.example.isNotEmpty())
        out.getDynamicObject()->setProperty ("example", command.example);
    return out;
}

// A group's help; with `onlyCommand` ("set"), that command alone.
juce::var describeGroup (const CommandGroup& group, const juce::String& onlyCommand = {})
{
    juce::Array<juce::var> commands;
    for (const auto& [key, command] : group.commands)
        if (onlyCommand.isEmpty() || command.name == onlyCommand)
            commands.add (describeCommand (group, command));
    return makeObject ({ { "name", group.name }, { "description", group.description }, { "commands", commands } });
}
} // namespace

juce::var makeObject (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties)
{
    auto* object = new juce::DynamicObject();
    for (const auto& [name, value] : properties)
        object->setProperty (name, value);
    return juce::var (object);
}

CommandGroup& CommandGroup::add (Command command)
{
    // (a name may have dots for a sub-resource: "track.set" → "playlist.track.set", typed "playlist track set")
    jassert (command.name.isNotEmpty() && ! command.name.startsWithChar ('.') && ! command.name.endsWithChar ('.') && command.run != nullptr);
    jassert (command.name != "help" && ! command.name.endsWith (".help")); // ("help" is how the help is asked for)
    commands[command.name.toStdString()] = std::move (command);
    return *this;
}

CommandRegistry::CommandRegistry()
{
    group ("commands", "About this command bridge")
        .add ({ .name = "list",
                .description = "Every group and command, with their arguments",
                .example = "commands list",
                .run = [this] (const Args&) { return Result::success (describe()); } });
}

CommandGroup& CommandRegistry::group (const juce::String& name, const juce::String& description)
{
    jassert (name.isNotEmpty() && ! name.containsChar ('.'));
    auto& g = groups[name.toStdString()];
    if (g.name.isEmpty())
        g.name = name;
    if (g.description.isEmpty())
        g.description = description;
    return g;
}

const CommandGroup* CommandRegistry::findGroup (const juce::String& name) const
{
    const auto found = groups.find (name.toStdString());
    return found == groups.end() ? nullptr : &found->second;
}

const Command* CommandRegistry::find (const juce::String& fullName) const
{
    const auto* g = findGroup (fullName.upToFirstOccurrenceOf (".", false, false));
    if (g == nullptr || ! fullName.containsChar ('.'))
        return nullptr;
    const auto found = g->commands.find (fullName.fromFirstOccurrenceOf (".", false, false).toStdString());
    return found == g->commands.end() ? nullptr : &found->second;
}

bool CommandRegistry::exists (const juce::String& name) const
{
    return find (name) != nullptr || (! name.containsChar ('.') && findGroup (name) != nullptr);
}

Result CommandRegistry::run (const juce::String& name, const juce::var& givenArgs) const
{
    // a group alone: its help ("tempo" → what tempo can do)
    if (! name.containsChar ('.'))
    {
        if (const auto* g = findGroup (name))
            return Result::success (describeGroup (*g));
        return Result::failure ("unknown group \"" + name + "\" (commands.list shows them all)");
    }

    // "<group>.help" → the group's help; "<group>.<command>.help" → that command's (typed "project help",
    // "tempo set help"). A command can't be named "help" itself, so nothing else answers to it.
    if (name.endsWithIgnoreCase (".help"))
    {
        const auto target = name.dropLastCharacters (5);
        const auto* g = findGroup (target.upToFirstOccurrenceOf (".", false, false));
        if (g == nullptr)
            return Result::failure ("unknown group \"" + target.upToFirstOccurrenceOf (".", false, false) + "\" (help lists them all)");
        if (! target.containsChar ('.'))
            return Result::success (describeGroup (*g));
        if (find (target) == nullptr)
            return Result::failure ("unknown command \"" + target.replaceCharacter ('.', ' ') + "\" (" + g->name + " help lists them)");
        return Result::success (describeGroup (*g, target.fromFirstOccurrenceOf (".", false, false)));
    }

    const auto* command = find (name);
    if (command == nullptr)
    {
        if (const auto* g = findGroup (name.upToFirstOccurrenceOf (".", false, false)))
        {
            juce::StringArray known;
            for (const auto& [key, c] : g->commands)
                known.add (c.name);
            return Result::failure ("unknown command \"" + name + "\" (" + g->name + " has: " + known.joinIntoString (", ") + ")");
        }
        return Result::failure ("unknown command \"" + name + "\" (commands.list shows them all)");
    }

    // check the arguments against the declaration
    const auto* given = givenArgs.getDynamicObject();
    auto* checked = new juce::DynamicObject();
    juce::var checkedVar (checked);

    if (given != nullptr)
        for (const auto& property : given->getProperties())
        {
            const auto known = std::any_of (command->args.begin(), command->args.end(),
                                            [&] (const Arg& a) { return a.name == property.name.toString(); });
            if (! known)
                return Result::failure (name + " doesn't take --" + property.name.toString() + " (it takes " + argList (*command) + ")");
        }

    for (const auto& arg : command->args)
    {
        const auto value = given != nullptr ? given->getProperty (arg.name) : juce::var();
        if (value.isVoid())
        {
            if (arg.required)
                return Result::failure (name + " needs --" + arg.name + " (" + arg.description + ")");
            continue;
        }
        const auto [converted, error] = checkArg (name, arg, value);
        if (error.isNotEmpty())
            return Result::failure (error);
        checked->setProperty (arg.name, converted);
    }

    try
    {
        return command->run (Args (checkedVar));
    }
    catch (const std::exception& e)
    {
        return Result::failure (name + " failed: " + e.what());
    }
    catch (...)
    {
        return Result::failure (name + " failed");
    }
}

juce::var CommandRegistry::describe (const juce::String& onlyGroup) const
{
    if (onlyGroup.isNotEmpty())
        if (const auto* g = findGroup (onlyGroup))
            return describeGroup (*g);

    juce::Array<juce::var> all;
    for (const auto& [key, g] : groups)
        all.add (describeGroup (g));
    return makeObject ({ { "groups", all } });
}
} // namespace lynx::commands
