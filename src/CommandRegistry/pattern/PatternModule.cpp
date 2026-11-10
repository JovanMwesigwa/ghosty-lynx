#include "CommandRegistry/pattern/PatternModule.h"

#include "CommandRegistry/shared/Lookup.h"
#include "core/NoteNames.h"
#include "core/RackOps.h"
#include "core/Song.h"
#include "ui/ChannelRackComponent.h"

#include <cmath>
#include <optional>
#include <set>
#include <vector>

namespace lynx::commands
{
namespace
{
std::int64_t barTicks (const Song& song)
{
    return (std::int64_t) ppq * std::max (1, song.beatsPerBar);
}

double rounded (double v)
{
    return std::round (v * 1000.0) / 1000.0;
}

// A position inside a pattern, from its start: bar 1, beat 1 is the first tick.
juce::var patternPosition (const Song& song, std::int64_t tick)
{
    const auto bar = barTicks (song);
    return makeObject ({ { "bar", (int) (tick / bar) + 1 }, { "beat", rounded (1.0 + (double) (tick % bar) / ppq) }, { "tick", (juce::int64) tick } });
}

juce::var summary (const Song& song, int index)
{
    const auto& p = song.patterns[(std::size_t) index];
    std::set<int> used;
    for (const auto& n : p.notes)
        used.insert (n.channel);
    juce::Array<juce::var> channels;
    for (auto c : used)
        channels.add (channelName (song, c));

    return makeObject ({ { "pattern", index + 1 },
                         { "name", patternName (song, index) },
                         { "notes", (int) p.notes.size() },
                         { "channels", channels },
                         { "lengthBars", rounded ((double) p.effectiveLength (song.beatsPerBar) / (double) barTicks (song)) },
                         { "fixedLength", p.lengthTicks > 0 },
                         { "current", index == song.currentPattern } });
}

juce::var describeNote (const Song& song, const Note& n)
{
    return makeObject ({ { "channel", channelName (song, n.channel) },
                         { "key", juce::String (notenames::keyName (n.key)) }, // as the Piano Roll names it (FL: 60 = C5)
                         { "midi", n.key },
                         { "start", patternPosition (song, n.start) },
                         { "lengthBeats", rounded ((double) n.length / ppq) },
                         { "velocity", rounded (n.velocity) },
                         { "pan", rounded (n.pan) },
                         { "muted", n.muted },
                         { "slide", n.slide } });
}

// --pattern (a name or number), or the current pattern when it isn't given. -1 with `error` otherwise.
int patternFromArgs (const Song& song, const Args& args, juce::String& error)
{
    if (song.patterns.empty())
    {
        error = "the project has no patterns yet (pattern new makes one)";
        return -1;
    }
    return args.has ("pattern") ? findPattern (song, args.text ("pattern"), error) : song.currentPattern;
}

const auto patternArg = [] { return Arg::text ("pattern", "its name or number (pattern list); default: the current one").optional(); };
const auto channelArg = [] { return Arg::text ("channel", "its name or number (channel list)"); };

// "x...x...x...x..." → on / off per step. x X o 1 = a hit; . - _ 0 = a rest; spaces and | only separate.
// nullopt (with the bad character in `error`) when something else is in it.
std::optional<std::vector<bool>> parseSteps (const juce::String& text, juce::String& error)
{
    std::vector<bool> steps;
    for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (c == 'x' || c == 'X' || c == 'o' || c == '1')
            steps.push_back (true);
        else if (c == '.' || c == '-' || c == '_' || c == '0')
            steps.push_back (false);
        else if (c != ' ' && c != '|')
        {
            error = "--steps: \"" + juce::String::charToString (c) + "\" isn't a step (x = hit, . = rest; spaces and | to separate)";
            return std::nullopt;
        }
    }
    if (steps.empty())
        error = "--steps is empty (e.g. \"x...x...x...x...\")";
    return steps.empty() ? std::nullopt : std::optional (steps);
}

// A channel's steps from `first` on, as a grid string grouped by beat: "x... x... x... x..."
juce::String stepGrid (const Pattern& pattern, int channel, int first, int count)
{
    juce::String grid;
    for (int i = 0; i < count; ++i)
    {
        if (i > 0 && i % stepsPerBeat == 0)
            grid << ' ';
        grid << (pattern.hasStep (channel, first + i) ? "x" : ".");
    }
    return grid;
}
} // namespace

void PatternModule::define()
{
    query ("list", "Every pattern: number, name, notes, the channels it plays, length", [] (Context& ctx, const Args&) {
        const auto& song = ctx.song();
        juce::Array<juce::var> list;
        for (int i = 0; i < (int) song.patterns.size(); ++i)
            list.add (summary (song, i));
        return Result::success (makeObject ({ { "current", song.patterns.empty() ? juce::var() : juce::var (song.currentPattern + 1) },
                                              { "patterns", list } }));
    });

    mutation ("new", "A new empty pattern, made current (like the toolbar's New pattern button)",
              { Arg::text ("name", "its name; default \"Pattern N\"").optional(),
                Arg::number ("bars", "a fixed length in bars; default: follows its notes", 1, 512).optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  auto name = args.text ("name").trim();

                  // names are how commands find patterns: keep them unique
                  if (name.isNotEmpty())
                  {
                      juce::String ignored;
                      if (findPattern (song, name, ignored) >= 0 || name.containsOnly ("0123456789"))
                          return Result::failure (name.containsOnly ("0123456789") ? "a pattern name can't be just a number (numbers pick patterns by position)"
                                                                                   : "there's already a pattern named \"" + name + "\"");
                  }

                  const auto lengthTicks = args.has ("bars") ? (std::int64_t) std::llround (args.number ("bars") * (double) barTicks (song)) : 0;
                  ctx.edit ([&] (Song& s) {
                      Pattern p;
                      p.name = name.isNotEmpty() ? name.toStdString() : newPatternName (s);
                      p.lengthTicks = lengthTicks;
                      s.patterns.push_back (std::move (p));
                      s.currentPattern = (int) s.patterns.size() - 1; // (as the button: the Channel Rack and Piano Roll show it)
                  });

                  return Result::success (summary (ctx.song(), (int) ctx.song().patterns.size() - 1));
              });

    query ("notes", "The notes in a pattern, in time order (default: the current pattern)",
           { Arg::text ("pattern", "its name or number (pattern list); default: the current one").optional() },
           [] (Context& ctx, const Args& args) {
               const auto& song = ctx.song();
               if (song.patterns.empty())
                   return Result::failure ("the project has no patterns yet (pattern new makes one)");

               auto index = song.currentPattern;
               if (args.has ("pattern"))
               {
                   juce::String error;
                   index = findPattern (song, args.text ("pattern"), error);
                   if (index < 0)
                       return Result::failure (error);
               }

               const auto& p = song.patterns[(std::size_t) index];
               juce::Array<juce::var> notes;
               for (const auto& n : p.notes) // (kept sorted by start)
                   notes.add (describeNote (song, n));

               auto result = summary (song, index);
               result.getDynamicObject()->setProperty ("noteList", notes);
               return Result::success (result);
           });

    mutation ("note.add", "Add one note to a pattern (like drawing it in the Piano Roll)",
              { patternArg(),
                channelArg(),
                Arg::text ("key", "C5 = middle C (LYNX/FL naming; Ableton's C3), F#3, Bb4, or a MIDI number"),
                Arg::number ("bar", "the bar it starts in, from 1 (default 1)", 1, 512).optional(),
                Arg::number ("beat", "the beat in that bar, from 1; 1.5 = halfway through beat 1 (default 1)", 1, 17).optional(),
                Arg::number ("length", "length in beats: 1 = a quarter note, 0.25 = a 16th (default 0.25)", 1.0 / 64.0, 1024).optional(),
                Arg::number ("velocity", "0 to 1 (default 0.78, FL's)", 0, 1).optional(),
                Arg::boolean ("slide", "a slide note: glides from the sounding note").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto pattern = patternFromArgs (song, args, error);
                  const auto channel = pattern < 0 ? -1 : findChannel (song, args.text ("channel"), error);
                  const auto key = channel < 0 ? -1 : parseKey (args.text ("key"), error);
                  if (key < 0)
                      return Result::failure (error);

                  const auto beat = args.number ("beat", 1.0);
                  if (beat >= song.beatsPerBar + 1)
                      return Result::failure ("--beat " + juce::String (beat) + ": a bar has " + juce::String (song.beatsPerBar) + " beats (1 to "
                                              + juce::String (song.beatsPerBar) + ".x)");

                  Note n;
                  n.channel = channel;
                  n.key = key;
                  n.start = (std::int64_t) std::llround ((args.number ("bar", 1.0) - 1.0) * (double) barTicks (song) + (beat - 1.0) * ppq);
                  n.length = std::max<std::int64_t> (1, (std::int64_t) std::llround (args.number ("length", 0.25) * ppq));
                  n.velocity = (float) args.number ("velocity", n.velocity);
                  n.slide = args.boolean ("slide");

                  if (const auto fixed = song.patterns[(std::size_t) pattern].lengthTicks; fixed > 0 && n.start >= fixed)
                      return Result::failure ("that's after the end of " + patternName (song, pattern) + " (it's "
                                              + juce::String ((double) fixed / (double) barTicks (song)) + " bars long)");

                  ctx.edit ([&] (Song& s) {
                      auto& p = s.patterns[(std::size_t) pattern];
                      p.notes.push_back (n);
                      p.sortNotes();
                  });

                  auto result = summary (ctx.song(), pattern);
                  result.getDynamicObject()->setProperty ("added", describeNote (ctx.song(), n));
                  return Result::success (result);
              });

    mutation ("steps", "Set a channel's steps in a pattern from a grid: x = hit, . = rest (like clicking the Channel Rack)",
              { patternArg(),
                channelArg(),
                Arg::text ("steps", "one character per 16th step, e.g. \"x...x...x...x...\" (spaces and | just separate)"),
                Arg::number ("bar", "the bar the grid starts at, from 1 (default 1)", 1, 512).optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto pattern = patternFromArgs (song, args, error);
                  const auto channel = pattern < 0 ? -1 : findChannel (song, args.text ("channel"), error);
                  std::optional<std::vector<bool>> steps;
                  if (channel >= 0)
                      steps = parseSteps (args.text ("steps"), error);
                  if (! steps)
                      return Result::failure (error);

                  const auto stepsPerBar = stepsPerBeat * std::max (1, song.beatsPerBar);
                  const auto first = ((int) args.number ("bar", 1.0) - 1) * stepsPerBar;
                  const auto count = (int) steps->size();
                  if (const auto fixed = song.patterns[(std::size_t) pattern].lengthTicks; fixed > 0 && (std::int64_t) (first + count) * ticksPerStep > fixed)
                      return Result::failure ("the grid runs past the end of " + patternName (song, pattern) + " ("
                                              + juce::String (fixed / ticksPerStep) + " steps long)");

                  // each step on or off, the way the Channel Rack's step buttons do (its "Mute removed steps" option too):
                  // only the steps the grid covers change, the channel's other notes stay
                  int added = 0, removed = 0;
                  ctx.edit ([&] (Song& s) {
                      auto& p = s.patterns[(std::size_t) pattern];
                      for (int i = 0; i < count; ++i)
                          if (p.hasStep (channel, first + i) != (*steps)[(std::size_t) i])
                          {
                              ++((*steps)[(std::size_t) i] ? added : removed);
                              p.toggleStep (channel, first + i, ChannelRackComponent::muteRemovedSteps);
                          }
                  });

                  auto result = summary (ctx.song(), pattern);
                  auto* o = result.getDynamicObject();
                  o->setProperty ("channel", channelName (ctx.song(), channel));
                  o->setProperty ("grid", stepGrid (ctx.song().patterns[(std::size_t) pattern], channel, first, count));
                  o->setProperty ("added", added);
                  o->setProperty ("removed", removed);
                  return Result::success (result);
              });

    mutation ("split", "One new pattern per channel in a pattern, \"<pattern> - <channel>\" (the Pattern menu's Split by channel)",
              { patternArg() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto index = patternFromArgs (song, args, error);
                  if (index < 0)
                      return Result::failure (error);

                  // (the new names must stay unique: commands find patterns by name)
                  std::set<int> channels;
                  for (const auto& n : song.patterns[(std::size_t) index].notes)
                      channels.insert (n.channel);
                  if (channels.size() < 2)
                      return Result::failure (patternName (song, index) + " plays " + (channels.empty() ? "no channel" : "one channel")
                                              + ": there's nothing to split");
                  for (auto c : channels)
                  {
                      juce::String ignored;
                      if (const auto name = patternName (song, index) + " - " + channelName (song, c); findPattern (song, name, ignored) >= 0)
                          return Result::failure ("there's already a pattern named \"" + name + "\" (split before?)");
                  }

                  int created = 0;
                  ctx.edit ([&] (Song& s) { created = splitPatternByChannel (s, index); });

                  // the new ones sit right after it
                  juce::Array<juce::var> list;
                  for (int i = index + 1; i <= index + created; ++i)
                      list.add (summary (ctx.song(), i));
                  return Result::success (makeObject ({ { "from", patternName (ctx.song(), index) }, { "created", list } }));
              });

    examples ({ { "split", "pattern split --pattern Chords" } });
    examples ({ { "list", "pattern list" },
                { "new", "pattern new --name Bassline --bars 4" },
                { "notes", "pattern notes --pattern Bassline" },
                { "note.add", "pattern note add --pattern Bassline --channel Synth --key A3 --bar 1 --beat 1 --length 1" },
                { "steps", "pattern steps --pattern Beat --channel Kick --steps \"x...x...x...x...\"" } });
}
} // namespace lynx::commands
