How to play the game
====================

To run, double click RunGame.bat. Use mouse and keyboard to play.

This game should run on Windows 10 computers. The code has not been tested
very extensively, so apologies in advance if it breaks.

Tools used
----------

- Blender - 3D modeling
- Krita - Image editor
- [ Sfxr ]( http://www.drpetter.se/project_sfxr.html ) - Sound effects
- LAME - MP3 encoding
- Sublime Text 3 - Text editor
- Visual Studio 2019 - Compiler & Debugger
- PIX, Renderdoc and Nvidia Nsight - Graphics debugger
- Git and github for source control and LFS storage.

All code was written from scratch except for Windows API, DirectX 12 API and
the following libraries, which are included in the repository:

- [ stb libraries ]( https://github.com/nothings/stb ): stb_ds.h, stb_image.h and stb_truetype.h
- [ meow_hash ]( https://github.com/cmuratori/meow_hash )
- [ minimp3 ]( https://github.com/lieff/minimp3 )


Documentation for the source
============================

This is a game engine written for fun, intended to be used for making small
games and as a playground to implement different graphics features and
techniques.

The source is released to comply with the Ludum Dare Compo rules. You can use it
for educational or non-commercial purposes.

Engine features
---------------

- Hot reloading of C++ sources
- Hot reloading of HLSL shaders
- Static OBJ loading
- In-game material sliders
- Simple UI system
- Omni point lights
- Soft depth shadows (naive PCF impl)
- Raytraced hard shadows

How to build and run
--------------------

This source has been tested with Visual Studio 2019, but other versions might
work.

Either open a Visual Studio 2019 x64 Native Tools command prompt, or in a
regular command prompt, run the vcvarsall.bat script included in your Visual
Studio installation, like so: `vcvarsall.bat x64`

Then, from root directory in the command prompt, run the build.bat script.

The game will be built to `Build\game.exe`

There's a RunGame.bat script in the root directory, which you can run to
launch the game.

The `game.exe` executable will run the test suite by default. You can run the
game by passing the `-game` flag, or just `-g` for short. For any flag, you
can use either the full name or any prefix.

The valid flags are:

- `-fullscreen` to run in fullscreen.
- `-width` and `-height` to set the window size when not in fullscreen.
- `-game` to run the game and `-test` to run tests.

So, for example, `Build\game -t -w 800 -h 600` will run the test suite in
a 800x600 window, while `Build\game.exe -f -g` will run the game in fullscreen
mode.

You can build the code while the game is open. The engine will attempt to
reload the code. As long as there are no changes to struct definitions it
should work, otherwise you'll get undefined behavior ;)

If you edit shaders, building will also hot-reload, but if you'd like
instantaneous updates, you can do `Build\shaders` instead.

Finding your way around the code
--------------------------------

Code is in the `Code` directory and shaders in the `Shaders` directory.
Most definitions are in the header file `Engine.h`. The header is sorted by
subsystem, from low level to high level.

The backend is DX12, though the low level graphics API is designed to make it
easy to add a new modern API backend.


Code style
----------

0. Zero is initialization. Init to zero by default. (i.e. MyType x = {}; for most declarations)
1. No RAII and only default constructors and destructors.
2. No templates or operator overloading, except for math library.
3. No classes or type hierarchies
4. Only lifespan allocators (i.e. no malloc/free or new/delete)
5. Single translation unit. (Might revisit if/when compile times become a problem)
6. (Almost) never put pointers in structs. Use handles
7. Handles are invalid iff they are 0
8. Hungarian notation for core data structures: 's' for Stretchy buffers `sMyVariable` and 'hm' for hash maps: `hmMyVariable`.
9. Constant buffer types are suffixed with CB as in `MaterialConstantsCB`
10. CamelCase for types and macros
11. Snake_Camel_Case for enum values.
12. mixedCase for functions and variables.
13. K&R brace style; with else clause in its own line
