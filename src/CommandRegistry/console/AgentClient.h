#pragma once

// AgentClient: the Ghosty panel's connection to Ghosty's agent, which runs on the Ghosty server (Account::serverUrl(),
// https://api.officialghosty.app; the signed-in user's token goes with every request).
//
// start() sends one user message, with the model, the user's own API key, and LYNX's command list (the agent's tools),
// and reads the reply as it streams: one JSON event per line (the turn's id, text deltas, a command to run, a command
// done, done, error). Each event is handed to `onEvent` on the message thread; `onFinished` follows the last one.
// A "tool" event carries `run` {name, args}: the panel runs that command on the open project and hands its result to
// sendResult(), which posts it back to the waiting agent. stop() ends the turn; reset() starts a new conversation.

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

namespace lynx::commands
{
class AgentClient final : private juce::Thread
{
public:
    AgentClient();
    ~AgentClient() override;

    std::function<void (const juce::var& event)> onEvent; // {"t": "turn" | "text" | "tool" | "tool_end" | "done" | "error", ...}
    std::function<void()> onFinished;

    bool isRunning() const noexcept { return running; }
    // `commands`: the registry's describe() ({"groups": [...]}), the agent's tools
    void start (const juce::String& text, const juce::String& model, const juce::String& apiKey, const juce::var& commands,
                const juce::String& baseUrl = {}); // (OpenAI's "Override Base URL", when it's on)
    void stop();
    void reset(); // forget the conversation (a new chat)

    // A command's result for the running turn's call `callId` (posted in the background).
    void sendResult (const juce::String& callId, bool ok, const juce::var& result, const juce::String& error);

private:
    void run() override;
    void deliver (juce::var event); // → the message thread
    void finish();
    static void post (const juce::String& path, const juce::String& json); // a quick POST to the agent (/stop, /reset, /result)

    juce::String text, model, apiKey, baseUrl, commandsJson;
    juce::CriticalSection turnLock;
    juce::String turnId; // (the running turn: its results go to it)
    std::atomic<bool> running { false };
    juce::CriticalSection streamLock;
    juce::WebInputStream* stream = nullptr; // (while reading: stop() cancels it)
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace lynx::commands
