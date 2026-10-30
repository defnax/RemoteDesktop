# RetroShare Remote Desktop Plugin

A remote desktop plugin for RetroShare: view and control a friend's screen over your encrypted RetroShare friend connection.

# Features

- **Remote Desktop** tab on RetroShare's Network page
- Request a friend's desktop; every request must be accepted on the other side
- Accept dialog with screen choice, keyboard/mouse control, clipboard and chat permissions
- Red "sharing" banner with Chat, Send file and Stop while your screen is shared
- Remote desktop opens in its own viewer window
- Viewer: full screen, fit to window or 100 %, quality presets, bandwidth scaling
- Ctrl+Alt+Del and common key combinations (Win, Alt+Tab, Alt+F4, …)
- Windows keys (Win, Alt+Tab) are sent to the remote computer while the viewer has focus
- Text clipboard sync and chat
- Send files through RetroShare file transfer (download link in private chat)
- No open ports: the VNC connection only listens on 127.0.0.1 and travels inside RetroShare

# Dependencies

#### Debian/Ubuntu/Linux Mint

```
sudo apt install libvncserver-dev libx11-dev libxtst-dev
```

#### Arch Linux

```
sudo pacman -S libvncserver libx11 libxtst
```

#### Windows (MSYS2 MinGW64)

```
pacman -S mingw-w64-x86_64-libvncserver
```

# Build & install

Clone into the `plugins` folder of your built RetroShare source and build with the same Qt kit as RetroShare:

```
cd RetroShare/plugins
git clone https://github.com/defnax/RemoteDesktop.git
cd RemoteDesktop
qmake RemoteDesktop.pro
make            # Windows: mingw32-make
```

Or with CMake (`RETROSHARE_COMPILE_DEFINITIONS` must match the defines RetroShare was built with):

```
cmake -S . -B build -DRETROSHARE_SOURCE_DIR=/path/to/RetroShare \
      -DRETROSHARE_COMPILE_DEFINITIONS="RS_ENABLE_GXS;RS_DIRECT_CHAT"
cmake --build build
```

On Windows add `-G "MinGW Makefiles"` and the import libraries `-DRETROSHARE_CORE_LIBRARY=.../libretroshare/src/lib/libretroshare.dll.a -DRETROSHARE_GUI_LIBRARY=.../retroshare-gui/src/lib/libretroshare-gui.a`. For a Qt 6 RetroShare add `-DDESKTOP_QT_MAJOR=6`.

Copy the built plugin (qmake: `lib/RemoteDesktop.dll`, CMake: `build/libRemoteDesktop.dll`, Linux: `.so`) into RetroShare's plugin folder, keep the LibVNC DLLs next to RetroShare on Windows, then restart RetroShare and enable the plugin in Settings → Plugins.

#### Windows: LibVNC DLLs

The plugin needs `libvncserver.dll` and `libvncclient.dll` from MSYS2. Copy them from `C:\msys64\mingw64\bin` into the folder that contains `retroshare.exe`:

```
copy C:\msys64\mingw64\bin\libvncserver.dll "C:\path\to\RetroShare\"
copy C:\msys64\mingw64\bin\libvncclient.dll "C:\path\to\RetroShare\"
```

If RetroShare still cannot load the plugin, a dependency of LibVNC is missing. 
List all needed DLLs in the MSYS2 MinGW64 shell with `ldd lib/RemoteDesktop.dll | grep mingw64` and copy the missing ones from the same folder. 
(Starting RetroShare from the MSYS2 shell hides this, because MSYS2 puts `C:\msys64\mingw64\bin` on the PATH.)

# Usage

Both friends need the plugin. The friend who shares opens Network → Remote Desktop and ticks "Allow incoming desktop requests". The other friend picks them in the list and clicks "Request desktop". The sharing side gets a pop-up to accept and choose what is allowed (view only or control, clipboard, chat); the viewer window then opens on the requesting side. Closing the viewer, or Stop on either side, ends the session. Linux sharing needs an X11 session (Wayland is not supported); macOS needs Screen Recording and Accessibility permission.
