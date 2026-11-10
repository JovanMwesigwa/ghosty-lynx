#pragma once

// tempo:
//   tempo.get             → {"bpm": 124}
//   tempo.set --bpm <N>   → {"bpm": 128, "previous": 124}   (10–522 BPM, one undo step)

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class TempoModule final : public CommandModule
{
public:
    TempoModule() : CommandModule ("tempo", "The project tempo (the toolbar's BPM display)") {}
    void define() override;
};
} // namespace lynx::commands
