/* overhead - a simple overhead display for Windows.

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
*/

namespace {
    constexpr char *g_usage = 
        "Usage: overhead [X [Y [W [H]]]] [--countdown=MINUTES] [--background=BACKGROUND_IMAGE] [--overlay=OVERLAY_IMAGE]\n"
        "\n"
        "Note: W and H are ignored if you specify a BACKGROUND_IMAGE.\n";
}

/*
    The following options invoke the features currently implemented:

    *) --countdown=MINUTES ... shows (and immediate starts) a countdown
       of MINUTES minutes. The timer is rendered by default on a black
       rectangle with the positions and dimensions you specify as
       X, Y, W, H. If you specify --background=IMAGE, it is rendered on
       top of the given image instead and W(idth) and H(eight) are taken
       from this image. IMAGE is expected to be an RGB image without alpha
       channel.

    *) --overlay=IMAGE ... This option expects IMAGE to be in RGBA format.
       It analyzes the alpha channel of the given image and finds its
       transparent regions (defined by alpha < 255). It then displays
       single-pixel-wide red lines just outside the transparent areas.
       The intended use is to pass an IMAGE that is used as a stream
       overlay in, say, OBS Studio, so that you can exactly see the
       outlines of the screen area that will be visible to your viewers
       (but you can still use the overlayed areas for your own viewing).

       Note: The algorithm for finding the transparent regions is currently
       very dumb and will work well only for relatively simple shapes.
       The outline of the transparent area should be made piecewise of
       not too many horizontal and vertical straight lines because every
       straight line portion is translated into a separate window.
       (This is to avoid reliance on the compositing window manager
       as mentioned above.)

    Limitations
    -----------

    The program sets the window styles that put it on top of all other
    windows. However, it may loose the fight against the Windows task bar
    which also draws itself over all other windows. Therefore you should
    avoid putting your countdown timer in the area covered by the Windows
    task bar. Please tell me if you find a simple way to remove this
    limitation.
 */

/*
This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.

In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

For more information, please refer to <http://unlicense.org/>

For the license terms applying to the included distribution of
the stb_image library see the end of the file

    third_party/stb_image.h

*/

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <cinttypes>
#include <cctype>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

//#define UNICODE
#include <windows.h>

namespace {
    HWND g_main_window = NULL;

    void print_windows_system_error(FILE *file)
    {
        // XXX @Incomplete extend this function for UNICODE
        DWORD error = ::GetLastError();

        fprintf(file, ": (0x%08X) ", error);

        char buffer[1024] = { 0 };
        DWORD bufsize = (DWORD)min(sizeof(buffer), (size_t)65535);
        DWORD result = ::FormatMessage(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, // dwFlags
            NULL, // lpSource
            error, // dwMessageId
            0, // dwLanguageId
            (LPSTR)buffer, // lpBuffer
            bufsize, // nSize
            NULL); // Arguments
        assert(result < sizeof(buffer));
        if (result > 0)
            fputs(buffer, file);
        else
            fputs("UNKNOWN ERROR (FormatMessage failed)", file);

        // only add the newline if FormatMessage did not supply it
        if (result == 0 || buffer[result - 1] != '\n')
            fputc('\n', file);
    }

    void open_console_window()
    {
        AllocConsole();
        freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
        freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);

        // move the main window back in z-order
        (void)::SetWindowPos(
                g_main_window, // hWnd
                HWND_BOTTOM, // hWndInsertAfter
                0, 0, 0, 0, // X, Y, cx, cy
                SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);

        // move the console window to the top
        HWND console_window = ::GetConsoleWindow();
        if (console_window)
            (void)::SetWindowPos(
                    console_window, // hWnd
                    HWND_TOP, // hWndInsertAfter
                    0, 0, 0, 0, // X, Y, cx, cy
                    SWP_NOMOVE | SWP_NOSIZE);
    }

    void prompt_for_console_key_press()
    {
        HANDLE hstdin = ::GetStdHandle(STD_INPUT_HANDLE);
        if (!hstdin || hstdin == INVALID_HANDLE_VALUE)
            return;
        TCHAR *prompt = TEXT("Press any key to continue...");
        DWORD count;
        if (!::WriteConsole(
            ::GetStdHandle(STD_OUTPUT_HANDLE),
            prompt,
            lstrlen(prompt),
            &count,
            NULL))
            return;

        DWORD mode;
        if (!::GetConsoleMode(hstdin, &mode))
            return;
        (void)::SetConsoleMode(hstdin, 0);
        (void)::WaitForSingleObject(hstdin, INFINITE);
        TCHAR ch;
        (void)::ReadConsole(hstdin, &ch, 1, &count, NULL);
        (void)::SetConsoleMode(hstdin, mode);
    }

    void exit_error(char *fmt, ...)
    {
        // XXX @Incomplete extend this function for UNICODE
        open_console_window();
        va_list vl;
        va_start(vl, fmt);
        fprintf(stderr, "error: ");
        vfprintf(stderr, fmt, vl);
        prompt_for_console_key_press();
        exit(EXIT_FAILURE);
    }

    void exit_usage(char *fmt, ...)
    {
        // XXX @Incomplete extend this function for UNICODE
        open_console_window();
        va_list vl;
        va_start(vl, fmt);
        fprintf(stderr, "error: ");
        vfprintf(stderr, fmt, vl);
        fprintf(stderr, "\n\n%s\n", g_usage);
        prompt_for_console_key_press();
        exit(EXIT_FAILURE);
    }

    void exit_clib_error(char *fmt, ...)
    {
        // XXX @Incomplete extend this function for UNICODE
        open_console_window();
        va_list vl;
        fputs("error: ", stderr);
        va_start(vl, fmt);
        vfprintf(stderr, fmt, vl);
        va_end(vl);
        #pragma warning (suppress : 4996) // no need for strerror_s
        fprintf(stderr, ": (%d) %s\n", errno, strerror(errno));
        prompt_for_console_key_press();
        exit(EXIT_FAILURE);
    }

    void exit_windows_system_error(char *fmt, ...)
    {
        // XXX @Incomplete extend this function for UNICODE
        open_console_window();
        va_list vl;
        fputs("error: ", stderr);
        va_start(vl, fmt);
        vfprintf(stderr, fmt, vl);
        va_end(vl);
        print_windows_system_error(stderr);
        prompt_for_console_key_press();
        exit(EXIT_FAILURE);
    }
}

namespace {
    int g_countdown_minutes = 0;
    SYSTEMTIME g_expiry_time = { 0 };
    HFONT g_font = NULL;
    int g_position_x = 0;
    int g_position_y = 0;
    CHAR *g_background_image_filename = nullptr;
    int g_background_image_width = 150;
    int g_background_image_height = 25;
    uint8_t *g_background_image_data = nullptr;
    BITMAPINFO g_background_image_info = { 0 };

    CHAR *g_overlay_image_filename = nullptr;

    struct MarkerWindow {
        HWND window;
        int x;
        int y;
        int w;
        int h;
    };

    struct MarkerWindowArray {
        MarkerWindow *array;
        int n_allocated;
        int n_used;
    };

    MarkerWindowArray g_marker_windows;

    void load_background_image()
    {
        char *filename = g_background_image_filename;
        if (!filename)
            return;
        int image_width;
        int image_height;
        int image_n_components;
        unsigned char *data = stbi_load(filename, &image_width, &image_height, &image_n_components, 0);
        if (!data)
            exit_error("could not load image from file '%s'\n", filename);
        if (image_n_components != 3)
            exit_error("unexpected number of components in image '%s' (is %d; expected 3)\n", filename, image_n_components);

        g_background_image_width = image_width;
        g_background_image_height = image_height;
        g_background_image_info.bmiHeader.biSize = sizeof(g_background_image_info);
        g_background_image_info.bmiHeader.biWidth = image_width;
        g_background_image_info.bmiHeader.biHeight = -image_height; // negative means top-down storage
        g_background_image_info.bmiHeader.biPlanes = 1;
        g_background_image_info.bmiHeader.biBitCount = 24;
        g_background_image_info.bmiHeader.biCompression = BI_RGB;
        g_background_image_info.bmiHeader.biSizeImage = 0; // automatically calculated for BI_RGB
        g_background_image_info.bmiHeader.biXPelsPerMeter = 0;
        g_background_image_info.bmiHeader.biYPelsPerMeter = 0;
        g_background_image_info.bmiHeader.biClrUsed = 0;
        g_background_image_info.bmiHeader.biClrImportant = 0;

        // rearrange bitmap data for consumption by the GDI in a slow and straight-forward way
        uint32_t unaligned_scanline_size = image_width * 3;
        uint32_t aligned_scanline_size = ((unaligned_scanline_size + 3) / 4) * 4;
        uint32_t aligned_size = aligned_scanline_size * image_height;
        g_background_image_data = (uint8_t*)malloc(aligned_size);
        if (!g_background_image_data)
            exit_error("could not allocate memory for background bitmap");
        for (uint32_t y = 0; y < (uint32_t)image_height; ++y)
            for (uint32_t x = 0; x < (uint32_t)image_width; ++x) {
                g_background_image_data[aligned_scanline_size * y + 3*x + 0] = data[unaligned_scanline_size * y + 3*x + 2];
                g_background_image_data[aligned_scanline_size * y + 3*x + 1] = data[unaligned_scanline_size * y + 3*x + 1];
                g_background_image_data[aligned_scanline_size * y + 3*x + 2] = data[unaligned_scanline_size * y + 3*x + 0];
            }
        stbi_image_free(data);
        // XXX @Leak currently leaking g_background_image_data
    }

    int add_marker_rectangle(int x, int y, int w, int h)
    {
        if (w <= 0 || h <= 0)
            return -1;
        if (g_marker_windows.n_used == g_marker_windows.n_allocated) {
            g_marker_windows.n_allocated *= 2;
            MarkerWindow *new_array = (MarkerWindow*)realloc(g_marker_windows.array, g_marker_windows.n_allocated * sizeof(MarkerWindow));
            if (!new_array)
                exit_error("out of memory: could not reallocate marker window array");
            g_marker_windows.array = new_array;
        }
        assert(g_marker_windows.n_used < g_marker_windows.n_allocated);
        g_marker_windows.array[g_marker_windows.n_used].x = x;
        g_marker_windows.array[g_marker_windows.n_used].y = y;
        g_marker_windows.array[g_marker_windows.n_used].w = w;
        g_marker_windows.array[g_marker_windows.n_used].h = h;
        return g_marker_windows.n_used++;
    }
        
    bool transparent(uint8_t *data, int x, int y, int stride)
    {
        return data[stride * y + 4 * x + 3] < 255;
    }

    // XXX @Leak g_marker_windows is never freed currently
    void load_overlay_image_and_determine_marker_lines()
    {
        char *filename = g_overlay_image_filename;
        if (!filename)
            return;

        int image_width;
        int image_height;
        int image_n_components;
        unsigned char *data = stbi_load(filename, &image_width, &image_height, &image_n_components, 0);
        if (!data)
            exit_error("could not load image from file '%s'\n", filename);
        if (image_n_components != 4)
            exit_error("unexpected number of components in image '%s' (is %d; expected 4)\n", filename, image_n_components);

        int center_x = image_width / 2;
        int stride = image_width * 4;

        struct RowInfo {
            int transparent_start;
            int transparent_end;
        };

        RowInfo *rows = (RowInfo*)malloc(image_height * sizeof(RowInfo));
        for (int y = 0; y < image_height; ++y) {
            RowInfo *row = rows + y;
            row->transparent_start = 0;
            row->transparent_end = 0;
            bool found_transparent = false;
            // first look for transparent areas starting from the center and walking left
            for (int x = center_x; x >= 0; --x) {
                bool is_transparent = transparent(data, x, y, stride);
                if (!found_transparent && is_transparent) {
                    row->transparent_start = x;
                    row->transparent_end = x + 1;
                    found_transparent = true;
                }
                else if (is_transparent) {
                    row->transparent_start = x;
                }
                else
                    break;
            }
            if (!found_transparent) {
                // try to find a transparent area to the right of the center
                for (int x = center_x; x < image_width; ++x) {
                    bool is_transparent = transparent(data, x, y, stride);
                    if (is_transparent) {
                        row->transparent_start = x;
                        row->transparent_end = x + 1;
                        found_transparent = true;
                        break;
                    }
                }
            }
            if (!found_transparent)
                continue;
            for (int x = row->transparent_end; x < image_width; ++x) {
                if (!transparent(data, x, y, stride))
                    break;
                row->transparent_end++;
            }
        }
        stbi_image_free(data);

        g_marker_windows.n_allocated = 1;
        g_marker_windows.n_used = 0;
        g_marker_windows.array = (MarkerWindow*)malloc(g_marker_windows.n_allocated * sizeof(MarkerWindow));

        int prev_left_index = -1;
        int prev_right_index = -1;
        for (int y = 0; y < image_height; ++y) {
            RowInfo *row = rows + y;
            // XXX @Incomplete handle non-overlapping transparent ranges in adjacent rows
            if (row->transparent_start >= row->transparent_end) {
                // no transparent pixels in this row
                // if this is the first fully opaque row after a transparent one, draw a horizontal marker
                if (y > 0 && rows[y - 1].transparent_start < rows[y - 1].transparent_end)
                    add_marker_rectangle(rows[y - 1].transparent_start, y, rows[y - 1].transparent_end - rows[y - 1].transparent_start, 1);
                prev_left_index = -1;
                prev_right_index = -1;
                continue;
            }
            // we have at least one transparent pixel in this row
            if (prev_left_index < 0 && y > 0) {
                // the row before was fully opaque, so draw a horizontal marker in it
                add_marker_rectangle(row->transparent_start, y - 1, row->transparent_end - row->transparent_start, 1);
            }
            for (int i = 0; i < 2; ++i) {
                int marker_x;
                int prev_index;
                if (i == 0 && row->transparent_start > 0) {
                    marker_x = row->transparent_start - 1;
                    prev_index = prev_left_index;
                }
                else if (i == 1 && row->transparent_end > row->transparent_start + 1) {
                    marker_x = row->transparent_end;
                    prev_index = prev_right_index;
                }
                else
                    continue;

                if (prev_index >= 0) {
                    int old_x = g_marker_windows.array[prev_index].x;
                    if (marker_x == old_x) {
                        g_marker_windows.array[prev_index].h++;
                        continue;
                    }
                    int link_x = min(marker_x, old_x);
                    int link_w = max(marker_x, old_x) - link_x + 1;
                    bool shrinking = (i == 0 && marker_x > old_x) || (i == 1 && marker_x < old_x);
                    assert(y > 0);
                    add_marker_rectangle(link_x, shrinking ? y : (y - 1), link_w, 1);
                }
                int index = add_marker_rectangle(marker_x, y, 1, 1);
                if (i == 0)
                    prev_left_index = index;
                else
                    prev_right_index = index;
            }
        }
        free(rows);
    }

    void create_marker_windows(HINSTANCE hInstance, ATOM window_class)
    {
        for (int index = 0; index < g_marker_windows.n_used; ++index)
        {
            MarkerWindow *marker = g_marker_windows.array + index;

            HWND window = ::CreateWindowEx(
                    WS_EX_TOPMOST, // dwExStyle
                    reinterpret_cast<LPCTSTR>(window_class), // lpClassName
                    TEXT("Overhead Marker"), // lpWindowName
                    WS_POPUP | WS_VISIBLE, // dwStyle
                    marker->x, marker->y, marker->w, marker->h, // X, Y, nWidth, nHeight
                    g_main_window, // hWndParent
                    0, // hMenu
                    hInstance, // hInstance
                    NULL); // lpParam
            if (!window)
                exit_windows_system_error("could not create marker window");
            marker->window = window;
        }
    }
        
    void create_main_window(HINSTANCE hInstance, ATOM window_class)
    {
        HWND window = ::CreateWindowEx(
                WS_EX_TOPMOST /* | WS_EX_LAYERED see :LayeredWindow */, // dwExStyle
                reinterpret_cast<LPCTSTR>(window_class), // lpClassName
                TEXT("Overhead Display"), // lpWindowName
                WS_POPUP | WS_VISIBLE, // dwStyle
                g_position_x, g_position_y, g_background_image_width, g_background_image_height, // X, Y, nWidth, nHeight
                0, // hWndParent
                0, // hMenu
                hInstance, // hInstance
                NULL); // lpParam
        if (!window)
            exit_windows_system_error("could not create main window");
        g_main_window = window;

        // XXX If only OBS would work with the compositing window manager, we could do so much nice
        //     stuff with a layered window. :LayeredWindow
#if 0
        if (!::SetLayeredWindowAttributes(
                    window, // hwnd,
                    RGB(0, 0, 0), // crKey
                    128, // bAlpha
                    LWA_ALPHA /* | LWA_COLORKEY */)) // dwFlags
            exit_windows_system_error("could not set layered window attributes");
#endif
    }

    bool calculate_time_until_expiry(SYSTEMTIME *remaining)
    {
        bool still_running = true;
        SYSTEMTIME current_time;
        ::GetLocalTime(&current_time);
        int64_t delta_ms =
            (((      (int64_t)g_expiry_time.wHour         - (int64_t)current_time.wHour)
            *   60 + (int64_t)g_expiry_time.wMinute       - (int64_t)current_time.wMinute)
            *   60 + (int64_t)g_expiry_time.wSecond       - (int64_t)current_time.wSecond)
            * 1000 + (int64_t)g_expiry_time.wMilliseconds - (int64_t)current_time.wMilliseconds;
        if (delta_ms < 0) {
            delta_ms = 0;
            still_running = false;
        }
        memset(remaining, 0, sizeof(*remaining));
        remaining->wHour   = (WORD)(delta_ms  / (60 * 60 * 1000));
        delta_ms -= remaining->wHour          * (60 * 60 * 1000);
        remaining->wMinute = (WORD)(delta_ms  / (     60 * 1000));
        delta_ms -= remaining->wMinute        * (     60 * 1000);
        remaining->wSecond = (WORD)(delta_ms  / (          1000));
        delta_ms -= remaining->wSecond        * (          1000);
        remaining->wMilliseconds = (WORD)delta_ms;
        return still_running;
    }

    /**
     * \note There are no sane conventions for parsing the command line on Windows.
     *       We try to do something simple here that allows the user to specify
     *       paths containing whitespace by using quotes in a way that is also
     *       understood by DOS commands like 'dir'. For example, the following are
     *       valid ways to specify the file 'back ground.png' in 'example directory':
     *
     *           overhead --background="example directory\back ground.png"
     *           overhead --background="example directory"\"back ground.png"
     *           overhead "--background=example directory\back ground.png"
     *
     *       and even the very ugly
     *
     *           overhead --background="example directory\"foo.png
     *
     *       (This last one is the reason why we cannot support backslash-escaped
     *       quotes within quoted string arguments.)
     *       Note also that quotes themselves are not allowed within
     *       Windows filenames.
     */
    char *consume_and_dup_command_line_argument(char **cmdline)
    {
        // XXX @Incomplete extend this function for UNICODE

        // skip leading whitespace
        while (**cmdline && isspace(**cmdline))
            (*cmdline)++;

        if (!**cmdline)
            return nullptr;

        size_t maxsize = strlen(*cmdline) + 1;
        char *arg = (char *)malloc(maxsize);
        if (!arg)
            exit_error("out of memory: could not allocate space for command line argument\n");

        char *arg_ptr = arg;
        bool in_quotes = false;
        while (true) {
            char ch = *(*cmdline)++;
            switch (ch) {
                case '"':
                    in_quotes = !in_quotes;
                    break;
                default:
                    if (ch && isspace(ch)) {
                        if (in_quotes)
                            *arg_ptr++ = ch;
                        else
                            goto end_of_arg;
                    }
                    else {
                        if (!ch) {
                            (*cmdline)--;
                            goto end_of_arg;
                        }
                        *arg_ptr++ = ch;
                    }
                    break;
            }
        }
    end_of_arg:
        assert(arg_ptr < arg + maxsize);
        *arg_ptr = 0;
        return arg;
    }

    // XXX @Leak g_background_image_filename, g_overlay_image_filename are never freed
    void parse_command_line(LPSTR cmdline)
    {
        // XXX @Incomplete extend this function for UNICODE
        // XXX @Incomplete filenames containing spaces are not supported
        uint32_t index = 0;
        char *arg;
        while ((arg = consume_and_dup_command_line_argument(&cmdline))) {
            char *end = arg + strlen(arg);
            if (strncmp(arg, "--background=", 13) == 0) {
                g_background_image_filename = _strdup(arg + 13);
            }
            else if (strncmp(arg, "--overlay=", 10) == 0) {
                g_overlay_image_filename = _strdup(arg + 10);
            }
            else if (strncmp(arg, "--countdown=", 12) == 0) {
                char *parseend = nullptr;
                long value = strtol(arg + 12, &parseend, 10);
                if (parseend != end)
                    exit_error("countdown time did not parse as an integer: %s\n", arg);
                if (value < 0 || value >= 1440)
                    exit_error("countdown time is out of range ([0; 1440) minutes expected)\n");
                g_countdown_minutes = (int)value;
            }
            else {
                // handle positional arguments
                switch (index) {
                    case 0:
                    case 1:
                    case 2:
                    case 3:
                        {
                            char *parseend = nullptr;
                            long value = strtol(arg, &parseend, 10);
                            if (parseend != end)
                                exit_error("command-line argument did not parse as an integer: %s\n", arg);
                            if (value < INT_MIN || value > INT_MAX)
                                exit_error("command-line argument is out of range: %s\n", arg);
                            switch (index) {
                                case 0: g_position_x = (int)value; break;
                                case 1: g_position_y = (int)value; break;
                                case 2: g_background_image_width = (int)value; break;
                                case 3: g_background_image_height = (int)value; break;
                            }
                        }
                        break;

                    default:
                        exit_usage("unexpected positional command-line argument: %s\n", arg);
                }
                index++;
            }
            free(arg);
        }
    }

    // XXX @Leak the font is never deleted currently
    void create_font()
    {
        g_font = ::CreateFont(
                30, // cHeight
                0, // cWidth (0 = choose closest match)
                0, // cEscapement  (angle in tenth of a degree from the x-axis)
                0, // cOrientation (angle in tenth of a degree from the x-axis)
                FW_HEAVY, // cWeight
                0, // bItalic
                0, // bUnderline
                0, // bStrikeout
                ANSI_CHARSET, // iCharset
                OUT_DEFAULT_PRECIS, // iOutPrecision
                CLIP_DEFAULT_PRECIS, // iClipPrecision
                ANTIALIASED_QUALITY, // iQuality
                FIXED_PITCH | FF_MODERN, // iPitchAndFamily
                TEXT("Courier New")); // pszFaceName
        if (!g_font)
            exit_windows_system_error("could not create logical font");
    }

    void paint_countdown_window(HWND hWnd)
    {
        if (!g_countdown_minutes)
            return;
        PAINTSTRUCT paint;
        HDC dc = ::BeginPaint(hWnd, &paint);
        if (!dc)
            exit_windows_system_error("BeginPaint failed");
        SYSTEMTIME remaining;
        calculate_time_until_expiry(&remaining);
        HDC memory_dc;
        memory_dc = ::CreateCompatibleDC(dc);
        if (!memory_dc)
            exit_windows_system_error("could not create compatible memory device context");
        HBITMAP bitmap = ::CreateCompatibleBitmap(dc, g_background_image_width, g_background_image_height);
        if (!bitmap)
            exit_windows_system_error("could not create compatible bitmap");
        if (g_background_image_data) {
            int result = ::SetDIBits(memory_dc, bitmap,
                    0, // start
                    g_background_image_height, // cLines
                    g_background_image_data, // lpBits
                    &g_background_image_info, // lpbmi
                    DIB_RGB_COLORS); // ColorUse
            if (result != g_background_image_height)
                exit_windows_system_error("could not copy background image data");
        }
        HBITMAP old_bitmap = (HBITMAP)::SelectObject(memory_dc, bitmap);
        if (!old_bitmap)
            exit_windows_system_error("could not select bitmap into memory device context");
        (void)::DeleteObject(old_bitmap);
        HFONT old_font;
        COLORREF old_color;
        COLORREF old_bk_color;
        int old_bk_mode;
        if (g_font
                && (old_font = (HFONT)::SelectObject(memory_dc, g_font))
                && ((old_color = ::SetTextColor(memory_dc, RGB(255, 255, 255))) != CLR_INVALID)
                && ((old_bk_color = ::SetBkColor(memory_dc, RGB(0, 0, 0))) != CLR_INVALID)
                && ((old_bk_mode = ::SetBkMode(memory_dc, TRANSPARENT)) != 0)
           )
        {
            char format_buf[20];
            int result;
            if (g_countdown_minutes >= 60)
                result = snprintf(format_buf, sizeof(format_buf), "%2u:%02u:%02u", remaining.wHour, remaining.wMinute, remaining.wSecond);
            else
                result = snprintf(format_buf, sizeof(format_buf), "%02u:%02u", remaining.wMinute, remaining.wSecond);
            if (result < 0)
                exit_clib_error("snprintf failed");
            if (result >= sizeof(format_buf)) {
                result = sizeof(format_buf) - 1;
                format_buf[result] = 0;
            }
            if (!::TextOut(memory_dc, 5, -3, format_buf, result))
                exit_windows_system_error("TextOut failed");
        }
        if (!::BitBlt(dc, 0, 0, g_background_image_width, g_background_image_height,
                    memory_dc, 0, 0, SRCCOPY))
            exit_windows_system_error("bit block transfer failed");
        (void)::DeleteDC(memory_dc);
        if (!::DeleteObject(bitmap))
            exit_windows_system_error("could not delete compatible bitmap");
        (void)::EndPaint(hWnd, &paint);
    }

    void paint_marker_window(HWND hWnd)
    {
        PAINTSTRUCT paint;
        HDC dc = ::BeginPaint(hWnd, &paint);
        if (!dc)
            exit_windows_system_error("BeginPaint failed");
        HBRUSH brush = ::CreateSolidBrush(RGB(255, 128, 128));
        if (!brush)
            exit_windows_system_error("could not create brush for marker window");
        RECT rect;
        if (!::GetClientRect(hWnd, &rect))
            exit_windows_system_error("could not get client rectangle of marker window");
        if (!::FillRect(dc, &rect, brush))
            exit_windows_system_error("could not fill marker window");
        (void)::DeleteObject(brush);
        (void)::EndPaint(hWnd, &paint);
    }

    ATOM register_window_class(HINSTANCE hInstance, WNDPROC wndproc)
    {
        WNDCLASS wc = {0}; 
        wc.lpfnWndProc = wndproc;
        wc.hInstance = hInstance;
        wc.lpszClassName = TEXT("overhead_app");
        // XXX @Incomplete set wc.hIcon
        ATOM window_class = ::RegisterClass(&wc);
        if (!window_class)
            exit_windows_system_error("could not register window class");
        return window_class;
    }

    void set_expiry_time()
    {
        ::GetLocalTime(&g_expiry_time);
        g_expiry_time.wMinute += g_countdown_minutes;
        g_expiry_time.wHour += g_expiry_time.wMinute / 60;
        g_expiry_time.wMinute %= 60;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch(message)
    {
        case WM_CLOSE:
            ::PostQuitMessage(0);
            break;
        case WM_NCHITTEST:
            // make our windows transparent to clicks
            return HTTRANSPARENT;
        case WM_PAINT:
            if (hWnd == g_main_window)
                paint_countdown_window(hWnd);
            else
                paint_marker_window(hWnd);
            break;
        case WM_TIMER:
            {
                SYSTEMTIME remaining;
                if (calculate_time_until_expiry(&remaining)) {
                    // set the next timer expiry right after the second flips
                    UINT_PTR timer = ::SetTimer(hWnd, wParam, max(USER_TIMER_MINIMUM, remaining.wMilliseconds + 1), NULL);
                    if (!timer)
                        exit_windows_system_error("could not re-set update timer");
                }
                if (!::InvalidateRect(hWnd, NULL, FALSE))
                    exit_windows_system_error("InvalidateRect failed");
            }
            break;
        default:
            return ::DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}  

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    parse_command_line(lpCmdLine);
    set_expiry_time();
    load_background_image();
    load_overlay_image_and_determine_marker_lines();

    ATOM window_class = register_window_class(hInstance, WndProc);
    create_main_window(hInstance, window_class);
    create_marker_windows(hInstance, window_class);
    create_font();

    if (g_countdown_minutes) {
        // start the update timer for the countdown window
        UINT_PTR timer = ::SetTimer(g_main_window, 0, USER_TIMER_MINIMUM, NULL);
        if (!timer)
            exit_windows_system_error("could not set update timer");
    }

    MSG msg = {0};
    while (::GetMessage(&msg, NULL, 0, 0))
        (void)::DispatchMessage(&msg);

    return 0;
}
