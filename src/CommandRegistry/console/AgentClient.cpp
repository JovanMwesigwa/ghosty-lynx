#include "CommandRegistry/console/AgentClient.h"

#include "CommandRegistry/console/Account.h"

namespace lynx::commands
{
namespace
{
juce::String agentUrl (const juce::String& path) { return Account::serverUrl() + "/api/agent" + path; }

juce::String authHeaders()
{
    juce::String h = "Content-Type: application/json";
    if (const auto token = Account::get().token())
        h << "\r\nAuthorization: Bearer " << *token;
    return h;
}

juce::var errorEvent (const juce::String& message)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("t", "error");
    o->setProperty ("message", message);
    return juce::var (o);
}
} // namespace

AgentClient::AgentClient() : juce::Thread ("Ghosty agent client") {}

AgentClient::~AgentClient()
{
    alive->store (false);
    stop();
    stopThread (3000);
}

void AgentClient::start (const juce::String& t, const juce::String& m, const juce::String& key, const juce::var& commands,
                         const juce::String& url)
{
    if (running)
        return;
    stopThread (2000); // (the last turn's thread, if it's still winding down)
    text = t;
    model = m;
    apiKey = key;
    baseUrl = url;
    commandsJson = juce::JSON::toString (commands, true);
    {
        const juce::ScopedLock lock (turnLock);
        turnId.clear();
    }
    running = true;
    startThread();
}

void AgentClient::stop()
{
    if (! running)
        return;
    juce::Thread::launch ([] { post ("/stop", "{}"); }); // the agent ends its loop (and says "Stopped.")
    const juce::ScopedLock lock (streamLock);
    if (stream != nullptr)
        stream->cancel();
}

void AgentClient::reset()
{
    stop();
    juce::Thread::launch ([] { post ("/reset", "{}"); });
}

void AgentClient::sendResult (const juce::String& callId, bool ok, const juce::var& result, const juce::String& error)
{
    juce::String turn;
    {
        const juce::ScopedLock lock (turnLock);
        turn = turnId;
    }
    if (turn.isEmpty())
        return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("turn", turn);
    o->setProperty ("id", callId);
    o->setProperty ("ok", ok);
    if (ok)
        o->setProperty ("result", result);
    else
        o->setProperty ("error", error);
    const auto json = juce::JSON::toString (juce::var (o), true);
    juce::Thread::launch ([json] { post ("/result", json); });
}

void AgentClient::post (const juce::String& path, const juce::String& json)
{
    // (short and best-effort: a result the agent no longer waits for is simply dropped)
    juce::URL (agentUrl (path)).withPOSTData (json).createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData).withConnectionTimeoutMs (15000).withExtraHeaders (authHeaders()));
}

void AgentClient::run()
{
    auto* body = new juce::DynamicObject();
    body->setProperty ("text", text);
    body->setProperty ("model", model);
    if (baseUrl.isNotEmpty())
        body->setProperty ("baseUrl", baseUrl);
    body->setProperty ("commands", juce::JSON::parse (commandsJson)["groups"]);
    commandsJson.clear();

    juce::WebInputStream web (juce::URL (agentUrl ("/chat")).withPOSTData (juce::JSON::toString (juce::var (body), true)), true);
    web.withExtraHeaders (authHeaders() + "\r\nx-ghosty-key: " + apiKey); // (the user's own key: used for this message, never kept)
    web.withConnectionTimeout (120000); // (on macOS this is also the longest quiet gap: the agent pings every 3 s)
    apiKey.clear();
    {
        const juce::ScopedLock lock (streamLock);
        stream = &web;
    }

    if (! web.connect (nullptr))
        deliver (errorEvent (threadShouldExit() ? juce::String ("Stopped.") : "Ghosty's server isn't answering (" + Account::serverUrl() + ")."));
    else if (web.getStatusCode() == 401)
        deliver (errorEvent ("Your Ghosty sign-in has expired: sign in again."));
    else if (web.getStatusCode() != 200)
        deliver (errorEvent ("Ghosty's server answered " + juce::String (web.getStatusCode()) + "."));
    else
        while (! threadShouldExit() && ! web.isExhausted())
        {
            const auto line = web.readNextLine().trim();
            if (line.isEmpty())
                continue;
            const auto event = juce::JSON::parse (line);
            if (event["t"].toString() == "turn")
            {
                const juce::ScopedLock lock (turnLock);
                turnId = event["id"].toString(); // (the results go to this turn)
                continue;
            }
            deliver (event);
        }

    {
        const juce::ScopedLock lock (streamLock);
        stream = nullptr;
    }
    finish();
}

void AgentClient::deliver (juce::var event)
{
    juce::MessageManager::callAsync ([this, stillAlive = alive, event] {
        if (stillAlive->load() && onEvent && event.isObject())
            onEvent (event);
    });
}

void AgentClient::finish()
{
    juce::MessageManager::callAsync ([this, stillAlive = alive] {
        if (! stillAlive->load())
            return;
        running = false;
        if (onFinished)
            onFinished();
    });
}
} // namespace lynx::commands
