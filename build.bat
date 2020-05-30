@echo off

rem Compiler options used:
rem     /nologo ... do not display Microsoft startup banner
rem     /Dxxx ... define preprocessor macros
rem     /EHa-s-c- ... turn off all exception handling
rem     /MT ... link to static run-time library
rem     /O1 ... optimize for small code size
rem     /W3 ... "production quality" warnings
rem     /Zi ... put debug info in .pdb (not in .obj file)
rem     /link ... pass following options to the linker
rem Linker options used:
rem     /DEBUG:FULL ... move all debug info into the .pdb
rem     /DEBUG:NONE ... don't generate debug info
rem     /OUT:filename ... specifies the output file to create
rem     /INCREMENTAL:NO ... do not link incrementally
rem     /SUBSYSTEM:WINDOWS ... mark this as a Windows GUI application

set CXX_FLAGS=/nologo /DWIN32 /D_WINDOWS /W3 /EHa-s-c- /MT
set LINK_LIBRARIES=kernel32.lib user32.lib gdi32.lib

cl %CXX_FLAGS% /Zi overhead.cpp %LINK_LIBRARIES% /link /DEBUG:FULL /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /OUT:overhead_debug.exe

cl %CXX_FLAGS% /O1 overhead.cpp %LINK_LIBRARIES% /link /DEBUG:NONE /INCREMENTAL:NO /SUBSYSTEM:WINDOWS /OUT:overhead.exe
