# ddwater

This is a demo of a 2D water effect using DirectDraw. Very quick and dirty.

It uses the '[two buffer trick](https://www.gamedev.net/articles/programming/graphics/the-water-effect-explained-r915/)' to get 3D-looking water in screenspace.

Notes:
* The application runs in windowed mode.
* It suspends (stops drawing) when the window isn't active, and resumes when the window is active again.
* The display is at a 640x448 fixed logical resolution. 
  * That said, you get swapchain scaling if the window is scaled.

![Example image](https://raw.githubusercontent.com/clandrew/ddwater/master/Demo/Animation.gif "Example image.")

## Controls

Click or drag the left mouse button on the water to raise the water heightmap.

Press the escape key, or close the window to exit the application.

## Build
The source code is organized as a Visual Studio 2019 built for x86-64 architecture. It has been tested on Windows 10. It is built against the Windows SDK.

Images get loaded using WIC.

The program uses WRL COM object wrappers, which might prevent it from being used on very old OS. Yeah, it was weird using those wrappers alongside DirectDraw.
