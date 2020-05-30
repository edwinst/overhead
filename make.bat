@echo off
call D:\Programs\VS_IDE\Common7\Tools\VsDevCmd.bat -arch=amd64 -host_arch=amd64

set DIR=%~dp0
echo Entering directory %DIR%
cd %DIR%
call build.bat
