#include "CommandRegistry/core/CommandLine.h"

namespace lynx::commands
{
bool splitWords (const juce::String& line, juce::StringArray& words)
{
    words.clear();
    juce::String current;
    bool inWord = false;
    juce::juce_wchar quote = 0;

    for (auto p = line.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (quote != 0)
        {
            if (c == quote)
                quote = 0;
            else
                current += c;
        }
        else if (c == '"' || c == '\'')
        {
            quote = c;
            inWord = true;
        }
        else if (juce::CharacterFunctions::isWhitespace (c))
        {
            if (inWord)
                words.add (current);
            current.clear();
            inWord = false;
        }
        else
        {
            current += c;
            inWord = true;
        }
    }
    if (inWord)
        words.add (current);
    return quote == 0;
}

CommandLine parseCommandLine (const juce::String& line)
{
    CommandLine parsed;
    auto* args = new juce::DynamicObject();
    parsed.args = juce::var (args);

    juce::StringArray words;
    if (! splitWords (line, words))
    {
        parsed.error = "a quote isn't closed";
        return parsed;
    }

    juce::StringArray nameParts;
    for (int i = 0; i < words.size(); ++i)
    {
        const auto& word = words[i];
        if (word == "--help" || word == "-h")
        {
            parsed.help = true;
        }
        else if (word.startsWith ("--") && word.length() > 2)
        {
            const auto flag = word.substring (2);
            if (flag.containsChar ('='))                                       // --bpm=128
                args->setProperty (flag.upToFirstOccurrenceOf ("=", false, false), flag.fromFirstOccurrenceOf ("=", false, false));
            else if (i + 1 < words.size() && ! words[i + 1].startsWith ("--")) // --bpm 128 (also --by -5)
                args->setProperty (flag, words[++i]);
            else                                                              // --discard
                args->setProperty (flag, true);
        }
        else if (args->getProperties().isEmpty())
        {
            nameParts.add (word); // the words before the first flag name the command
        }
        else
        {
            parsed.error = "\"" + word + "\" after the flags: put the command first (e.g. tempo set --bpm 128)";
            return parsed;
        }
    }

    // "project help", "tempo set help" and "help project": that section's help (like --help); "help" alone stays
    // the name "help" (everything)
    if (nameParts.size() > 1 && nameParts[nameParts.size() - 1].equalsIgnoreCase ("help"))
    {
        nameParts.remove (nameParts.size() - 1);
        parsed.help = true;
    }
    else if (nameParts.size() > 1 && nameParts[0].equalsIgnoreCase ("help"))
    {
        nameParts.remove (0);
        parsed.help = true;
    }

    parsed.name = nameParts.joinIntoString (".");
    return parsed;
}
} // namespace lynx::commands
