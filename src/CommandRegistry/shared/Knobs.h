#pragma once

// Knobs: the parameters of an effect or a plugin instrument, for the commands that list and turn them (mixer effect
// params / param set, channel params / param set). A knob is 0..1 and reads as text ("-6.0 dB", "1.2 kHz", "On");
// --text finds the value that reads closest to what's asked.

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <optional>
#include <vector>

namespace lynx::commands
{
struct Knob
{
    int index = 0;            // (0-based; shown from 1)
    juce::String name, text;  // text: the value as the effect shows it ("-6.0 dB", "1.2 kHz", "On")
    float value = 0.0f;       // 0..1
    float defaultValue = 0.0f;
};

// The text a plugin parameter shows for `value`, with its unit ("-6.0" + "dB").
juce::String pluginText (juce::AudioProcessorParameter& p, float value);

// Every parameter of a running plugin, except the "MIDI CC 12|48" slots some plugins add (one per CC and MIDI channel,
// often over a thousand: MIDI routing, not knobs). `hidden` counts those left out. Knob::index stays the plugin's own.
std::vector<Knob> pluginKnobs (juce::AudioPluginInstance& plugin, int* hidden = nullptr);

// {"param": n (from 1), "name", "text", "value", "default"}
juce::var knobInfo (const Knob& k);

// --param: its number (from 1, Knob::index + 1) or its name (any letter case; a unique part of the name also works). -1 with `error`,
// which points at `listCommand` ("mixer effect params"). Returns the position in `knobs` (its Knob::index is the param).
int findKnob (const std::vector<Knob>& knobs, const juce::String& text, const juce::String& listCommand, juce::String& error);

// The 0..1 value whose text is closest to `want`: a number ("-6", "-6 dB", "2.5k", "1.2 s") → the nearest number (with
// a unit, converted: "1.2 s" finds "1200 ms"); a word ("On", "Lowpass") → the value showing it. nullopt when none does.
std::optional<float> valueForText (const std::function<juce::String (float)>& textOf, const juce::String& want);
} // namespace lynx::commands
