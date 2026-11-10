#include "CommandRegistry/console/Keychain.h"

#include "platform/Paths.h"

#if JUCE_MAC
 #include <Security/Security.h>
#endif

#include <vector>

namespace lynx::commands::keychain
{
#if JUCE_MAC
// ---- the Keychain: one generic-password item per provider, service "LYNX SHARE" -----------------------------------

constexpr const char* keychainService = "LYNX SHARE";

struct CfDictionary
{
    CFMutableDictionaryRef ref = CFDictionaryCreateMutable (nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    std::vector<CFTypeRef> owned;
    ~CfDictionary()
    {
        for (auto o : owned)
            CFRelease (o);
        CFRelease (ref);
    }
    void set (CFStringRef key, CFTypeRef value) { CFDictionarySetValue (ref, key, value); }
    void setOwned (CFStringRef key, CFTypeRef value)
    {
        set (key, value);
        owned.push_back (value);
    }
};

static void identify (CfDictionary& q, const juce::String& account)
{
    q.set (kSecClass, kSecClassGenericPassword);
    q.setOwned (kSecAttrService, juce::String (keychainService).toCFString());
    q.setOwned (kSecAttrAccount, account.toCFString());
}

bool has (const juce::String& account)
{
    CfDictionary q;
    identify (q, account);
    q.set (kSecMatchLimit, kSecMatchLimitOne);
    return SecItemCopyMatching (q.ref, nullptr) == errSecSuccess;
}

std::optional<juce::String> get (const juce::String& account)
{
    CfDictionary q;
    identify (q, account);
    q.set (kSecMatchLimit, kSecMatchLimitOne);
    q.set (kSecReturnData, kCFBooleanTrue);
    CFTypeRef result = nullptr;
    if (SecItemCopyMatching (q.ref, &result) != errSecSuccess || result == nullptr)
        return std::nullopt;
    const auto data = (CFDataRef) result;
    auto text = juce::String::fromUTF8 ((const char*) CFDataGetBytePtr (data), (int) CFDataGetLength (data));
    CFRelease (result);
    return text;
}

bool set (const juce::String& account, const juce::String& secret, juce::String& error)
{
    {
        CfDictionary old; // (replace: remove any earlier one)
        identify (old, account);
        SecItemDelete (old.ref);
    }
    CfDictionary q;
    identify (q, account);
    const auto utf8 = secret.toStdString();
    q.setOwned (kSecValueData, CFDataCreate (nullptr, (const UInt8*) utf8.data(), (CFIndex) utf8.size()));
    q.set (kSecAttrAccessible, kSecAttrAccessibleWhenUnlocked);
    const auto status = SecItemAdd (q.ref, nullptr);
    if (status != errSecSuccess)
        error = "the Keychain refused it (error " + juce::String ((int) status) + ")";
    return status == errSecSuccess;
}

void remove (const juce::String& account)
{
    CfDictionary q;
    identify (q, account);
    SecItemDelete (q.ref);
}
#else
// ---- no Keychain: a file in the settings folder (one "<account>=<key>" per line) ------------------------------------

static juce::File keyFile() { return platform::settingsFile ("console-ai-keys.txt"); } // (the name from when only API keys were kept)

static juce::StringPairArray readKeys()
{
    juce::StringPairArray keys;
    for (const auto& line : juce::StringArray::fromLines (keyFile().loadFileAsString()))
        if (line.containsChar ('='))
            keys.set (line.upToFirstOccurrenceOf ("=", false, false), line.fromFirstOccurrenceOf ("=", false, false));
    return keys;
}

static void writeKeys (const juce::StringPairArray& keys)
{
    juce::String text;
    for (const auto& k : keys.getAllKeys())
        text << k << "=" << keys[k] << "\n";
    keyFile().replaceWithText (text);
}

bool has (const juce::String& account) { return readKeys()[account].isNotEmpty(); }
std::optional<juce::String> get (const juce::String& account)
{
    const auto k = readKeys()[account];
    return k.isEmpty() ? std::nullopt : std::optional<juce::String> (k);
}
bool set (const juce::String& account, const juce::String& secret, juce::String&)
{
    auto keys = readKeys();
    keys.set (account, secret);
    writeKeys (keys);
    return true;
}
void remove (const juce::String& account)
{
    auto keys = readKeys();
    // (by position: StringPairArray::remove has a key and an index overload, ambiguous for a String with GCC)
    if (const auto index = keys.getAllKeys().indexOf (account); index >= 0)
        keys.remove (index);
    writeKeys (keys);
}
#endif
} // namespace lynx::commands::keychain
