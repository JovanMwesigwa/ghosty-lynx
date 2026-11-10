#include "CommandRegistry/shared/Catalog.h"

#include "CommandRegistry/core/CommandRegistry.h"

#include <algorithm>

namespace lynx::commands
{
namespace
{
// When the same plugin is installed in several formats and no --format says which: the first of these.
int formatPreference (const juce::String& format)
{
    const juce::StringArray order { "VST3", "CLAP", "AudioUnit", "VST" };
    const auto i = order.indexOf (format);
    return i < 0 ? order.size() : i;
}
} // namespace

juce::String pluginFormatFromId (const juce::String& id)
{
    return id.upToFirstOccurrenceOf ("-", false, false);
}

juce::String normaliseFormat (const juce::String& text)
{
    const auto f = text.trim().toLowerCase().removeCharacters (" -_");
    if (f == "vst3") return "VST3";
    if (f == "au" || f == "audiounit") return "AudioUnit";
    if (f == "clap") return "CLAP";
    if (f == "vst" || f == "vst2") return "VST";
    return {};
}

juce::String withoutRequirement (const juce::String& label, bool available)
{
    return ! available && label.endsWithChar (')') ? label.upToLastOccurrenceOf (" (", false, false) : label;
}

juce::var catalogList (const std::vector<CatalogEntry>& entries, const juce::String& searchText, const juce::String& sourceText,
                       const juce::StringArray& sources, const juce::String& formatText, const juce::String& key, juce::String& error)
{
    const auto search = searchText.trim();
    const auto source = sourceText.trim().toLowerCase();
    if (source.isNotEmpty() && ! sources.contains (source))
    {
        error = "--source: \"" + source + "\" isn't one of " + sources.joinIntoString (", ");
        return {};
    }
    const auto format = normaliseFormat (formatText);
    if (formatText.trim().isNotEmpty() && format.isEmpty())
    {
        error = "--format: \"" + formatText.trim() + "\" isn't one of vst3, au, clap, vst";
        return {};
    }

    juce::Array<juce::var> list;
    for (const auto& e : entries)
        if ((search.isEmpty() || e.name.containsIgnoreCase (search)) && (source.isEmpty() || e.source == source)
            && (format.isEmpty() || e.format == format))
        {
            auto entry = makeObject ({ { "name", e.name }, { "source", e.source }, { "detail", e.detail }, { "available", e.available }, { "id", e.id } });
            if (e.format.isNotEmpty())
                entry.getDynamicObject()->setProperty ("format", e.format);
            list.add (entry);
        }
    return makeObject ({ { "count", list.size() }, { key, list } });
}

int findInCatalog (const std::vector<CatalogEntry>& entries, const juce::String& text, const juce::String& formatText,
                   const juce::String& noun, const juce::String& listCommand, juce::String& error, juce::String& note)
{
    const auto wanted = text.trim();
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(std::size_t) i].id == wanted)
            return i;

    std::vector<int> found;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(std::size_t) i].name.equalsIgnoreCase (wanted))
            found.push_back (i);
    if (found.empty())
    {
        error = "no " + noun + " named \"" + wanted + "\" (" + listCommand + " lists them; --search narrows it)";
        return -1;
    }
    const auto at = [&entries] (int i) -> const CatalogEntry& { return entries[(std::size_t) i]; };

    juce::StringArray formats; // (the formats it's installed as, for the messages)
    for (auto i : found)
        if (at (i).format.isNotEmpty())
            formats.addIfNotAlreadyThere (at (i).format);

    // --format: only that format
    if (formatText.trim().isNotEmpty())
    {
        const auto format = normaliseFormat (formatText);
        if (format.isEmpty())
        {
            error = "--format: \"" + formatText.trim() + "\" isn't one of vst3, au, clap, vst";
            return -1;
        }
        found.erase (std::remove_if (found.begin(), found.end(), [&] (int i) { return at (i).format != format; }), found.end());
        if (found.empty())
        {
            error = wanted + " isn't installed as " + format
                    + (formats.isEmpty() ? juce::String (" (it's built in: leave out --format)") : " (it is as: " + formats.joinIntoString (", ") + ")");
            return -1;
        }
    }

    if (found.size() == 1)
        return found.front();

    // the same plugin in several formats: the preferred one
    std::stable_sort (found.begin(), found.end(), [&] (int a, int b) { return formatPreference (at (a).format) < formatPreference (at (b).format); });
    if (at (found[0]).format.isNotEmpty() && at (found[0]).format != at (found[1]).format)
    {
        juce::StringArray others;
        for (const auto& f : formats)
            if (f != at (found[0]).format)
                others.add (f == "AudioUnit" ? "au" : f.toLowerCase());
        note = wanted + " is installed as " + formats.joinIntoString (" and ") + ": used " + at (found[0]).format + " (--format "
               + others.joinIntoString (" or --format ") + " for the other)";
        return found[0];
    }

    // still several (two of the same format, or a built-in name a plugin also has): only the id tells them apart
    juce::StringArray ids;
    for (auto i : found)
        ids.add (at (i).id + "  (" + at (i).detail + ")");
    error = "several " + noun + "s are named \"" + wanted + "\": give its id instead, one of: " + ids.joinIntoString ("; ");
    return -1;
}
} // namespace lynx::commands
