# FH3_FreshOuttaFactory

FreshOuttaFactory for Forza Horizon 3 on Azimuth. Requires [FH3 ModPlatform 1.4.2 or later](https://github.com/Levilente/FH3_ModPlatform/releases) and the game executable with PE timestamp `0x5B16ECCA`.

- **No crash damage (all cars)** is on by default. It suppresses new visual impact damage and detached body parts for the player and Drivatars. Existing damage stays until the car is repaired or reloaded. The game still simulates mechanical damage according to its difficulty settings.
- **Always clean (no dirt)** is off by default. It clears dirt, mud, random dirtiness, and mud dampness from the car render snapshot on each update.

Copy the DLL and INI into the `fh3-mods` folder next to `azimuth.exe`. Keep your existing INI when upgrading. Change either switch from **Home > MODS > Installed mods > FH3_FreshOuttaFactory**. The platform checks [the mod's GitHub releases](https://github.com/suderek22/FH3_FreshOuttaFactory/releases) for newer versions.

Run `powershell -ExecutionPolicy Bypass -File .\build.ps1` with Visual Studio C++ Build Tools and CMake to stage the release in `dist`. The SDK header is from ModPlatform v1.4.2.

## Game hooks

The damage hooks target the concrete FH3 car-presentation methods at `0x1E33770` (ApplyDamage) and `0x1E3CF60` (DetachPart).

The dirt hook intercepts the render snapshot call at `0x1E7D0E9`. It lets the game populate the snapshot, then clears the dirt and mud amounts, random dirtiness vector, and mud dampness before the game submits the snapshot.

Check `fh3-mods\platform.log` for setup errors.
