#include "CommandRegistry/shared/Knobs.h"

#include "CommandRegistry/core/CommandRegistry.h"

#include <cmath>

namespace lynx::commands
{
namespace
{
// The number in a knob's text: as shown ("600 ms" → 600), and in base units when it has a unit that converts
// (→ 0.6 s): ms / s, Hz / kHz, and "k" (×1000). "-inf" → very low. nullopt when the text has no number.
struct Reading
{
    double shown = 0.0, base = 0.0;
    bool hasUnit = false;
};

std::optional<Reading> numberIn (juce::String text)
{
    text = text.trim().replace (juce::String::fromUTF8 ("\xe2\x88\x92"), "-"); // (− the minus sign some effects show)
    if (text.startsWithIgnoreCase ("-inf"))
        return Reading { -1.0e9, -1.0e9, false };
    const auto first = text[0];
    if (! (juce::CharacterFunctions::isDigit (first) || first == '-' || first == '+' || first == '.'))
        return std::nullopt;

    Reading r;
    r.shown = r.base = text.getDoubleValue();
    const auto unit = text.trimCharactersAtStart ("+-0123456789.").trim().toLowerCase();
    const auto word = unit.upToFirstOccurrenceOf (" ", false, false);
    double factor = 1.0;
    if (word == "ms")
        factor = 0.001;
    else if (word == "s" || word == "sec" || word == "secs" || word == "seconds")
        factor = 1.0;
    else if (word == "khz" || word == "k")
        factor = 1000.0;
    else if (word == "hz")
        factor = 1.0;
    else
        return r; // (%, dB, a word: compared as shown)
    r.base = r.shown * factor;
    r.hasUnit = true;
    return r;
}

} // namespace

// The text a plugin parameter shows for `value`, with its unit ("-6.0" + "dB").
juce::String pluginText (juce::AudioProcessorParameter& p, float value)
{
    const auto text = p.getText (value, 64).trim();
    const auto label = p.getLabel().trim();
    return label.isEmpty() || text.endsWithIgnoreCase (label) ? text : text + " " + label;
}

std::vector<Knob> pluginKnobs (juce::AudioPluginInstance& plugin, int* hidden)
{
    std::vector<Knob> out;
    const auto& params = plugin.getParameters();
    if (hidden != nullptr)
        *hidden = 0;
    for (int i = 0; i < params.size(); ++i)
    {
        const auto name = params[i]->getName (64).trim();
        if (name.startsWith ("MIDI CC ") && name.substring (8).containsOnly ("0123456789|"))
        {
            if (hidden != nullptr)
                ++*hidden;
            continue;
        }
        out.push_back ({ i, params[i]->getName (64).trim(), pluginText (*params[i], params[i]->getValue()), params[i]->getValue(),
                         params[i]->getDefaultValue() });
    }
    return out;
}

juce::var knobInfo (const Knob& k)
{
    auto round3 = [] (float v) { return std::round ((double) v * 1000.0) / 1000.0; };
    return makeObject ({ { "param", k.index + 1 }, { "name", k.name }, { "text", k.text }, { "value", round3 (k.value) }, { "default", round3 (k.defaultValue) } });
}

// --param: its number (from 1) or its name (any letter case; a unique part of the name also works). -1 with `error`.
int findKnob (const std::vector<Knob>& knobs, const juce::String& text, const juce::String& listCommand, juce::String& error)
{
    const auto wanted = text.trim();
    if (wanted.isNotEmpty() && wanted.containsOnly ("0123456789"))
    {
        const auto n = wanted.getIntValue();
        for (int i = 0; i < (int) knobs.size(); ++i)
            if (knobs[(std::size_t) i].index == n - 1)
                return i;
        error = "there's no param " + wanted + " (" + listCommand + " lists them)";
        return -1;
    }
    std::vector<int> exact, partial;
    for (int i = 0; i < (int) knobs.size(); ++i)
    {
        if (knobs[(std::size_t) i].name.equalsIgnoreCase (wanted))
            exact.push_back (i);
        else if (knobs[(std::size_t) i].name.containsIgnoreCase (wanted))
            partial.push_back (i);
    }
    const auto& found = exact.empty() ? partial : exact;
    if (found.size() == 1)
        return found.front();
    if (found.empty())
        error = "no param named \"" + wanted + "\" (" + listCommand + " lists them; --search narrows it)";
    else
    {
        juce::StringArray names;
        for (auto i : found)
            names.add (juce::String (i + 1) + ": " + knobs[(std::size_t) i].name);
        error = "several params match \"" + wanted + "\": use its number, one of " + names.joinIntoString (", ");
    }
    return -1;
}

// The 0..1 value whose text is closest to `want`: a number ("-6", "-6 dB", "2.5k", "1.2 s") → the nearest number (with
// a unit, converted: "1.2 s" finds "1200 ms"); a word ("On", "Lowpass") → the value showing it. nullopt when none does.
std::optional<float> valueForText (const std::function<juce::String (float)>& textOf, const juce::String& want)
{
    // Shown values are rounded, so a run of values reads the same ("50%" from 0.495 to 0.504): the middle of the best
    // run is taken (0.5), or its end when it reaches one ("off" from 0 → 0).
    const auto target = numberIn (want);
    double bestError = 1.0e300;
    int runStart = -1, runEnd = -1;
    bool inRun = false;
    constexpr int steps = 2000;
    for (int k = 0; k <= steps; ++k)
    {
        const auto text = textOf ((float) k / steps);
        double error = 1.0e300;
        if (target)
        {
            if (const auto got = numberIn (text))
                error = target->hasUnit && got->hasUnit ? std::abs (got->base - target->base) : std::abs (got->shown - target->shown);
        }
        else if (text.trim().equalsIgnoreCase (want.trim()))
            error = 0.0;

        if (error < bestError - 1.0e-9)
        {
            bestError = error;
            runStart = runEnd = k;
            inRun = true;
        }
        else if (inRun && std::abs (error - bestError) <= 1.0e-9)
            runEnd = k;
        else
            inRun = false;
    }
    if (runStart < 0 || bestError >= 1.0e299)
        return std::nullopt;
    if (runStart == 0)
        return 0.0f;
    if (runEnd == steps)
        return 1.0f;
    const auto coarse = (float) (runStart + runEnd) / 2.0f / (float) steps;

    // a number not hit exactly (a fine-grained knob: "800 ms" between two steps): look closer around it
    if (! target || bestError <= 1.0e-9)
        return coarse;
    const auto lo = (double) (runStart - 1) / steps, hi = (double) (runEnd + 1) / steps;
    auto best = coarse;
    constexpr int fine = 400;
    for (int k = 0; k <= fine; ++k)
    {
        const auto v = (float) (lo + (hi - lo) * k / fine);
        if (const auto got = numberIn (textOf (v)))
        {
            const auto error = target->hasUnit && got->hasUnit ? std::abs (got->base - target->base) : std::abs (got->shown - target->shown);
            if (error < bestError - 1.0e-9)
            {
                bestError = error;
                best = v;
            }
        }
    }
    return best;
}
} // namespace lynx::commands
