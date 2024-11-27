# The Force Engine (TFE)
* [Website](https://theforceengine.github.io/)
* [Release Downloads](https://theforceengine.github.io/downloads.html)
* [Forums](https://the-force-engine.freeforums.net/)
* [Discord](https://discord.gg/hpsJnY9)
* [Project Structure](ProjectStructure.md)

## About
“The Force Engine” is a project with the goal to reverse engineer and rebuild the “Jedi Engine” for modern systems and the games that used that engine, like “STAR WARS™: Dark Forces” and “Outlaws”. The project will include modern, built‑in tools, such as a level editor and will make it easy to play “STAR WARS™: Dark Forces” and “Outlaws” on modern systems as well as the many community mods designed to work with the original games.

Playing “STAR WARS™: Dark Forces” or, in the future, “Outlaws” using “The Force Engine” adds ease of use and modern features, such as higher resolutions and modern control schemes, such as mouselook. Using the built‑in tools, once they are available, will enable easier modding with more modern UI, greater flexibility, and the option to use enhancements made to the “Jedi Engine for “Outlaws” in custom “STAR WARS™: Dark Forces” levels, such as slopes, stacked sectors, per‑sector color palettes and more.

“STAR WARS™: Dark Forces” support is complete, but “Outlaws” is not playable yet. The focus so far has been on the framework, “STAR WARS™: Dark Forces” support, and “Jedi Engine” reverse‑engineering. However, “Outlaws” support is planned and will be complete in TFE version 2.0. See [Current State](#current-state) below.

**A purchased copy of the original game is required and is not provided by “The Force Engine”.** The [documentation](https://theforceengine.github.io/Documentation.html) has some information on how to legally purchase “STAR WARS™: Dark Forces”. TFE is **not** a remaster, it is essentially a source port designed to run the *original* game natively on modern systems with quality of life improvements and optional enhancements. “STAR WARS™: Dark Forces” and “Outlaws” are owned by Disney and are still active, commercial products. The IP is owned solely by Disney.

**Linux is supported but may require additional setup.** You may need to compile TFE from source in order to run it directly on your favorite Linux distro. For most Linux users, it is recommended to install and run [TFE via Flatpak from Flathub](https://flathub.org/apps/io.github.theforceengine.tfe) on those Linux systems where Flatpak is available. Search your distro’s software center or app portal; if you are already using [GNOME Software](https://apps.gnome.org/de/Software "Software") or KDE’s [Discover](https://apps.kde.org/discover "Discover") (like on the “[Steam Deck](#steamdeck)”) then installing TFE may be just one click away from you. See the [Linux](#linux) section below.

In addition, a [Snap](https://snapcraft.io) package is planned for the future, alleviating the need to manually compile the project on systems employing Snap. If you don’t want to compile the code, it might be better to use Windows for now or use Proton on the “Steam Deck”.

### Steam Deck
On this system it is recommended to run “The Force Engine” in desktop mode and installing it via KDE’s Discover (from Flathub).

## Current Features
* Full “STAR WARS™: Dark Forces” support, including mods. “Outlaws” support is coming in version 2.0.
* Mod Loader — simply place your mods in the `Mods` directory as zip files or directories.
* High resolution and widescreen support — when using 320×200 you get the original software renderer. TFE also includes a floating‑point software renderer which supports widescreen, including ultrawide, and much higher resolutions.
* GPU renderer with perspective correct pitch — play at much higher resolutions with improved performance.
* Extended Limits — TFE, by default, will support much higher limits than the original game’s “Jedi Engine” was capable of, which removes most of the HOM (Hall of Mirrors) issues in advanced mods.
* Full input binding, mouse sensitivity adjustment, and controller support. Note however that menus currently **require a pointing input device, like a mouse or touchscreen**.
* Optional quality‑of‑life improvements, such as full mouselook, aiming reticle, improved Boba Fett AI, autorun, and more.
* A new save system that works seamlessly with the exiting checkpoint and lives system. You can ignore it entirely, use it just as an exit save so you don’t have to play long user levels in one sitting, or full save and load with quicksaves like “Doom” or “Duke Nukem 3D”.
* Sound Font (sf2) and OPL3 emulation support.
* Optional and quality‑of‑life features, even mouselook, can be disabled if you want the original experience. Play in 320×200, turn the mouse mode (input menu) to menus only or horizontal, and enable the classic (software) renderer — and it will look and play just like DOS but at a higher frame rate and no need to adjust cycles in “DosBox”.

## Current State
Full support for “STAR WARS™: Dark Forces” has been completed. You can play through the entire game, with all AI, weapons, items, and functionality working as expected. While the project shares a legacy with DarkXL, it is a complete rewrite, rebuilt from the ground up with a much greater focus on accuracy. It is much more focused than the “XL Engine”, focused on being a “Jedi Engine” replacement/port only. Thus, full support for “STAR WARS™: Dark Forces” and later “Outlaws”. Please, check the [Roadmap](Roadmap.md) for more information.

## Current Release
The current release only supports “STAR WARS™: Dark Forces”. All weapons, AI, items, and all other systems work, including [iMUSE](https://en.wikipedia.org/wiki/IMUSE). You can play through “STAR WARS™: Dark Forces” from the beginning to the end and even play “STAR WARS™: Dark Forces” mods. As with any project of this nature, there may be bugs and system specific issues. If you run into any bugs that cannot be reproduced in the DOS version, please post them on the [forums](https://the-force-engine.freeforums.net) or [GitHub](https://github.com/luciusDXL/TheForceEngine/issues).

## Minimum Requirements
* OpenGL 3.3
* Windows 7, 64 bit / modern Linux distro / macOS

Note that there are plans to lower the requirements for using the classic software renderer in the future. However, the minimum requirements for GPU rendering are here to stay. For now, only OpenGL is supported, which may limit the use of some older Intel integrated GPUs that would otherwise be capable. There are near term plans to add Direct3D 10/11, Vulkan, and maybe Metal render backends which should enable more GPUs to run the engine efficiently.

## Windows
The release package includes the Windows binary and all of the data (except for the original game) needed to run. If you want to compile TFE yourself, use the Visual Studio solution provided.

## Linux
### General Notes
Runtime data like savegames, configuration, mods, etc. are by default stored in `${HOME}/.local/share/TheForceEngine`.
This can be overridden by defining the `TFE_DATA_HOME` environment variable.

### Required Libraries
* [SDL2](http://libsdl.org) Version 2.24
* [SDL2-image](https://github.com/libsdl-org/SDL_image) Version 2.6.3
* OpenGL 3.3 capable driver (latest [Mesa 3D](https://www.mesa3d.org) or Nvidia proprietary driver recommended)

### Optional Libraries
* [RtMidi](https://www.music.mcgill.ca/~gary/rtmidi/) 5.0.0 or higher for external MIDI synthesizer support

### Building from Source
#### Recommended Tools
* [CMake](https://cmake.org) 3.12 or higher to build the source.
* GCC 11 and newer or equivalent clang version.
#### How to build
* Unpack the source or fetch from GitHub
* Create a build directory and chdir into it:
  ```sh
  mkdir tfe-build
  cd tfe-build
  ```
* Run CMake in the build directory, the build type must be specified (debug or release):
  ```sh
  cmake -S /path/to/tfe-source
  ```
* Build it:
  ```sh
  make
  ```
* Install it:
  ```sh
  sudo make install
  ```
* If no additional parameters were added to CMake, files will be installed in `/usr/local/bin` and `/usr/local/share/TheForceEngine`

#### Running TFE
##### External application dependencies
* “[KDialog](https://develop.kde.org/docs/administration/kdialog "Shell scripting with KDE dialogs")” for file dialog on KDE Plasma Desktop Environment.
* “zenity” for file dialog on all other desktop environments.
* external MIDI is no longer required but still supported through the “System MIDI” device.

##### Launch
* Start the engine by clicking on the “The Force Engine” desktop icon or by running `theforceengine` in a shell.

## MacOS

Prerequisites available via Homebrew:

```bash
brew install cmake pkg-config sdl2 sdl2_image rtmidi
```

You also need Xcode Command Line Tools:
```bash
xcode-select --install
```

```bash
mkdir build && cd build
cmake ..
make
```

## Packaging
TFE comes with the build‑in “[AdjustableHud](TheForceEngine/Mods/TFE/AdjustableHud)” mod. Package maintainers may wish and are encouraged to package “AdjustableHud” into a separate *required* or *recommended* package.

By default TFE builds and installs with the “AdjustableHud” mod. If you do not want to install the “AdjustableHud” mod, for example when building separate packages, configure the CMake build with the `-DENABLE_ADJUSTABLEHUD_MOD=OFF` option. If you want to install the “AdjustableHud” mod only use the `-DENABLE_TFE=OFF` option.

### Freedesktop
On Freedesktop compliant systems package maintainers are encouraged to use the provided [AppStream meta data file](TheForceEngine/io.github.theforceengine.tfe.Mod.AdjustableHud.metadata.xml) when packing the “AdjustableHud” mod into a separate package.
