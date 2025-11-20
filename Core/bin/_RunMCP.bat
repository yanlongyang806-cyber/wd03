@echo off
BatUtil CompareTimestamps ..\..\Core\bin\MasterControlProgramFD.exe ..\..\Core\bin\MasterControlProgram.exe
if ERRORLEVEL 2 (
	echo Error launching BatUtil.  Most likely cause is you do not have C:\Cryptic\tools\bin in your PATH.
	pause
) else if ERRORLEVEL 1 (
	start ..\..\Core\bin\MasterControlProgramFD.exe %*
) else (
	start ..\..\Core\bin\MasterControlProgram.exe %*
)