#pragma once

// settings: LYNX's own settings (not the project's). These are actions: they change LYNX itself, so they aren't undo
// steps, and LYNX keeps them for the next launch (the audio device: <settings>/audio-device.xml, as Settings > Audio).
// For now: the audio device.
//
//   settings.audio                  what's in use: driver, output / input device, sample rate, buffer (and the
//                                   latency it gives), the output channels, CPU load; and the choices: drivers,
//                                   sample rates, buffer sizes the device offers
//   settings.audio.devices          the output and input devices there are now (looked for again, so headphones
//                                   plugged in a moment ago are there), which one LYNX uses, and which one the
//                                   system uses (macOS: the output chosen in the menu bar / the one just plugged in)
//   settings.audio.set [--driver "Core Audio"] [--output "AirPods"|default] [--input "MacBook Pro Microphone"|none]
//                      [--rate 48000] [--buffer 256]
//                                   changes them, as Settings > Audio does (--output default: the system's output)
//   settings.audio.check            looks for why there could be no sound (no device, the system's output isn't
//                                   LYNX's, outputs off, the Master muted or down, every channel muted, dropouts)
//                                   and says the command that fixes each
//   settings.audio.test             plays the test tone on the output (Settings > Audio's Test)

#include "CommandRegistry/core/CommandModule.h"

namespace lynx::commands
{
class SettingsModule final : public CommandModule
{
public:
    SettingsModule() : CommandModule ("settings", "LYNX's own settings (not the project): the audio device") {}
    void define() override;
};
} // namespace lynx::commands
