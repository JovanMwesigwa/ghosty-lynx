#pragma once

// Catalog: a list of things the user picks by name (the instruments a channel can be, the effects a mixer slot can
// hold), with the plugin-format handling both share:
//
//   "Nexus"                     → the one named so (any letter case), or its id
//   "Nexus" --format au         → that format of it (vst3, au / audiounit, clap, vst / vst2)
//   "Nexus", installed twice    → VST3 first (then CLAP, AU, VST2), and a note saying so
//
// A module builds the entries (the menus' list) next to its own "make one" functions, same order.

#include <juce_core/juce_core.h>

#include <vector>

namespace lynx::commands
{
struct CatalogEntry
{
    juce::String name;        // as the menus show it
    juce::String source;      // where it comes from: "lynx", "fl", "vfx", "plugin"
    juce::String detail;      // the menus' second line ("built-in", "reFX · VST3")
    juce::String id;          // unique: "lynx:fx:Fruity Limiter", "VST3-Nexus-1a2b..."
    bool available = true;    // false: an FL engine this PC can't make (no FL Studio installation)
    juce::String format;      // a plugin's format: "VST3", "AudioUnit", "CLAP", "VST" (empty for the built-in ones)
};

// A plugin's format from its id (JUCE's identifier string starts with it: "VST3-Nexus-1a2b...").
juce::String pluginFormatFromId (const juce::String& id);

// What --format accepts → the format's name in ids: vst3, au / audiounit, clap, vst / vst2 (any letter case); "" else.
juce::String normaliseFormat (const juce::String& text);

// The menus' "Sytrus (requires FL Studio)" → "Sytrus" for an entry that isn't available (`available` says why).
juce::String withoutRequirement (const juce::String& label, bool available);

// --search / --source / --format filtered, as a JSON list under `key` with "count". `sources` are the valid --source
// values. Undefined var with `error` for a bad --source / --format.
juce::var catalogList (const std::vector<CatalogEntry>& entries, const juce::String& search, const juce::String& source,
                       const juce::StringArray& sources, const juce::String& format, const juce::String& key, juce::String& error);

// The entry `text` names (see above): its index, or -1 with `error`. `note` says which format was taken when the plugin
// is installed in several and `format` didn't say. `noun` ("instrument") and `listCommand` ("channel instruments")
// go into the messages.
int findInCatalog (const std::vector<CatalogEntry>& entries, const juce::String& text, const juce::String& format,
                   const juce::String& noun, const juce::String& listCommand, juce::String& error, juce::String& note);
} // namespace lynx::commands
