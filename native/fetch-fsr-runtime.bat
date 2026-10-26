@echo off
rem Downloads the AMD FidelityFX runtime the Frame Generation uses into native\
rem (FSR SDK v2.3.0, signed binaries) and checks their SHA-256.
cd /d "%~dp0"
setlocal
set "BASE=https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/raw/v2.3.0/Kits/FidelityFX/signedbin"
call :get amd_fidelityfx_loader_dx12.dll e2d85aa05a9bd9ed8b38935fdf5199372cca6f74c12015143bb6f945ee1608aa || exit /b 1
call :get amd_fidelityfx_framegeneration_dx12.dll 02297beedd285e822d3a64f314cf00faf378dcec0edc47ff0c4dd71b3a8c2f18 || exit /b 1
echo FSR runtime ready.
exit /b 0
:get
powershell -NoProfile -Command "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest '%BASE%/%1' -OutFile '%1.part'; if ((Get-FileHash '%1.part' -Algorithm SHA256).Hash.ToLower() -ne '%2') { Remove-Item '%1.part'; exit 1 }"
if errorlevel 1 ( echo Download or hash check failed for %1 & exit /b 1 )
move /y "%1.part" "%1" >nul
exit /b 0
