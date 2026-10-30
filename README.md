# RetroShare Remote Desktop

A C++17 / Qt 5 or Qt 6 RetroShare plugin (one library, `RemoteDesktop.dll`). It shares one screen with one online friend after explicit acceptance; the friend watches and controls it in a separate viewer pop-up window that the plugin opens. Both qmake and CMake are supported.

## Getting the source

This repository is the plugin only; RetroShare itself is not modified. Two ways to build:

- **Inside a RetroShare checkout (recommended):** clone into `RetroShare/plugins/RemoteDesktop`; `qmake RemoteDesktop.pro` then inherits RetroShare's plugin settings automatically.
  ```sh
  cd RetroShare/plugins
  git clone <this repository> RemoteDesktop
  ```
- **Standalone:** keep it anywhere and pass `RETROSHARE_SOURCE_DIR`, `RETROSHARE_QT_MAJOR` and (Windows) the import libraries to qmake, or the matching CMake variables, as shown below.

## Implemented

**Plugin (sharing and requesting)**
- **Remote Desktop** tab on RetroShare's **Network** page (next to Network graph and Keyring): online friend selection, **Request desktop**, Stop, **Send file…**, session log. RetroShare has no plugin hook for Network tabs, so the plugin finds the Network page's tab widget at start-up; if it cannot (another RetroShare version), it falls back to a normal sidebar page. Setting `showInNetworkTab=false` in the `RetroShare/RemoteDesktop` settings forces the sidebar page.
- Incoming requests disabled by default. Every request opens an always-on-top pop-up showing the friend's name and peer ID, with choices: screen to share, keyboard/mouse control, clipboard sharing, chat. It declines automatically after 55 s.
- While sharing, an always-on-top red banner (“Sharing screen with …”) with **Chat**, **Send file…** and **Stop sharing**.
- RFB stream carried in RetroShare friend service packets (TLS friend link); VNC endpoints listen on 127.0.0.1 only, IPv6 listener refused, fresh per-session password, LibVNC's own file transfer and HTTP server disabled.
- Local brute-force protection: three wrong VNC passwords on the loopback bridge end the session.
- Per-friend receive budgets so one friend cannot starve another; session/peer binding, ordered offsets, acknowledgements, timeouts.
- Changed-tile updates (64×64) instead of full frames; host cursor drawn into the picture; multi-monitor screen choice; DPI-aware pointer mapping on Windows.
- **Files go through RetroShare**, not VNC: “Send file…” hashes the file into RetroShare's extra-file list (as the chat “attach file” button does) and posts a normal RetroShare file link in private chat. The friend never gets access to your file system through the session.

**Viewer window (pop-up, part of the plugin)**
- Opens as its own top-level window when your friend accepts, not inside the plugin page. Closing it ends the session; when the session ends it closes.
- Toolbar: Disconnect, Full screen (Ctrl+Alt+Shift+F), Fit to window / 100 % with scroll bars (Ctrl+Alt+Shift+S), Server scale (100 %, 1/2, 1/3, 1/4 to save bandwidth), Quality (Balanced Tight+JPEG, lossless, low bandwidth), Ctrl+Alt+Del, Keys menu (Win, Ctrl+Esc, Alt+Tab, Alt+F4, Ctrl+Shift+Esc, Win+L/D/E/R, Print Screen, F11), Refresh (Ctrl+Alt+Shift+R), View only, Clipboard sync, Chat, Send file…, More (screenshot, connection info).
- On Windows, Win key, Alt+Tab, Alt+Esc, Ctrl+Esc and Alt+Space are captured and sent to the remote computer while the viewer has focus (can be turned off in Keys).
- Full keyboard mapping including keypad, F1–F24, AltGr, Caps/Num/Scroll Lock, auto-repeat; horizontal and vertical wheel; status bar with resolution, frame rate, mode.
- UTF-8 clipboard and chat through a small private RFB extension that only RetroShare hosts answer; the host enforces every permission.

## Platform status

| Platform | Implementation | Validation |
|---|---|---|
| Windows | Qt capture + SendInput | DLL compiled against local RetroShare |
| Linux X11 | Qt capture + XTest | Source provided; native build and desktop integration not yet run |
| macOS | Qt capture + Quartz | Source provided; native build and desktop permissions not yet tested |
| Linux Wayland | Viewer can run; hosting rejected explicitly | Portal/PipeWire capture and remote-input backend still needed |

This is an initial implementation, not a validated release. Two-peer RetroShare interoperability, real desktop input, Linux/macOS native builds, keyboard layouts, HiDPI positioning and performance still need testing. macOS needs Screen Recording permission for capture and Accessibility permission for control. Windows cannot control elevated applications from a non-elevated RetroShare process or the secure desktop. macOS keyboard support is basic Unicode/navigation; full physical shortcut mapping is not implemented. The primary display is fixed for a session; a geometry change ends sharing.

## Dependencies and matching the host

Build with **the same Qt major/minor, compiler ABI, RetroShare headers and ABI feature definitions as the host**. On Windows, the module imports both `retroshare.dll` and exports from `retroshare.exe`; it must be loaded by the matching RetroShare executable.

Requirements: C++17 compiler, Qt 5.12+ or Qt 6.2+ Widgets/Network, LibVNC development libraries (0.9.13+), and a built RetroShare checkout. Use qmake from the chosen Qt kit, or CMake 3.20+. Linux additionally needs X11 and XTest development packages. macOS uses ApplicationServices.

### Installed packages (default; no clone or LibVNC source build)

**Windows (MSYS2 MINGW64)** — install build tools and the LibVNC package:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-make \
  mingw-w64-x86_64-cmake mingw-w64-x86_64-pkgconf mingw-w64-x86_64-libvncserver
# Choose the Qt package matching your RetroShare build:
pacman -S --needed mingw-w64-x86_64-qt5-base   # Qt 5
# OR: pacman -S --needed mingw-w64-x86_64-qt6-base
```

Use the MINGW64 terminal/toolchain for these package names. Do not mix MINGW64, UCRT64 and 32-bit libraries. A different host toolchain needs that toolchain's matching packages. Package references: [MSYS2 LibVNC](https://packages.msys2.org/packages/mingw-w64-x86_64-libvncserver), [Qt 5](https://packages.msys2.org/packages/mingw-w64-x86_64-qt5-base), [Qt 6](https://packages.msys2.org/base/mingw-w64-qt6-base).

**Ubuntu / Debian-family Linux** — install development headers and tools:

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config libvncserver-dev libx11-dev libxtst-dev
# Choose the Qt development package matching your RetroShare build:
sudo apt install qtbase5-dev qt5-qmake       # Qt 5
# OR: sudo apt install qt6-base-dev qt6-base-dev-tools qmake6
```

Ubuntu package reference: [libvncserver-dev](https://packages.ubuntu.com/libvncserver-dev). The Linux hosting backend requires an X11 session; installing these packages does not add Wayland hosting support.

**Arch Linux** — development headers are included in the regular library packages:

```sh
sudo pacman -Syu --needed base-devel cmake pkgconf libvncserver libx11 libxtst
# Choose the Qt package matching your RetroShare build:
sudo pacman -S --needed qt5-base            # Qt 5
# OR: sudo pacman -S --needed qt6-base
```

Arch references: [LibVNC](https://archlinux.org/packages/extra/x86_64/libvncserver/files/), [Qt packages](https://wiki.archlinux.org/title/Qt).

These are the plugin's build prerequisites, not the complete dependency list for building RetroShare itself. Build RetroShare first using its own instructions. Use the same Qt kit/compiler and host configuration for this plugin. The packages include client/server headers and link libraries; the package manager handles their runtime dependencies. CMake finds the installed LibVNC CMake config, falling back to pkg-config. qmake uses pkg-config. Neither path reads or requires `third_party/libvncserver` in package mode.

The client uses `rfbInitClient`, available in packaged releases, instead of newer development-only initialization APIs. Native package validation was performed with MSYS2 LibVNC 0.9.15; Ubuntu/Arch builds still require native validation.

### qmake (Qt 5 and Qt 6)

Open `RemoteDesktop.pro` in Qt Creator with the kit matching your RetroShare host, or use the command line. The project supports both RetroShare's plugin-tree layout and standalone builds.

#### Inside RetroShare/plugins/RemoteDesktop

The project automatically includes `../Common/retroshare_plugin.pri` and inherits the host's feature definitions and library paths. Do not add another include manually. No `RETROSHARE_SOURCE_DIR`, `RETROSHARE_QT_MAJOR` or import-library arguments are required in this mode. Use the same qmake and build options as the parent RetroShare build.

From **MSYS2 MINGW64**, after building RetroShare:

```sh
cd /c/Users/username/Documents/GitHub/RetroShare/plugins/RemoteDesktop
qmake RemoteDesktop.pro
mingw32-make -j4
```

Output: `lib/RemoteDesktop.dll`. If your Qt 5 installation names the tool `qmake-qt5`, use that executable. For a Qt 6 host use its `qmake6` (or `qmake-qt6`) instead. Verify with `qmake -v` before building.

For Ubuntu/Arch inside the same plugin-tree layout:

```sh
cd /path/to/RetroShare/plugins/RemoteDesktop
qmake RemoteDesktop.pro   # use qmake6 for a Qt 6 host
make -j4
```

Build directly in the plugin directory for the common configuration's existing relative library paths. For an independent shadow build, pass `CONFIG+=desktop_standalone` and the explicit host paths shown below. Regenerate Makefiles after moving the project; do not reuse build files copied from the old location.

When invoking the in-tree Windows build from PowerShell rather than MINGW64, set the environment first:

```powershell
$env:MINGW_PREFIX = 'C:/msys64/mingw64'
$env:PATH = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:PATH
qmake RemoteDesktop.pro
mingw32-make -j4
```

Without `MINGW_PREFIX`, some RetroShare versions incorrectly autodetect `mingw32`. `Set RETROSHARE_SOURCE_DIR` from an old plugin file indicates the standalone-only project: use this updated `.pro` and rerun qmake.

#### Standalone build

Outside the RetroShare tree, specify the host paths and ABI settings. Use a separate build directory per Qt kit. `CONFIG+=desktop_standalone` also selects this mode explicitly when the source is inside the tree.

After installing the package, build the plugin directly (PowerShell example):

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
New-Item -ItemType Directory -Force build/qmake5 | Out-Null
C:/msys64/mingw64/bin/qmake.exe -o build/qmake5/Makefile RemoteDesktop.pro `
  RETROSHARE_QT_MAJOR=5 `
  RETROSHARE_SOURCE_DIR=C:/Users/username/Documents/GitHub/RetroShare `
  RETROSHARE_CORE_LIBRARY=C:/Users/username/Documents/GitHub/RetroShare/libretroshare/src/lib/libretroshare.dll.a `
  RETROSHARE_GUI_LIBRARY=C:/Users/username/Documents/GitHub/RetroShare/retroshare-gui/src/lib/libretroshare-gui.a `
  'RETROSHARE_DEFINES=RS_USE_GXS_DISTANT_SYNC RS_USE_WIKI RS_ENABLE_GXS RS_GXS_TRANS RS_USE_BITDHT RS_DIRECT_CHAT RS_JSONAPI RS_WEBUI RS_BROADCAST_DISCOVERY USE_RNP_LIB'
mingw32-make -C build/qmake5 -j4
```

Output: `build/qmake5/lib/RemoteDesktop.dll`. Replace paths/feature definitions for your host. `RETROSHARE_DEFINES` is space-separated for qmake; CMake's corresponding variable uses semicolons.

For Qt 6 use **`qmake6`**, a fresh `build/qmake6` directory, `RETROSHARE_QT_MAJOR=6`, and **headers/import libraries from a Qt 6 RetroShare build**. The project rejects a declared host Qt major that differs from the selected qmake kit. A Qt 6 DLL cannot be loaded into Qt 5 RetroShare. Matching the Qt major alone is not sufficient: compiler ABI and host feature definitions must also match.

On Linux/macOS, use `qmake` or `qmake6` and `make`; omit the Windows import-library arguments. Leave `LIBVNC_BUILD_DIR` unset for packages. To diagnose discovery, run `pkg-config --modversion libvncclient libvncserver` in the same build environment. File transfer is disabled by the server and this plugin never registers the optional TightVNC file-transfer extension.

### CMake Qt selection

CMake defaults to Qt 5. Pass `-DDESKTOP_QT_MAJOR=6` for Qt 6, using a separate build directory and matching RetroShare libraries.

### Windows / MSYS2 MinGW64

Use the matching MSYS2 Qt installation, not another Qt kit earlier in PATH. This example targets the existing Qt 5 host (replace host paths as needed):

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
cmake -S . -B build/plugin -G 'MinGW Makefiles' `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe `
  -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 `
  -DRETROSHARE_SOURCE_DIR=C:/Users/username/Documents/GitHub/RetroShare `
  -DRETROSHARE_CORE_LIBRARY=C:/Users/username/Documents/GitHub/RetroShare/libretroshare/src/lib/libretroshare.dll.a `
  -DRETROSHARE_GUI_LIBRARY=C:/Users/username/Documents/GitHub/RetroShare/retroshare-gui/src/lib/libretroshare-gui.a `
  '-DRETROSHARE_COMPILE_DEFINITIONS=RS_USE_GXS_DISTANT_SYNC;RS_USE_WIKI;RS_ENABLE_GXS;RS_GXS_TRANS;RS_USE_BITDHT;RS_DIRECT_CHAT;RS_JSONAPI;RS_WEBUI;RS_BROADCAST_DISCOVERY;USE_RNP_LIB'
cmake --build build/plugin -j 4
```

The example definitions match the inspected local host configuration; other builds may differ. Output: `build/plugin/libRemoteDesktop.dll`. Package mode links the installed LibVNC DLLs: keep `libvncclient.dll`, `libvncserver.dll` and their transitive dependencies from the matching MSYS2 installation accessible to RetroShare. The required crypto/compression DLLs depend on how the package was built. Do not replace the host's Qt DLLs with a different kit. When reusing an old build directory, explicitly pass `-DDESKTOP_USE_BUNDLED_LIBVNC=OFF` to switch to packages.

### Linux / macOS

```sh
cmake -S . -B build/plugin -DRETROSHARE_SOURCE_DIR=/path/to/RetroShare \
  -DRETROSHARE_COMPILE_DEFINITIONS='HOST_FEATURE_DEFINITIONS_HERE'
cmake --build build/plugin -j4
```

Replace the placeholder definitions with the host build definitions, or omit the argument if none are required. Set `CMAKE_PREFIX_PATH` to the selected Qt kit when it is not the default. Unix modules resolve RetroShare symbols from the running host; macOS uses dynamic symbol lookup. These build paths require native validation.

### Optional LibVNC source build

Only use this method if you want to build the dependency yourself. First fetch the pinned source:

```sh
git clone https://github.com/LibVNC/libvncserver.git third_party/libvncserver
git -C third_party/libvncserver checkout --detach 98aaee0560baa0332339b2ef464ea96ed9b9b28c
```

PowerShell users can instead run `./scripts/fetch-libvnc.ps1`. This script is optional and is never run automatically by package-mode builds.

**CMake:** add `-DDESKTOP_USE_BUNDLED_LIBVNC=ON` to the plugin or engine configuration above, preferably with a fresh build directory. CMake builds the checkout together with the plugin and links LibVNC statically. Install the dependency's development prerequisites first (OpenSSL, JPEG, zlib; PNG/LZO if enabled). Missing source is an error only when this option is ON.

**qmake:** build LibVNC separately, then pass its build directory to qmake:

```powershell
cmake -S third_party/libvncserver -B build/libvnc -G 'MinGW Makefiles' `
  -DCMAKE_MAKE_PROGRAM=C:/msys64/mingw64/bin/mingw32-make.exe `
  -DBUILD_SHARED_LIBS=OFF -DWITH_EXAMPLES=OFF -DWITH_TESTS=OFF `
  -DWITH_TIGHTVNC_FILETRANSFER=OFF -DWITH_SASL=OFF -DWITH_FFMPEG=OFF `
  -DWITH_GCRYPT=OFF -DWITH_GNUTLS=OFF
cmake --build build/libvnc -j 4
```

Append `LIBVNC_BUILD_DIR=C:/path/to/RemoteDesktop/build/libvnc` to the qmake command, keeping the host arguments. For Unix, omit the MinGW generator/make arguments. For a custom static dependency build, override `LIBVNC_EXTRA_LIBS` to match its enabled libraries (defaults: SSL, crypto, JPEG, PNG16, LZO2, zlib). `LIBVNC_SOURCE_DIR` can override the default source location. Remove `LIBVNC_BUILD_DIR` and rerun qmake in a fresh build directory to return to package mode.

## Install and use

1. Close RetroShare. Copy `RemoteDesktop.dll` (or `.so`/`.dylib`) into RetroShare's plugin directory. Keep the matching LibVNC runtime DLLs (`libvncserver.dll`, `libvncclient.dll` and their dependencies) reachable.
2. Restart RetroShare and enable the plugin. Both friends need this version (service protocol 2.0; it does not talk to 0.1).
3. On the computer to share: Network → Remote Desktop tab → enable **Allow incoming desktop requests**.
4. On the viewing computer: choose the friend → **Request desktop**.
5. The sharing computer shows the request pop-up. Choose the screen and permissions → **Accept**. Capture starts only now; the red banner stays on top while sharing.
6. The viewer pop-up window opens on the viewing computer. Closing it, **Stop sharing**, or **Stop session** on either side ends the session.
7. To send files, use **Send file…** (viewer toolbar, banner or plugin page). Your friend receives a RetroShare download link in private chat.


## Protocol and limits

Experimental service ID `0xa174`, protocol **2.0** (minimum 2.0). Coordinate a service ID before public distribution. Packets contain a kind, a random 128-bit session token, a 64-bit offset/value and a bounded payload. Request/Accept establish consent; Accept carries the host's permission bits (control, clipboard, chat) and the session password. Data/Ack carry ordered bytes with backpressure; Ping provides liveness; Stop invalidates the session.

Inside the RFB stream, the viewer window advertises pseudo-encoding `-0x52534401`; only then does the host send message type 230 (Hello with permissions, Chat, Clipboard). The host enforces all permissions: input is ignored unless control was granted (LibVNC view-only mode plus a plugin check), incoming clipboard needs clipboard and control, chat needs chat.

Network confidentiality and peer identity rely on RetroShare's friend connection. The loopback VNC password and lockout are defence in depth against other local programs; they do not protect against a compromised local account. Ctrl+Alt+Del is sent as ordinary keys: Windows only opens the security screen for a system service. A non-elevated RetroShare cannot control elevated windows or UAC prompts. Only one session at a time.


## License

New plugin source is AGPL-3.0-or-later, consistent with the supplied RetroShare plugin example. See LICENSE. LibVNC and other dependencies retain their own licenses; use the package's copyright/license files or the optional source checkout's COPYING file. Distributions must include the applicable notices and corresponding source.
