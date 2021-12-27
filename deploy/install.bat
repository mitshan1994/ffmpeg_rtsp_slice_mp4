%~d0
cd %~dp0

set service_name=ffmpeg_slice_segment

nssm install %service_name% ffmpeg_slice_segment.exe
nssm set %service_name% AppDirectory %~dp0
REM nssm set %service_name% AppStopMethodConsole 1500
REM nssm set %service_name% AppStopMethodWindow 1500
REM nssm set %service_name% AppStopMethodThreads 1500

pause
