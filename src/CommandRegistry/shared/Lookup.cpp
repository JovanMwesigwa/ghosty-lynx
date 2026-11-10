#include "CommandRegistry/shared/Lookup.h"

#include "core/QuickRender.h"
#include "core/Song.h"

namespace lynx::commands
{
juce::String patternName (const Song& song, int pattern)
{
    return juce::String::fromUTF8 (quickrender::patternName (song, pattern).c_str());
}

juce::String channelName (const Song& song, int channel)
{
    if (channel >= 0 && channel < (int) song.channels.size())
        if (const auto& name = song.channels[(std::size_t) channel].name; ! name.empty())
            return juce::String::fromUTF8 (name.c_str());
    return "Channel " + juce::String (channel + 1);
}

int findChannel (const Song& song, const juce::String& text, juce::String& error)
{
    const auto wanted = text.trim();
    const auto count = (int) song.channels.size();

    if (wanted.isNotEmpty() && wanted.containsOnly ("0123456789"))
    {
        const auto n = wanted.getIntValue();
        if (n >= 1 && n <= count)
            return n - 1;
        error = "there's no channel " + wanted + " (the Channel Rack has " + juce::String (count) + ")";
        return -1;
    }

    int found = -1;
    for (int i = 0; i < count; ++i)
        if (channelName (song, i).equalsIgnoreCase (wanted))
        {
            if (found >= 0)
            {
                error = "two channels are named \"" + wanted + "\": use its number instead (channel list)";
                return -1;
            }
            found = i;
        }
    if (found < 0)
        error = "no channel named \"" + wanted + "\" (channel list shows them)";
    return found;
}

std::optional<std::uint32_t> parseColour (juce::String text)
{
    text = text.trim().toLowerCase();
    if (text == "none" || text == "default" || text.isEmpty())
        return 0u;
    if (text.startsWithChar ('#'))
        text = text.substring (1);
    if (text.length() == 3) // #abc → #aabbcc
    {
        juce::String full;
        for (int i = 0; i < 3; ++i)
            full += juce::String::charToString (text[i]) + juce::String::charToString (text[i]);
        text = full;
    }
    if (text.length() != 6 || ! text.containsOnly ("0123456789abcdef"))
        return std::nullopt;
    return 0xff000000u | (std::uint32_t) text.getHexValue32();
}

juce::var colourText (std::uint32_t argb)
{
    if (argb == 0)
        return {};
    return "#" + juce::String::toHexString ((int) (argb & 0xffffffu)).paddedLeft ('0', 6);
}

juce::File pathFromArg (const juce::String& text)
{
    auto path = text.trim();
    if (path == "~" || path.startsWith ("~/"))
        path = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName() + path.substring (1);
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

int parseKey (const juce::String& text, juce::String& error)
{
    const auto t = text.trim();
    int key = -1;

    if (t.isNotEmpty() && t.containsOnly ("0123456789"))
    {
        key = t.getIntValue(); // a MIDI number
    }
    else if (t.isNotEmpty())
    {
        // letter, optional # or b, octave (0..10: in FL's numbering key 0 is C0, so there are no negative octaves)
        static const int letterPitch[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        const auto letter = juce::CharacterFunctions::toUpperCase (t[0]);
        auto rest = t.substring (1);
        int accidental = 0;
        if (rest.startsWithChar ('#') || rest.startsWithChar ('b'))
        {
            accidental = rest.startsWithChar ('#') ? 1 : -1;
            rest = rest.substring (1);
        }

        const auto octaveText = rest.trim();
        const auto octaveOk = octaveText.isNotEmpty() && octaveText.containsOnly ("0123456789");
        if (letter >= 'A' && letter <= 'G' && octaveOk)
            key = octaveText.getIntValue() * 12 + letterPitch[letter - 'A'] + accidental; // (FL: key 60 = C5 → octave × 12)
    }

    if (key < 0 || key > 127)
    {
        error = key < 0 && ! t.containsOnly ("0123456789")
                    ? "\"" + t + "\" isn't a key: use a name like C5, F#3, Bb4 (C5 = middle C) or a MIDI number 0-127"
                    : "key " + t + " is outside the MIDI range (0-127, C0 to G10)";
        return -1;
    }
    return key;
}

int findPattern (const Song& song, const juce::String& text, juce::String& error)
{
    const auto wanted = text.trim();
    const auto count = (int) song.patterns.size();

    // a number: its position in the list (from 1)
    if (wanted.isNotEmpty() && wanted.containsOnly ("0123456789"))
    {
        const auto n = wanted.getIntValue();
        if (n >= 1 && n <= count)
            return n - 1;
        error = "there's no pattern " + wanted + " (the project has " + juce::String (count) + ")";
        return -1;
    }

    // a name: the one pattern with it
    int found = -1;
    for (int i = 0; i < count; ++i)
        if (patternName (song, i).equalsIgnoreCase (wanted))
        {
            if (found >= 0)
            {
                error = "two patterns are named \"" + wanted + "\": use its number instead (pattern list)";
                return -1;
            }
            found = i;
        }
    if (found < 0)
        error = "no pattern named \"" + wanted + "\" (pattern list shows them)";
    return found;
}
} // namespace lynx::commands
