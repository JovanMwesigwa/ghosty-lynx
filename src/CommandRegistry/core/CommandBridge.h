#pragma once

// CommandBridge: what the app owns (src/Main.cpp: made after the AudioEngine, destroyed before it).
// It holds the Context the handlers share, installs every module (CommandBridge.cpp lists them) and starts the HTTP
// server.
//
//   LYNX_COMMAND_PORT=7878   the port (default 7878); 0 turns the bridge off

#include "CommandRegistry/core/CommandModule.h"
#include "CommandRegistry/core/CommandRegistry.h"
#include "CommandRegistry/core/CommandServer.h"

#include <functional>
#include <memory>
#include <vector>

namespace lynx::commands
{
class CommandBridge
{
public:
    // `mainUi`: the main window's component, or nullptr while there is none (the bridge starts before the window).
    CommandBridge (AudioEngine& engine, std::function<MainComponent*()> mainUi);
    ~CommandBridge();

    CommandRegistry& getRegistry() noexcept { return registry; }

    // The running bridge's registry, for in-app clients such as the Console panel (nullptr when there is none).
    // Message thread only.
    static CommandRegistry* activeRegistry() noexcept;

private:
    // (destroyed bottom-up: the server stops first, so no command runs while the rest goes)
    Context context;
    CommandRegistry registry;
    std::vector<std::unique_ptr<CommandModule>> modules;
    std::unique_ptr<CommandServer> server;
};
} // namespace lynx::commands
