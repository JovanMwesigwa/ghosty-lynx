#pragma once

// mixer: the Mixer's tracks ("inserts"): levels, pan, mute / solo, routing, and which channels play into them.
// Tracks are named by number (0 = Master, 1, 2... as the Mixer shows them) or by name ("Master", "Drums").
// Levels are in dB (0 dB = the fader's unity mark, +5.6 dB its top, -100 = silent); pan from -100 (left) to 100 (right).
//
//   mixer.tracks [--all]                     the tracks in use (a channel, a route, effects, a name, a moved fader...)
//   mixer.track --track 3                    one track: level, pan, mute / solo, its channels, routes in and out, effects
//   mixer.track.set --track 3 [--name --colour --volume dB --pan --stereo --muted --solo]
//                                            change it (one undo step for everything given)
//   mixer.link --channel Kick --track 3      the channel plays into track 3 (or --free: the next free track)
//   mixer.route --from 3 --to 10 [--level dB] [--only]
//                                            send track 3 into track 10 (0 = Master); --only: nowhere else
//   mixer.unroute --from 3 --to 0            remove that route (a track with no route at all is silent)
//
//   mixer.sidechain --from 1 --to "4,7"     the Mixer's "Sidechain to this track": a level-0 send, and the compressors
//                                            on 4 and 7 duck to track 1 (one added later listens by itself)
//   mixer.unsidechain --from 1 --to 4        undo it
//   mixer.effects [--search verb] [--source lynx|fl|plugin] [--format vst3]
//                                            what a slot can hold: exactly the Mixer slot menu's list
//   mixer.effect.add --track 7 --effect "Valhalla Room" [--format au] [--slot 3]
//                                            into the first empty slot (or --slot), as the slot menu does
//   mixer.effect.remove --track 7 --slot 3   the slot stays empty, the others keep their places (FL)
//   mixer.effect.set --track 7 --slot 3 [--enabled false] [--mix 40]
//                                            bypass / turn on, and the slot's dry / wet
//   mixer.effect.params --track 7 --slot 3 [--search decay]
//                                            its knobs: number, name, value 0-1, and how it reads ("-6.0 dB")
//   mixer.effect.param.set --track 7 --slot 3 --param Decay (--text "2.5 s" | --value 0.4)
//                                            turn one: built-ins are undo steps; a plugin's knob is set as its window
//                                            does (the plugin keeps it, a save stores it)
// Slots are 1..10 as the Mixer shows them; an effect can sit in slot 5 with 1-4 empty (FL's slots are places).

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class MixerModule final : public CommandModule
{
public:
    MixerModule() : CommandModule ("mixer", "The Mixer's tracks: level, pan, mute / solo, routing, linked channels") {}
    void define() override;
};
} // namespace lynx::commands
