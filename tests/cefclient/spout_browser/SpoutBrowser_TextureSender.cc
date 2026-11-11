#include "SpoutBrowser_TextureSender.h"
#include "SPOUTSDK/SpoutDirectX/SpoutDX/SpoutDX.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <ctime>


namespace client::spout {

    namespace {

        std::wstring DebugLogPath() {
            wchar_t exePath[MAX_PATH] = {};
            DWORD len = ::GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            std::wstring path(exePath, len);
            size_t sep = path.find_last_of(L"/\\");
            path = (sep == std::wstring::npos) ? std::wstring() : path.substr(0, sep + 1);
            return path + L"debug.log";
        }

    }

    void AppendDebugLog(const std::string& message) {
        FILE* file = _wfsopen(DebugLogPath().c_str(), L"ab", _SH_DENYNO);
        if (!file) {
            return;
        }
        SYSTEMTIME t;
        ::GetLocalTime(&t);
        char stamp[64];
        sprintf_s(stamp, "[%04d-%02d-%02d %02d:%02d:%02d] [SpoutBrowser] [pid %lu] ",
                  t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond,
                  static_cast<unsigned long>(::GetCurrentProcessId()));
        std::string line = std::string(stamp) + message + "\r\n";
        fwrite(line.data(), 1, line.size(), file);
        fclose(file);
    }

    void FailFast(const std::string& message, int exit_code) {
        AppendDebugLog("ERROR: " + message);
        // Terminate right away: orderly CEF shutdown is not needed and could
        // hang while the browser is half started. Child processes exit when
        // the browser process is gone.
        ::TerminateProcess(::GetCurrentProcess(), static_cast<UINT>(exit_code));
        for (;;) {
            ::Sleep(1000);
        }
    }

    namespace {

        // Registers |name| on a throwaway spoutDX instance (no DirectX needed).
        // spoutDX::SetSenderName appends "_N" when the name exists, so a changed
        // name means the requested one is taken.
        bool NameTaken(spoutDX& dx, const std::string& name) {
            // Cleans up registry entries of senders that no longer exist.
            dx.GetSenderCount();
            dx.SetSenderName(name.c_str());
            const char* effective = dx.GetSenderName();
            return !effective || name != effective;
        }

    }

    bool IsSenderNameInUse(const std::string& name) {
        spoutDX dx;
        return NameTaken(dx, name);
    }

    bool TextureSender::Initialize(ID3D11Device* pDevice, const std::string& senderName) {
        if (!_sender) {
            _sender = new spoutDX();
            if (!_sender->OpenDirectX11(pDevice)) {
                return false;
            }
            if (!senderName.empty()) {
                if (senderName.size() > kMaxSenderNameBytes) {
                    FailFast("Spout sender name is longer than " + std::to_string(kMaxSenderNameBytes) + " bytes", kExitBadArguments);
                }
                if (NameTaken(*_sender, senderName)) {
                    FailFast("Spout sender name \"" + senderName + "\" is already in use", kExitSenderNameInUse);
                }
                AppendDebugLog("Spout sender name: \"" + senderName + "\"");
            }
            return true;
        }
        return false;
    }
    

    void TextureSender::Cleanup() {
        if (_sender) {
            _sender->ReleaseSender();
            _sender->CloseDirectX11();
            delete _sender;
            _sender = nullptr;
        }
    }

    bool TextureSender::SendTexture() {
        if (_sender) {
            return _sender->SendBackBuffer();
        }
        return false;
    }

}
