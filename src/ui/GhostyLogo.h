#pragma once

// Ghosty: the name of LYNX's AI (the chat panel, its account and plan). Its logo, a ghost with headphones
// (brand/ghosty-logo.svg), as a vector path for any size.

#include <juce_gui_basics/juce_gui_basics.h>

namespace lynx::ghosty
{
inline constexpr const char* name = "Ghosty";

// The logo's outline (fill it, non-zero winding). Its own coordinates: about 1263 x 1332.
const juce::Path& path();

// The logo drawn in `colour`, as large as fits `area`, centred.
void draw (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour);
// The logo on a round badge: a disc in `background`, the ghost in `ink` (Ghosty's avatar in the chat).
void drawBadge (juce::Graphics& g, juce::Rectangle<float> disc, juce::Colour background, juce::Colour ink);
} // namespace lynx::ghosty
