#include "CommandRegistry/account/AccountModule.h"

#include "CommandRegistry/console/Account.h"

namespace lynx::commands
{
namespace
{
juce::String stateName (Account::State s)
{
    switch (s)
    {
        case Account::State::signedOut: return "signed out";
        case Account::State::starting: return "starting";
        case Account::State::waiting: return "waiting for approval";
        case Account::State::signedIn: return "signed in";
    }
    return {};
}

juce::var orNull (const juce::String& s) { return s.isEmpty() ? juce::var() : juce::var (s); }

juce::var status()
{
    const auto& a = Account::get();
    return makeObject ({ { "signedIn", a.state() == Account::State::signedIn },
                         { "state", stateName (a.state()) },
                         { "name", orNull (a.name()) },
                         { "email", orNull (a.email()) },
                         { "plan", orNull (a.plan()) },
                         { "billedEvery", orNull (a.billedEvery()) },
                         { "planText", orNull (a.planText()) },
                         { "subscription", a.hasSubscription() ? makeObject ({ { "status", a.subscriptionState() },
                                                                                { "endsAtPeriodEnd", a.endsAtPeriodEnd() },
                                                                                { "nextBillingDate", orNull (a.nextBillingDate()) } })
                                                               : juce::var() },
                         { "upgrading", a.upgrading() },
                         { "onTrial", a.onTrial() },
                         { "trialEnds", orNull (a.trialEndDate()) },
                         { "trialOnUpgrade", a.trialDays() },
                         { "billingAvailable", a.billingAvailable() },
                         { "code", orNull (a.code()) },
                         { "approvalPage", orNull (a.verificationUrl()) },
                         { "error", orNull (a.error()) },
                         { "server", Account::serverUrl() } });
}
} // namespace

void AccountModule::define()
{
    query ("status", "Whether LYNX is signed in to Ghosty (its AI account), and as whom (name, email, plan)",
           [] (Context&, const Args&) { return Result::success (status()); });

    action ("signin", "Sign in to Ghosty: the browser opens on the page that approves this LYNX (account status shows the code)", {},
            [] (Context&, const Args&) {
                if (Account::get().state() == Account::State::signedIn)
                    return Result::failure ("already signed in (account signout first to use another account)");
                Account::get().signIn();
                return Result::success (makeObject ({ { "state", "starting" },
                                                      { "next", "approve the code in the browser that opens (account status shows it)" } }));
            });

    action ("signout", "Sign out of Ghosty", {}, [] (Context&, const Args&) {
        if (Account::get().state() == Account::State::signedOut)
            return Result::failure ("not signed in");
        Account::get().signOut();
        return Result::success (status());
    });

    action ("upgrade", "Upgrade Ghosty to the Launch plan, billed monthly or yearly: Dodo's checkout opens in the browser; LYNX switches once the payment goes through",
            { Arg::text ("billing", "monthly (default) or yearly").optional() },
            [] (Context&, const Args& args) {
                auto& a = Account::get();
                if (a.state() != Account::State::signedIn)
                    return Result::failure ("sign in first (account signin)");
                if (a.isPaid())
                    return Result::failure ("already on Launch (account manage changes or cancels it)");
                if (! a.billingAvailable())
                    return Result::failure ("billing isn't set up on Ghosty's server yet");
                const auto billing = args.has ("billing") ? args.text ("billing").trim().toLowerCase() : juce::String ("monthly");
                const auto interval = billing.startsWith ("year") || billing == "annual" ? "year" : billing.startsWith ("month") ? "month" : "";
                if (juce::String (interval).isEmpty())
                    return Result::failure ("--billing: monthly or yearly");
                juce::String label;
                for (const auto& o : a.offers())
                    if (o.interval == interval)
                        label = o.label;
                if (label.isEmpty())
                    return Result::failure (juce::String ("Launch isn't offered ") + (juce::String (interval) == "year" ? "yearly" : "monthly") + " on this server");
                a.upgrade (interval);
                return Result::success (makeObject ({ { "plan", "launch" },
                                                      { "price", label },
                                                      { "next", "finish the payment in the browser; account status shows Launch once it's through" } }));
            });

    action ("manage", "Manage the subscription in Dodo's customer portal (cancel, change the card, invoices): it opens in the browser", {},
            [] (Context&, const Args&) {
                auto& a = Account::get();
                if (a.state() != Account::State::signedIn)
                    return Result::failure ("sign in first (account signin)");
                if (! a.isPaid() && ! a.hasSubscription())
                    return Result::failure ("there's no subscription yet (account upgrade)");
                a.manageSubscription();
                return Result::success (makeObject ({ { "next", "the customer portal opens in the browser" } }));
            });

    examples ({ { "status", "account status" },
                { "signin", "account signin" },
                { "signout", "account signout" },
                { "upgrade", "account upgrade --billing yearly" },
                { "manage", "account manage" } });
}
} // namespace lynx::commands
