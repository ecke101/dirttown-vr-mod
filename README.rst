Dirt Town VR Mod
================

OpenXR stereo, head tracking, temporal antialiasing and controller support for
Dirt Town Demo, Steam app 5471710, build 26658761. Game files and saves are not
modified. Menus and HUD remain on the monitor.

Run
---

Use Windows x64 or Linux x86-64 with Python 3.10+ and Tk. Start your PC OpenXR
runtime and connect the headset before launching. Linux additionally requires
Steam, a recent Proton with OpenXR support, and the native OpenXR loader.

Extract a release archive, then run ``run.bat`` on Windows or ``./run.sh`` on
Linux. Select the game folder if it is not detected. Click **Detect headset**,
choose a resolution percentage, then **Launch VR** and load a site on the
monitor. Start at 50–75% if performance is poor. Preferences are remembered.

The active OpenXR runtime is used by default. The optional runtime-manifest
field overrides it for this launch without changing the system selection.
On Linux, the override uses a private XDG configuration overlay so Proton's
Windows loader can keep its OpenXR bridge.

Graphics **auto** chooses D3D12 when advertised by the runtime, otherwise D3D11
through D3D11On12 or shared textures. The D3D11 fallback remains experimental
and requires working graphics interoperability. Both the game and VR runtime
must use the same GPU. Other runtimes
and headsets require testing; OpenXR support alone is not a compatibility claim.

Controls
--------

* F9: recenter. F10: toggle VR. F11: toggle temporal antialiasing.
* Dozer: left stick drives/steers; right stick lifts/rolls the blade.
* Walking: left stick moves; right stick turns/looks. Head tracking also works.
* Wheel loaders: right stick lifts/curls the bucket. The native LB or custom
  right-stick-click modifier is detected and applied automatically.
* B/right secondary: toggle grade hold. Leave it off for manual blade control.
* Y/left secondary: enter/exit. X/left primary: interact.
* Right grip + B/right secondary: free tilt. Left grip + Y: tailgate action.
* Left-stick click + right stick: dozer blade angle/pitch.
* Right-stick click: change camera. Both stick clicks: pause/menu.
* Triggers and grips otherwise follow the game's native vehicle bindings.

Bindings are provided for Touch, Pico 4, Index, Vive, Microsoft motion
controllers and Khronos simple controllers. Index uses its left A/B buttons
for the left-primary/secondary actions. Vive uses trackpads for axes, pad clicks
for primary actions, and menu buttons for secondary actions; pad movement is
neutral when untouched. Microsoft motion controllers use thumbsticks, pad
clicks and menu buttons. Simple controllers provide buttons only; use the
keyboard/gamepad for driving. Devices with fewer buttons need keyboard
fallbacks for actions without an equivalent input.

**Controller settings…** opens ``.local/controls.ini``. Set deadzone, grip
threshold, hand swapping, axis inversion or per-profile OpenXR component
bindings there. Restart the game after editing. Controller input is released
when tracking input becomes inactive, VR loses focus, or updates go stale.

Command line
------------

From the extracted folder::

    python -m dirttown_vr runtime
    python -m dirttown_vr launch
    python -m dirttown_vr --resolution-scale 0.5 --no-controllers launch
    python -m dirttown_vr --runtime /path/to/runtime.json launch
    python -m dirttown_vr probe
    python -m dirttown_vr gpu-test

Use ``python3`` on Linux. ``--game`` selects the game directory; ``--proton``
selects a Linux Proton executable. ``--graphics d3d12`` or ``d3d11`` selects a
graphics path. Logs are in ``.local/logs``. **Test headset** displays a tracked
cube; it does not load the game.

Build and test
--------------

Source checkouts need a build before launching. Install CMake and either Visual
Studio C++ tools on Windows or x86-64 MinGW-w64 plus Ninja on Linux::

    python tools/build_vr.py
    python -m unittest discover -s tests -v
    python tools/package.py

The builder downloads hash-verified OpenXR and MinHook dependencies. Release
archives contain the compiled Windows binaries used natively or through Proton,
the Python launcher and dependency licences. Packaging is manual; CI only
builds and tests Windows and Linux targets. Hardware VR checks must be run on
the target system.

Validated hardware: Pico 4 through WiVRn/Proton on Linux. Windows runtimes and
other controller/headset combinations are implemented but need physical tests.
