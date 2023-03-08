@echo off
if exist Build\Certificate.pfx goto :EOF
set /p CN="Enter CN: "
set /p CERT_PASSWORD="Enter PFX Password: "
echo %CERT_PASSWORD%> Build\CertificatePassword.txt
set MAKECERT="makecert.exe"
set PVK2PFX="pvk2pfx.exe"
%MAKECERT% -eku 1.3.6.1.5.5.7.3.3 -r -pe -n "CN=%CN%" -a sha512 -len 4096 -cy authority -sv Build\CARoot.pvk Build\CARoot.cer
%MAKECERT% -eku 1.3.6.1.5.5.7.3.3 -pe -n "CN=%CN%" -iv Build\CARoot.pvk -ic Build\CARoot.cer -a sha512 -len 4096 -cy end -sky signature -sv Build\Certificate.pvk Build\Certificate.cer
%PVK2PFX% -pvk Build\CARoot.pvk -spc Build\CARoot.cer -pfx Build\CARoot.pfx -po "%CERT_PASSWORD%"
%PVK2PFX% -pvk Build\Certificate.pvk -spc Build\Certificate.cer -pfx Build\Certificate.pfx -po "%CERT_PASSWORD%"