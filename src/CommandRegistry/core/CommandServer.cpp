#include "CommandRegistry/core/CommandServer.h"

#include <juce_events/juce_events.h>

#include <cstdio>
#include <string>

#if JUCE_MAC
 #include <sys/socket.h>
#endif

namespace lynx::commands
{
namespace
{
constexpr int maxRequestBytes = 1 << 20;   // 1 MB is plenty for a command and its arguments
constexpr int requestTimeoutMs = 3000;     // a client that sends nothing is dropped after this
constexpr int commandTimeoutMs = 5000;     // the message thread is busy (a modal dialog, a long load)

juce::String statusText (int status)
{
    switch (status)
    {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 503: return "Service Unavailable";
        default:  return "Error";
    }
}

void respond (juce::StreamingSocket& client, int status, const juce::var& body)
{
    // (6 decimals at most: 10.7, not 10.699999999999999)
    const auto json = juce::JSON::toString (body, juce::JSON::FormatOptions {}.withSpacing (juce::JSON::Spacing::singleLine).withMaxDecimalPlaces (6)) + "\n";
    const auto bodyBytes = json.toStdString();
    const auto head = "HTTP/1.1 " + juce::String (status) + " " + statusText (status) + "\r\n"
                    + "Content-Type: application/json; charset=utf-8\r\n"
                    + "Content-Length: " + juce::String ((int) bodyBytes.size()) + "\r\n"
                    + "Cache-Control: no-store\r\n"
                    + "Connection: close\r\n\r\n";
    const auto all = head.toStdString() + bodyBytes;

    // (one write can take only part of a long reply, e.g. a plugin's 1,600 params: keep writing until it's all sent)
    for (std::size_t sent = 0; sent < all.size();)
    {
        if (client.waitUntilReady (false, 5000) != 1)
            return; // (the client stopped reading)
        const auto n = client.write (all.data() + sent, (int) juce::jmin<std::size_t> (all.size() - sent, 64 * 1024));
        if (n <= 0)
            return;
        sent += (std::size_t) n;
    }
}

void respondError (juce::StreamingSocket& client, int status, const juce::String& command, const juce::String& message)
{
    auto* body = new juce::DynamicObject();
    body->setProperty ("ok", false);
    if (command.isNotEmpty())
        body->setProperty ("command", command);
    body->setProperty ("error", message);
    respond (client, status, juce::var (body));
}

// "a=1&name=My%20Song" → {"a": 1, "name": "My Song"}; values that read as JSON (numbers, true/false) keep their type
juce::var parseQuery (const juce::String& query)
{
    auto* args = new juce::DynamicObject();
    for (const auto& pair : juce::StringArray::fromTokens (query, "&", ""))
    {
        if (pair.isEmpty())
            continue;
        const auto decode = [] (const juce::String& s) { return juce::URL::removeEscapeChars (s.replaceCharacter ('+', ' ')); };
        const auto key = decode (pair.upToFirstOccurrenceOf ("=", false, false));
        const auto text = decode (pair.fromFirstOccurrenceOf ("=", false, false));
        const auto parsed = juce::JSON::parse (text);
        args->setProperty (key, parsed.isVoid() || parsed.isObject() || parsed.isArray() ? juce::var (text) : parsed);
    }
    return juce::var (args);
}

// Only requests addressed to this machine: a browser page that rebinds its own domain to 127.0.0.1 still sends its
// domain in Host. Clients that send no Host at all (raw sockets) are local programs and allowed.
bool isLocalHost (const juce::String& hostHeader)
{
    if (hostHeader.isEmpty())
        return true;
    auto host = hostHeader.trim().toLowerCase();
    if (host.startsWith ("["))
        host = host.upToFirstOccurrenceOf ("]", true, false); // [::1]:7878 → [::1]
    else
        host = host.upToFirstOccurrenceOf (":", false, false);
    return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}
} // namespace

CommandServer::CommandServer (CommandRegistry& r, int p) : juce::Thread ("LYNX commands"), registry (r), port (p)
{
    listening = listener.createListener (port, "127.0.0.1");
    if (listening)
    {
        std::fprintf (stderr, "LYNX commands: listening on http://127.0.0.1:%d (try: curl localhost:%d/tempo.get)\n", port, port);
        startThread();
    }
    else
    {
        std::fprintf (stderr, "LYNX commands: port %d is taken (another LYNX open?), commands are off in this window\n", port);
    }
}

CommandServer::~CommandServer()
{
    alive->store (false);
    signalThreadShouldExit();
    listener.close(); // wakes the thread blocked in waitForNextConnection()
    stopThread (requestTimeoutMs + 1000);
}

void CommandServer::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
        if (threadShouldExit())
            break;
        if (client == nullptr)
        {
            wait (50); // (an accept error: don't spin)
            continue;
        }
       #if JUCE_MAC
        int on = 1; // a client that hangs up before the reply must not kill the app with SIGPIPE
        setsockopt (client->getRawSocketHandle(), SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof (on));
       #endif
        handleConnection (*client);
    }
}

void CommandServer::handleConnection (juce::StreamingSocket& client)
{
    // ---- read the request: headers up to the blank line, then Content-Length bytes of body
    std::string data;
    std::size_t headerEnd = std::string::npos;
    int contentLength = 0;
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) requestTimeoutMs;

    for (;;)
    {
        if (threadShouldExit() || juce::Time::getMillisecondCounter() > deadline)
            return;
        const auto ready = client.waitUntilReady (true, 100);
        if (ready < 0)
            return;
        if (ready == 0)
            continue;

        char buffer[4096];
        const auto n = client.read (buffer, (int) sizeof (buffer), false);
        if (n <= 0)
            break; // the client closed its side
        data.append (buffer, (std::size_t) n);

        if ((int) data.size() > maxRequestBytes)
            return respondError (client, 413, {}, "request too large");

        if (headerEnd == std::string::npos && (headerEnd = data.find ("\r\n\r\n")) != std::string::npos)
            for (const auto& line : juce::StringArray::fromLines (juce::String (data.substr (0, headerEnd))))
                if (line.startsWithIgnoreCase ("content-length:"))
                    contentLength = juce::jlimit (0, maxRequestBytes, line.fromFirstOccurrenceOf (":", false, false).trim().getIntValue());

        if (headerEnd != std::string::npos && data.size() >= headerEnd + 4 + (std::size_t) contentLength)
            break;
    }
    if (headerEnd == std::string::npos)
        return respondError (client, 400, {}, "expected an HTTP request, e.g. curl localhost:" + juce::String (port) + "/tempo.get");

    // ---- parse: "GET /tempo.get?x=1 HTTP/1.1" + headers + body
    const auto lines = juce::StringArray::fromLines (juce::String (data.substr (0, headerEnd)));
    const auto requestLine = juce::StringArray::fromTokens (lines[0], " ", "");
    const auto method = requestLine[0].toUpperCase();
    const auto target = requestLine[1];

    juce::String host, origin, contentType;
    for (int i = 1; i < lines.size(); ++i)
    {
        const auto value = lines[i].fromFirstOccurrenceOf (":", false, false).trim();
        if (lines[i].startsWithIgnoreCase ("host:"))
            host = value;
        else if (lines[i].startsWithIgnoreCase ("origin:"))
            origin = value;
        else if (lines[i].startsWithIgnoreCase ("content-type:"))
            contentType = value;
    }
    // (a browser names the page that sent the request in Origin: only pages served from this machine)
    const auto originHost = origin.fromFirstOccurrenceOf ("://", false, false);
    if (! isLocalHost (host) || (origin.isNotEmpty() && (originHost.isEmpty() || ! isLocalHost (originHost))))
        return respondError (client, 403, {}, "only local requests (Host: localhost / 127.0.0.1)");

    auto name = juce::URL::removeEscapeChars (target.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/"));
    if (name.isEmpty())
        name = "commands.list";

    juce::var args;
    if (method == "GET")
    {
        args = parseQuery (target.fromFirstOccurrenceOf ("?", false, false));
    }
    else if (method == "POST")
    {
        const auto body = juce::String (data.substr (headerEnd + 4, (std::size_t) contentLength)).trim();
        args = body.isEmpty() ? juce::var (new juce::DynamicObject()) : juce::JSON::parse (body);
        if (! args.isObject())
            return respondError (client, 400, name, "the POST body must be a JSON object, e.g. {\"bpm\": 128}");
    }
    else
    {
        return respondError (client, 405, name, "use GET or POST");
    }

    // A command that changes the project needs POST + a JSON body: a web page can make a browser send a plain GET (or
    // a form POST) to localhost, but not a JSON POST to another site without a CORS preflight, which this server never
    // allows. (find() reads the registry from this thread: it is only filled at start-up, before the server runs.)
    if (const auto* command = registry.find (name); command != nullptr && (command->changesProject || command->changesSettings)
                                                    && (method != "POST" || ! contentType.containsIgnoreCase ("application/json")))
        return respondError (client, 405, name, name + " makes a change: send it as a POST with a JSON body, e.g. "
                                                "curl localhost:" + juce::String (port) + "/" + name + " -H 'Content-Type: application/json' -d '{...}'");

    // ---- run it on the message thread and reply
    const auto result = runOnMessageThread (name, args);
    if (! result.ok)
        return respondError (client, registry.exists (name) ? 400 : 404, name, result.error);

    auto* body = new juce::DynamicObject();
    body->setProperty ("ok", true);
    body->setProperty ("command", name);
    body->setProperty ("result", result.value);
    respond (client, 200, juce::var (body));
}

Result CommandServer::runOnMessageThread (const juce::String& name, const juce::var& args)
{
    struct Pending
    {
        juce::WaitableEvent done;
        Result result;
    };
    auto pending = std::make_shared<Pending>();

    juce::MessageManager::callAsync ([pending, stillAlive = alive, reg = &registry, name, args] {
        // (stillAlive is false once the server, and with it maybe the engine, is gone: do nothing then)
        pending->result = stillAlive->load() ? reg->run (name, args) : Result::failure ("LYNX is closing");
        pending->done.signal();
    });

    for (int waited = 0; ! pending->done.wait (100); waited += 100)
        if (threadShouldExit() || waited >= commandTimeoutMs)
            return Result::failure ("the app didn't answer in time (a dialog open?)");
    return pending->result;
}
} // namespace lynx::commands
