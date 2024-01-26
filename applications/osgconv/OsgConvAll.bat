@echo off

SETLOCAL ENABLEDELAYEDEXPANSION

REM Capture all commandline parameters to a string
SET "additionalParams=%*"

REM Loop through every directory on current folder
FOR /D %%D in (*) DO (
    REM Enters directory
    cd "%%D"
    
    REM Execute conversion
	echo Converting... %%~nxD
    osgconv file.osgjs %%~nxD.gltf !additionalParams!
	
    REM Return to main folder
    cd ..
	
	echo.
)

echo Conversions complete!
pause