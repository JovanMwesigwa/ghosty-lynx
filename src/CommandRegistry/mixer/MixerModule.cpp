#include "CommandRegistry/mixer/MixerModule.h"

#include "CommandRegistry/shared/Catalog.h"
#include "CommandRegistry/shared/Knobs.h"
#include "CommandRegistry/shared/Lookup.h"
#include "core/MixerOps.h"
#include "core/ProjectContents.h"
#include "core/RackOps.h"
#include "core/Effects.h"
#include "core/Song.h"
#include "engine/AudioEngine.h"
#include "engine/PluginHost.h"
#include "ui/QuickPick.h"

#include <cmath>
#include <functional>
#include <optional>

namespace lynx::commands
{
namespace
{
constexpr double silentDb = -100.0; // (what a fader at the bottom, gain 0, reads as)
constexpr double maxDb = 5.6;       // the fader's top (maxFaderGain: the fader position is clamped to it)

double rounded1 (double v)
{
    return std::round (v * 10.0) / 10.0;
}

// linear gain → dB (the bottom: silentDb)
double gainToDb (double gain)
{
    return gain > 0.0 ? juce::jmax (silentDb, rounded1 (20.0 * std::log10 (gain))) : silentDb;
}

// dB → linear gain (silentDb and below: 0)
float dbToGain (double db)
{
    return db <= silentDb ? 0.0f : (float) std::pow (10.0, db / 20.0);
}

// The name the Mixer shows: its own, else "Master" / "Insert N".
juce::String insertName (const Song& song, int t)
{
    if (t >= 0 && t < (int) song.mixer.size())
        if (const auto& name = song.mixer[(std::size_t) t].name; ! name.empty())
            return juce::String::fromUTF8 (name.c_str());
    return t == 0 ? juce::String ("Master") : "Insert " + juce::String (t);
}

// --track / --from / --to: a number (0 = Master) or a name (any letter case). -1 with `error` otherwise.
int findInsert (const Song& song, const juce::String& text, juce::String& error)
{
    const auto wanted = text.trim();
    const auto count = (int) song.mixer.size();
    if (count == 0)
    {
        error = "this project has no mixer";
        return -1;
    }
    if (wanted.isNotEmpty() && wanted.containsOnly ("0123456789"))
    {
        const auto n = wanted.getIntValue();
        if (n < count)
            return n;
        error = "the mixer has tracks 0 to " + juce::String (count - 1) + " (0 = Master)";
        return -1;
    }

    int found = -1;
    for (int t = 0; t < count; ++t)
        if (insertName (song, t).equalsIgnoreCase (wanted))
        {
            if (found >= 0)
            {
                error = "two mixer tracks are named \"" + wanted + "\": use its number instead (mixer tracks)";
                return -1;
            }
            found = t;
        }
    if (found < 0)
        error = "no mixer track named \"" + wanted + "\" (mixer tracks shows them; 0 = Master)";
    return found;
}

juce::var trackRef (const Song& song, int t)
{
    return makeObject ({ { "track", t }, { "name", insertName (song, t) } });
}

// Where track `t` goes: Master (with its level) and its sends.
juce::Array<juce::var> routesOut (const Song& song, int t)
{
    juce::Array<juce::var> out;
    if (t == 0)
        return out; // (the Master goes to the output)
    const auto& m = song.mixer[(std::size_t) t];
    if (m.toMaster)
        out.add (makeObject ({ { "to", 0 }, { "name", insertName (song, 0) }, { "levelDb", gainToDb (m.masterLevel) } }));
    for (const auto& s : m.sends)
    {
        auto route = makeObject ({ { "to", s.target }, { "name", insertName (song, s.target) }, { "levelDb", gainToDb (s.level) } });
        if (s.level <= 0.0f) // (a level-0 send: a sidechain link, heard only by the target's compressor)
            route.getDynamicObject()->setProperty ("sidechain", true);
        out.add (route);
    }
    return out;
}

juce::Array<juce::var> channelNamesOn (const Song& song, int t)
{
    juce::Array<juce::var> names;
    for (auto c : mixerops::channelsOn (song, t))
        names.add (channelName (song, c));
    return names;
}

// ---- effect slots: 1..10 as the Mixer shows them (FL's slots are places: slot 5 can hold one with 1-4 empty) ------

// One effect: its slot, name, on / bypassed, mix (the slot's dry / wet knob, %), and a plugin's format.
juce::var slotInfo (const Song& song, int t, int index)
{
    const auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) index];
    const auto rows = mixerops::effectRows (song.mixer[(std::size_t) t].effects);
    auto out = makeObject ({ { "slot", rows[(std::size_t) index] + 1 },
                             { "name", juce::String::fromUTF8 (projectcontents::effectSlotName (fx).c_str()) },
                             { "enabled", fx.enabled },
                             { "mix", juce::roundToInt (fx.mix * 100.0f) } });
    if (fx.kind == EffectKind::plugin)
        out.getDynamicObject()->setProperty ("format", pluginFormatFromId (juce::String::fromUTF8 (fx.plugin.id.c_str())));
    if (fx.sidechain >= 0 && fx.sidechain < (int) song.mixer.size()) // (its detector hears that track: sidechained)
        out.getDynamicObject()->setProperty ("sidechainFrom", makeObject ({ { "track", fx.sidechain }, { "name", insertName (song, fx.sidechain) } }));
    return out;
}

juce::Array<juce::var> effectList (const Song& song, int t)
{
    juce::Array<juce::var> list;
    for (int i = 0; i < (int) song.mixer[(std::size_t) t].effects.size(); ++i)
        list.add (slotInfo (song, t, i));
    return list;
}

// The effects a slot can hold: exactly the Mixer slot menu's list (quickpick::effectEntries: LYNX's effects, FL's,
// the installed effect plugins), with the function that makes each, same order.
struct Effects
{
    std::vector<CatalogEntry> entries;
    std::vector<std::function<std::optional<EffectSlot>()>> make;
};

Effects effects (AudioEngine& engine)
{
    Effects out;
    for (const auto& e : quickpick::effectEntries (engine, true))
    {
        const auto id = juce::String::fromUTF8 (e.key.c_str());
        const juce::String source = e.folder == 3 ? "plugin" : (e.folder == 1 || e.flPlugin) ? "fl" : "lynx";
        out.entries.push_back ({ withoutRequirement (quickpick::shownName (engine, e.key, e.label), e.available), source, e.detail, id,
                                 e.available, source == "plugin" ? pluginFormatFromId (id) : juce::String() });
        out.make.push_back (e.make);
    }
    return out;
}

// --slot (1..10) on track `t`: the effect's index there, or -1 with `error` (out of range, an empty slot).
int effectAtSlot (const Song& song, int t, double slot, juce::String& error)
{
    const auto index = mixerops::effectAtRow (song.mixer[(std::size_t) t].effects, (int) slot - 1);
    if (index < 0)
        error = "slot " + juce::String ((int) slot) + " of " + insertName (song, t) + " is empty (mixer track --track " + juce::String (t)
                + " lists its effects)";
    return index;
}

juce::var summary (const Song& song, int t)
{
    const auto& m = song.mixer[(std::size_t) t];
    return makeObject ({ { "track", t },
                         { "name", insertName (song, t) },
                         { "volumeDb", gainToDb (faderGain (m.volume)) },
                         { "pan", juce::roundToInt (m.pan * 100.0f) },
                         { "muted", m.muted },
                         { "solo", m.solo },
                         { "channels", channelNamesOn (song, t) },
                         { "routes", routesOut (song, t) } });
}

juce::var details (const Song& song, int t)
{
    const auto& m = song.mixer[(std::size_t) t];
    auto out = summary (song, t);
    auto* o = out.getDynamicObject();

    juce::Array<juce::var> from; // the tracks that send into this one
    for (int s = 1; s < (int) song.mixer.size(); ++s)
        if (s != t && song.routes (s, t))
            from.add (makeObject ({ { "from", s }, { "name", insertName (song, s) }, { "levelDb", gainToDb (song.routeLevel (s, t)) } }));

    o->setProperty ("receivesFrom", from);
    o->setProperty ("stereo", juce::roundToInt (m.stereo * 100.0f));
    o->setProperty ("colour", colourText (m.colour));
    o->setProperty ("effects", effectList (song, t));
    return out;
}

const auto trackArg = [] (const char* name, const char* help) { return Arg::text (name, help); };

// ---- the knobs of an effect ----------------------------------------------------------------------------------------
// A built-in effect's knobs live in its slot (EffectSlot::params, 0..1, named by effectInfo, shown by formatEffectParam:
// undoable song edits). A plugin's live in the running plugin (its parameters; set the way its window does).

// The running plugin of slot `index` on track `t` (kept alive by `holder`), or nullptr with `error`.
PluginEffect* runningPlugin (Context& ctx, int t, int index, std::shared_ptr<Effect>& holder, juce::String& error)
{
    holder = ctx.engine.getEffect (t, index);
    auto* plugin = dynamic_cast<PluginEffect*> (holder.get());
    if (plugin == nullptr)
        error = "the plugin isn't running (still loading, or not installed / scanned): try again in a moment";
    else if (plugin->hasCrashed())
    {
        error = "this plugin crashed and was switched off: save the project and open it again to reload it";
        plugin = nullptr;
    }
    return plugin;
}

// Every knob of the effect in slot `index` of track `t`. False with `error` when its plugin isn't running.
bool knobsOf (Context& ctx, int t, int index, std::vector<Knob>& out, juce::String& error)
{
    const auto& song = ctx.song();
    const auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) index];
    if (fx.kind != EffectKind::plugin)
    {
        const auto& info = effectInfo (fx.kind);
        for (int p = 0; p < (int) info.params.size() && p < maxEffectParams; ++p)
            out.push_back ({ p, juce::String::fromUTF8 (info.params[(std::size_t) p].name),
                             juce::String::fromUTF8 (formatEffectParam (fx.kind, p, fx.params[(std::size_t) p], song.bpm, fx.params).c_str()),
                             fx.params[(std::size_t) p], info.params[(std::size_t) p].defaultValue });
        return true;
    }

    std::shared_ptr<Effect> holder;
    auto* plugin = runningPlugin (ctx, t, index, holder, error);
    if (plugin == nullptr)
        return false;
    out = pluginKnobs (plugin->getInstance());
    return true;
}

} // namespace

void MixerModule::define()
{
    query ("tracks", "The mixer tracks in use (0 = Master): level, pan, mute / solo, channels, routes",
           { Arg::boolean ("all", "every track, used or not").optional() },
           [] (Context& ctx, const Args& args) {
               const auto& song = ctx.song();
               juce::Array<juce::var> list;
               for (int t = 0; t < (int) song.mixer.size(); ++t)
                   if (args.boolean ("all") || song.insertInUse (t))
                       list.add (summary (song, t));
               return Result::success (makeObject ({ { "tracks", list }, { "total", (int) song.mixer.size() } }));
           });

    query ("track", "One mixer track: level, pan, stereo, mute / solo, its channels, routes in and out, effects",
           { trackArg ("track", "its number (0 = Master) or name") },
           [] (Context& ctx, const Args& args) {
               juce::String error;
               const auto t = findInsert (ctx.song(), args.text ("track"), error);
               if (t < 0)
                   return Result::failure (error);
               return Result::success (details (ctx.song(), t));
           });

    mutation ("track.set", "Change a mixer track: name, colour, volume (dB), pan, stereo, mute, solo (one undo step)",
              { trackArg ("track", "its number (0 = Master) or name"),
                Arg::text ("name", "a new name; empty = back to \"Insert N\"").optional(),
                Arg::text ("colour", "#rrggbb, or none for the default").optional(),
                Arg::number ("volume", "the fader in dB: 0 = unity, +5.6 = the top, -100 = silent", silentDb, maxDb).optional(),
                Arg::number ("pan", "-100 (left) to 100 (right), 0 = centre", -100, 100).optional(),
                Arg::number ("stereo", "stereo separation: -100 = widest, 0 = normal, 100 = mono", -100, 100).optional(),
                Arg::boolean ("muted", "mute or unmute it").optional(),
                Arg::boolean ("solo", "solo it or not (other solos stay)").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto t = findInsert (song, args.text ("track"), error);
                  if (t < 0)
                      return Result::failure (error);

                  juce::StringArray changed;
                  for (const auto* key : { "name", "colour", "volume", "pan", "stereo", "muted", "solo" })
                      if (args.has (key))
                          changed.add (key);
                  if (changed.isEmpty())
                      return Result::failure ("nothing to change: give --name, --colour, --volume, --pan, --stereo, --muted or --solo");

                  std::optional<std::uint32_t> colour;
                  if (args.has ("colour"))
                  {
                      colour = parseColour (args.text ("colour"));
                      if (! colour)
                          return Result::failure ("--colour: \"" + args.text ("colour") + "\" isn't a colour (use #rrggbb, e.g. #5b8def, or none)");
                  }
                  if (t == 0 && args.has ("solo"))
                      return Result::failure ("the Master can't be soloed (solo the tracks you want to hear)");

                  ctx.edit ([&] (Song& s) {
                      auto& m = s.mixer[(std::size_t) t];
                      if (args.has ("name"))
                          m.name = args.text ("name").trim().toStdString();
                      if (colour)
                          m.colour = *colour;
                      if (args.has ("volume"))
                          m.volume = faderPosition (dbToGain (args.number ("volume"))); // (the fader curve: 0 dB at 80 %)
                      if (args.has ("pan"))
                          m.pan = (float) (args.number ("pan") / 100.0);
                      if (args.has ("stereo"))
                          m.stereo = (float) (args.number ("stereo") / 100.0);
                      if (args.has ("muted"))
                          m.muted = args.boolean ("muted");
                      if (args.has ("solo"))
                          m.solo = args.boolean ("solo");
                  });

                  auto result = details (ctx.song(), t);
                  result.getDynamicObject()->setProperty ("changed", changed);
                  return Result::success (result);
              });

    mutation ("link", "Play a channel into a mixer track (--track N), or into the next free one (--free)",
              { Arg::text ("channel", "its name or number (channel list)"),
                Arg::number ("track", "the mixer track, 1 and up (0 = straight to Master)", 0, maxMixerTracks - 1).optional(),
                Arg::boolean ("free", "the next track nothing uses yet").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto channel = findChannel (song, args.text ("channel"), error);
                  if (channel < 0)
                      return Result::failure (error);
                  if (args.has ("track") == args.boolean ("free"))
                      return Result::failure ("give either --track N or --free");

                  const auto before = song.channels[(std::size_t) channel].mixerTrack;
                  if (args.boolean ("free"))
                      // the track nothing uses (no channel, route, effect, name, moved fader...: Song::insertInUse), as
                      // a new channel gets; a new one is added when every track is in use
                      ctx.edit ([&] (Song& s) { mixerops::routeChannel (s, channel, s.claimFreeInsert()); });
                  else
                      ctx.edit ([&] (Song& s) { mixerops::routeChannel (s, channel, (int) args.number ("track")); }); // (adds tracks up to it)

                  const auto& now = ctx.song();
                  const auto t = now.channels[(std::size_t) channel].mixerTrack;
                  return Result::success (makeObject ({ { "channel", channelName (now, channel) },
                                                        { "track", trackRef (now, t) },
                                                        { "previousTrack", trackRef (now, before) } }));
              });

    mutation ("route", "Send a mixer track into another (0 = Master), at a level; --only makes it the only destination",
              { trackArg ("from", "the track that sends (1 and up, or its name)"),
                trackArg ("to", "where it goes (0 = Master, or a name)"),
                Arg::number ("level", "the route's level in dB (default 0)", silentDb, maxDb).optional(),
                Arg::boolean ("only", "remove the track's other routes (Master included)").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto from = findInsert (song, args.text ("from"), error);
                  const auto to = from < 0 ? -1 : findInsert (song, args.text ("to"), error);
                  if (to < 0)
                      return Result::failure (error);
                  if (from == 0)
                      return Result::failure ("the Master goes to the audio output: it can't be routed");
                  if (from == to)
                      return Result::failure ("a track can't route into itself");
                  if (! song.routes (from, to) && ! song.canRoute (from, to))
                      return Result::failure (insertName (song, to) + " already sends into " + insertName (song, from)
                                              + " (directly or through others): this route would make a loop");

                  const auto level = dbToGain (args.number ("level", 0.0));
                  ctx.edit ([&] (Song& s) {
                      if (args.boolean ("only"))
                          mixerops::routeTo (s, { from }, to, true);
                      if (s.routes (from, to))
                          s.setRouteLevel (from, to, level);
                      else
                          s.setRoute (from, to, true, level);
                  });
                  return Result::success (details (ctx.song(), from));
              });

    mutation ("unroute", "Remove a route between two mixer tracks (0 = Master)",
              { trackArg ("from", "the track that sends (1 and up, or its name)"), trackArg ("to", "where it goes (0 = Master, or a name)") },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto from = findInsert (song, args.text ("from"), error);
                  const auto to = from < 0 ? -1 : findInsert (song, args.text ("to"), error);
                  if (to < 0)
                      return Result::failure (error);
                  if (! song.routes (from, to))
                      return Result::failure (insertName (song, from) + " doesn't route into " + insertName (song, to));

                  ctx.edit ([&] (Song& s) { s.setRoute (from, to, false); });
                  auto result = details (ctx.song(), from);
                  if (ctx.song().mixer[(std::size_t) from].sends.empty() && ! ctx.song().mixer[(std::size_t) from].toMaster)
                      result.getDynamicObject()->setProperty ("note", insertName (ctx.song(), from) + " has no route now: it's silent until you route it");
                  return Result::success (result);
              });

    // ---- effects in the slots -------------------------------------------------------------------------------------

    query ("effects", "What a mixer slot can hold: everything the slot menu offers (LYNX, FL's effects, your plugins)",
           { Arg::text ("search", "only names containing this (any letter case)").optional(),
             Arg::text ("source", "only one source: lynx, fl or plugin").optional(),
             Arg::text ("format", "only plugins of one format: vst3, au, clap or vst").optional() },
           [] (Context& ctx, const Args& args) {
               juce::String error;
               const auto list = catalogList (effects (ctx.engine).entries, args.text ("search"), args.text ("source"),
                                              { "lynx", "fl", "plugin" }, args.text ("format"), "effects", error);
               return error.isNotEmpty() ? Result::failure (error) : Result::success (list);
           });

    mutation ("effect.add", "Put an effect in a mixer track's slot (the first empty one, or --slot), as the slot menu does",
              { trackArg ("track", "its number (0 = Master) or name"),
                Arg::text ("effect", "a name from mixer effects (or its id)"),
                Arg::text ("format", "a plugin installed in several formats: vst3, au, clap or vst (default: VST3 first)").optional(),
                Arg::number ("slot", "which slot, 1 to 10 (default: the first empty one)", 1, maxEffectSlots).optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error, note;
                  const auto t = findInsert (song, args.text ("track"), error);
                  if (t < 0)
                      return Result::failure (error);
                  const auto& slots = song.mixer[(std::size_t) t].effects;
                  if ((int) slots.size() >= maxEffectSlots)
                      return Result::failure (insertName (song, t) + "'s " + juce::String (maxEffectSlots) + " slots are full (mixer effect remove frees one)");

                  // the slot: the one asked for (it must be empty), else the first empty one
                  int row = -1;
                  if (args.has ("slot"))
                  {
                      row = (int) args.number ("slot") - 1;
                      if (const auto there = mixerops::effectAtRow (slots, row); there >= 0)
                          return Result::failure ("slot " + juce::String (row + 1) + " already holds "
                                                  + juce::String::fromUTF8 (projectcontents::effectSlotName (slots[(std::size_t) there]).c_str())
                                                  + " (leave out --slot for the first empty one, or remove it first)");
                  }
                  else
                      for (int r = 0; r < maxEffectSlots && row < 0; ++r)
                          if (mixerops::effectAtRow (slots, r) < 0)
                              row = r;

                  // the effect (the slot menu's own list)
                  const auto list = effects (ctx.engine);
                  const auto index = findInCatalog (list.entries, args.text ("effect"), args.text ("format"), "effect", "mixer effects", error, note);
                  if (index < 0)
                      return Result::failure (error);
                  const auto& entry = list.entries[(std::size_t) index];
                  if (! entry.available)
                      return Result::failure (entry.name + " is one of FL Studio's effects that needs the data files of an FL Studio installation, which weren't found");
                  auto fx = list.make[(std::size_t) index]();
                  if (! fx)
                      return Result::failure (entry.name + " couldn't be made");

                  // as the slot menu: into that row (or after the others), then a new compressor hears the sidechain
                  int at = -1;
                  ctx.edit ([&] (Song& s) {
                      at = mixerops::insertEffectAtRow (s, t, *fx, row);
                      if (at < 0)
                      {
                          s.mixer[(std::size_t) t].effects.push_back (std::move (*fx));
                          at = (int) s.mixer[(std::size_t) t].effects.size() - 1;
                      }
                      mixerops::linkAddedSidechain (s, t, at);
                  });

                  auto result = makeObject ({ { "track", trackRef (ctx.song(), t) },
                                              { "added", slotInfo (ctx.song(), t, at) },
                                              { "effects", effectList (ctx.song(), t) } });
                  if (note.isNotEmpty())
                      result.getDynamicObject()->setProperty ("note", note);
                  return Result::success (result);
              });

    mutation ("effect.remove", "Take the effect out of a mixer slot (the slot stays empty; the others keep their places)",
              { trackArg ("track", "its number (0 = Master) or name"), Arg::number ("slot", "the slot, 1 to 10", 1, maxEffectSlots) },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto t = findInsert (song, args.text ("track"), error);
                  const auto index = t < 0 ? -1 : effectAtSlot (song, t, args.number ("slot"), error);
                  if (index < 0)
                      return Result::failure (error);

                  const auto removed = slotInfo (song, t, index);
                  // (as the slot menu's Delete: a structural edit, so the other plugins keep their running instances)
                  ctx.engine.editSongStructural ([t, index] (Song& s) { mixerops::removeEffectKeepRows (s, t, index); });
                  return Result::success (makeObject ({ { "track", trackRef (ctx.song(), t) }, { "removed", removed }, { "effects", effectList (ctx.song(), t) } }));
              });

    mutation ("effect.set", "Bypass a slot's effect or turn it back on, or set its mix (the slot's dry / wet knob)",
              { trackArg ("track", "its number (0 = Master) or name"),
                Arg::number ("slot", "the slot, 1 to 10", 1, maxEffectSlots),
                Arg::boolean ("enabled", "false = bypassed (muted), true = on").optional(),
                Arg::number ("mix", "dry / wet in %: 0 = only the dry signal, 100 = fully wet", 0, 100).optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto t = findInsert (song, args.text ("track"), error);
                  const auto index = t < 0 ? -1 : effectAtSlot (song, t, args.number ("slot"), error);
                  if (index < 0)
                      return Result::failure (error);
                  if (! args.has ("enabled") && ! args.has ("mix"))
                      return Result::failure ("nothing to change: give --enabled true|false or --mix 0-100");

                  ctx.edit ([&] (Song& s) {
                      auto& fx = s.mixer[(std::size_t) t].effects[(std::size_t) index];
                      if (args.has ("enabled"))
                          fx.enabled = args.boolean ("enabled");
                      if (args.has ("mix"))
                          fx.mix = (float) (args.number ("mix") / 100.0);
                  });
                  return Result::success (makeObject ({ { "track", trackRef (ctx.song(), t) }, { "effect", slotInfo (ctx.song(), t, index) } }));
              });

    // ---- sidechain ---------------------------------------------------------------------------------------------------

    mutation ("sidechain", "Sidechain a track into others (the Mixer's \"Sidechain to this track\"): their compressors duck to it",
              { trackArg ("from", "the track that triggers it (the kick's), number or name"),
                trackArg ("to", "the track(s) that duck: a number or name, or several: \"4,7\"") },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto from = findInsert (song, args.text ("from"), error);
                  if (from < 0)
                      return Result::failure (error);
                  if (from == 0)
                      return Result::failure ("the Master can't feed a sidechain (it plays last): use the track the kick is on");

                  // every --to, checked before anything changes
                  std::vector<int> targets;
                  for (const auto& part : juce::StringArray::fromTokens (args.text ("to"), ",", "\""))
                  {
                      if (part.trim().isEmpty())
                          continue;
                      const auto to = findInsert (song, part, error);
                      if (to < 0)
                          return Result::failure (error);
                      if (to == 0 || to == from)
                          return Result::failure (to == 0 ? "the Master can't be sidechained here: pick the tracks that should duck"
                                                          : "a track can't sidechain itself");
                      if (! song.routes (from, to) && ! song.canRoute (from, to))
                          return Result::failure (insertName (song, to) + " already sends into " + insertName (song, from)
                                                  + ": a sidechain from it would make a loop");
                      if (std::find (targets.begin(), targets.end(), to) == targets.end())
                          targets.push_back (to);
                  }
                  if (targets.empty())
                      return Result::failure ("--to is empty");

                  // the Mixer's own link: a level-0 send (so the trigger is processed first) and the compressors listen
                  std::vector<int> linked (targets.size(), 0);
                  ctx.edit ([&] (Song& s) {
                      for (std::size_t i = 0; i < targets.size(); ++i)
                          linked[i] = mixerops::sidechainTo (s, from, targets[i]);
                  });

                  juce::Array<juce::var> results;
                  juce::StringArray withoutCompressor;
                  for (std::size_t i = 0; i < targets.size(); ++i)
                  {
                      results.add (makeObject ({ { "track", trackRef (ctx.song(), targets[i]) },
                                                 { "listening", linked[i] },
                                                 { "effects", effectList (ctx.song(), targets[i]) } }));
                      if (linked[i] == 0)
                          withoutCompressor.add (juce::String (targets[i]));
                  }
                  auto result = makeObject ({ { "from", trackRef (ctx.song(), from) }, { "to", results } });
                  if (! withoutCompressor.isEmpty())
                      result.getDynamicObject()->setProperty (
                          "note", "track " + withoutCompressor.joinIntoString (", ") + " has no compressor to duck yet: add one (mixer effect add --track "
                                      + withoutCompressor[0] + " --effect Compressor) and it listens to " + insertName (ctx.song(), from) + " by itself");
                  return Result::success (result);
              });

    mutation ("unsidechain", "Undo a sidechain: the level-0 send goes, and the track's compressors stop listening to it",
              { trackArg ("from", "the track that triggers it"), trackArg ("to", "the track that ducks") },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto from = findInsert (song, args.text ("from"), error);
                  const auto to = from < 0 ? -1 : findInsert (song, args.text ("to"), error);
                  if (to < 0)
                      return Result::failure (error);

                  const auto& effectsOnTo = song.mixer[(std::size_t) to].effects;
                  const auto listening = std::any_of (effectsOnTo.begin(), effectsOnTo.end(), [from] (const EffectSlot& fx) { return fx.sidechain == from; });
                  const auto sidechainSend = song.routes (from, to) && song.routeLevel (from, to) <= 0.0f;
                  if (! listening && ! sidechainSend)
                      return Result::failure (insertName (song, to) + " isn't sidechained to " + insertName (song, from));

                  ctx.edit ([&] (Song& s) {
                      for (auto& fx : s.mixer[(std::size_t) to].effects)
                          if (fx.sidechain == from)
                              fx.sidechain = -1;
                      if (sidechainSend) // (only the silent link: a real route stays)
                          s.setRoute (from, to, false);
                  });
                  return Result::success (details (ctx.song(), to));
              });

    // ---- an effect's knobs -----------------------------------------------------------------------------------------

    query ("effect.params", "The knobs of the effect in a slot: number, name, value (0-1) and how it reads (\"-6.0 dB\")",
           { trackArg ("track", "its number (0 = Master) or name"),
             Arg::number ("slot", "the slot, 1 to 10", 1, maxEffectSlots),
             Arg::text ("search", "only knobs whose name contains this").optional() },
           [] (Context& ctx, const Args& args) {
               const auto& song = ctx.song();
               juce::String error;
               const auto t = findInsert (song, args.text ("track"), error);
               const auto index = t < 0 ? -1 : effectAtSlot (song, t, args.number ("slot"), error);
               std::vector<Knob> knobs;
               if (index < 0 || ! knobsOf (ctx, t, index, knobs, error))
                   return Result::failure (error);

               const auto search = args.text ("search").trim();
               juce::Array<juce::var> list;
               for (const auto& k : knobs)
                   if (search.isEmpty() || k.name.containsIgnoreCase (search))
                       list.add (knobInfo (k));
               return Result::success (makeObject ({ { "track", trackRef (song, t) },
                                                     { "effect", slotInfo (song, t, index) },
                                                     { "count", list.size() },
                                                     { "params", list } }));
           });

    mutation ("effect.param.set", "Turn one knob of the effect in a slot: to a --value (0-1) or to what it should read (--text \"-6 dB\")",
              { trackArg ("track", "its number (0 = Master) or name"),
                Arg::number ("slot", "the slot, 1 to 10", 1, maxEffectSlots),
                Arg::text ("param", "its number or name (mixer effect params lists them)"),
                Arg::number ("value", "0 to 1 (the knob's whole range)", 0, 1).optional(),
                Arg::text ("text", "what it should read: a number (\"-6\", \"-6 dB\", \"2.5k\") or a word (\"On\")").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto t = findInsert (song, args.text ("track"), error);
                  const auto index = t < 0 ? -1 : effectAtSlot (song, t, args.number ("slot"), error);
                  std::vector<Knob> knobs;
                  if (index < 0 || ! knobsOf (ctx, t, index, knobs, error))
                      return Result::failure (error);
                  if (args.has ("value") == args.has ("text"))
                      return Result::failure ("give either --value (0 to 1) or --text (e.g. \"-6 dB\")");
                  const auto at = findKnob (knobs, args.text ("param"), "mixer effect params", error);
                  if (at < 0)
                      return Result::failure (error);
                  const auto k = knobs[(std::size_t) at].index; // (the effect's own param number)

                  const auto& fx = song.mixer[(std::size_t) t].effects[(std::size_t) index];
                  const auto isPlugin = fx.kind == EffectKind::plugin;
                  std::shared_ptr<Effect> holder;
                  auto* plugin = isPlugin ? runningPlugin (ctx, t, index, holder, error) : nullptr;
                  if (isPlugin && plugin == nullptr)
                      return Result::failure (error);
                  auto* parameter = isPlugin ? plugin->getInstance().getParameters()[k] : nullptr;

                  // the value: given, or the one whose text reads closest to --text
                  auto value = (float) args.number ("value");
                  if (args.has ("text"))
                  {
                      const auto kind = fx.kind;
                      const auto params = fx.params;
                      const auto bpm = song.bpm;
                      const auto found = valueForText ([&] (float v) {
                          if (isPlugin)
                              return pluginText (*parameter, v);
                          auto context = params;
                          context[(std::size_t) k] = v;
                          return juce::String::fromUTF8 (formatEffectParam (kind, k, v, bpm, context).c_str());
                      }, args.text ("text"));
                      if (! found)
                          return Result::failure (knobs[(std::size_t) at].name + " never reads \"" + args.text ("text").trim() + "\" (it reads e.g. \""
                                                  + knobs[(std::size_t) at].text + "\")");
                      value = *found;
                  }

                  if (isPlugin)
                  {
                      // (as its window turns it; the plugin keeps it and a save stores it: not an undo step)
                      plugin->setParameterFromUi (k, value);
                      ctx.engine.markModified();
                  }
                  else
                  {
                      ctx.edit ([&] (Song& s) { s.mixer[(std::size_t) t].effects[(std::size_t) index].params[(std::size_t) k] = value; });
                  }

                  std::vector<Knob> after;
                  knobsOf (ctx, t, index, after, error);
                  juce::var turned;
                  for (const auto& a : after)
                      if (a.index == k)
                          turned = knobInfo (a);
                  auto result = makeObject ({ { "track", trackRef (ctx.song(), t) },
                                              { "effect", slotInfo (ctx.song(), t, index) },
                                              { "param", turned },
                                              { "previous", knobs[(std::size_t) at].text } });
                  if (isPlugin)
                      result.getDynamicObject()->setProperty ("note", "a plugin's knobs aren't undo steps (the plugin keeps them; project save stores them)");
                  return Result::success (result);
              });

    examples ({ { "tracks", "mixer tracks" },
                { "track", "mixer track --track 1" },
                { "track.set", "mixer track set --track 1 --name Drums --volume -3 --pan -20" },
                { "link", "mixer link --channel Kick --track 1" },
                { "route", "mixer route --from 1 --to 10 --level -6" },
                { "unroute", "mixer unroute --from 1 --to 10" },
                { "effects", "mixer effects --search reverb" },
                { "effect.add", "mixer effect add --track 7 --effect \"Reeverb 2\" --slot 1" },
                { "effect.remove", "mixer effect remove --track 7 --slot 1" },
                { "effect.set", "mixer effect set --track 7 --slot 1 --mix 35" },
                { "sidechain", "mixer sidechain --from 1 --to \"4,7\"" },
                { "unsidechain", "mixer unsidechain --from 1 --to 4" },
                { "effect.params", "mixer effect params --track 7 --slot 1 --search decay" },
                { "effect.param.set", "mixer effect param set --track 7 --slot 1 --param Decay --text \"2.5 s\"" } });
}
} // namespace lynx::commands
