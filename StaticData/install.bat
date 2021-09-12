@echo off

pushd "%~dp0"
mkdir "%LOCALAPPDATA%\XivAlexander"
copy XivAlexanderLoader32.exe "%LOCALAPPDATA%\XivAlexander\"
copy XivAlexanderLoader64.exe "%LOCALAPPDATA%\XivAlexander\"
copy XivAlexander32.dll "%LOCALAPPDATA%\XivAlexander\"
copy XivAlexander64.dll "%LOCALAPPDATA%\XivAlexander\"

mkdir "%APPDATA%\XivAlexander"
mkdir "%APPDATA%\XivAlexander\FontConfig"
mkdir "%APPDATA%\XivAlexander\ReplacementFileEntries"
mkdir "%APPDATA%\XivAlexander\TexToolsMods"
copy game.*.json "%APPDATA%\XivAlexander\"
copy config.runtime.json "%APPDATA%\XivAlexander\"
copy FontConfig\*.json "%APPDATA%\XivAlexander\FontConfig\"
echo.
echo All required files copied.
echo.
echo *****************************************************************************************
echo *****************************************************************************************
echo ****                                                                                 ****
echo **** FINAL STEPS REMAINING:                                                          ****
echo ****                                                                                 ****
echo **** * Open game installation directory with ffxiv.exe and ffxiv_dx11.exe.           ****
echo **** * Copy XivAlexander32.dll as d3d9.dll into there.                               ****
echo **** * Copy XivAlexander64.dll as d3d11.dll or dxgi.dll into there. Pick ONLY ONE.   ****
echo **** * Alternatively, if you use only one of either DirectX 9 or DirectX 11 version, ****
echo ****   then you can copy corresponding file (32 or 64) as dinput8.dll into there.    ****
echo ****                                                                                 ****
echo **** Installation is not done until you do these!                                    ****
echo ****                                                                                 ****
echo *****************************************************************************************
echo *****************************************************************************************
echo.
echo.
popd
pause