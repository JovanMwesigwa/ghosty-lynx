#include "CommandRegistry/console/Account.h"

#include "CommandRegistry/console/Keychain.h"
#include "core/Sha256.h"

#include <functional>
#include <random>

namespace lynx::commands
{
namespace
{
constexpr const char* tokenAccount = "lynx-session"; // (the Keychain item)
constexpr const char* clientId = "lynx-desktop";     // (the server's device sign-in checks it)

struct Reply
{
    int status = 0; // 0: no answer
    juce::var json;
};

// One request to the server (a background thread): POST with a JSON body, or GET when `body` is void.
Reply request (const juce::String& path, const juce::var& body, const juce::String& token = {})
{
    juce::String headers = "Content-Type: application/json\r\nOrigin: " + Account::serverUrl();
    if (token.isNotEmpty())
        headers << "\r\nAuthorization: Bearer " << token;
    auto url = juce::URL (Account::serverUrl() + path);
    const auto post = ! body.isVoid();
    if (post)
        url = url.withPOSTData (juce::JSON::toString (body, true));

    Reply reply;
    auto stream = url.createInputStream (juce::URL::InputStreamOptions (post ? juce::URL::ParameterHandling::inPostData
                                                                             : juce::URL::ParameterHandling::inAddress)
                                             .withExtraHeaders (headers)
                                             .withConnectionTimeoutMs (8000)
                                             .withStatusCode (&reply.status));
    if (stream == nullptr)
    {
        reply.status = 0;
        return reply;
    }
    reply.json = juce::JSON::parse (stream->readEntireStreamAsString());
    return reply;
}

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> properties)
{
    auto* o = new juce::DynamicObject();
    for (const auto& [k, v] : properties)
        o->setProperty (k, v);
    return juce::var (o);
}

// A random hex string of `bytes` bytes (the sign-in's state and PKCE verifier).
juce::String randomHex (int bytes)
{
    std::random_device device;
    juce::String out;
    for (int i = 0; i < bytes; ++i)
        out << juce::String::toHexString ((int) (device() & 0xff)).paddedLeft ('0', 2);
    return out;
}

// The first line of an HTTP request on `client` ("GET /callback?... HTTP/1.1"; the rest is read and ignored).
juce::String readRequestLine (juce::StreamingSocket& client)
{
    juce::MemoryBlock data;
    char buffer[2048];
    for (int tries = 0; tries < 20 && data.getSize() < 16384; ++tries)
    {
        if (client.waitUntilReady (true, 250) != 1)
            continue;
        const auto n = client.read (buffer, (int) sizeof (buffer), false);
        if (n <= 0)
            break;
        data.append (buffer, (size_t) n);
        if (data.toString().contains ("\r\n\r\n"))
            break;
    }
    return data.toString().upToFirstOccurrenceOf ("\r\n", false, false);
}

void respond (juce::StreamingSocket& client, int status, const juce::String& body)
{
    const auto bytes = body.toStdString();
    const auto head = "HTTP/1.1 " + juce::String (status) + (status == 200 ? " OK" : status == 404 ? " Not Found" : " Bad Request")
                    + "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + juce::String ((int) bytes.size())
                    + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    const auto all = head.toStdString() + bytes;
    client.write (all.data(), (int) all.size());
}

// The page the browser shows on LYNX's address after the sign-in (Ghosty's look).
juce::String pageHtml (const juce::String& title, const juce::String& text)
{
    return "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
           "<title>" + title + " · Ghosty</title><style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#101114;"
           "color:#ecedef;font:16px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}main{text-align:center;padding:24px}"
           "h1{font-size:22px;margin:0 0 8px}p{color:#9aa0a8;margin:0}</style></head><body><main><h1>" + title + "</h1><p>" + text
           + "</p></main></body></html>";
}

const juce::String notAnswering = "Ghosty's server isn't answering (bun run dev starts it).";
} // namespace

Account& Account::get()
{
    static Account instance;
    return instance;
}

juce::String Account::serverUrl()
{
    return juce::SystemStats::getEnvironmentVariable ("LYNX_SERVER_URL", "https://api.officialghosty.app").trimCharactersAtEnd ("/");
}

Account::Account()
{
    if (keychain::has (tokenAccount))
    {
        current = State::signedIn; // (until the server says otherwise)
        refresh();
    }
}

Account::~Account()
{
    alive->store (false);
    generation->fetch_add (1);
}

std::optional<juce::String> Account::token() const
{
    return keychain::get (tokenAccount);
}

void Account::setState (State s)
{
    current = s;
    sendChangeMessage();
}

// ---- signing in ------------------------------------------------------------------------------------------------------

void Account::signIn (bool createAccount)
{
    const auto mine = generation->fetch_add (1) + 1; // (any earlier sign-in stops)
    lastError.clear();
    userCode.clear();
    pageUrl.clear();
    setState (State::starting);

    juce::Thread::launch ([this, mine, createAccount, gen = generation, stillAlive = alive] {
        auto post = [stillAlive] (std::function<void()> f) {
            juce::MessageManager::callAsync ([stillAlive, f = std::move (f)] {
                if (stillAlive->load())
                    f();
            });
        };
        auto fail = [this, post, mine, gen] (const juce::String& message) {
            post ([this, message, mine, gen] {
                if (gen->load() != mine)
                    return;
                lastError = message;
                pageUrl.clear();
                setState (State::signedOut);
            });
        };

        // 1. LYNX's address for the browser to come back to: 127.0.0.1, a free port, only for this sign-in
        juce::StreamingSocket listener;
        if (! listener.createListener (0, "127.0.0.1"))
            return fail ("LYNX couldn't open its sign-in address on this computer.");
        const auto redirect = "http://127.0.0.1:" + juce::String (listener.getBoundPort()) + "/callback";
        const auto state = randomHex (24), verifier = randomHex (32);
        const auto challenge = juce::String (lynx::sha256Hex ((const std::uint8_t*) verifier.toRawUTF8(), verifier.getNumBytesAsUTF8()));
        const auto page = juce::URL (serverUrl() + "/desktop/authorize")
                              .withParameter ("redirect_uri", redirect)
                              .withParameter ("state", state)
                              .withParameter ("challenge", challenge)
                              .withParameter ("mode", createAccount ? "create" : "signin")
                              .toString (true);

        // 2. the browser: sign in (or create the account) there
        post ([this, page, mine, gen] {
            if (gen->load() != mine)
                return;
            pageUrl = page;
            setState (State::waiting);
            juce::URL (page).launchInDefaultBrowser();
        });

        // 3. wait for it to come back with the one-time code (10 minutes; Cancel or a new sign-in stops it)
        const auto deadline = juce::Time::getMillisecondCounter() + 10u * 60u * 1000u;
        while (gen->load() == mine && juce::Time::getMillisecondCounter() < deadline)
        {
            if (listener.waitUntilReady (true, 500) != 1)
                continue;
            std::unique_ptr<juce::StreamingSocket> client (listener.waitForNextConnection());
            if (client == nullptr)
                continue;

            const auto requestLine = readRequestLine (*client); // "GET /callback?code=...&state=... HTTP/1.1"
            const auto target = requestLine.fromFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (" ", false, false);
            if (! target.startsWith ("/callback"))
            {
                respond (*client, 404, "Not found");
                continue; // (a browser asking for a favicon, say)
            }
            const juce::URL url ("http://127.0.0.1" + target);
            const auto names = url.getParameterNames(), values = url.getParameterValues();
            auto param = [&] (const char* name) { return values[names.indexOf (name)]; };
            if (param ("state") != state)
            {
                respond (*client, 400, pageHtml ("This sign-in isn't LYNX's", "Start it again from LYNX."));
                continue;
            }

            // 4. the code (with the verifier only LYNX has) for a session token
            const auto reply = request ("/api/desktop/token", object ({ { "code", param ("code") }, { "verifier", verifier } }));
            const auto token = reply.json["token"].toString();
            if (reply.status != 200 || token.isEmpty())
            {
                respond (*client, 200, pageHtml ("The sign-in didn't work", "Go back to LYNX and try again."));
                return fail (reply.status == 0 ? notAnswering : reply.json["message"].toString().isNotEmpty() ? reply.json["message"].toString()
                                                                                                                : juce::String ("The sign-in didn't work: try again."));
            }
            respond (*client, 200, pageHtml ("You're signed in", "You can close this tab and go back to LYNX."));
            post ([this, token, mine, gen] {
                if (gen->load() != mine)
                    return;
                finishSignIn (token);
                juce::Process::makeForegroundProcess(); // (back to LYNX)
            });
            return;
        }
        if (gen->load() == mine)
            fail ("The sign-in timed out: try again.");
    });
}

void Account::finishSignIn (const juce::String& token)
{
    juce::String error;
    if (! keychain::set (tokenAccount, token, error))
    {
        lastError = "Signed in, but the Keychain refused the token: " + error;
        setState (State::signedOut);
        return;
    }
    userCode.clear();
    pageUrl.clear();
    lastError.clear();
    setState (State::signedIn);
    refresh();
}

void Account::openPageAgain()
{
    if (current == State::waiting && pageUrl.isNotEmpty())
        juce::URL (pageUrl).launchInDefaultBrowser();
}

void Account::cancel()
{
    generation->fetch_add (1);
    userCode.clear();
    pageUrl.clear();
    if (current == State::starting || current == State::waiting)
        setState (State::signedOut);
}

// ---- signed in -------------------------------------------------------------------------------------------------------

void Account::refresh()
{
    const auto t = token();
    if (! t)
        return;
    juce::Thread::launch ([this, token = *t, stillAlive = alive] {
        const auto reply = request ("/api/me", {}, token);
        juce::MessageManager::callAsync ([this, reply, stillAlive] {
            if (! stillAlive->load() || current != State::signedIn)
                return;
            if (reply.status == 200)
            {
                userName = reply.json["user"]["name"].toString();
                userEmail = reply.json["user"]["email"].toString();
                userPlan = reply.json["plan"].toString();
                applyBilling (reply.json["billing"]);
                lastError.clear();
            }
            else if (reply.status == 401)
            {
                // the session ended on the server (expired, or signed out elsewhere)
                keychain::remove (tokenAccount);
                userName = userEmail = userPlan = {};
                lastError = "Your sign-in expired: sign in again.";
                current = State::signedOut;
            }
            else
                lastError = reply.status == 0 ? notAnswering : "The server answered " + juce::String (reply.status) + ".";
            sendChangeMessage();
        });
    });
}

void Account::signOut()
{
    cancel();
    billingWatch->fetch_add (1);
    upgradeInProgress = false;
    billingConfigured = cancelAtPeriodEnd = false;
    subscriptionStatus = nextBilling = planInterval = trialEnds = switchesTo = switchesAt = {};
    trialOffer = 0;
    planOffers.clear();
    if (const auto t = token())
        juce::Thread::launch ([token = *t] { request ("/api/auth/sign-out", object ({}), token); }); // (best effort)
    keychain::remove (tokenAccount);
    userName = userEmail = userPlan = {};
    lastError.clear();
    setState (State::signedOut);
}
// ---- billing ---------------------------------------------------------------------------------------------------------

juce::String Account::planText() const
{
    auto date = [] (const juce::String& iso) {
        const auto t = juce::Time::fromISO8601 (iso);
        return t.toMilliseconds() > 0 ? t.formatted ("%d %b %Y") : juce::String();
    };
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    if (isPaid())
    {
        const auto when = date (nextBilling);
        const auto name = userPlan.substring (0, 1).toUpperCase() + userPlan.substring (1); // ("Launch")
        const auto billed = planInterval == "year" ? " (yearly)" : planInterval == "month" ? " (monthly)" : "";
        if (onTrial())
            return name + " plan" + billed + dot + "free trial until " + date (trialEnds) + (cancelAtPeriodEnd ? " (cancelled)" : "");
        auto text = name + " plan" + billed + (when.isEmpty() ? juce::String() : dot + (cancelAtPeriodEnd ? "ends " : "renews ") + when);
        if (switchesTo.isNotEmpty() && ! cancelAtPeriodEnd)
            text << dot << (switchesTo == "year" ? "yearly" : "monthly") << " from " << date (switchesAt);
        return text;
    }
    if (subscriptionStatus == "on_hold" || subscriptionStatus == "past_due" || subscriptionStatus == "failed")
        return "Free plan" + dot + "the payment failed";
    return userPlan.isEmpty() ? juce::String() : juce::String ("Free plan");
}

void Account::applyBilling (const juce::var& billing)
{
    if (! billing.isObject())
        return;
    userPlan = billing["plan"].toString();
    billingConfigured = (bool) billing["configured"];
    const auto& sub = billing["subscription"];
    subscriptionStatus = sub.isObject() ? sub["status"].toString() : juce::String();
    cancelAtPeriodEnd = sub.isObject() && (bool) sub["cancelAtPeriodEnd"];
    nextBilling = sub.isObject() && ! sub["nextBillingAt"].isVoid() ? sub["nextBillingAt"].toString() : juce::String();
    planInterval = billing["interval"].isString() ? billing["interval"].toString() : juce::String();
    trialOffer = (int) billing["trialDays"];
    trialEnds = sub.isObject() && sub["trialEndsAt"].isString() ? sub["trialEndsAt"].toString() : juce::String();
    switchesTo = sub.isObject() && sub["switchesTo"].isString() ? sub["switchesTo"].toString() : juce::String();
    switchesAt = sub.isObject() && sub["switchesAt"].isString() ? sub["switchesAt"].toString() : juce::String();
    planOffers.clear();
    if (auto* list = billing["offers"].getArray())
        for (const auto& o : *list)
            planOffers.push_back ({ o["interval"].toString(), o["label"].toString() });
    if (isPaid())
        upgradeInProgress = false;
}

void Account::upgrade (const juce::String& interval)
{
    const auto t = token();
    if (current != State::signedIn || ! t || upgradeInProgress)
        return;
    upgradeInProgress = true;
    lastError.clear();
    sendChangeMessage();

    juce::Thread::launch ([this, interval, token = *t, stillAlive = alive] {
        const auto reply = request ("/api/billing/checkout", object ({ { "interval", interval } }), token);
        juce::MessageManager::callAsync ([this, reply, stillAlive] {
            if (! stillAlive->load())
                return;
            const auto url = reply.json["url"].toString();
            if (reply.status != 200 || url.isEmpty())
            {
                upgradeInProgress = false;
                lastError = reply.status == 0 ? notAnswering : reply.json["message"].toString();
                sendChangeMessage();
                return;
            }
            juce::URL (url).launchInDefaultBrowser();
            watchBilling (true, 20 * 60); // (until the payment shows up as the plan)
        });
    });
}

void Account::cancelUpgrade()
{
    billingWatch->fetch_add (1);
    upgradeInProgress = false;
    sendChangeMessage();
}

void Account::manageSubscription()
{
    if (current != State::signedIn)
        return;
    juce::URL (serverUrl() + "/account").launchInDefaultBrowser();
    watchBilling (false, 10 * 60); // (a switch or a cancel there: picked up while the page is open)
}

void Account::checkPlan()
{
    const auto t = token();
    if (current != State::signedIn || ! t)
        return;
    lastError.clear();
    sendChangeMessage();
    juce::Thread::launch ([this, token = *t, stillAlive = alive] {
        const auto reply = request ("/api/billing/sync", object ({}), token);
        juce::MessageManager::callAsync ([this, reply, stillAlive] {
            if (! stillAlive->load() || current != State::signedIn)
                return;
            if (reply.status == 200)
                applyBilling (reply.json);
            else
                lastError = reply.status == 0 ? notAnswering : "The server answered " + juce::String (reply.status) + ".";
            sendChangeMessage();
        });
    });
}

void Account::openUpgradePage()
{
    if (current != State::signedIn)
        return;
    juce::URL (serverUrl() + "/upgrade").launchInDefaultBrowser();
    watchBilling (true, 20 * 60); // (until the payment shows up as the plan)
}

void Account::watchBilling (bool untilPaid, int seconds)
{
    const auto t = token();
    if (! t)
        return;
    const auto mine = billingWatch->fetch_add (1) + 1;
    juce::Thread::launch ([this, untilPaid, seconds, mine, watch = billingWatch, token = *t, stillAlive = alive] {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) seconds * 1000;
        while (watch->load() == mine && juce::Time::getMillisecondCounter() < deadline)
        {
            for (int i = 0; i < 50 && watch->load() == mine; ++i)
                juce::Thread::sleep (100); // (every 5 s)
            if (watch->load() != mine)
                return;
            const auto reply = request ("/api/billing/sync", object ({}), token);
            if (reply.status != 200)
                continue;
            const auto paid = reply.json["plan"].toString() != "free";
            juce::MessageManager::callAsync ([this, reply, mine, watch, stillAlive] {
                if (! stillAlive->load() || watch->load() != mine || current != State::signedIn)
                    return;
                applyBilling (reply.json);
                sendChangeMessage();
            });
            if (untilPaid && paid)
                return;
        }
        juce::MessageManager::callAsync ([this, mine, watch, stillAlive] {
            if (stillAlive->load() && watch->load() == mine && upgradeInProgress)
            {
                upgradeInProgress = false; // (no payment seen in time: the button comes back)
                sendChangeMessage();
            }
        });
    });
}
} // namespace lynx::commands
