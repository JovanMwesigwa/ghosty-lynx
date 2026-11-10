#pragma once

// playlist (the arrangement window). Tracks are numbered from 1, as the Playlist shows them ("Track 1").
//
//   playlist.tracks                    the current arrangement's tracks that hold something (clips, or a changed header)
//   playlist.track --track N           one track: its header (name, colour, mute, height, group, mode) and its clips
//   playlist.track.set --track N ...   change its header, one undo step for everything given:
//                                        --name "Drums" (empty = back to "Track N")   --colour "#d9534f" (none = default)
//                                        --muted true|false   --height 20..160 (38 = default)
//                                        --grouped true|false (with the track above)   --collapsed true|false (its group)

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class PlaylistModule final : public CommandModule
{
public:
    PlaylistModule() : CommandModule ("playlist", "The Playlist (arrangement window): tracks and their clips") {}
    void define() override;
};
} // namespace lynx::commands
