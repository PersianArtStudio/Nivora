**English** | [فارسی](README.fa.md)

<p align="center"><img src="assets/logo.png" width="80" alt="Nivora logo"></p>
<h1 align="center">Nivora</h1>
<p align="center">A native Windows launcher for Antigravity App, IDE and CLI.</p>

Nivora is built for people in regions or on networks where access to Antigravity is restricted. It lets you start an installed Antigravity client with a local HTTP or SOCKS5 proxy you already run. Service availability depends on your network, account eligibility, and the provider’s access requirements.

[**Download for Windows x64**](https://github.com/PersianArtStudio/Nivora/releases)

Choose a target, start your local proxy and click **Launch selected target**. Nivora checks the listener, starts the selected application and can move to the notification area. Each target remembers its own executable path.

## Install on Windows

Download and run **Nivora-Setup-1.1.0-x64.exe** from the **Assets** section of release `v1.1.0`. Windows 10 or 11 (x64), your chosen Antigravity client, and a running local proxy are required. The installer adds a Start menu shortcut for the current user. A desktop shortcut is optional. Remove Nivora through Windows Settings → Apps. The source archive includes everything needed to build the launcher with the tools below.

## Choose a launch target

| Dashboard option | What opens | How to configure it |
| --- | --- | --- |
| Antigravity App | The standalone desktop app | Automatic installation discovery or select Antigravity.exe |
| Antigravity IDE | The standalone editor | Automatic discovery or select Antigravity IDE.exe |
| Antigravity CLI | An interactive terminal running agy | Install the CLI, select agy.exe if needed, then choose a project folder each time you launch |
| Editor with Antigravity extension | Your installed editor | Install and configure its Antigravity integration, then select its trusted executable in Settings |

Select the target on the dashboard **before** opening Settings → Browse. Close an existing instance of that target before launch so it receives the new process environment. A session uses one target at a time. Open the notification icon to stop or restart the session. Save work before stopping it.

## Build the installer

On the Windows x64 build computer install Python 3.7+ with pip and Inno Setup 6.3 or later in the 6.x series. The build reuses Zig 0.13.0 when available. Otherwise it fetches the pinned Windows x64 compiler (80 MB), checks its SHA-256 and caches it under `.build-tools` in the project. No system PATH change is required. For an offline build, put `ziglang-0.13.0-py3-none-win_amd64.whl` next to the BAT file. For a custom Inno Setup installation, set ISCC to its compiler path.

1. Extract or clone the source into a writable folder.
2. Double-click **Build-Installer.bat**.
3. Distribute **dist/Nivora-Setup-1.1.0-x64.exe** and its **SHA256SUMS.txt**.

The compiler and packaging tools are needed only on the build computer. The Windows workflow builds the installer and defines dashboard, install and uninstall smoke tests. A version tag triggers release publication after the build succeeds. Release artifacts contain the installer and its checksum. Generated executables are excluded from source control.

## Connection and privacy

Nivora probes local HTTP or unauthenticated SOCKS5 listeners on IPv4 loopback. Select a protocol and port in Settings to override discovery. App and IDE receive the Chromium proxy argument. All targets receive proxy environment variables scoped to their own process tree. CLI and external editor traffic depends on proxy support in the target, extension and subprocesses. A successful probe confirms only the local listener. Nivora uses the application's normal sign-in.

Activity → Export saves filtered events. Preferences live under HKCU/Software/Nivora. Rotated logs live under %LOCALAPPDATA%/Nivora. Startup and notifications are configurable. Uninstall preserves preferences and logs.

## Troubleshooting

- **Target missing** — select the actual executable under Settings. CLI discovery checks absolute PATH entries and common user-install locations.
- **Listener unavailable** — start your proxy client and verify its local port. An unrelated HTTP server can respond to the HTTP probe.
- **Editor opens without Antigravity** — install and enable the relevant editor extension or configure ACP inside the editor. The editor target launches the host.
- **CLI session ends** — start a fresh session and select an accessible project folder. Its terminal belongs to the session and closes when the process exits.

## License and acknowledgments

Apache License 2.0. Nivora is an independent project. The original local proxy discovery and launcher workflow comes from dalroot/antigravity-cleaner. See [NOTICE](NOTICE) and [LICENSE](LICENSE).
