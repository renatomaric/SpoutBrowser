SpoutBrowser adds [Spout](https://spout.zeal.co/) texture-sending support to a Chromium-based browser.  
This enables using any web content - WebGL, shaders, interactive pages - as a live video source in VJ software such as Resolume, MadMapper, and others.

**Demo video**: https://www.youtube.com/watch?v=vOFj6aN5QR4

<a href="https://www.youtube.com/watch?v=vOFj6aN5QR4">
 <img src="https://img.youtube.com/vi/vOFj6aN5QR4/maxresdefault.jpg" width="600" alt="Video cover"/>
</a>


# Implementation

This project is a fork of the Chromium Embedded Framework (CEF): https://github.com/chromiumembedded/cef  
It is based on the classic CEF sample application [cefclient](https://github.com/chromiumembedded/cef/tree/master/tests/cefclient).

Modifications to the CEF source are intentionally minimal to keep upgrading to newer CEF versions straightforward.  
This is the key difference from the existing [cef-spout](https://github.com/fg-uulm/cef-spout) project by Florian Geiselhart and the reason this project was created.

Spout integration uses CEF's off-screen rendering mode with shared textures and D3D11 rendering.  
See the [SpoutBrowser_TextureSender](tests/cefclient/spout_browser) module for details.


# Prebuilt Binaries

If you prefer not to build from source, prebuilt Windows binaries are available on itch.io: https://bntr.itch.io/spout-browser  
Two versions are provided:
- **Free Demo** - fully functional, with a subtle checker-style watermark.
- **Full Version** - no watermark, available for a small coffee-like price.

Purchasing the full version is simply a way to support ongoing development.  
Of course, you can always build SpoutBrowser yourself - see the instructions below.


# Build

Prerequisites: Visual Studio 2022; CMake 3.21+; Python 3.9–3.11.  
Refer to the [CEF Project Setup](https://github.com/chromiumembedded/cef-project#setup) for details.

### Quick Start
1. Run `_SpoutBrowser_generate_solution.bat`. 
2. Open the generated solution: `/_cef_binary/[cef_distribution_name]/build/cef.sln`.
3. Build the **cefclient** project.

### Build Workflow
The build script automates the setup by working on top of prebuilt CEF binaries (similar to [cef-project](https://github.com/chromiumembedded/cef-project)):

1. **Download**: Fetches the specified CEF distribution into `/_cef_binary`.
2. **Patch**: Injects SpoutBrowser source code and modifies CMake scripts within the CEF directory.
3. **Generate**: Runs CMake to create the VS solution (this also fetches the Spout2 dependency).

The diagram below illustrates the sequence of operations performed during the build process.

<img src="_SpoutBrowser_build_diagram.svg" width="600" alt="Build diagram"/>


# Running

SpoutBrowser supports standard cefclient command-line switches (see [cefclient/README.md](tests/cefclient/README.md)).

**Common switches:**
* **--url="https://your-content.com"** - Sets the starting page.
* **--transparent-painting-enabled** - Enables the Alpha channel, useful for overlaying web content over other layers.
* **--off-screen-frame-rate=60** - Sets preferred FPS; default is 30.  
  Note: use fps 60 with **--multi-threaded-message-loop** to avoid dragging/resizing issues, related to [#4008](https://github.com/chromiumembedded/cef/issues/4008).
* **--always-on-top** - Keeps the browser window in the foreground.
* **--hide-controls** - Hides the address bar and navigation buttons.
* **--cache-path="cache"** - Specifies the directory where cache data will be stored.  
  Using different paths allows running multiple isolated instances.
* **--spout-sender-name="Name"** - Sets the Spout sender name (UTF-8, spaces and parentheses are fine, max 255 bytes).  
  Default: `SpoutBrowser`. If a sender with this name already exists, SpoutBrowser does not rename it to `Name_1`.
  It writes a line to `debug.log` (next to the exe) and exits with code 3.
* **--width=2592 --height=72** - Fixes the size of the page view and of the Spout texture in pixels.  
  The size does not depend on the window size or position, on the DPI (device scale factor is 1),
  and the output keeps running when the window is off-screen or minimized.
  Without these switches the window size determines the render size, as before.
  Each switch can also be used alone, the other dimension then follows the window.
* **--help** - Prints all supported switches and exits.  
  The exe has the GUI subsystem, so in PowerShell use `.\SpoutBrowser.exe --help | Out-Host`
  (or `Start-Process -Wait -NoNewWindow .\SpoutBrowser.exe --help`) to see the output.

On a resize the Spout sender is updated at once, even if the page is static.
The last frame is re-sent at the **--off-screen-frame-rate** rate, so receivers like Resolume never see the sender as inactive.

Full list of switches: [common/client_switches.cc](tests/shared/common/client_switches.cc).

The following switches are ignored and forced to true: **--off-screen-rendering-enabled**, **--shared-texture-enabled**, **--use-alloy-style**.

**Pro Tip:** Create a .bat file for quick startup:
```batch
SpoutBrowser.exe --transparent-painting-enabled ^
                 --off-screen-frame-rate=60 --multi-threaded-message-loop ^
                 --url="https://threejs.org/examples/"
```

**Multiple Instances:** Every instance uses its own browser profile, which is the folder given by **--cache-path**
(default: `cache` next to the exe). Instances with different cache paths are fully independent and never hand their
command line to a running instance (no "Opening in existing browser session").
Starting a second instance with the same cache path does reuse the first one, so always give each instance its own.
Give each instance its own **--spout-sender-name** as well.

Example with two instances (for example two Resolume layers):
```batch
SpoutBrowser.exe --url="https://example.com/a" --width=2592 --height=72 --transparent-painting-enabled --off-screen-frame-rate=60 --multi-threaded-message-loop --hide-controls --cache-path="C:\SpoutBrowser\cache-a" --spout-sender-name="Sender A"
SpoutBrowser.exe --url="https://example.com/b" --width=1040 --height=400 --transparent-painting-enabled --off-screen-frame-rate=60 --multi-threaded-message-loop --hide-controls --cache-path="C:\SpoutBrowser\cache-b" --spout-sender-name="Sender B"
```

**Exit codes:** `0` normal, `2` invalid value for a SpoutBrowser flag, `3` Spout sender name already in use.

# Licenses

SpoutBrowser consists of several components with different licenses:

1. Chromium Embedded Framework (CEF)  
   Licensed under the BSD 3-Clause License.
   See LICENSE.txt for the original CEF license.

2. Spout (external dependency)  
   Spout is not included in this repository but is downloaded during CMake configuration.
   Spout is licensed under the BSD 2-Clause License:
   https://github.com/leadedge/Spout2

3. SpoutBrowser code  
   All additional code written specifically for SpoutBrowser is licensed under the MIT License.
   See LICENSE.spoutbrowser.txt for details.






