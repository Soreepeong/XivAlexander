@echo off
if exist Build\Certificate.pfx goto :EOF
set /p CN="Enter CN: "
set /p CERT_PASSWORD="Enter PFX Password: "
echo %CERT_PASSWORD%> Build\CertificatePassword.txt
set MAKECERT="C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\makecert.exe"
set PVK2PFX="C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\pvk2pfx.exe"
%MAKECERT% -r -pe -n "CN=%CN%" -a sha512 -len 4096 -cy authority -sv Build\CARoot.pvk Build\CARoot.cer
%MAKECERT% -pe -n "CN=%CN%" -iv Build\CARoot.pvk -ic Build\CARoot.cer -a sha512 -len 4096 -cy end -sky signature -sv Build\Certificate.pvk Build\Certificate.cer
%PVK2PFX% -pvk Build\CARoot.pvk -spc Build\CARoot.cer -pfx Build\CARoot.pfx -po "%CERT_PASSWORD%"
%PVK2PFX% -pvk Build\Certificate.pvk -spc Build\Certificate.cer -pfx Build\Certificate.pfx -po "%CERT_PASSWORD%"