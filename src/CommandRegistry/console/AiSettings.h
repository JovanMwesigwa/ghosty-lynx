#pragma once

// AiSettings: the Console's AI setup — which model the chat uses, and each provider's API key and options.
//
//   - API keys are secrets: they live in the macOS Keychain (service "LYNX SHARE", one item per provider), never in a
//     project or a settings file. Elsewhere (no Keychain) they fall back to a file in the settings folder.
//   - Everything else (the chosen model, which providers are on, OpenAI's base URL, the OpenAI / Google model names)
//     is in <settings folder>/console-ai.json.
//
// One shared instance (get()); it broadcasts a change message whenever something changes, so the Console's model
// picker and Settings view stay in step. Message thread only.

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace lynx::commands
{
enum class AiProvider { anthropic, openai, google };

struct AiProviderSettings
{
    bool enabled = false;          // "Use <provider> API Key"
    bool overrideBaseUrl = false;  // OpenAI: requests go to `baseUrl` instead of OpenAI's
    juce::String baseUrl;
    juce::String model;            // OpenAI / Google: a model typed in Settings (optional for OpenAI: its list is fetched)
    juce::StringArray models;      // OpenAI: the chat models the key can use (fetched from the API, newest first)
};

// One entry in the model picker.
struct AiModelChoice
{
    juce::String id;      // "commands", "claude-opus-5-5", "openai:<model>", "google:<model>"
    juce::String label;   // "Claude Opus 5.5"
    juce::String group;   // "LYNX", "Anthropic", "OpenAI", "Google"
    bool usable = true;   // false: the provider has no key / is off (shown greyed out, with `why`)
    juce::String why;
};

class AiSettings final : public juce::ChangeBroadcaster
{
public:
    static AiSettings& get();

    static juce::String providerName (AiProvider p); // "Anthropic"

    // ---- keys (the Keychain)
    bool hasKey (AiProvider p) const;
    // Stores the key; false with `error` when the Keychain refuses. A first key also turns the provider on.
    bool setKey (AiProvider p, const juce::String& key, juce::String& error);
    void removeKey (AiProvider p);
    // The key itself (for the requests the chat will make). nullopt when there's none.
    std::optional<juce::String> key (AiProvider p) const;

    // ---- the rest (saved on every change)
    const AiProviderSettings& provider (AiProvider p) const;
    void setEnabled (AiProvider p, bool on);
    void setOverrideBaseUrl (AiProvider p, bool on);
    void setBaseUrl (AiProvider p, const juce::String& url);
    void setModel (AiProvider p, const juce::String& model);

    // ---- the model picker
    std::vector<AiModelChoice> models() const;
    const juce::String& selectedModel() const noexcept { return selected; }
    void select (const juce::String& id);
    juce::String selectedLabel() const; // "Commands" when no AI model is chosen (or the chosen one isn't usable)
    bool selectedIsAi() const;

    // OpenAI: asks the API (GET <base>/models) which models the key can use, in the background; the picker lists them
    // when it answers. fetchModelsIfNeeded() does it only when there's a key and no list yet (or the last try failed).
    void fetchModels (AiProvider p);
    void fetchModelsIfNeeded (AiProvider p);

private:
    AiSettings();
    ~AiSettings() override;
    void load();
    void save() const;
    void changed();

    AiProviderSettings providers[3];
    juce::String selected = "commands";
    mutable bool keyCache[3] {};
    mutable bool keyCacheValid = false;

    bool fetching[3] {};
    juce::String fetchError[3];
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace lynx::commands
