#pragma once

// CommandServer: a tiny HTTP server on 127.0.0.1 that runs CommandRegistry commands.
//
//   GET  /                       → commands.list
//   GET  /tempo                  → the tempo group's commands
//   GET  /tempo.get              → runs "tempo.get"
//   GET  /some.command?a=1&b=x   → arguments from the query string ({"a": 1, "b": "x"})
//   POST /tempo.set {"bpm": 128} → arguments from a JSON body
//
// Commands that change the project (Command::changesProject) only run from a POST with a JSON body.
// Every reply is JSON: {"ok": true, "command": "...", "result": ...} or {"ok": false, "error": "..."}.
//
// One background thread accepts connections and parses requests; each command is then handed to the message (UI)
// thread, the only thread allowed to touch the Song, and the server thread waits for its result.
//
// Only local programs can connect (it listens on 127.0.0.1), and requests whose Host header is not localhost are
// refused, so a web page open in a browser cannot reach it through DNS rebinding.

#include "CommandRegistry/core/CommandRegistry.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>

namespace lynx::commands
{
class CommandServer final : private juce::Thread
{
public:
    // Starts listening at once; isListening() says whether the port could be taken.
    CommandServer (CommandRegistry& registry, int port);
    ~CommandServer() override; // stops the thread; commands still queued for the message thread are dropped

    bool isListening() const noexcept { return listening; }
    int getPort() const noexcept { return port; }

private:
    void run() override;
    void handleConnection (juce::StreamingSocket& client);
    Result runOnMessageThread (const juce::String& name, const juce::var& args);

    CommandRegistry& registry;
    const int port;
    juce::StreamingSocket listener;
    bool listening = false;
    // Read by commands queued on the message thread: false once this server is gone, so a late one does nothing.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace lynx::commands
