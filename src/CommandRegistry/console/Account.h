#pragma once

// Account: the LYNX account this app is signed in to (the LYNX server: server/, NestJS + Better Auth).
//
// Signing in, as desktop apps do it (a loopback redirect with PKCE): LYNX listens on 127.0.0.1 (a free port), opens the
// browser on the server's /desktop/authorize page (sign in or create the account there), and the browser comes back to
// LYNX's address with a one-time code, which LYNX trades (with a secret only it has) for a session token, kept in the
// Keychain ("lynx-session") and sent as "Authorization: Bearer <token>" from then on. LYNX never sees the password.
//
//   signedOut ──signIn()──▶ starting ──▶ waiting (the browser open) ──back with the code──▶ signedIn
//                                            └──cancel() / denied / expired──▶ signedOut (with `error`)
//
// One shared instance (get()), message thread only; it broadcasts a change message whenever its state changes.
//
//   LYNX_SERVER_URL=https://api.officialghosty.app   where Ghosty's server is (bun run dev: the local one, 127.0.0.1:7882)

#include <juce_events/juce_events.h>

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace lynx::commands
{
class Account final : public juce::ChangeBroadcaster
{
public:
    static Account& get();
    static juce::String serverUrl();

    enum class State { signedOut, starting, waiting, signedIn };
    State state() const noexcept { return current; }

    // signedIn: who (name / email / plan come from the server; empty until it has answered)
    const juce::String& name() const noexcept { return userName; }
    const juce::String& email() const noexcept { return userEmail; }
    const juce::String& plan() const noexcept { return userPlan; }          // "free" | "launch"
    bool isPaid() const noexcept { return userPlan.isNotEmpty() && userPlan != "free"; }
    bool planKnown() const noexcept { return userPlan.isNotEmpty(); } // (false: the server hasn't answered yet)
    const juce::String& billedEvery() const noexcept { return planInterval; } // "month" | "year" (on a paid plan)

    // what an upgrade can be, as the server offers it: { "month", "$50 / month" }, { "year", "$490 / year" }
    struct Offer
    {
        juce::String interval, label;
    };
    const std::vector<Offer>& offers() const noexcept { return planOffers; }

    // billing (the server's Dodo Payments): whether upgrades are possible, the subscription, and an upgrade in progress
    bool billingAvailable() const noexcept { return billingConfigured; }
    bool hasSubscription() const noexcept { return subscriptionStatus.isNotEmpty(); }
    const juce::String& subscriptionState() const noexcept { return subscriptionStatus; } // active, on_hold, cancelled...
    bool endsAtPeriodEnd() const noexcept { return cancelAtPeriodEnd; }
    const juce::String& nextBillingDate() const noexcept { return nextBilling; }        // ISO date, or empty
    bool upgrading() const noexcept { return upgradeInProgress; }
    int trialDays() const noexcept { return trialOffer; }                      // the free trial an upgrade gives now (0: none)
    bool onTrial() const noexcept { return trialEnds.isNotEmpty(); }
    const juce::String& trialEndDate() const noexcept { return trialEnds; }     // ISO date, while on the trial
    // "Launch plan (yearly) · renews 10 Dec 2027", "Launch plan (monthly) · ends 10 Dec 2026", "Free plan"...
    juce::String planText() const;
    // waiting: the code to approve, and the page that approves it
    const juce::String& code() const noexcept { return userCode; }
    const juce::String& verificationUrl() const noexcept { return pageUrl; }
    // what went wrong last (sign-in denied, expired, the server not answering...); empty when nothing did
    const juce::String& error() const noexcept { return lastError; }

    // Starts a device sign-in and opens the browser; `createAccount`: the page opens on "Create account" (a new user)
    void signIn (bool createAccount = false);
    void openPageAgain();    // waiting: the browser on the approval page again
    void cancel();           // stops waiting
    void signOut();          // ends the session on the server (best effort) and forgets the token
    void refresh();          // asks the server who the token belongs to again (in the background)

    // Launch, billed every `interval` ("month" / "year"): opens Dodo's checkout in the browser, then asks the server every
    // few seconds until the plan turns paid (or 20 minutes pass, or cancelUpgrade()).
    void upgrade (const juce::String& interval);
    void cancelUpgrade();
    // Opens the account page in the browser (switch monthly / yearly, cancel or resume, billing and invoices); LYNX
    // checks the plan again for a while after.
    void manageSubscription();
    void openUpgradePage(); // the plans compared, in the browser
    void checkPlan();       // asks the server to check with the payment provider now (after paying), then updates the plan

    std::optional<juce::String> token() const; // the session token (for requests to the server)

private:
    Account();
    ~Account() override;
    void setState (State s);
    void finishSignIn (const juce::String& token);
    void applyBilling (const juce::var& billing);
    void watchBilling (bool untilPaid, int seconds); // (in the background: sync with Dodo until the plan is paid / for a while)

    State current = State::signedOut;
    juce::String userName, userEmail, userPlan, userCode, pageUrl, lastError;
    bool billingConfigured = false, cancelAtPeriodEnd = false, upgradeInProgress = false;
    juce::String subscriptionStatus, nextBilling, planInterval, trialEnds, switchesTo, switchesAt;
    int trialOffer = 0;
    std::vector<Offer> planOffers;
    std::shared_ptr<std::atomic<int>> billingWatch = std::make_shared<std::atomic<int>> (0); // (a newer watch stops the older)
    std::shared_ptr<std::atomic<int>> generation = std::make_shared<std::atomic<int>> (0); // (a newer sign-in or a cancel: the old one stops)
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace lynx::commands
