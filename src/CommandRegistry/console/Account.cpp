#include "CommandRegistry/console/Account.h"

#include "CommandRegistry/console/Keychain.h"

#include <functional>

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
                userCode.clear();
                setState (State::signedOut);
            });
        };

        // 1. a code for this sign-in
        const auto codeReply = request ("/api/auth/device/code", object ({ { "client_id", clientId } }));
        if (codeReply.status != 200)
            return fail (codeReply.status == 0 ? notAnswering : "The server didn't start the sign-in (" + juce::String (codeReply.status) + ").");
        const auto deviceCode = codeReply.json["device_code"].toString();
        const auto code = codeReply.json["user_code"].toString();
        const auto page = codeReply.json["verification_uri_complete"].toString() + (createAccount ? "&mode=create" : "");
        auto interval = juce::jmax (1, (int) codeReply.json["interval"]);
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (60, (int) codeReply.json["expires_in"]) * 1000;

        // 2. show it, and open the browser on the page that approves it
        post ([this, code, page, mine, gen] {
            if (gen->load() != mine)
                return;
            userCode = code;
            pageUrl = page;
            setState (State::waiting);
            juce::URL (page).launchInDefaultBrowser();
        });

        // 3. wait for the approval
        while (gen->load() == mine)
        {
            for (int t = 0; t < interval * 10 && gen->load() == mine; ++t)
                juce::Thread::sleep (100);
            if (gen->load() != mine)
                return; // (cancelled, or a newer sign-in)
            if (juce::Time::getMillisecondCounter() > deadline)
                return fail ("The sign-in code expired: sign in again.");

            const auto reply = request ("/api/auth/device/token",
                                        object ({ { "grant_type", "urn:ietf:params:oauth:grant-type:device_code" },
                                                  { "device_code", deviceCode },
                                                  { "client_id", clientId } }));
            const auto token = reply.json["access_token"].toString();
            if (reply.status == 200 && token.isNotEmpty())
            {
                post ([this, token, mine, gen] {
                    if (gen->load() == mine)
                        finishSignIn (token);
                });
                return;
            }
            const auto err = reply.json["error"].toString();
            if (reply.status == 0 || err == "authorization_pending")
                continue; // (the server may restart in development: keep waiting)
            if (err == "slow_down")
            {
                interval += 5;
                continue;
            }
            if (err == "access_denied")
                return fail ("The sign-in was declined in the browser.");
            if (err == "expired_token")
                return fail ("The sign-in code expired: sign in again.");
            const auto description = reply.json["error_description"].toString();
            return fail ("The sign-in failed: " + (description.isNotEmpty() ? description : err));
        }
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
