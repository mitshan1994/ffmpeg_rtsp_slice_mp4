%~d0
cd %~dp0

set service_name=ffmpeg_slice_segment

nssm remove %service_name%
