#include "CommandRegistry/playlist/PlaylistModule.h"

#include "CommandRegistry/shared/Lookup.h"

#include "core/EditorPolish.h"
#include "core/NoteEdit.h"
#include "core/Performance.h"
#include "core/PlaylistTrackOps.h"
#include "core/QuickRender.h"
#include "core/Song.h"
#include "engine/AudioEngine.h"
#include "ui/PlaylistComponent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>

namespace lynx::commands
{
namespace
{
// ---- reading ----------------------------------------------------------------------------------------------------

// The header as stored, or the default one for a track that was never touched.
const PlaylistTrack& header (const Song& song, int track)
{
    static const PlaylistTrack untouched {};
    return track < (int) song.playlistTracks.size() ? song.playlistTracks[(std::size_t) track] : untouched;
}

// The name the Playlist shows: the track's own, or "Track N" (N from 1).
juce::String trackName (const Song& song, int track)
{
    if (const auto& name = header (song, track).name; ! name.empty())
        return juce::String::fromUTF8 (name.c_str());
    return "Track " + juce::String (track + 1);
}

// 0xFFRRGGBB → "#rrggbb"; 0 (the default colour) → null
// A song position as people read it: bar 5, beat 1 (beats count from 1, fractions for off-beat starts).
juce::var position (const Song& song, std::int64_t tick)
{
    const auto bar = song.barAt (tick);
    const auto beat = 1.0 + (double) (tick - bar.tick) / (double) ppq;
    return makeObject ({ { "bar", bar.number }, { "beat", std::round (beat * 1000.0) / 1000.0 }, { "tick", (juce::int64) tick } });
}

const char* kindName (ClipKind kind)
{
    switch (kind)
    {
        case ClipKind::pattern: return "pattern";
        case ClipKind::audio: return "audio";
        case ClipKind::automation: return "automation";
    }
    return "?";
}

// What the clip plays, by name: the pattern, the audio channel (or its file), the automation.
juce::String clipSource (const Song& song, const Clip& clip)
{
    switch (clip.kind)
    {
        case ClipKind::pattern:
            return juce::String::fromUTF8 (quickrender::patternName (song, clip.pattern).c_str());
        case ClipKind::audio:
            if (clip.channel >= 0 && clip.channel < (int) song.channels.size())
            {
                const auto& channel = song.channels[(std::size_t) clip.channel];
                if (! channel.name.empty())
                    return juce::String::fromUTF8 (channel.name.c_str());
                return juce::File (juce::String::fromUTF8 (channel.samplePath.c_str())).getFileName();
            }
            return "?";
        case ClipKind::automation:
            if (clip.automation >= 0 && clip.automation < (int) song.automations.size())
                if (const auto& name = song.automations[(std::size_t) clip.automation].name; ! name.empty())
                    return juce::String::fromUTF8 (name.c_str());
            return "Automation " + juce::String (clip.automation + 1);
    }
    return "?";
}

juce::var describeClip (const Song& song, int index)
{
    const auto& clip = song.playlist[(std::size_t) index];
    auto out = makeObject ({ { "index", index }, // (its place in the arrangement's clip list: for clip commands later)
                             { "kind", kindName (clip.kind) },
                             { "source", clipSource (song, clip) },
                             { "start", position (song, clip.start) },
                             { "end", position (song, clip.end()) },
                             { "lengthBeats", std::round ((double) clip.length / ppq * 1000.0) / 1000.0 },
                             { "muted", clip.muted } });
    if (clip.kind == ClipKind::audio)
    {
        auto* o = out.getDynamicObject();
        const auto db = clip.gain > 0.0f ? std::max (-96.0, 20.0 * std::log10 ((double) clip.gain)) : -96.0;
        o->setProperty ("gainDb", std::round (db * 10.0) / 10.0);
        o->setProperty ("pan", clip.pan);
        o->setProperty ("pitch", clip.pitch);
        o->setProperty ("reversed", clip.reversed);
        o->setProperty ("stretch", clip.stretch);
        o->setProperty ("fadeInBeats", (double) clip.fadeIn / ppq);
        o->setProperty ("fadeOutBeats", (double) clip.fadeOut / ppq);
    }
    return out;
}

juce::var describeTrack (const Song& song, int track)
{
    const auto& h = header (song, track);
    const auto groupHeader = noteedit::groupHeaderOf (song, track);

    juce::var mode = "unassigned", linkedChannel, linkedMixer;
    if (h.mode == PlaylistTrackMode::audio)
        mode = "audio";
    else if (h.mode == PlaylistTrackMode::instrument)
        mode = "instrument";
    if (h.mixerTrack >= 0)
        linkedMixer = h.mixerTrack;
    if (h.channel >= 0 && h.channel < (int) song.channels.size())
        linkedChannel = juce::String::fromUTF8 (song.channels[(std::size_t) h.channel].name.c_str());

    // its clips, in time order
    std::vector<int> clips;
    for (int i = 0; i < (int) song.playlist.size(); ++i)
        if (song.playlist[(std::size_t) i].track == track)
            clips.push_back (i);
    std::stable_sort (clips.begin(), clips.end(), [&] (int a, int b) { return song.playlist[(std::size_t) a].start < song.playlist[(std::size_t) b].start; });
    juce::Array<juce::var> clipList;
    for (auto i : clips)
        clipList.add (describeClip (song, i));

    return makeObject ({ { "track", track + 1 },
                         { "name", trackName (song, track) },
                         { "colour", colourText (h.colour) },
                         { "muted", h.muted },
                         { "height", h.height > 0 ? polish::clampTrackHeight (h.height) : polish::defaultTrackHeight },
                         { "grouped", h.grouped && track > 0 },
                         { "groupHeader", groupHeader >= 0 ? juce::var (groupHeader + 1) : juce::var() },
                         { "collapsed", groupHeader >= 0 && header (song, groupHeader).collapsed },
                         { "mode", mode },
                         { "mixerTrack", linkedMixer },
                         { "channel", linkedChannel },
                         { "clips", clipList } });
}

// ---- changing ---------------------------------------------------------------------------------------------------

const juce::String trackArgHelp = "the track, from 1 (as the Playlist numbers them)";

// The tick bar `bar` (from 1) starts at: the bars are walked from the start, so time signature markers count.
std::int64_t barStartTick (const Song& song, int bar)
{
    const auto longest = (std::int64_t) ppq * 16; // (no bar is longer than 16 beats)
    for (const auto& b : song.barsBetween (0, (std::int64_t) bar * longest))
        if (b.number == bar)
            return b.tick;
    return (std::int64_t) (bar - 1) * ppq * std::max (1, song.beatsPerBar);
}

std::int64_t barTicks (const Song& song)
{
    return (std::int64_t) ppq * std::max (1, song.beatsPerBar);
}

juce::String markerKind (const Marker& m)
{
    if (m.isSignature())
        return "time signature " + juce::String (m.numerator) + "/" + juce::String (m.denominator);
    switch (m.action)
    {
        case Marker::none: return "section";
        case Marker::markerLoop: return "loop";
        case Marker::markerSkip: return "skip";
        case Marker::markerPause: return "pause";
        case Marker::songLoop: return "song loop";
        case Marker::start: return "start";
        case Marker::punchIn: return "start recording";
        case Marker::punchOut: return "stop recording";
        default: return "marker";
    }
}

juce::var markerList (const Song& song)
{
    juce::Array<juce::var> list;
    for (const auto& m : song.markers())
    {
        auto entry = makeObject ({ { "name", juce::String::fromUTF8 (m.name.c_str()) }, { "kind", markerKind (m) } });
        auto* o = entry.getDynamicObject();
        const auto at = position (song, m.tick);
        o->setProperty ("bar", at["bar"]);
        o->setProperty ("beat", at["beat"]);
        list.add (entry);
    }
    return list;
}
} // namespace

void PlaylistModule::define()
{
    query ("tracks", "The tracks of the current arrangement that hold something", [] (Context& ctx, const Args&) {
        const auto& song = ctx.song();

        // Tracks have no list of their own: a track "exists" once it has clips (Song::playlist, one flat list where
        // each clip names its track) or a header that differs from the default.
        std::map<int, int> clipsPerTrack; // track (0-based) → clips; std::map keeps them in order
        for (const auto& clip : song.playlist)
            if (clip.track >= 0)
                ++clipsPerTrack[clip.track];
        for (int t = 0; t < (int) song.playlistTracks.size(); ++t)
            if (! (song.playlistTracks[(std::size_t) t] == PlaylistTrack {}))
                clipsPerTrack.try_emplace (t, 0);

        juce::Array<juce::var> tracks;
        for (const auto& [track, clips] : clipsPerTrack)
            tracks.add (makeObject ({ { "track", track + 1 },
                                      { "name", trackName (song, track) },
                                      { "muted", song.isTrackMuted (track) },
                                      { "clips", clips } }));

        const auto& arrangement = song.arrangements[(std::size_t) song.currentArrangement];
        return Result::success (makeObject ({ { "arrangement", juce::String::fromUTF8 (arrangement.name.c_str()) }, { "tracks", tracks } }));
    });

    query ("track", "One track: name, colour, mute, height, group, mode and its clips",
           { Arg::number ("track", trackArgHelp, 1, perf::maxTracks) },
           [] (Context& ctx, const Args& args) {
               return Result::success (describeTrack (ctx.song(), (int) args.number ("track") - 1));
           });

    mutation ("track.set", "Change a track's header: name, colour, mute, height, grouping (one undo step)",
              { Arg::number ("track", trackArgHelp, 1, perf::maxTracks),
                Arg::text ("name", "the new name; empty = back to \"Track N\"").optional(),
                Arg::text ("colour", "#rrggbb, or none for the default colour").optional(),
                Arg::boolean ("muted", "mute or unmute the whole track").optional(),
                Arg::number ("height", "row height in px (38 = default)", polish::minTrackHeight, polish::maxTrackHeight).optional(),
                Arg::boolean ("grouped", "group with the track above (FL's \"Group with above track\") or leave the group").optional(),
                Arg::boolean ("collapsed", "fold or unfold the track's group").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto track = (int) args.number ("track") - 1;
                  const auto& song = ctx.song();

                  // ---- check everything before changing anything (all or nothing, one undo step)
                  juce::StringArray changed;
                  for (const auto* key : { "name", "colour", "muted", "height", "grouped", "collapsed" })
                      if (args.has (key))
                          changed.add (key);
                  if (changed.isEmpty())
                      return Result::failure ("nothing to change: give --name, --colour, --muted, --height, --grouped or --collapsed");

                  std::optional<std::uint32_t> colour;
                  if (args.has ("colour"))
                  {
                      colour = parseColour (args.text ("colour"));
                      if (! colour)
                          return Result::failure ("--colour: \"" + args.text ("colour") + "\" isn't a colour (use #rrggbb, e.g. #d9534f, or none)");
                  }
                  if (args.boolean ("grouped") && track == 0)
                      return Result::failure ("track 1 has no track above it to group with");
                  const auto willBeGrouped = args.has ("grouped") ? args.boolean ("grouped") : noteedit::groupHeaderOf (song, track) >= 0;
                  if (args.has ("collapsed") && ! willBeGrouped && noteedit::groupHeaderOf (song, track) < 0)
                      return Result::failure ("track " + juce::String (track + 1) + " isn't in a group: nothing to collapse");

                  // ---- the change, the way the Playlist's own menus make it
                  ctx.edit ([&] (Song& s) {
                      if ((int) s.playlistTracks.size() <= track)
                          s.playlistTracks.resize ((std::size_t) track + 1);
                      {
                          auto& h = s.playlistTracks[(std::size_t) track];
                          if (args.has ("name"))
                              h.name = args.text ("name").trim().toStdString();
                          if (colour)
                              h.colour = *colour;
                          if (args.has ("muted"))
                              h.muted = args.boolean ("muted");
                          if (args.has ("height"))
                          {
                              const auto px = (int) std::lround (args.number ("height"));
                              h.height = px == polish::defaultTrackHeight ? 0 : polish::clampTrackHeight (px);
                              pltracks::unlockSize (h); // (a new size unlocks "Lock to this size", as the Size menu does)
                          }
                      }
                      // (the group helpers may touch other tracks' headers: after the header above, not through `h`)
                      if (args.has ("grouped"))
                      {
                          if (args.boolean ("grouped"))
                              noteedit::groupWithAbove (s, track);
                          else
                              noteedit::ungroupTrack (s, track);
                      }
                      if (args.has ("collapsed"))
                          noteedit::setGroupCollapsed (s, track, args.boolean ("collapsed"));
                  });

                  auto result = describeTrack (ctx.song(), track);
                  result.getDynamicObject()->setProperty ("changed", changed);
                  return Result::success (result);
              });

    // ---- clips ----------------------------------------------------------------------------------------------------

    mutation ("clip.add", "Place a pattern on a track from a bar (a pattern clip); --repeat lays copies end to end",
              { Arg::number ("track", trackArgHelp, 1, perf::maxTracks),
                Arg::text ("pattern", "its name or number (pattern list)"),
                Arg::number ("bar", "the bar it starts at, from 1 (default 1)", 1, 9999).optional(),
                Arg::number ("bars", "each clip's length in bars; default: the pattern's own length", 0.25, 512).optional(),
                Arg::number ("repeat", "how many copies, end to end (default 1)", 1, 256).optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  juce::String error;
                  const auto pattern = findPattern (song, args.text ("pattern"), error);
                  if (pattern < 0)
                      return Result::failure (error);

                  const auto track = (int) args.number ("track") - 1;
                  const auto start = barStartTick (song, (int) args.number ("bar", 1.0));
                  const auto length = args.has ("bars") ? (std::int64_t) std::llround (args.number ("bars") * (double) barTicks (song))
                                                        : song.patterns[(std::size_t) pattern].effectiveLength (song.beatsPerBar);
                  const auto copies = (int) args.number ("repeat", 1.0);

                  // the clips the Playlist's brush makes: the pattern, from `start`, `length` long (one undo step for all)
                  ctx.edit ([&] (Song& s) {
                      for (int i = 0; i < copies; ++i)
                      {
                          Clip c;
                          c.kind = ClipKind::pattern;
                          c.pattern = pattern;
                          c.track = track;
                          c.start = start + (std::int64_t) i * length;
                          c.length = length;
                          s.playlist.push_back (c);
                      }
                  });

                  const auto& now = ctx.song();
                  return Result::success (makeObject ({ { "track", track + 1 },
                                                        { "trackName", trackName (now, track) },
                                                        { "pattern", patternName (now, pattern) },
                                                        { "clips", copies },
                                                        { "from", position (now, start) },
                                                        { "to", position (now, start + (std::int64_t) copies * length) } }));
              });

    mutation ("audio.add", "Place a sample on the arrangement as an audio clip (a crash, an impact, a riser...): at a bar or a section marker; "
                           "--ending lines its end up there instead (a swell into the drop)",
              { Arg::text ("sample", "the sample: a path from sample search (absolute or ~/...)"),
                Arg::number ("bar", "the bar, from 1 (or --at)", 1, 9999).optional(),
                Arg::number ("beat", "the beat in that bar, from 1 (default 1; 2.5 = halfway through beat 2)", 1, 17).optional(),
                Arg::text ("at", "a section marker's name instead of --bar: its start (playlist markers lists them)").optional(),
                Arg::boolean ("ending", "the sample ends at that point instead of starting there (risers, reverse cymbals, swells)").optional(),
                Arg::number ("track", trackArgHelp + "; default: the first track with room that holds no patterns", 1, perf::maxTracks).optional(),
                Arg::number ("gain", "the clip's volume in dB (default 0)", -60, 24).optional(),
                Arg::number ("fadein", "fade in, in beats (default none)", 0, 256).optional(),
                Arg::number ("fadeout", "fade out, in beats (default none)", 0, 256).optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();

                  // the sample (read the way a file dropped on the Playlist is)
                  const auto file = pathFromArg (args.text ("sample"));
                  if (file == juce::File())
                      return Result::failure ("--sample must be an absolute path or start with ~/ (got \"" + args.text ("sample") + "\")");
                  if (! file.existsAsFile())
                      return Result::failure ("no file at " + file.getFullPathName() + " (sample search finds samples)");
                  const auto length = PlaylistComponent::audioClipTicks (ctx.engine, file);
                  if (length <= 0)
                      return Result::failure ("couldn't read " + file.getFileName() + " as audio (WAV, AIFF, FLAC, OGG, MP3...)");

                  // where: a bar (and beat), or a section marker
                  std::int64_t point = 0;
                  if (args.has ("at"))
                  {
                      if (args.has ("bar"))
                          return Result::failure ("give --bar or --at, not both");
                      const auto wanted = args.text ("at").trim();
                      const Marker* found = nullptr;
                      for (const auto& m : song.markers())
                          if (! m.isSignature() && juce::String::fromUTF8 (m.name.c_str()).trim().equalsIgnoreCase (wanted))
                              found = &m;
                      if (found == nullptr)
                          return Result::failure ("no section marker named \"" + wanted + "\" (playlist markers lists them; playlist marker add makes one)");
                      point = found->tick;
                  }
                  else if (args.has ("bar"))
                  {
                      point = barStartTick (song, (int) args.number ("bar"));
                  }
                  else
                  {
                      return Result::failure ("say where: --bar 17 (and --beat), or --at Chorus (a section marker)");
                  }
                  if (args.has ("beat"))
                      point += (std::int64_t) std::llround ((args.number ("beat") - 1.0) * ppq);

                  const auto ending = args.boolean ("ending");
                  if (ending && point - length < 0)
                      return Result::failure (file.getFileName() + " is longer than everything before that point: it would have to start before bar 1");
                  const auto start = ending ? point - length : point;

                  // which track: the one given, or the first with nothing where the clip goes
                  int track = 0;
                  if (args.has ("track"))
                  {
                      track = (int) args.number ("track") - 1;
                  }
                  else
                  {
                      // (not a lane of patterns or automation, even where it's empty: audio goes on audio lanes)
                      for (;; ++track)
                      {
                          const auto busy = std::any_of (song.playlist.begin(), song.playlist.end(), [&] (const Clip& c) {
                              return c.track == track && (c.kind != ClipKind::audio || (c.start < start + length && c.end() > start));
                          });
                          if (! busy)
                              break;
                      }
                  }

                  const auto gain = (float) juce::Decibels::decibelsToGain (args.number ("gain", 0.0), -96.0);
                  const auto fadeIn = (std::int64_t) std::llround (args.number ("fadein", 0.0) * ppq);
                  const auto fadeOut = (std::int64_t) std::llround (args.number ("fadeout", 0.0) * ppq);
                  const auto index = PlaylistComponent::addAudioClip (ctx.engine, file, start, track, true, [&] (Clip& c) {
                      c.gain = gain;
                      c.fadeIn = std::min (fadeIn, c.length);
                      c.fadeOut = std::min (fadeOut, c.length);
                  });
                  if (index < 0)
                      return Result::failure ("the Playlist couldn't add " + file.getFileName());

                  const auto& now = ctx.song();
                  auto result = describeClip (now, index);
                  auto* o = result.getDynamicObject();
                  o->setProperty ("track", track + 1);
                  o->setProperty ("trackName", trackName (now, track));
                  o->setProperty ("sample", file.getFullPathName());
                  return Result::success (result);
              });

    mutation ("clip.delete", "Remove a track's clips (pattern and audio): all of them, or the ones starting in --bar, or of --pattern",
              { Arg::number ("track", trackArgHelp, 1, perf::maxTracks),
                Arg::number ("bar", "only the clips starting in this bar", 1, 9999).optional(),
                Arg::text ("pattern", "only this pattern's clips (its name or number)").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto& song = ctx.song();
                  const auto track = (int) args.number ("track") - 1;

                  int pattern = -1;
                  if (args.has ("pattern"))
                  {
                      juce::String error;
                      pattern = findPattern (song, args.text ("pattern"), error);
                      if (pattern < 0)
                          return Result::failure (error);
                  }
                  std::int64_t from = 0, to = std::numeric_limits<std::int64_t>::max();
                  if (args.has ("bar"))
                  {
                      from = barStartTick (song, (int) args.number ("bar"));
                      to = barStartTick (song, (int) args.number ("bar") + 1);
                  }

                  auto matches = [&] (const Clip& c) {
                      return c.track == track && c.start >= from && c.start < to
                             && (pattern < 0 || (c.kind == ClipKind::pattern && c.pattern == pattern));
                  };
                  const auto count = (int) std::count_if (song.playlist.begin(), song.playlist.end(), matches);
                  if (count == 0)
                      return Result::failure ("no clips on track " + juce::String (track + 1) + " match (playlist track --track "
                                              + juce::String (track + 1) + " lists them)");

                  ctx.edit ([&] (Song& s) { s.playlist.erase (std::remove_if (s.playlist.begin(), s.playlist.end(), matches), s.playlist.end()); });
                  return Result::success (makeObject ({ { "track", track + 1 }, { "removed", count } }));
              });

    mutation ("clear", "Remove every clip from the arrangement (and its section markers with --markers): a fresh start",
              { Arg::boolean ("markers", "also remove the section markers").optional() },
              [] (Context& ctx, const Args& args) {
                  const auto clips = (int) ctx.song().playlist.size();
                  int markers = 0;
                  ctx.edit ([&] (Song& s) {
                      s.playlist.clear();
                      if (args.boolean ("markers"))
                      {
                          // (only plain section markers: time signatures and the others shape playback)
                          auto& list = s.markers();
                          const auto before = list.size();
                          list.erase (std::remove_if (list.begin(), list.end(), [] (const Marker& m) { return ! m.isSignature() && m.action == Marker::none; }),
                                      list.end());
                          markers = (int) (before - list.size());
                      }
                  });
                  return Result::success (makeObject ({ { "clipsRemoved", clips }, { "markersRemoved", markers } }));
              });

    // ---- section markers --------------------------------------------------------------------------------------------

    query ("markers", "The markers on the Playlist's ruler: sections (Intro, Verse...), time signatures, loops", [] (Context& ctx, const Args&) {
        return Result::success (makeObject ({ { "markers", markerList (ctx.song()) } }));
    });

    mutation ("marker.add", "Name a section on the Playlist's ruler, from a bar (Intro, Verse, Chorus...)",
              { Arg::text ("name", "the section's name"), Arg::number ("bar", "the bar it starts at, from 1 (default 1)", 1, 9999).optional() },
              [] (Context& ctx, const Args& args) {
                  const auto name = args.text ("name").trim();
                  if (name.isEmpty())
                      return Result::failure ("--name is empty");
                  const auto tick = barStartTick (ctx.song(), (int) args.number ("bar", 1.0));
                  ctx.edit ([&] (Song& s) {
                      Marker m;
                      m.tick = tick;
                      m.name = name.toStdString();
                      s.addMarker (m);
                  });
                  return Result::success (makeObject ({ { "markers", markerList (ctx.song()) } }));
              });

    mutation ("marker.delete", "Remove section markers by name (any letter case) or by the bar they're at",
              { Arg::text ("name", "the section's name").optional(), Arg::number ("bar", "the bar it's at", 1, 9999).optional() },
              [] (Context& ctx, const Args& args) {
                  if (! args.has ("name") && ! args.has ("bar"))
                      return Result::failure ("give the marker's --name or --bar (playlist markers lists them)");
                  const auto& song = ctx.song();
                  const auto name = args.text ("name").trim();
                  const auto tick = args.has ("bar") ? barStartTick (song, (int) args.number ("bar")) : -1;
                  auto matches = [&] (const Marker& m) {
                      return ! m.isSignature() && m.action == Marker::none && (name.isEmpty() || juce::String::fromUTF8 (m.name.c_str()).equalsIgnoreCase (name))
                             && (tick < 0 || m.tick == tick);
                  };
                  const auto count = (int) std::count_if (song.markers().begin(), song.markers().end(), matches);
                  if (count == 0)
                      return Result::failure ("no section marker matches (playlist markers lists them)");
                  ctx.edit ([&] (Song& s) {
                      auto& list = s.markers();
                      list.erase (std::remove_if (list.begin(), list.end(), matches), list.end());
                  });
                  return Result::success (makeObject ({ { "removed", count }, { "markers", markerList (ctx.song()) } }));
              });

    examples ({ { "tracks", "playlist tracks" },
                { "track", "playlist track --track 1" },
                { "track.set", "playlist track set --track 1 --name Drums --colour \"#d9534f\"" },
                { "clip.add", "playlist clip add --track 1 --pattern Eastside --bar 5 --repeat 8" },
                { "audio.add", "playlist audio add --sample \"~/Samples/Crash 01.wav\" --at Chorus" },
                { "clip.delete", "playlist clip delete --track 1 --bar 5" },
                { "clear", "playlist clear --markers" },
                { "markers", "playlist markers" },
                { "marker.add", "playlist marker add --bar 5 --name Verse" },
                { "marker.delete", "playlist marker delete --name Verse" } });
}
} // namespace lynx::commands
