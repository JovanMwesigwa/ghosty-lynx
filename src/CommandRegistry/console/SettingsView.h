#pragma once

// SettingsView: the Console's Settings screen (the gear on the composer opens it, "Back" returns to the chat).
//
//   ‹ Chat   Settings
//   ⌄ API Keys
//     Anthropic API Key                         ← a card per provider: title, description, then rows
//     ┌──────────────────────────────────────┐
//     │ API Key                  [••••••••••] │   the key (saved to the Keychain on Enter, never shown again)
//     │ Use Anthropic API Key           (●  ) │   on / off, and whether a key is saved
//     └──────────────────────────────────────┘
//     OpenAI API Key: + Override base URL (and the URL), Model
//     Google API Key: + Model
//
// Everything reads and writes AiSettings, and follows its changes.

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace lynx::commands
{
class SettingsView final : public juce::Component, private juce::ChangeListener
{
public:
    SettingsView();
    ~SettingsView() override;

    std::function<void()> onBack; // "‹ Chat"

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    class Content; // the scrolling part: the API Keys section and its cards
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    juce::TextButton back { "Chat" };
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
};
} // namespace lynx::commands
