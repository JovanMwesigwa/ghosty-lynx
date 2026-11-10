#include "CommandRegistry/console/AiSettings.h"

#include "CommandRegistry/console/Keychain.h"

#include "platform/Paths.h"

#include <algorithm>


namespace lynx::commands
{
namespace
{
const char* accountOf (AiProvider p)
{
    switch (p)
    {
        case AiProvider::anthropic: return "anthropic-api-key";
        case AiProvider::openai: return "openai-api-key";
        case AiProvider::google: return "google-api-key";
    }
    return "api-key";
}

const char* idOf (AiProvider p)
{
    switch (p)
    {
        case AiProvider::anthropic: return "anthropic";
        case AiProvider::openai: return "openai";
        case AiProvider::google: return "google";
    }
    return "";
}

// Claude's current models, as the picker lists them (claude-api reference, 2026-10); Opus 5.5 is the default choice.
struct ClaudeModel
{
    const char* id;
    const char* label;
};
constexpr ClaudeModel claudeModels[] = { { "claude-opus-5-5", "Claude Opus 5.5" },
                                         { "claude-sonnet-5-5", "Claude Sonnet 5.5" },
                                         { "claude-haiku-5-5", "Claude Haiku 5.5" },
                                         { "claude-fable-5-1", "Claude Fable 5.1" } };

bool keychainHas (AiProvider p) { return keychain::has (accountOf (p)); }
std::optional<juce::String> keychainGet (AiProvider p) { return keychain::get (accountOf (p)); }
bool keychainSet (AiProvider p, const juce::String& secret, juce::String& error) { return keychain::set (accountOf (p), secret, error); }
void keychainRemove (AiProvider p) { keychain::remove (accountOf (p)); }

juce::File settingsJson() { return platform::settingsFile ("console-ai.json"); }
} // namespace

AiSettings& AiSettings::get()
{
    static AiSettings instance;
    return instance;
}

AiSettings::AiSettings()
{
    load();
}

AiSettings::~AiSettings()
{
    alive->store (false);
}

juce::String AiSettings::providerName (AiProvider p)
{
    switch (p)
    {
        case AiProvider::anthropic: return "Anthropic";
        case AiProvider::openai: return "OpenAI";
        case AiProvider::google: return "Google";
    }
    return {};
}

// ---- keys -----------------------------------------------------------------------------------------------------------

bool AiSettings::hasKey (AiProvider p) const
{
    if (! keyCacheValid) // (the Keychain is asked once, then after each change)
    {
        for (auto q : { AiProvider::anthropic, AiProvider::openai, AiProvider::google })
            keyCache[(int) q] = keychainHas (q);
        keyCacheValid = true;
    }
    return keyCache[(int) p];
}

bool AiSettings::setKey (AiProvider p, const juce::String& key, juce::String& error)
{
    const auto trimmed = key.trim();
    if (trimmed.isEmpty())
    {
        error = "the key is empty";
        return false;
    }
    const auto first = ! hasKey (p);
    if (! keychainSet (p, trimmed, error))
        return false;
    keyCacheValid = false;
    if (first)
        providers[(int) p].enabled = true; // (a first key: use it)
    changed();
    // (a first key while no AI model is picked: pick one, so Ghosty answers right away; OpenAI's once its list is in)
    if (p == AiProvider::anthropic && ! selectedIsAi())
        select ("claude-opus-5-5");
    if (p == AiProvider::openai)
        fetchModels (p);
    return true;
}

void AiSettings::removeKey (AiProvider p)
{
    keychainRemove (p);
    keyCacheValid = false;
    providers[(int) p].enabled = false;
    providers[(int) p].models.clear();
    changed();
}

std::optional<juce::String> AiSettings::key (AiProvider p) const
{
    return keychainGet (p);
}

// ---- options --------------------------------------------------------------------------------------------------------

const AiProviderSettings& AiSettings::provider (AiProvider p) const
{
    return providers[(int) p];
}

void AiSettings::setEnabled (AiProvider p, bool on)
{
    providers[(int) p].enabled = on;
    changed();
}

void AiSettings::setOverrideBaseUrl (AiProvider p, bool on)
{
    providers[(int) p].overrideBaseUrl = on;
    providers[(int) p].models.clear(); // (another server: another list)
    changed();
}

void AiSettings::setBaseUrl (AiProvider p, const juce::String& url)
{
    if (providers[(int) p].baseUrl == url.trim())
        return;
    providers[(int) p].baseUrl = url.trim();
    if (providers[(int) p].overrideBaseUrl)
        providers[(int) p].models.clear();
    changed();
}

void AiSettings::setModel (AiProvider p, const juce::String& model)
{
    providers[(int) p].model = model.trim();
    changed();
}

// ---- the model picker -----------------------------------------------------------------------------------------------

std::vector<AiModelChoice> AiSettings::models() const
{
    std::vector<AiModelChoice> out;
    out.push_back ({ "commands", "Commands", "LYNX", true, {} });

    auto availability = [this] (AiProvider p, juce::String& why) {
        if (! hasKey (p))
            why = "add your " + providerName (p) + " key in Settings";
        else if (! providers[(int) p].enabled)
            why = "turn on \"Use " + providerName (p) + " API Key\" in Settings";
        return why.isEmpty();
    };

    {
        juce::String why;
        const auto usable = availability (AiProvider::anthropic, why);
        for (const auto& m : claudeModels)
            out.push_back ({ m.id, m.label, "Anthropic", usable, why });
    }
    for (auto p : { AiProvider::openai }) // (Google: not offered for now)
    {
        juce::String why;
        const auto usable = availability (p, why);
        const auto& s = providers[(int) p];
        const auto id = juce::String (idOf (p)) + ":";

        juce::StringArray names; // the typed model first (if any), then the fetched list
        if (s.model.isNotEmpty())
            names.add (s.model);
        if (usable)
            for (const auto& m : s.models)
                names.addIfNotAlreadyThere (m);

        for (const auto& m : names)
            out.push_back ({ id + m, m, providerName (p), usable, why });
        if (names.isEmpty())
        {
            const auto reason = ! usable ? why
                              : fetching[(int) p] ? juce::String ("loading the list...")
                              : fetchError[(int) p].isNotEmpty() ? fetchError[(int) p] + ": type a model name in Settings"
                              : p == AiProvider::openai ? juce::String ("open this menu again in a moment")
                                                        : juce::String ("type a model name in Settings");
            out.push_back ({ id, providerName (p) + " models", providerName (p), false, reason });
        }
    }
    return out;
}

void AiSettings::select (const juce::String& id)
{
    selected = id;
    changed();
}

juce::String AiSettings::selectedLabel() const
{
    for (const auto& m : models())
        if (m.id == selected && m.usable)
            return m.label;
    return "Commands";
}

bool AiSettings::selectedIsAi() const
{
    return selectedLabel() != "Commands";
}

// ---- the OpenAI model list ------------------------------------------------------------------------------------------

namespace
{
// A model the chat can use: GPT and o-series chat models, not audio / image / embedding ones or dated snapshots.
bool isChatModel (const juce::String& id)
{
    const auto gpt = id.startsWith ("gpt-") || (id.length() > 1 && id[0] == 'o' && juce::CharacterFunctions::isDigit (id[1]));
    if (! gpt)
        return false;
    for (auto* skip : { "audio", "realtime", "tts", "transcribe", "image", "search", "embedding", "instruct", "codex", "moderation" })
        if (id.contains (skip))
            return false;
    // (gpt-4o-2024-08-06: a dated snapshot of a model that's listed by its own name too)
    const auto tail = id.getLastCharacters (11);
    return ! (tail.length() == 11 && tail[0] == '-' && tail[5] == '-' && tail[8] == '-' && tail.substring (1).containsOnly ("0123456789-"));
}
} // namespace

void AiSettings::fetchModelsIfNeeded (AiProvider p)
{
    if (p == AiProvider::openai && hasKey (p) && providers[(int) p].enabled && providers[(int) p].models.isEmpty() && ! fetching[(int) p])
        fetchModels (p);
}

void AiSettings::fetchModels (AiProvider p)
{
    if (p != AiProvider::openai || fetching[(int) p])
        return;
    const auto apiKey = key (p);
    if (! apiKey)
        return;
    const auto& s = providers[(int) p];
    const auto custom = s.overrideBaseUrl && s.baseUrl.isNotEmpty();
    const auto base = (custom ? s.baseUrl : juce::String ("https://api.openai.com/v1")).trimCharactersAtEnd ("/");

    fetching[(int) p] = true;
    fetchError[(int) p].clear();
    sendChangeMessage();

    juce::Thread::launch ([this, p, base, custom, apiKey = *apiKey, stillAlive = alive] {
        int status = 0;
        juce::StringArray found;
        juce::String error;
        auto stream = juce::URL (base + "/models")
                          .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                  .withExtraHeaders ("Authorization: Bearer " + apiKey)
                                                  .withConnectionTimeoutMs (10000)
                                                  .withStatusCode (&status));
        if (stream == nullptr)
            error = "couldn't reach " + base;
        else if (status == 401)
            error = "the key was rejected";
        else if (status != 200)
            error = "the model list failed (HTTP " + juce::String (status) + ")";
        else
        {
            const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
            if (auto* list = json["data"].getArray())
                for (const auto& m : *list)
                {
                    const auto id = m["id"].toString();
                    if (id.isNotEmpty() && (custom || isChatModel (id))) // (another server: its models as they are)
                        found.add (id);
                }
            found.sort (true);
            std::reverse (found.begin(), found.end()); // (newest first: gpt-5... before gpt-4...)
            if (found.isEmpty())
                error = "no chat models in the list";
        }

        juce::MessageManager::callAsync ([this, p, found, error, stillAlive] {
            if (! stillAlive->load())
                return;
            fetching[(int) p] = false;
            fetchError[(int) p] = error;
            if (! found.isEmpty())
            {
                providers[(int) p].models = found;
                if (! selectedIsAi()) // (no AI model picked yet: the newest GPT model)
                {
                    juce::String pick = found[0];
                    for (const auto& m : found)
                        if (m.startsWith ("gpt-"))
                        {
                            pick = m;
                            break;
                        }
                    selected = "openai:" + pick;
                }
                changed();
            }
            else
                sendChangeMessage();
        });
    });
}

// ---- persistence ----------------------------------------------------------------------------------------------------

void AiSettings::load()
{
    const auto json = juce::JSON::parse (settingsJson().loadFileAsString());
    if (! json.isObject())
        return;
    selected = json.getProperty ("selected", "commands").toString();
    for (auto p : { AiProvider::anthropic, AiProvider::openai, AiProvider::google })
    {
        const auto o = json.getProperty (idOf (p), {});
        auto& s = providers[(int) p];
        s.enabled = (bool) o.getProperty ("enabled", false);
        s.overrideBaseUrl = (bool) o.getProperty ("overrideBaseUrl", false);
        s.baseUrl = o.getProperty ("baseUrl", "").toString();
        s.model = o.getProperty ("model", "").toString();
        if (p == AiProvider::openai) // (its card no longer has these: requests go to OpenAI, models come from its list)
        {
            s.overrideBaseUrl = false;
            s.model.clear();
        }
        s.models.clear();
        if (auto* list = o.getProperty ("models", {}).getArray())
            for (const auto& m : *list)
                s.models.add (m.toString());
    }
}

void AiSettings::save() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("selected", selected);
    for (auto p : { AiProvider::anthropic, AiProvider::openai, AiProvider::google })
    {
        const auto& s = providers[(int) p];
        auto* o = new juce::DynamicObject(); // (never the key: that's in the Keychain)
        o->setProperty ("enabled", s.enabled);
        o->setProperty ("overrideBaseUrl", s.overrideBaseUrl);
        o->setProperty ("baseUrl", s.baseUrl);
        o->setProperty ("model", s.model);
        if (! s.models.isEmpty())
            o->setProperty ("models", s.models);
        root->setProperty (idOf (p), juce::var (o));
    }
    settingsJson().replaceWithText (juce::JSON::toString (juce::var (root)));
}

void AiSettings::changed()
{
    save();
    sendChangeMessage();
}
} // namespace lynx::commands
