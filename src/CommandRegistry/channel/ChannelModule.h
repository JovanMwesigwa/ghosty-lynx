#pragma once

// channel: the Channel Rack's instruments (what a pattern's notes play). Channels are numbered from 1, top to bottom.
//
//   channel.list                         every channel: number, name, instrument, mute, volume, pan, mixer track
//   channel.get --channel Kick|1         one channel: its settings, sample, mixer insert, and where it's used
//   channel.instruments [--search syn] [--source lynx|fl|vfx|plugin]
//                                        what a channel can be: exactly the Channel Rack's Add menu (LYNX's instruments
//                                        and tools, FL's engines, the VFX note processors, the installed plugins)
//   channel.add [--instrument Sytrus] [--name "Lead"] [--sample ~/Samples/oh.wav]
//                                        a new channel at the bottom of the rack (an empty Sampler by default), on a
//                                        free mixer insert, selected, as the rack's own Add menu does (--type: the same)
//                                        A plugin installed in several formats: --format vst3|au|clap|vst picks one;
//                                        without it VST3 comes first (then CLAP, AU, VST2) and the reply's note says so
//                                        (channel.replace takes --format too)
//   channel.replace --channel Lead --instrument "LYNX Soundfont" [--name ...]
//                                        the rack's Replace ›: another instrument on the channel; its notes, insert,
//                                        levels and (unlike the menu, so commands keep finding it) its name stay
//   channel.sample --channel Kick --sample ~/Music/Samples/Kick 02.wav [--rename]
//                                        swaps the sample it plays (as dropping a file on its row: sample search finds
//                                        them); notes, levels, insert and name stay; not a Sampler yet: it becomes one
//   channel.delete --channel "Open Hat"  removes it with its notes and audio clips (the rack keeps at least one);
//                                        the channels below it move up one number
//   channel.params --channel Lead [--search cutoff]
//                                        a plugin instrument's knobs: number, name, value 0-1, how it reads
//   channel.param.set --channel Lead --param Cutoff (--value 0.4 | --text "2 kHz")
//                                        turns one (as its window does: the plugin keeps it, not an undo step)
//   channel.presets --channel Lead [--search pad]
//                                        its presets, as the plugin window's preset menu lists them (the plugin's own
//                                        programs, then preset files saved for it), and the one it's on
//   channel.preset.set --channel Lead --preset 12|"Warm Pad"|next|previous

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class ChannelModule final : public CommandModule
{
public:
    ChannelModule() : CommandModule ("channel", "The Channel Rack's instruments (what pattern notes play)") {}
    void define() override;
};
} // namespace lynx::commands
