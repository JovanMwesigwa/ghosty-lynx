#pragma once

// transport: play, stop, and what plays (the toolbar's PAT / SONG switch).
//
//   transport.get                   → {"playing": false, "mode": "pattern"}
//   transport.mode --mode song      → song: the Playlist (the arrangement) plays; pattern: the current pattern loops
//   transport.play [--mode song]    → the toolbar's Play (from where the playhead is); --mode switches first
//   transport.stop                  → Stop, back to the start

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class TransportModule final : public CommandModule
{
public:
    TransportModule() : CommandModule ("transport", "Play, stop, and pattern / song mode (the toolbar's transport)") {}
    void define() override;
};
} // namespace lynx::commands
