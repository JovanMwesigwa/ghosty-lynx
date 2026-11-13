#include "CommandRegistry/channel/ChannelModule.h"

#include "CommandRegistry/shared/Catalog.h"
#include "CommandRegistry/shared/Knobs.h"
#include "CommandRegistry/shared/Lookup.h"
#include "core/QuickRender.h"
#include "core/Song.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "engine/SampleLibrary.h"
#include "ui/ChannelRackComponent.h"
#include "ui/MainComponent.h"
#include "ui/PluginPresetMenu.h"
#include "ui/QuickPick.h"

#include <functional>
#include <optional>

#include <algorithm>
#include <cmath>

namespace lynx::commands
{
namespace
{
double rounded2 (float v)
{
    return std::round ((double) v * 100.0) / 100.0; // (in double: 0.9, not 0.899999976)
}

juce::String instrumentOf (const ChannelDef& c)
{
    if (c.instrument == InstrumentKind::plugin && ! c.plugin.name.empty())
        return juce::String::fromUTF8 (c.plugin.name.c_str()); // (the plugin's own name)
    return instrumentDisplayName (c.instrument);
}

juce::var mixerTrackName (const Song& song, int track)
{
    if (track < 0 || track >= (int) song.mixer.size())
        return {};
    if (const auto& name = song.mixer[(std::size_t) track].name; ! name.empty())
        return juce::String::fromUTF8 (name.c_str());
    return track == 0 ? juce::String ("Master") : "Insert " + juce::String (track);
}

juce::var summary (const Song& song, int index)
{
    const auto& c = song.channels[(std::size_t) index];
    return makeObject ({ { "channel", index + 1 },
                         { "name", channelName (song, index) },
                         { "instrument", instrumentOf (c) },
                         { "muted", c.muted },
                         { "volume", rounded2 (c.volume) },
                         { "pan", rounded2 (c.pan) },
                         { "mixerTrack", c.mixerTrack } });
}

// Everything channel.get shows: the summary, its sample, mixer insert, colour, group, and where it's used.
juce::var details (const Song& song, int index)
{
    const auto& c = song.channels[(std::size_t) index];
    auto out = summary (song, index);
    auto* o = out.getDynamicObject();

    o->setProperty ("sample", c.samplePath.empty() ? juce::var() : juce::var (juce::String::fromUTF8 (c.samplePath.c_str())));
    if (c.instrument == InstrumentKind::plugin) // (which format of the plugin: "VST3-Nexus-..." → VST3)
        o->setProperty ("format", juce::String::fromUTF8 (c.plugin.id.c_str()).upToFirstOccurrenceOf ("-", false, false));
    o->setProperty ("mixerTrackName", mixerTrackName (song, c.mixerTrack));
    o->setProperty ("colour", c.colour == 0 ? juce::var() : juce::var ("#" + juce::String::toHexString ((int) (c.colour & 0xffffffu)).paddedLeft ('0', 6)));
    o->setProperty ("group", c.group >= 0 && c.group < (int) song.channelGroups.size()
                                 ? juce::var (juce::String::fromUTF8 (song.channelGroups[(std::size_t) c.group].c_str()))
                                 : juce::var());

    // the patterns with notes for it, and the Playlist's audio clips that play it
    juce::Array<juce::var> usedIn;
    int totalNotes = 0;
    for (int p = 0; p < (int) song.patterns.size(); ++p)
    {
        const auto notes = (int) std::count_if (song.patterns[(std::size_t) p].notes.begin(), song.patterns[(std::size_t) p].notes.end(),
                                                [index] (const Note& n) { return n.channel == index; });
        if (notes > 0)
            usedIn.add (makeObject ({ { "pattern", p + 1 }, { "name", patternName (song, p) }, { "notes", notes } }));
        totalNotes += notes;
    }
    const auto audioClips = (int) std::count_if (song.playlist.begin(), song.playlist.end(),
                                                 [index] (const Clip& clip) { return clip.kind == ClipKind::audio && clip.channel == index; });
    o->setProperty ("usedIn", usedIn);
    o->setProperty ("notes", totalNotes);
    o->setProperty ("audioClips", audioClips);
    return out;
}

int channelFromArgs (const Song& song, const Args& args, juce::String& error)
{
    return findChannel (song, args.text ("channel"), error);
}

// ---- the instruments a channel can have: exactly what the Channel Rack's Add / Insert / Replace menus offer --------

const char* sourceName (int folder)
{
    switch (folder)
    {
        case 0: return "lynx";
        case 1: return "fl";
        case 2: return "vfx";
        default: return "plugin";
    }
}

// The menus' list (quickpick::generatorEntries: LYNX's instruments and tools, FL's engines, the VFX note processors,
// the installed instrument plugins), with the name each shows (a plugin renamed in the Plugin database: that name),
// and next to each entry the function that makes its channel. Sampler, which the menus offer as "Sampler (choose
// file)...", comes first.
struct Instruments
{
    std::vector<CatalogEntry> entries;
    std::vector<std::function<std::optional<ChannelDef>()>> make; // (same order)
};

Instruments instruments (AudioEngine& engine)
{
    Instruments out;
    out.entries.push_back ({ "Sampler", "lynx", "plays a sample (add --sample to load one)", "lynx:gen:Sampler" });
    out.make.push_back ([] {
        ChannelDef c;
        c.name = "Sampler";
        c.instrument = InstrumentKind::sampler;
        return std::optional<ChannelDef> (c);
    });
    for (const auto& e : quickpick::generatorEntries (engine, true))
    {
        const auto id = juce::String::fromUTF8 (e.key.c_str());
        const juce::String source = sourceName (e.folder);
        // (the menu labels an engine this PC can't make "Sytrus (requires FL Studio)"; here `available` says so)
        out.entries.push_back ({ withoutRequirement (quickpick::shownName (engine, e.key, e.label), e.available), source, e.detail, id,
                                 e.available, source == "plugin" ? pluginFormatFromId (id) : juce::String() });
        out.make.push_back (e.make);
    }
    return out;
}

// The channel --instrument / --type, --format and --sample describe, ready to add or to put on a channel. nullopt with
// `error`; `note` says which format was taken when the plugin has several and --format didn't say.
std::optional<ChannelDef> channelFromInstrumentArgs (Context& ctx, const Args& args, juce::String& error, juce::String& note)
{
    const auto list = instruments (ctx.engine);
    const auto wanted = args.has ("instrument") ? args.text ("instrument") : args.text ("type", "Sampler");
    const auto index = findInCatalog (list.entries, wanted, args.text ("format"), "instrument", "channel instruments", error, note);
    if (index < 0)
        return std::nullopt;
    const auto& entry = list.entries[(std::size_t) index];
    if (! entry.available)
    {
        error = entry.name + " is one of FL Studio's engines: it needs the data files of an FL Studio installation, which weren't found";
        return std::nullopt;
    }

    auto c = list.make[(std::size_t) index]();
    if (! c)
    {
        error = entry.name + " couldn't be made (an FL engine whose data files couldn't be read)";
        return std::nullopt;
    }

    // a sample (checked and loaded the way the rack's "choose file" does)
    if (args.has ("sample"))
    {
        if (c->instrument != InstrumentKind::sampler)
        {
            error = "--sample is for a Sampler (leave out --instrument, or use --instrument Sampler)";
            return std::nullopt;
        }
        const auto sample = pathFromArg (args.text ("sample"));
        if (sample == juce::File())
            error = "--sample must be an absolute path or start with ~/ (got \"" + args.text ("sample") + "\")";
        else if (! sample.existsAsFile())
            error = "no file at " + sample.getFullPathName();
        else if (! SampleLibrary::isSampleFile (sample) || ctx.engine.getSampleLibrary().load (sample.getFullPathName()) == nullptr)
            error = "couldn't read " + sample.getFileName() + " as audio (WAV, AIFF, FLAC, OGG, MP3...)";
        if (error.isNotEmpty())
            return std::nullopt;
        c->samplePath = sample.getFullPathName().toStdString();
        c->name = sample.getFileNameWithoutExtension().toStdString();
    }
    return c;
}

// A --name for a channel: not just a number, not taken by another channel (`except` may keep its own). Empty error = ok.
juce::String checkNewName (const Song& song, const juce::String& name, int except = -1)
{
    if (name.containsOnly ("0123456789"))
        return "a channel name can't be just a number (numbers pick channels by position)";
    juce::String ignored;
    if (const auto taken = findChannel (song, name, ignored); taken >= 0 && taken != except)
        return "there's already a channel named \"" + name + "\"";
    return {};
}

const auto instrumentArg = [] {
    return Arg::text ("instrument", "a name from channel instruments: Sampler, Synth, Kick, Patcher, an FL engine, a plugin...").optional();
};
const auto sampleArg = [] { return Arg::text ("sample", "a sample for a Sampler to play (absolute path or ~/...)").optional(); };
const auto formatArg = [] {
    return Arg::text ("format", "a plugin installed in several formats: vst3, au, clap or vst (default: VST3 first)").optional();
};
} // namespace

// ---- a plugin instrument's knobs and presets --------------------------------------------------------------------------

// The running plugin of channel `index` (kept alive by `holder`), or nullptr with `error`.
PluginInstrument* runningInstrument (Context& ctx, int index, std::shared_ptr<Instrument>& holder, juce::String& error)
{
    const auto& c = ctx.song().channels[(std::size_t) index];
    if (c.instrument != InstrumentKind::plugin)
    {
        error = channelName (ctx.song(), index) + " plays " + instrumentOf (c)
              + ", a built-in instrument: knobs and presets are listed for plugin instruments (VST3, AU, CLAP, VST)";
        return nullptr;
    }
    holder = ctx.engine.getChannelInstrument (index);
    auto* plugin = dynamic_cast<PluginInstrument*> (holder.get());
    if (plugin == nullptr)
        error = "the plugin isn't running (still loading, or not installed / scanned): try again in a moment";
    else if (plugin->hasCrashed())
    {
        error = "this plugin crashed and was switched off: save the project and open it again to reload it";
        plugin = nullptr;
    }
    return plugin;
}

juce::var presetInfo (int number, const pluginpresetui::PresetEntry& e)
{
    return makeObject ({ { "preset", number }, { "name", e.name }, { "source", e.program >= 0 ? "plugin" : "file" } });
}

// --preset: its number (from 1), its name (a unique part works), or next / previous (from the plugin's current program).
// -1 with `error`.
int findPreset (const std::vector<pluginpresetui::PresetEntry>& presets, int current, const juce::String& text, juce::String& error)
{
    const auto wanted = text.trim();
    const auto n = (int) presets.size();
    if (wanted.equalsIgnoreCase ("next") || wanted.equalsIgnoreCase ("previous") || wanted.equalsIgnoreCase ("prev"))
    {
        const auto delta = wanted.equalsIgnoreCase ("next") ? 1 : -1;
        return current < 0 ? (delta > 0 ? 0 : n - 1) : (current + delta + n) % n;
    }
    if (wanted.isNotEmpty() && wanted.containsOnly ("0123456789"))
    {
        const auto k = wanted.getIntValue();
        if (k >= 1 && k <= n)
            return k - 1;
        error = "there's no preset " + wanted + " (it has " + juce::String (n) + ")";
        return -1;
    }
    std::vector<int> exact, partial;
    for (int i = 0; i < n; ++i)
    {
        if (presets[(std::size_t) i].name.equalsIgnoreCase (wanted))
            exact.push_back (i);
        else if (presets[(std::size_t) i].name.containsIgnoreCase (wanted))
            partial.push_back (i);
    }
    const auto& found = exact.empty() ? partial : exact;
    if (found.size() == 1 || (exact.size() > 1))
        return found.front();
    if (found.empty())
        error = "no preset named \"" + wanted + "\" (channel presets lists them; --search narrows it)";
    else
    {
        juce::StringArray names;
        for (std::size_t i = 0; i < found.size() && i < 12; ++i)
            names.add (juce::String (found[i] + 1) + ": " + presets[(std::size_t) found[i]].name);
        error = juce::String ((int) found.size()) + " presets match \"" + wanted + "\": use its number, e.g. " + names.joinIntoString (", ")
              + (found.size() > 12 ? ", ..." : "");
    }
    return -1;
}

// The preset the plugin is on: its current program (when it shares programs). -1 when unknown (a preset file was
// loaded, or the plugin keeps its presets to itself).
int currentPreset (juce::AudioPluginInstance& plugin, const std::vector<pluginpresetui::PresetEntry>& presets)
{
    if (plugin.getNumPrograms() <= 1)
        return -1;
    const auto program = plugin.getCurrentProgram();
    for (int i = 0; i < (int) presets.size(); ++i)
        if (presets[(std::size_t) i].program == program)
            return i;
    return -1;
}

void ChannelModule::define()
{
    query ("list", "Every channel in the Channel Rack: name, instrument, mute, volume, pan, mixer track", [] (Context& ctx, const Args&) {
        const auto& song = ctx.song();
        juce::Array<juce::var> list;
        for (int i = 0; i < (int) song.channels.size(); ++i)
            list.add (summary (song, i));
        return Result::success (list);
    });

    query ("get", "One channel: settings, sample, mixer insert, and the patterns that use it",
           { Arg::text ("channel", "its name or number (channel list)") },
           [] (Context& ctx, const Args& args) {
               juce::String error;
               const auto index = channelFromArgs (ctx.song(), args, error);
               if (index < 0)
                   return Result::failure (error);
               return Result::success (details (ctx.song(), index));
           });

    query ("instruments", "What a channel can be: everything the Channel Rack's Add menu offers (LYNX, FL engines, VFX, plugins)",
           { Arg::text ("search", "only names containing this (any letter case)").optional(),
             Arg::text ("source", "only one source: lynx, fl, vfx or plugin").optional(),
             Arg::text ("format", "only plugins of one format: vst3, au, clap or vst").optional() },
           [] (Context& ctx, const Args& args) {
               juce::String error;
               const auto list = catalogList (instruments (ctx.engine).entries, args.text ("search"), args.text ("source"),
                                              { "lynx", "fl", "vfx", "plugin" }, args.text ("format"), "instruments", error);
               return error.isNotEmpty() ? Result::failure (error) : Result::success (list);
           });

    mutation ("add", "A new channel at the bottom of the rack with any instrument (an empty Sampler by default), on a free insert",
              { instrumentArg(),
                formatArg(),
                Arg::text ("type", "the same as --instrument (kept for older scripts)").optional(),
                Arg::text ("name", "its name; default: the instrument's (or the sample's file name)").optional(),
                sampleArg() },
              [] (Context& ctx, const Args& args) {
                  auto* ui = ctx.ui();
                  if (ui == nullptr)
                      return Context::windowNotReady();
                  const auto& song = ctx.song();

                  juce::String error, note;
                  auto c = channelFromInstrumentArgs (ctx, args, error, note);
                  if (! c)
                      return Result::failure (error);

                  // its name: a given one must be free (commands find channels by name), a default one is made free
                  if (const auto name = args.text ("name").trim(); name.isNotEmpty())
                  {
                      if (const auto problem = checkNewName (song, name); problem.isNotEmpty())
                          return Result::failure (problem);
                      c->name = name.toStdString();
                  }
                  else
                  {
                      c->name = quickrender::uniqueChannelName (song, c->name.empty() ? instrumentDisplayName (c->instrument).toStdString() : c->name);
                  }

                  const auto index = ui->addChannelFromCommand (std::move (*c));
                  auto result = details (ctx.song(), index);
                  if (note.isNotEmpty())
                      result.getDynamicObject()->setProperty ("note", note);
                  return Result::success (result);
              });

    mutation ("replace", "Put another instrument on a channel (the rack's Replace): its notes, insert, levels and name stay",
              { Arg::text ("channel", "its name or number (channel list)"),
                instrumentArg(),
                formatArg(),
                sampleArg(),
                Arg::text ("name", "a new name for it; default: it keeps its own").optional() },
              [] (Context& ctx, const Args& args) {
                  auto* ui = ctx.ui();
                  if (ui == nullptr)
                      return Context::windowNotReady();
                  const auto& song = ctx.song();

                  juce::String error;
                  const auto index = channelFromArgs (song, args, error);
                  if (index < 0)
                      return Result::failure (error);
                  if (! args.has ("instrument") && ! args.has ("sample"))
                      return Result::failure ("give the new --instrument (channel instruments lists them), or a --sample for a Sampler");

                  juce::String note;
                  auto c = channelFromInstrumentArgs (ctx, args, error, note);
                  if (! c)
                      return Result::failure (error);

                  // the name: the channel's own (commands find channels by name), unless --name gives another
                  auto name = juce::String::fromUTF8 (song.channels[(std::size_t) index].name.c_str());
                  if (const auto given = args.text ("name").trim(); given.isNotEmpty())
                  {
                      if (const auto problem = checkNewName (song, given, index); problem.isNotEmpty())
                          return Result::failure (problem);
                      name = given;
                  }
                  c->name = name.toStdString();

                  const auto previous = instrumentOf (song.channels[(std::size_t) index]);
                  if (! ui->replaceInstrumentFromCommand (index, std::move (*c)))
                      return Result::failure ("the Channel Rack didn't replace it");

                  auto result = details (ctx.song(), index);
                  result.getDynamicObject()->setProperty ("previousInstrument", previous);
                  if (note.isNotEmpty())
                      result.getDynamicObject()->setProperty ("note", note);
                  return Result::success (result);
              });

    mutation ("sample", "Swap the sample a channel plays (as dropping a file on its row): its notes, levels, insert and name stay; "
                        "a channel that isn't a Sampler becomes one",
              { Arg::text ("channel", "its name or number (channel list)"),
                Arg::text ("sample", "the new sample: a path from sample search (absolute or ~/...)"),
                Arg::boolean ("rename", "name the channel after the sample (default: it keeps its name)").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto index = channelFromArgs (song, args, error);
                  if (index < 0)
                      return Result::failure (error);

                  // checked and loaded the way the rack's "choose file" does (core/SampleFiles: wav, aif, flac, ogg, mp3, wv)
                  const auto file = pathFromArg (args.text ("sample"));
                  if (file == juce::File())
                      return Result::failure ("--sample must be an absolute path or start with ~/ (got \"" + args.text ("sample") + "\")");
                  if (! file.existsAsFile())
                      return Result::failure ("no file at " + file.getFullPathName() + " (sample search finds samples)");
                  if (! SampleLibrary::isSampleFile (file) || file.hasFileExtension ("speech")
                      || ctx.engine.getSampleLibrary().load (file.getFullPathName()) == nullptr)
                      return Result::failure ("couldn't read " + file.getFileName() + " as audio (WAV, AIFF, FLAC, OGG, MP3...)");

                  const auto& before = song.channels[(std::size_t) index];
                  const auto previousSample = before.samplePath.empty() ? juce::var() : juce::var (juce::String::fromUTF8 (before.samplePath.c_str()));
                  const auto previousInstrument = instrumentOf (before);

                  // its name: kept (commands find channels by name), or the sample's, made free like a new channel's
                  auto name = before.name;
                  if (args.boolean ("rename"))
                  {
                      const auto wanted = file.getFileNameWithoutExtension();
                      name = juce::String::fromUTF8 (before.name.c_str()).equalsIgnoreCase (wanted)
                                 ? before.name
                                 : quickrender::uniqueChannelName (song, wanted.toStdString());
                  }

                  // (ChannelRackComponent::assignSampleNow: FL's Granulizer and Wave Traveller keep their engine and
                  // play the file; anything else becomes a Sampler)
                  const auto path = file.getFullPathName().toStdString();
                  ctx.edit ([index, path, name] (Song& s) {
                      auto& c = s.channels[(std::size_t) index];
                      c.samplePath = path;
                      c.name = name;
                      if (c.instrument != InstrumentKind::granulizer && c.instrument != InstrumentKind::wavetraveller)
                          c.instrument = InstrumentKind::sampler;
                  });

                  auto result = details (ctx.song(), index);
                  auto* o = result.getDynamicObject();
                  o->setProperty ("previousSample", previousSample);
                  if (const auto now = instrumentOf (ctx.song().channels[(std::size_t) index]); now != previousInstrument)
                      o->setProperty ("previousInstrument", previousInstrument);
                  return Result::success (result);
              });

    mutation ("delete", "Remove a channel with its notes and audio clips (the channels below move up one)",
              { Arg::text ("channel", "its name or number (channel list)") },
              [] (Context& ctx, const Args& args) {
                  auto* ui = ctx.ui();
                  if (ui == nullptr)
                      return Context::windowNotReady();
                  juce::String error;
                  const auto index = channelFromArgs (ctx.song(), args, error);
                  if (index < 0)
                      return Result::failure (error);
                  if (ctx.song().channels.size() <= 1)
                      return Result::failure ("that's the last channel: the Channel Rack keeps at least one");

                  // what goes with it, for the reply (read before it's gone)
                  const auto before = details (ctx.song(), index);
                  if (! ui->deleteChannelFromCommand (index))
                      return Result::failure ("the Channel Rack didn't delete it");

                  return Result::success (makeObject ({ { "deleted", before["name"] },
                                                        { "notesRemoved", before["notes"] },
                                                        { "audioClipsRemoved", before["audioClips"] },
                                                        { "channels", (int) ctx.song().channels.size() } }));
              });

    // ---- a plugin instrument's knobs ------------------------------------------------------------------------------

    query ("params", "The knobs of a channel's plugin instrument: number, name, value (0-1) and how it reads (\"440 Hz\")",
           { Arg::text ("channel", "its number or name"), Arg::text ("search", "only knobs whose name contains this").optional() },
           [] (Context& ctx, const Args& args) {
               juce::String error;
               const auto ch = channelFromArgs (ctx.song(), args, error);
               std::shared_ptr<Instrument> holder;
               auto* plugin = ch < 0 ? nullptr : runningInstrument (ctx, ch, holder, error);
               if (plugin == nullptr)
                   return Result::failure (error);

               const auto search = args.text ("search").trim();
               int hidden = 0;
               juce::Array<juce::var> list;
               for (const auto& k : pluginKnobs (plugin->getInstance(), &hidden))
                   if (search.isEmpty() || k.name.containsIgnoreCase (search))
                       list.add (knobInfo (k));
               auto result = makeObject ({ { "channel", summary (ctx.song(), ch) },
                                           { "plugin", plugin->getInstance().getName() },
                                           { "count", list.size() },
                                           { "params", list } });
               if (hidden > 0)
                   result.getDynamicObject()->setProperty ("note", juce::String (hidden) + " \"MIDI CC\" routing slots aren't listed");
               return Result::success (result);
           });

    mutation ("param.set", "Turn one knob of a channel's plugin instrument: to a --value (0-1) or to what it should read (--text \"2 kHz\")",
              { Arg::text ("channel", "its number or name"),
                Arg::text ("param", "its number or name (channel params lists them)"),
                Arg::number ("value", "0 to 1 (the knob's whole range)", 0, 1).optional(),
                Arg::text ("text", "what it should read: a number (\"-6\", \"2k\", \"300 ms\") or a word (\"On\", \"Saw\")").optional() },
              [] (Context& ctx, const Args& args) {
                  juce::String error;
                  const auto ch = channelFromArgs (ctx.song(), args, error);
                  std::shared_ptr<Instrument> holder;
                  auto* plugin = ch < 0 ? nullptr : runningInstrument (ctx, ch, holder, error);
                  if (plugin == nullptr)
                      return Result::failure (error);
                  if (args.has ("value") == args.has ("text"))
                      return Result::failure ("give either --value (0 to 1) or --text (e.g. \"2 kHz\")");
                  auto& instance = plugin->getInstance();
                  const auto knobs = pluginKnobs (instance);
                  const auto at = findKnob (knobs, args.text ("param"), "channel params", error);
                  if (at < 0)
                      return Result::failure (error);
                  const auto& knob = knobs[(std::size_t) at];
                  const auto k = knob.index; // (the plugin's own parameter number)

                  auto value = (float) args.number ("value");
                  if (args.has ("text"))
                  {
                      auto* parameter = instance.getParameters()[k];
                      const auto found = valueForText ([&] (float v) { return pluginText (*parameter, v); }, args.text ("text"));
                      if (! found)
                          return Result::failure (knob.name + " never reads \"" + args.text ("text").trim() + "\" (it reads e.g. \"" + knob.text + "\")");
                      value = *found;
                  }
                  plugin->setParameterFromUi (k, value); // (as its window turns it)
                  ctx.engine.markModified();

                  auto now = knob;
                  now.value = instance.getParameters()[k]->getValue();
                  now.text = pluginText (*instance.getParameters()[k], now.value);
                  return Result::success (makeObject ({ { "channel", summary (ctx.song(), ch) },
                                                        { "plugin", instance.getName() },
                                                        { "param", knobInfo (now) },
                                                        { "previous", knob.text },
                                                        { "note", "a plugin's knobs aren't undo steps (the plugin keeps them; project save stores them)" } }));
              });

    // ---- a plugin instrument's presets ---------------------------------------------------------------------------

    query ("presets", "The presets of a channel's plugin instrument (as its window's preset menu lists them) and which one is on",
           { Arg::text ("channel", "its number or name"), Arg::text ("search", "only presets whose name contains this").optional() },
           [] (Context& ctx, const Args& args) {
               juce::String error;
               const auto ch = channelFromArgs (ctx.song(), args, error);
               std::shared_ptr<Instrument> holder;
               auto* plugin = ch < 0 ? nullptr : runningInstrument (ctx, ch, holder, error);
               if (plugin == nullptr)
                   return Result::failure (error);

               auto& instance = plugin->getInstance();
               const auto presets = pluginpresetui::presetsOf (instance);
               const auto current = currentPreset (instance, presets);
               const auto search = args.text ("search").trim();
               juce::Array<juce::var> list;
               for (int i = 0; i < (int) presets.size(); ++i)
                   if (search.isEmpty() || presets[(std::size_t) i].name.containsIgnoreCase (search))
                       list.add (presetInfo (i + 1, presets[(std::size_t) i]));

               auto result = makeObject ({ { "channel", summary (ctx.song(), ch) },
                                           { "plugin", instance.getName() },
                                           { "current", current >= 0 ? presetInfo (current + 1, presets[(std::size_t) current]) : juce::var() },
                                           { "count", list.size() },
                                           { "presets", list } });
               if (presets.empty())
                   result.getDynamicObject()->setProperty (
                       "note", instance.getName() + " doesn't share its presets with the host (pick them in its own window). Preset files (.fxp, "
                                   ".vstpreset, .lynxpreset) saved in " + pluginpresetui::folderFor (instance.getName()).getFullPathName()
                                   + " are listed here");
               return Result::success (result);
           });

    mutation ("preset.set", "Switch a channel's plugin instrument to a preset: its number or name (channel presets lists them), or next / previous",
              { Arg::text ("channel", "its number or name"), Arg::text ("preset", "its number, its name, next or previous") },
              [] (Context& ctx, const Args& args) {
                  juce::String error;
                  const auto ch = channelFromArgs (ctx.song(), args, error);
                  std::shared_ptr<Instrument> holder;
                  auto* plugin = ch < 0 ? nullptr : runningInstrument (ctx, ch, holder, error);
                  if (plugin == nullptr)
                      return Result::failure (error);

                  auto& instance = plugin->getInstance();
                  const auto presets = pluginpresetui::presetsOf (instance);
                  if (presets.empty())
                      return Result::failure (instance.getName() + " doesn't share its presets with the host: pick one in its own window (or save "
                                              "preset files in " + pluginpresetui::folderFor (instance.getName()).getFullPathName() + ")");
                  const auto before = currentPreset (instance, presets);
                  const auto k = findPreset (presets, before, args.text ("preset"), error);
                  if (k < 0)
                      return Result::failure (error);

                  const auto& preset = presets[(std::size_t) k];
                  if (preset.program >= 0)
                      instance.setCurrentProgram (preset.program);
                  else if (! pluginpresetui::load (instance, preset.file, error))
                      return Result::failure (error);
                  ctx.engine.markModified();

                  return Result::success (makeObject ({ { "channel", summary (ctx.song(), ch) },
                                                        { "plugin", instance.getName() },
                                                        { "preset", presetInfo (k + 1, preset) },
                                                        { "of", (int) presets.size() },
                                                        { "previous", before >= 0 ? juce::var (presets[(std::size_t) before].name) : juce::var() },
                                                        { "note", "a preset change isn't an undo step (project save stores it)" } }));
              });

    examples ({ { "list", "channel list" },
                { "get", "channel get --channel Kick" },
                { "instruments", "channel instruments --search synth" },
                { "add", "channel add --instrument Patcher --name \"FX Rack\"" },
                { "replace", "channel replace --channel Synth --instrument \"LYNX Soundfont\"" },
                { "sample", "channel sample --channel Kick --sample \"~/Music/Samples/Kick 02.wav\"" },
                { "delete", "channel delete --channel \"Open Hat\"" },
                { "params", "channel params --channel Lead --search cutoff" },
                { "param.set", "channel param set --channel Lead --param Cutoff --text \"2 kHz\"" },
                { "presets", "channel presets --channel Lead --search pad" },
                { "preset.set", "channel preset set --channel Lead --preset next" } });
}
} // namespace lynx::commands
