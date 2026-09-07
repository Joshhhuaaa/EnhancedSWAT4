# Enhanced SWAT 4

A patch for SWAT 4 and its expansion, SWAT 4: The Stetchkov Syndicate, fixing bugs and improving gameplay.

If you'd like to donate, all contributions are appreciated.
<div align="left">
  <a href="https://www.paypal.com/donate/?hosted_button_id=UB67N4GNTCEZ6">
    <img src="https://github.com/user-attachments/assets/6a8878e8-3ae8-48e5-8d2a-ae367c71df10" width="256" alt="PayPal"/>
  </a>
</div>

## Installation
The latest version of Enhanced SWAT 4 can be found on the [Releases](https://github.com/Joshhhuaaa/EnhancedSWAT4/releases) page.

### Game Setup
- After downloading Enhanced SWAT 4, extract the contents to your SWAT 4 directory.
- You can adjust additional settings in `EnhancedSWAT4.ini`, located in the `Content\System\plugins` folder for SWAT 4 or the `ContentExpansion\System\plugins` folder for SWAT 4: The Stetchkov Syndicate.

> [!TIP]
> Enhanced SWAT 4 is fully compatible with [SWAT: Elite Force](https://github.com/eezstreet/SWATEliteForce), and is recommended for playing SWAT 4 as it fixes many of the original game's bugs while also improving gameplay.

## Uninstallation
- Navigate to the `Content\System` folder for SWAT 4 or the `ContentExpansion\System` folder for SWAT 4: The Stetchkov Syndicate, then delete the `plugins` folder and `dinput8.dll`.

## Features
### Skip Intro
Skips the game's intro and logo screens for a faster launch into the game.

### Widescreen Support
In the stock game, the HUD stretches at widescreen aspect ratios and some elements shrink at higher resolutions. Enhanced SWAT 4 dynamically scales HUD elements to maintain their original proportions at any resolution.

Field of view is calculated automatically based on the aspect ratio, widening the horizontal FOV while preserving the vertical FOV from 4:3. `FieldOfView` in the ini can turn this off for the stock projection, or force a specific horizontal FOV instead.

<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src=""></td>
      <td width="50%"><img style="width:100%" src=""></td>
    </tr>
    <tr>
      <td align="center">Stock</td>
      <td align="center">Enhanced</td>
    </tr>
  </table>
</div>

### Raw Input
Mouse input accurately reads data at high polling rates, eliminating the need to cap the mouse at 125 Hz.

### Mouse Sensitivity Multiplier
Separate sensitivity multipliers for in-game aiming and the menu cursor allow for more control than the in-game slider.

### Borderless Support
Adds an option to run the game in borderless windowed mode. Borderless always renders at the native resolution, regardless of the in-game resolution setting.

### Anisotropic Filtering
Forces anisotropic texture filtering.

<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src=""></td>
      <td width="50%"><img style="width:100%" src=""></td>
    </tr>
    <tr>
      <td align="center">Stock</td>
      <td align="center">Anisotropic 16x</td>
    </tr>
  </table>
</div>

### Multisample Antialiasing (MSAA)
Enables MSAA to smooth jagged edges while preserving a sharp image. MSAA does not smooth alpha-tested edges such as fences and foliage.

<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src=""></td>
      <td width="50%"><img style="width:100%" src=""></td>
    </tr>
    <tr>
      <td align="center">Stock</td>
      <td align="center">MSAA 8x</td>
    </tr>
  </table>
</div>

### Subpixel Morphological Antialiasing (SMAA)
Enables SMAA to smooth jagged edges with a softer image. SMAA also smooths alpha-tested edges such as fences and foliage.

<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src=""></td>
      <td width="50%"><img style="width:100%" src=""></td>
    </tr>
    <tr>
      <td align="center">Stock</td>
      <td align="center">SMAA</td>
    </tr>
  </table>
</div>


### Optiwand Resolution
Removes the stock 256x256 resolution limit for the Optiwand, allowing its texture resolution to be increased up to 4096x4096.

<div align="center">
  <table>
    <tr>
      <td width="50%"><img style="width:100%" src=""></td>
      <td width="50%"><img style="width:100%" src=""></td>
    </tr>
    <tr>
      <td align="center">Stock</td>
      <td align="center">Enhanced</td>
    </tr>
  </table>
</div>

### Framerate Limiter
Sets a maximum framerate. A value of `0` disables the limiter, `-1` matches the monitor's refresh rate, and any other value enables a hard cap at that value.

### Multiplayer
- Updates the master server to use [swat4stats.com](https://swat4stats.com), restoring the in-game server browser without requiring a patched Engine.dll.
- Net speed is forced to 480 kbps (32x the stock LAN/T1 limit), providing plenty of bandwidth headroom for higher FPS. The in-game Connection Speed setting is ignored, preventing a misconfigured setting from bottlenecking the connection.

### Bug Fixes
- Fixed the long delay when making selections in SwatEd on modern hardware.
- Prevent SwatEd from changing the desktop gamma when launching.
- Fixed crashes that could occur when alt-tabbing out of the game while in fullscreen, particularly when an overlay such as RivaTuner was hooked.
