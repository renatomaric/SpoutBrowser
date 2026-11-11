#pragma once

#include <string>

struct ID3D11Device;
class spoutDX;

namespace client::spout {

    // Process exit codes used when a Spout related flag cannot be honored.
    constexpr int kExitBadArguments = 2;
    constexpr int kExitSenderNameInUse = 3;

    // Spout sender names are limited to 256 chars including the terminator.
    constexpr size_t kMaxSenderNameBytes = 255;

    // Appends a timestamped line to debug.log (next to the executable).
    // Safe to call before CefInitialize.
    void AppendDebugLog(const std::string& message);

    // Logs |message| to debug.log and terminates the process with |exit_code|.
    [[noreturn]] void FailFast(const std::string& message, int exit_code);

    // Returns true if a Spout sender called exactly |name| is already running.
    // Stale registry entries of crashed senders are cleaned up first.
    bool IsSenderNameInUse(const std::string& name);

    class TextureSender {

    public:
        TextureSender() {}
        virtual ~TextureSender() { Cleanup(); }

        // |senderName| is UTF-8. Empty keeps the default sender name
        // (the executable name, "SpoutBrowser"). A name that is already in
        // use is a fatal error (FailFast), it is never auto-renamed.
        bool Initialize(ID3D11Device* pDevice, const std::string& senderName = std::string());
        void Cleanup();

        bool SendTexture();

    protected:
        spoutDX* _sender = nullptr;
    };

}
