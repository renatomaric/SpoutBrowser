// Copyright (c) 2015 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>

#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"
#include "include/wrapper/cef_certificate_util_win.h"
#include "include/wrapper/cef_library_loader.h"
#include "include/wrapper/cef_util_win.h"
#include "tests/cefclient/browser/main_context_impl.h"
#include "tests/cefclient/browser/main_message_loop_multithreaded_win.h"
#include "tests/cefclient/browser/resource.h"
#include "tests/cefclient/browser/root_window_manager.h"
#include "tests/cefclient/browser/test_runner.h"
#include "tests/cefclient/spout_browser/SpoutBrowser_TextureSender.h"
#include "tests/shared/browser/client_app_browser.h"
#include "tests/shared/browser/main_message_loop_external_pump.h"
#include "tests/shared/browser/main_message_loop_std.h"
#include "tests/shared/browser/util_win.h"
#include "tests/shared/common/client_app_other.h"
#include "tests/shared/common/client_switches.h"
#include "tests/shared/renderer/client_app_renderer.h"

namespace client {
namespace {

// Configure code signing requirements. For a code signing example see
// https://github.com/chromiumembedded/cef/issues/3824#issuecomment-2892139995

// TODO(client): Optionally require that the primary certificate match a
// specific thumbprint by setting this value to the SHA1 hash (e.g. 40 character
// upper-case hex-encoded value). If this valus is empty and |kAllowUnsigned| is
// false then any valid signature will be allowed. This is the "Thumbprint"
// output reported by some Windows PowerShell commands. It can also be retrieved
// directly with a PowerShell command like: > (Get-ChildItem
// Cert:\CurrentUser\My -CodeSigningCert)[0].Thumbprint
constexpr char kRequiredThumbprint[] = "";

// TODO(client): Optionally disallow unsigned binaries by setting this value to
// false. This value is disregarded if |kRequiredThumbprint| is specified.
constexpr bool kAllowUnsigned = true;

// TODO(client): Optionally require that all binaries be signed with the same
// primary thumbprint. This value is ignored when |kRequiredThumbprint| is
// specified or if |kAllowUnsigned| is true.
constexpr bool kRequireMatchingThumbprints = false;

static_assert(sizeof(kRequiredThumbprint) == 1 ||
                  sizeof(kRequiredThumbprint) ==
                      cef_certificate_util::kThumbprintLength + 1,
              "invalid size for kRequiredThumbprint");

const char* RequiredThumbprint(std::string* exe_thumbprint) {
  if constexpr (sizeof(kRequiredThumbprint) ==
                cef_certificate_util::kThumbprintLength + 1) {
    return kRequiredThumbprint;
  }

  if (!kAllowUnsigned && kRequireMatchingThumbprints && exe_thumbprint &&
      exe_thumbprint->length() == cef_certificate_util::kThumbprintLength) {
    return exe_thumbprint->c_str();
  }

  return nullptr;
}

bool VerifyCodeSigningAndLoad(CefScopedLibraryLoader& library_loader,
                              cef_version_info_t* version_info) {
  // Enable early logging support (required before libcef is loaded).
  // The *Assert() calls below will output a FATAL error and crash on failure.
  cef::logging::ScopedEarlySupport scoped_logging({});

  if (library_loader.LoadInSubProcessAssert(version_info)) {
    // Running as a sub-process. We may be sandboxed. Nothing more to be done.
    return true;
  }

  std::string exe_thumbprint;

  // Check signatures for the already loaded executable. This may be the
  // bootstrap, or the client executable if not using the bootstrap.
  const std::wstring& exe_path = cef_util::GetExePath();
  cef_certificate_util::ThumbprintsInfo exe_info;
  cef_certificate_util::ValidateCodeSigningAssert(
      exe_path, RequiredThumbprint(nullptr), kAllowUnsigned, &exe_info);
  if (exe_info.IsSignedAndValid()) {
    exe_thumbprint = exe_info.valid_thumbprints[0];
    CHECK_EQ(cef_certificate_util::kThumbprintLength, exe_thumbprint.length());
  }

#if defined(CEF_USE_BOOTSTRAP)
  // Using a separate bootstrap executable that loaded a client DLL. Check
  // signatures for the already loaded client DLL.
  const std::wstring& client_dll_path =
      cef_util::GetModulePath(client::GetCodeModuleHandle());
  cef_certificate_util::ValidateCodeSigningAssert(
      client_dll_path, RequiredThumbprint(&exe_thumbprint), kAllowUnsigned);
#endif  // defined(CEF_USE_BOOTSTRAP)

  // Require libcef.dll in the same directory as the executable.
  auto sep_pos = exe_path.find_last_of(L"/\\");
  CHECK(sep_pos != std::wstring::npos);
  const auto& libcef_dll_path = exe_path.substr(0, sep_pos + 1) + L"libcef.dll";

  // Validate code signing requirements for libcef.dll before loading, and
  // then load.
  return library_loader.LoadInMainAssert(libcef_dll_path.c_str(),
                                         RequiredThumbprint(&exe_thumbprint),
                                         kAllowUnsigned, version_info);
}


// SpoutBrowser: text printed by --help. Keep in sync with client_switches.cc.
const char kHelpText[] =
    "SpoutBrowser: renders a web page off-screen and sends it as a Spout texture.\n"
    "\n"
    "Usage: SpoutBrowser.exe [--switch[=value]]...\n"
    "\n"
    "SpoutBrowser switches:\n"
    "  --help                          Print this list and exit.\n"
    "  --url=<url>                     Page to load.\n"
    "  --spout-sender-name=<name>      Spout sender name (UTF-8, max 255 bytes).\n"
    "                                  Default: SpoutBrowser. If the name is already\n"
    "                                  in use the process logs to debug.log and exits\n"
    "                                  with code 3.\n"
    "  --width=<px>                    Fixed render and Spout texture width.\n"
    "  --height=<px>                   Fixed render and Spout texture height.\n"
    "                                  Both ignore window size, position, minimized\n"
    "                                  state and DPI (device scale factor 1).\n"
    "                                  Without them the window size is used.\n"
    "  --off-screen-frame-rate=<fps>   Frame rate (default 30). The last frame is\n"
    "                                  re-sent at this rate on static pages.\n"
    "  --transparent-painting-enabled  Transparent background (alpha 0).\n"
    "  --cache-path=<dir>              Browser profile directory (default: cache\n"
    "                                  next to the exe). Use a different one per\n"
    "                                  instance to run several instances.\n"
    "  --multi-threaded-message-loop   Run the browser on its own UI thread.\n"
    "  --hide-controls                 Hide the URL bar and navigation buttons.\n"
    "\n"
    "Other cefclient switches:\n"
    "  --external-message-pump         Use the external message pump.\n"
    "  --off-screen-rendering-enabled  Always on in SpoutBrowser.\n"
    "  --shared-texture-enabled        Always on in SpoutBrowser.\n"
    "  --external-begin-frame-enabled  Drive frames with SendExternalBeginFrame.\n"
    "  --show-update-rect              Draw a border around updated areas.\n"
    "  --fake-screen-bounds            Report the view as the screen bounds.\n"
    "  --mouse-cursor-change-disabled  Do not change the mouse cursor.\n"
    "  --offline                       Start with the network disabled.\n"
    "  --filter-chrome-commands        Filter Chrome commands.\n"
    "  --request-context-per-browser   One request context per browser.\n"
    "  --request-context-shared-cache  Share the cache between request contexts.\n"
    "  --background-color=<color>      Browser background color.\n"
    "  --enable-gpu                    Enable the GPU.\n"
    "  --filter-url=<url>              Filter requests by URL.\n"
    "  --use-views                     Use the Views framework (not with OSR).\n"
    "  --use-native                    Use native windows.\n"
    "  --hide-frame                    Hide the window frame.\n"
    "  --hide-overlays                 Hide overlays.\n"
    "  --always-on-top                 Keep the window on top.\n"
    "  --hide-top-menu                 Hide the top menu.\n"
    "  --ssl-client-certificate=<x>    SSL client certificate.\n"
    "  --crl-sets-path=<path>          CRL sets path.\n"
    "  --no-activate                   Do not activate the window on creation.\n"
    "  --show-chrome-toolbar           Show the Chrome toolbar.\n"
    "  --initial-show-state=<state>    Initial window state (normal, minimized,\n"
    "                                  maximized).\n"
    "  --use-default-popup             Use the default popup window.\n"
    "  --use-client-dialogs            Use client dialogs.\n"
    "  --use-test-http-server          Use the test HTTP server.\n"
    "  --show-window-buttons           Show window buttons.\n"
    "  --use-window-modal-dialog       Use window modal dialogs.\n"
    "  --use-bottom-controls           Put the controls at the bottom.\n"
    "  --hide-pip-frame                Hide the Picture-in-Picture frame.\n"
    "  --move-pip-enabled              Allow moving the Picture-in-Picture window.\n"
    "  --hide-chrome-bubbles           Hide Chrome bubbles.\n"
    "  --hide-window-on-close          Hide the window instead of closing it.\n"
    "  --accepts-first-mouse           Accept the first mouse click.\n"
    "  --use-alloy-style               Use Alloy style.\n"
    "  --use-chrome-style-window       Use a Chrome style window.\n"
    "  --show-overlay-browser          Show the overlay browser.\n"
    "  --use-angle=<backend>           ANGLE backend.\n"
    "  --ozone-platform=<name>         Ozone platform.\n"
    "\n"
    "Any Chromium switch is accepted as well.\n"
    "Exit codes: 0 ok, 2 invalid flag value, 3 Spout sender name already in use.\n";

// SpoutBrowser: the exe has the GUI subsystem, so attach to the console of the
// parent (e.g. PowerShell) to make the text visible. When stdout is redirected
// the text goes there instead.
void PrintHelp() {
  ::AttachConsole(ATTACH_PARENT_PROCESS);

  HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
  if (!out || out == INVALID_HANDLE_VALUE) {
    out = ::CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
  }
  if (!out || out == INVALID_HANDLE_VALUE) {
    return;
  }

  std::string text;
  for (const char* c = kHelpText; *c; ++c) {
    if (*c == '\n') {
      text += '\r';
    }
    text += *c;
  }
  DWORD written = 0;
  ::WriteFile(out, text.data(), static_cast<DWORD>(text.size()), &written,
              nullptr);
}

// SpoutBrowser: parses a pixel size switch. Exits with an error on bad values.
void ValidateSizeSwitch(CefRefPtr<CefCommandLine> command_line,
                        const char* name) {
  if (!command_line->HasSwitch(name)) {
    return;
  }
  const std::string value = command_line->GetSwitchValue(name).ToString();
  char* end = nullptr;
  const long px = value.empty() ? 0 : std::strtol(value.c_str(), &end, 10);
  if (value.empty() || *end != '\0' || px < 1 || px > 16384) {
    spout::FailFast(std::string("--") + name + "=\"" + value +
                        "\" is not a size between 1 and 16384",
                    spout::kExitBadArguments);
  }
}

// SpoutBrowser: handles --help and validates the new flags in the browser
// process before CEF starts. Returns true when the process should exit.
bool HandleSpoutBrowserSwitches(CefRefPtr<CefCommandLine> command_line,
                                int* exit_code) {
  if (command_line->HasSwitch(switches::kHelp)) {
    PrintHelp();
    *exit_code = 0;
    return true;
  }

  ValidateSizeSwitch(command_line, switches::kWidth);
  ValidateSizeSwitch(command_line, switches::kHeight);

  if (command_line->HasSwitch(switches::kSpoutSenderName)) {
    const std::string name =
        command_line->GetSwitchValue(switches::kSpoutSenderName).ToString();
    if (name.empty()) {
      spout::FailFast("--spout-sender-name must not be empty",
                      spout::kExitBadArguments);
    }
    if (name.size() > spout::kMaxSenderNameBytes) {
      spout::FailFast("--spout-sender-name is longer than 255 bytes",
                      spout::kExitBadArguments);
    }
    if (spout::IsSenderNameInUse(name)) {
      spout::FailFast("Spout sender name \"" + name + "\" is already in use",
                      spout::kExitSenderNameInUse);
    }
  }
  return false;
}

int RunMain(HINSTANCE hInstance,
            int nCmdShow,
            void* sandbox_info,
            cef_version_info_t* version_info) {
  CefMainArgs main_args(hInstance);

  // Dynamically load the CEF library after code signing verification.
  CefScopedLibraryLoader library_loader;
  if (!VerifyCodeSigningAndLoad(library_loader, version_info)) {
    // The verification or load failed. We'll crash before reaching this line.
    NOTREACHED();
    return CEF_RESULT_CODE_KILLED;
  }

  // The CEF library (libcef) is loaded at this point.

  // Parse command-line arguments.
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
  command_line->InitFromString(::GetCommandLineW());

  // Create a ClientApp of the correct type.
  CefRefPtr<CefApp> app;
  ClientApp::ProcessType process_type = ClientApp::GetProcessType(command_line);
  if (process_type == ClientApp::BrowserProcess) {
    app = new ClientAppBrowser();
  } else if (process_type == ClientApp::RendererProcess) {
    app = new ClientAppRenderer();
  } else if (process_type == ClientApp::OtherProcess) {
    app = new ClientAppOther();
  }

  // Execute the secondary process, if any.
  int exit_code = CefExecuteProcess(main_args, app, sandbox_info);
  if (exit_code >= 0) {
    return exit_code;
  }

  // SpoutBrowser: --help and flag validation (browser process only).
  if (HandleSpoutBrowserSwitches(command_line, &exit_code)) {
    return exit_code;
  }

  // Create the main context object.
  auto context = std::make_unique<MainContextImpl>(command_line, true);

  CefSettings settings;

  if (!sandbox_info) {
    settings.no_sandbox = true;
  }

  // Populate the settings based on command line arguments.
  context->PopulateSettings(&settings);

  // Set the ID for the ICON resource that will be loaded from the main
  // executable and used when creating default Chrome windows such as DevTools
  // and Task Manager. Only used with the Chrome runtime.
#if defined(CEF_USE_BOOTSTRAP)
  // Use the default icon from bootstrap.exe.
  settings.chrome_app_icon_id = 32512;  // IDI_APPLICATION
#else
  // Use the default icon from cefclient.exe.
  settings.chrome_app_icon_id = IDR_MAINFRAME;
#endif

  // Create the main message loop object.
  std::unique_ptr<MainMessageLoop> message_loop;
  if (settings.multi_threaded_message_loop) {
    message_loop = std::make_unique<MainMessageLoopMultithreadedWin>();
  } else if (settings.external_message_pump) {
    message_loop = MainMessageLoopExternalPump::Create();
  } else {
    message_loop = std::make_unique<MainMessageLoopStd>();
  }

  // Initialize the CEF browser process. May return false if initialization
  // fails or if early exit is desired (for example, due to process singleton
  // relaunch behavior).
  if (!context->Initialize(main_args, settings, app, sandbox_info)) {
    return CefGetExitCode();
  }

  // Register scheme handlers.
  test_runner::RegisterSchemeHandlers();

  auto window_config = std::make_unique<RootWindowConfig>();
  window_config->always_on_top =
      command_line->HasSwitch(switches::kAlwaysOnTop);
  window_config->with_osr =
      settings.windowless_rendering_enabled ? true : false;

  // Create the first window.
  context->GetRootWindowManager()->CreateRootWindow(std::move(window_config));

  // Run the message loop. This will block until Quit() is called by the
  // RootWindowManager after all windows have been destroyed.
  int result = message_loop->Run();

  // Shut down CEF.
  context->Shutdown();

  // Release objects in reverse order of creation.
  message_loop.reset();
  context.reset();

  return result;
}

}  // namespace
}  // namespace client

#if defined(CEF_USE_BOOTSTRAP)

// Entry point called by bootstrap.exe when built as a DLL.
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE hInstance,
                                    LPTSTR lpCmdLine,
                                    int nCmdShow,
                                    void* sandbox_info,
                                    cef_version_info_t* version_info) {
  return client::RunMain(hInstance, nCmdShow, sandbox_info, version_info);
}

#else  // !defined(CEF_USE_BOOTSTRAP)

// Program entry point function.
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPTSTR lpCmdLine,
                      int nCmdShow) {
  UNREFERENCED_PARAMETER(hPrevInstance);
  UNREFERENCED_PARAMETER(lpCmdLine);

#if defined(ARCH_CPU_32_BITS)
  // Run the main thread on 32-bit Windows using a fiber with the preferred 4MiB
  // stack size. This function must be called at the top of the executable entry
  // point function (`main()` or `wWinMain()`). It is used in combination with
  // the initial stack size of 0.5MiB configured via the `/STACK:0x80000` linker
  // flag on executable targets. This saves significant memory on threads (like
  // those in the Windows thread pool, and others) whose stack size can only be
  // controlled via the linker flag.
  int exit_code = CefRunWinMainWithPreferredStackSize(wWinMain, hInstance,
                                                      lpCmdLine, nCmdShow);
  if (exit_code >= 0) {
    // The fiber has completed so return here.
    return exit_code;
  }
#endif

  void* sandbox_info = nullptr;

#if defined(CEF_USE_SANDBOX)
  // Manage the life span of the sandbox information object. This is necessary
  // for sandbox support on Windows. See cef_sandbox_win.h for complete details.
  CefScopedSandboxInfo scoped_sandbox;
  sandbox_info = scoped_sandbox.sandbox_info();
#endif

  cef_version_info_t version_info = {};
  CEF_POPULATE_VERSION_INFO(&version_info);

  return client::RunMain(hInstance, nCmdShow, sandbox_info, &version_info);
}

#endif  // !defined(CEF_USE_BOOTSTRAP)
