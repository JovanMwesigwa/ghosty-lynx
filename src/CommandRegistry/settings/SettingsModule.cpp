#include "CommandRegistry/settings/SettingsModule.h"

#include "core/Preferences.h"
#include "core/Song.h"
#include "engine/AudioEngine.h"

#include <algorithm>
#include <cmath>

namespace lynx::commands
{
namespace
{
using Setup = juce::AudioDeviceManager::AudioDeviceSetup;

// The current driver type, its devices looked for again first when `rescan` (a device plugged in since the last look).
juce::AudioIODeviceType* currentType (juce::AudioDeviceManager& devices, bool rescan)
{
    auto* type = devices.getCurrentDeviceTypeObject();
    if (type != nullptr && (rescan || type->getDeviceNames (false).isEmpty()))
        type->scanForDevices();
    return type;
}

// The device the system itself uses (macOS: the output picked in the menu bar, or the headphones just plugged in).
juce::String systemDevice (juce::AudioIODeviceType* type, bool input)
{
    if (type == nullptr)
        return {};
    const auto names = type->getDeviceNames (input);
    const auto index = type->getDefaultDeviceIndex (input);
    return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
}

// A name from `names`: the same (any letter case), or the only one containing it. Empty with `error` otherwise.
juce::String pick (const juce::StringArray& names, const juce::String& wanted, const juce::String& what, const juce::String& listCommand,
                   juce::String& error)
{
    const auto w = wanted.trim();
    for (const auto& n : names)
        if (n.equalsIgnoreCase (w))
            return n;
    juce::StringArray found;
    for (const auto& n : names)
        if (n.containsIgnoreCase (w))
            found.add (n);
    if (found.size() == 1)
        return found[0];
    error = found.isEmpty() ? "no " + what + " named \"" + w + "\": there's " + (names.isEmpty() ? juce::String ("none") : names.joinIntoString (", "))
                                  + " (" + listCommand + ")"
                            : "several " + what + "s match \"" + w + "\": " + found.joinIntoString (", ");
    return {};
}

juce::var nameOrNull (const juce::String& s) { return s.isEmpty() ? juce::var() : juce::var (s); }

// The buffer sizes worth offering for the open device (as Settings > Audio lists them).
std::vector<int> bufferSizes (juce::AudioIODevice& device)
{
    std::vector<int> offered;
    for (auto b : device.getAvailableBufferSizes())
        offered.push_back (b);
    return prefs::usableBufferSizes (prefs::driverKind (device.getTypeName().toStdString()), offered, device.getDefaultBufferSize());
}

double rounded1 (double v) { return std::round (v * 10.0) / 10.0; }

// What's in use, and the choices.
juce::var audioState (Context& ctx)
{
    auto& devices = ctx.engine.getDeviceManager();
    auto* type = currentType (devices, false);
    auto* device = devices.getCurrentAudioDevice();
    const auto setup = devices.getAudioDeviceSetup();

    juce::Array<juce::var> drivers;
    for (auto* t : devices.getAvailableDeviceTypes())
        drivers.add (t->getTypeName());

    auto out = makeObject ({ { "driver", type != nullptr ? juce::var (type->getTypeName()) : juce::var() },
                             { "running", device != nullptr },
                             { "output", nameOrNull (setup.outputDeviceName) },
                             { "systemOutput", nameOrNull (systemDevice (type, false)) },
                             { "input", setup.inputChannels.isZero() ? juce::var() : nameOrNull (setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName
                                                                                                                                      : setup.outputDeviceName) } });
    auto* o = out.getDynamicObject();

    juce::Array<juce::var> rates, buffers, channels;
    if (device != nullptr)
    {
        const auto rate = device->getCurrentSampleRate();
        const auto buffer = device->getCurrentBufferSizeSamples();
        o->setProperty ("sampleRate", (int) rate);
        o->setProperty ("bufferSize", buffer);
        o->setProperty ("bufferMs", rounded1 (prefs::bufferLatencyMs (buffer, rate)));
        o->setProperty ("outputLatencyMs", rounded1 (prefs::bufferLatencyMs (device->getOutputLatencyInSamples() + buffer, rate)));
        o->setProperty ("cpuPercent", juce::roundToInt (devices.getCpuUsage() * 100.0));
        o->setProperty ("dropouts", device->getXRunCount()); // (-1: the driver doesn't count them)

        const auto names = device->getOutputChannelNames();
        const auto active = device->getActiveOutputChannels();
        for (int c = 0; c < names.size(); ++c)
            channels.add (makeObject ({ { "name", names[c] }, { "on", active[c] } }));
        for (auto r : device->getAvailableSampleRates())
            rates.add ((int) r);
        // (a driver can offer dozens: the usual powers of two are the choices, plus the one in use; any size it offers
        // still works in settings audio set)
        const auto all = bufferSizes (*device);
        for (auto b : all)
            if (all.size() <= 12 || juce::isPowerOfTwo (b) || b == buffer)
                buffers.add (makeObject ({ { "samples", b }, { "ms", rounded1 (prefs::bufferLatencyMs (b, rate)) } }));
    }
    else
        o->setProperty ("problem", "no audio device is open: choose an output (settings audio set --output default)");
    o->setProperty ("outputChannels", channels);
    o->setProperty ("choices", makeObject ({ { "drivers", drivers }, { "sampleRates", rates }, { "bufferSizes", buffers } }));
    return out;
}

juce::var deviceList (juce::AudioIODeviceType* type, bool input, const juce::String& current)
{
    juce::Array<juce::var> list;
    if (type == nullptr)
        return list;
    const auto system = systemDevice (type, input);
    for (const auto& name : type->getDeviceNames (input))
        list.add (makeObject ({ { "name", name }, { "current", name == current }, { "system", name == system } }));
    return list;
}

double faderDb (float volume)
{
    const auto gain = faderGain (volume);
    return gain > 0.0f ? rounded1 (20.0 * std::log10 ((double) gain)) : -100.0;
}
} // namespace

void SettingsModule::define()
{
    query ("audio", "The audio device in use (driver, output, input, sample rate, buffer and latency, output channels, CPU) and the choices",
           [] (Context& ctx, const Args&) { return Result::success (audioState (ctx)); });

    query ("audio.devices", "The output and input devices there are now (looked for again: just-plugged-in headphones show), LYNX's and the system's",
           [] (Context& ctx, const Args&) {
               auto& devices = ctx.engine.getDeviceManager();
               auto* type = currentType (devices, true);
               const auto setup = devices.getAudioDeviceSetup();
               if (type == nullptr)
                   return Result::failure ("no audio driver is available");
               const auto separate = type->hasSeparateInputsAndOutputs();
               return Result::success (makeObject ({ { "driver", type->getTypeName() },
                                                     { "outputs", deviceList (type, false, setup.outputDeviceName) },
                                                     { "inputs", separate ? deviceList (type, true, setup.inputChannels.isZero() ? juce::String() : setup.inputDeviceName)
                                                                          : juce::var (juce::Array<juce::var>()) },
                                                     { "note", separate ? juce::var() : juce::var ("this driver's devices do both: the output's inputs are the input") } }));
           });

    action ("audio.set", "Change the audio device as Settings > Audio does: driver, output (default = the system's), input (none = off), sample rate, buffer",
            { Arg::text ("driver", "a driver from settings audio's choices (Core Audio, ASIO, Windows Audio...)").optional(),
              Arg::text ("output", "an output from settings audio devices, or default (the one the system uses now)").optional(),
              Arg::text ("input", "an input from settings audio devices, or none").optional(),
              Arg::number ("rate", "the sample rate in Hz (settings audio lists the device's)", 8000, 384000).optional(),
              Arg::number ("buffer", "the buffer size in samples (settings audio lists the device's)", 16, 8192).optional() },
            [] (Context& ctx, const Args& args) {
                if (! args.has ("driver") && ! args.has ("output") && ! args.has ("input") && ! args.has ("rate") && ! args.has ("buffer"))
                    return Result::failure ("give what to change: --driver, --output, --input, --rate or --buffer");
                auto& devices = ctx.engine.getDeviceManager();
                juce::String error;
                juce::StringArray changed;

                if (args.has ("driver"))
                {
                    juce::StringArray names;
                    for (auto* t : devices.getAvailableDeviceTypes())
                        names.add (t->getTypeName());
                    const auto name = pick (names, args.text ("driver"), "driver", "settings audio", error);
                    if (name.isEmpty())
                        return Result::failure (error);
                    devices.setCurrentAudioDeviceType (name, true);
                    changed.add ("driver");
                }

                if (args.has ("output"))
                {
                    auto* type = currentType (devices, true);
                    if (type == nullptr)
                        return Result::failure ("no audio driver is available");
                    const auto wanted = args.text ("output").trim();
                    const auto name = wanted.equalsIgnoreCase ("default") || wanted.equalsIgnoreCase ("system")
                                          ? systemDevice (type, false)
                                          : pick (type->getDeviceNames (false), wanted, "output", "settings audio devices", error);
                    if (name.isEmpty())
                        return Result::failure (error.isNotEmpty() ? error : juce::String ("the system has no output device"));
                    auto setup = devices.getAudioDeviceSetup();
                    setup.outputDeviceName = name;
                    if (! type->hasSeparateInputsAndOutputs())
                        setup.inputDeviceName = name; // (ASIO: one device does both)
                    if (const auto failed = devices.setAudioDeviceSetup (setup, true); failed.isNotEmpty())
                        return Result::failure ("the driver refused " + name + ": " + failed);
                    changed.add ("output");
                }

                if (args.has ("rate") || args.has ("buffer"))
                {
                    auto* device = devices.getCurrentAudioDevice();
                    if (device == nullptr)
                        return Result::failure ("no audio device is open: choose an output first (--output default)");
                    auto setup = devices.getAudioDeviceSetup();
                    if (args.has ("rate"))
                    {
                        const auto rate = (double) args.number ("rate");
                        juce::StringArray offered;
                        bool ok = false;
                        for (auto r : device->getAvailableSampleRates())
                        {
                            offered.add (juce::String ((int) r));
                            ok = ok || std::abs (r - rate) < 1.0;
                        }
                        if (! ok)
                            return Result::failure (device->getName() + " doesn't run at " + juce::String ((int) rate) + " Hz: it offers "
                                                    + offered.joinIntoString (", "));
                        setup.sampleRate = rate;
                    }
                    if (args.has ("buffer"))
                    {
                        const auto size = (int) args.number ("buffer");
                        const auto sizes = bufferSizes (*device);
                        if (std::find (sizes.begin(), sizes.end(), size) == sizes.end())
                        {
                            juce::StringArray offered;
                            for (auto b : sizes)
                                if (sizes.size() <= 12 || juce::isPowerOfTwo (b))
                                    offered.add (juce::String (b));
                            return Result::failure (device->getName() + " doesn't take a " + juce::String (size) + "-sample buffer: it takes "
                                                    + offered.joinIntoString (", "));
                        }
                        setup.bufferSize = size;
                    }
                    if (const auto failed = devices.setAudioDeviceSetup (setup, true); failed.isNotEmpty())
                        return Result::failure ("the driver refused it: " + failed);
                    if (args.has ("rate"))
                        changed.add ("sample rate");
                    if (args.has ("buffer"))
                        changed.add ("buffer");
                }

                if (args.has ("input"))
                {
                    const auto wanted = args.text ("input").trim();
                    if (wanted.equalsIgnoreCase ("none") || wanted.equalsIgnoreCase ("off"))
                    {
                        auto setup = devices.getAudioDeviceSetup();
                        setup.inputDeviceName = {};
                        setup.useDefaultInputChannels = false;
                        setup.inputChannels.clear();
                        if (const auto failed = devices.setAudioDeviceSetup (setup, true); failed.isNotEmpty())
                            return Result::failure ("the driver refused it: " + failed);
                    }
                    else
                    {
                        auto* type = currentType (devices, true);
                        if (type == nullptr)
                            return Result::failure ("no audio driver is available");
                        juce::String name;
                        if (type->hasSeparateInputsAndOutputs())
                        {
                            name = wanted.equalsIgnoreCase ("default") || wanted.equalsIgnoreCase ("system")
                                       ? systemDevice (type, true)
                                       : pick (type->getDeviceNames (true), wanted, "input", "settings audio devices", error);
                            if (name.isEmpty())
                                return Result::failure (error.isNotEmpty() ? error : juce::String ("the system has no input device"));
                        }
                        // (every channel of the device opens: each mixer insert picks its own, as Settings > Audio does)
                        const auto result = ctx.engine.openAudioInput (name);
                        if (result.issue == AudioEngine::InputIssue::failed)
                            return Result::failure ("the driver refused the input: " + result.detail);
                        if (result.issue == AudioEngine::InputIssue::noDevice || result.issue == AudioEngine::InputIssue::noInputDevice)
                            return Result::failure ("there's no input to open (open an output device first)");
                    }
                    changed.add ("input");
                }

                return Result::success (makeObject ({ { "changed", changed.joinIntoString (", ") }, { "audio", audioState (ctx) } }));
            });

    query ("audio.check", "Look for why there's no sound (or crackles): each finding says the command that fixes it",
           [] (Context& ctx, const Args&) {
               auto& devices = ctx.engine.getDeviceManager();
               auto* type = currentType (devices, true);
               auto* device = devices.getCurrentAudioDevice();
               const auto setup = devices.getAudioDeviceSetup();
               const auto& song = ctx.song();

               juce::Array<juce::var> findings;
               auto add = [&] (const char* level, const juce::String& text, const juce::String& fix = {}) {
                   findings.add (makeObject ({ { "level", level }, { "finding", text }, { "fix", nameOrNull (fix) } }));
               };

               const auto system = systemDevice (type, false);
               if (device == nullptr)
                   add ("problem", "No audio device is open, so LYNX can't play anything.", "settings audio set --output default");
               else
               {
                   if (system.isNotEmpty() && system != setup.outputDeviceName)
                       add ("problem", "The system plays through \"" + system + "\" (e.g. headphones just connected), but LYNX plays through \""
                                           + setup.outputDeviceName + "\".",
                            "settings audio set --output default");
                   if (device->getActiveOutputChannels().isZero())
                       add ("problem", "None of " + device->getName() + "'s outputs is switched on.", "settings audio set --output default");
                   const auto dropouts = device->getXRunCount();
                   if (dropouts > 0 || devices.getCpuUsage() > 0.85)
                       add ("warning", "The audio is breaking up (" + juce::String (juce::jmax (0, dropouts)) + " dropouts, CPU "
                                           + juce::String (juce::roundToInt (devices.getCpuUsage() * 100.0)) + "%): a bigger buffer gives it more room.",
                            "settings audio (choices.bufferSizes), then settings audio set --buffer <a bigger one>");
               }

               if (! song.mixer.empty())
               {
                   const auto& master = song.mixer[0];
                   if (master.muted)
                       add ("problem", "The Master track is muted: nothing reaches the output.", "mixer track set --track 0 --muted false");
                   if (faderDb (master.volume) <= -40.0)
                       add ("problem", "The Master fader is almost all the way down (" + juce::String (faderDb (master.volume), 1) + " dB).",
                            "mixer track set --track 0 --volume 0");
               }
               if (! song.channels.empty())
               {
                   bool allMuted = true;
                   for (const auto& c : song.channels)
                       allMuted = allMuted && c.muted;
                   if (allMuted)
                       add ("problem", "Every channel in the Channel Rack is muted.", "channel list (then unmute them in the rack)");
               }
               if (! ctx.engine.getTransport().playing.load())
                   add ("note", "Nothing is playing right now: play the project, or play the test tone to hear the output alone.",
                        "settings audio test");

               bool problems = false;
               for (const auto& f : findings)
                   problems = problems || f["level"].toString() == "problem";
               return Result::success (makeObject ({ { "ok", ! problems },
                                                     { "findings", findings },
                                                     { "output", nameOrNull (setup.outputDeviceName) },
                                                     { "systemOutput", nameOrNull (system) } }));
           });

    action ("audio.test", "Play the test tone on the output (Settings > Audio's Test), to hear whether the device works", {},
            [] (Context& ctx, const Args&) {
                auto& devices = ctx.engine.getDeviceManager();
                auto* device = devices.getCurrentAudioDevice();
                if (device == nullptr)
                    return Result::failure ("no audio device is open: choose an output first (settings audio set --output default)");
                devices.playTestSound();
                return Result::success (makeObject ({ { "playing", "a short test tone" }, { "output", device->getName() } }));
            });

    examples ({ { "audio", "settings audio" },
                { "audio.devices", "settings audio devices" },
                { "audio.set", "settings audio set --output default --buffer 256" },
                { "audio.check", "settings audio check" },
                { "audio.test", "settings audio test" } });
}
} // namespace lynx::commands
