erase %1.bak 2>nul
@REM Rename to redirect active handles
rename %1 %2.bak 2>nul
@REM Copy back to pacify the linker/dependency checker
copy %1.bak %1 2>nul
echo.