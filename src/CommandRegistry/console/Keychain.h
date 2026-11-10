#pragma once

// Keychain: LYNX's secrets (API keys, the account's sign-in token). On macOS each is a generic-password item in the
// Keychain (service "LYNX SHARE", one account name per secret: "anthropic-api-key", "lynx-session", ...); elsewhere
// they fall back to a file in the settings folder. Never in a project or a settings file.

#include <juce_core/juce_core.h>

#include <optional>

namespace lynx::commands::keychain
{
bool has (const juce::String& account);
std::optional<juce::String> get (const juce::String& account);
// Replaces any earlier one. False with `error` when the Keychain refuses.
bool set (const juce::String& account, const juce::String& secret, juce::String& error);
void remove (const juce::String& account);
} // namespace lynx::commands::keychain
