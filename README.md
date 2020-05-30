# overhead - a simple overhead display for Windows.

![overhead title image](https://github.com/edwinst/overhead/raw/master/overhead_title_gray_304px.png)

## Summary

This program can display the outlines of the transparent areas
of an image (a stream overlay for example) and optionally a
count-down timer on top of all other Windows.

I made this as a little helper for my streaming needs. Maybe
it is also useful to you because it can easily be modified and
extended by changing the source code which I release into
the public domain (see below for un-license terms).

One feature is that 'overhead' does not rely on the compositing
window manager because OBS does not work reliably for me when
compositing is active. Therefore, the program currently creates
a separate window for every non-transparent area it displays.

The program is written in a self-contained, "handmade" style
by directly calling the Win32 APIs. Drawing is currently done
via the Windows GDI but you could easily use any other API, too.
The only dependency besides the Windows DLLs is stb_image,
which is also distributed along the source code.

## License

This program and the `stb_image` library it uses are in the
public domain. See the file `LICENSE`, comments in `overhead.cpp` and in
`third_party/stb_image.h` for details.

## Building

Execute `build.bat` in an environment that is set up correctly for
Microsoft Visual C++ in order to build the program.

It should be straight-forward to adapt `build.bat` to other toolchains
if you so desire.

## Usage

See comments `overhead.cpp` for an explanation of how to use this program.

## Credits

`overhead` uses the great image loading library [stb_image](https://github.com/nothings/stb) by Sean Barrett.

The program itself was written by Edwin Steiner.
