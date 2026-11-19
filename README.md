# auto-apo-switch

AutoAPO is a small native Windows C++ tray app that switches selected Equalizer APO commands on while any watched process is running, and off when all watched processes close.

## First run

1. Launch AutoAPO normally first. Some Equalizer APO installations grant users write access to the config folder even under Program Files. If Windows denies access and the registered config folder is protected, launch AutoAPO as administrator.
2. AutoAPO reads Equalizer APO's registered configuration directory. Use **Choose config** to select a different file.
3. PUBG (`TslGame.exe`) is watched by default. Add other executable names separated by commas, then click **Save processes**.
4. Left checkboxes display and manually change the current enabled state. Click a right-side **Auto** checkbox to let the watcher control that entry. Selecting Auto applies the current game state immediately. All Auto boxes start unchecked.
5. **Pause** stops automatic changes and allows manual control. **Refresh** rereads the file. Closing the window hides it. Click the tray icon once to reopen it, or right-click and choose **Exit** to close the app and keep the current audio settings.

The watcher checks every two seconds. Multiple games use OR logic: automatic entries stay on until the last watched game exits. Effects still apply to all audio processed by that APO device.

**Start with Windows** is off by default. Enable it to launch AutoAPO in the tray when you sign in. Turn it off from the same checkbox. Keep the EXE at the same location, or toggle the option off and back on after moving it. Startup uses your normal Windows permissions, so automatic switching requires write access to your APO configuration.

## Configuration handling

Administrator mode only accepts Equalizer APO's registered config in a protected folder under Program Files. If AutoAPO is started as administrator and the config folder is user-writable (the Equalizer APO default) or not registered, it restarts itself without administrator rights. If the Windows desktop itself runs as administrator (UAC turned off, or the built-in Administrator account), no normal-rights session exists, so AutoAPO continues in normal mode instead. Custom config selection and startup registration are available in normal mode. Administrator preferences are stored separately in the machine registry. Set up your rules again the first time you use administrator mode after upgrading.

- Only the selected file is edited. Includes appear as single entries; contents of included files are not expanded.
- The first write saves `<config filename>.AutoAPO.backup` beside the original. Existing backups are retained.
- Writes add/remove a leading comment marker, preserving UTF-8 text, BOM and line endings. UTF-16 input is unsupported.
- Peace's `Include: peace.txt` supports manual and automatic switching. Structural commands remain protected.
- Rules match complete commands, independent of line order. Editing a plugin's parameters changes its identity: reselect Auto for that entry afterward. Identical duplicate commands cannot be automated.
- AutoAPO creates a fresh temporary file for each write and uses Windows file replacement to preserve the target's access rules. External edits detected before replacement cancel that write. Avoid simultaneous editing during mode changes.
- Normal-mode preferences live in `%LOCALAPPDATA%\AutoAPO\settings.txt`.

## Build and validation

Use `build.cmd` with Visual Studio 2022 C++ Build Tools and Windows SDK, or configure the supplied CMake project with MSVC. Release builds use the static C++ runtime. No web runtime or .NET installation is needed.

`build\AutoAPO.exe --self-test` checks UTF-8/BOM/CRLF preservation, command recognition, Peace include toggling, backup preservation and concurrent-edit rejection using a temporary fixture. Exit code 0 means success.

Native GUI, Windows sign-in and real-game testing are still required before release.
