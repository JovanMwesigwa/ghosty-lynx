#pragma once

// sample: the samples on this computer that LYNX can play, found in the Browser's folders (FL's Packs when FL Studio
// is installed, the folders added to the Browser, Music, Documents, Desktop, Downloads), so a channel's kick or snare
// can be swapped for another one (channel sample).
//
//   sample.folders                          → the folders searched, and how many samples each holds
//   sample.search --kind kick               → kicks: name, path, kind, folder; --query "punchy", --folder, --random
//   sample.search --query "808 OR sub"      → the Browser's search words (AND, OR, -not, "phrase", wild*cards),
//                                             matched against the folders' names too (Drums/Kicks/Big.wav is a kick)
//   sample.preview --path ~/Music/kick.wav  → plays it in the Browser's preview (no --path: stops)
//
// Searches read the Browser's search index (instant). A folder not indexed yet is indexed in the background: the reply
// says "indexing" and a later search finds more.

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class SampleModule final : public CommandModule
{
public:
    SampleModule() : CommandModule ("sample", "Samples on this computer (the Browser's folders): find kicks, snares, hats... and preview them") {}
    void define() override;
};
} // namespace lynx::commands
