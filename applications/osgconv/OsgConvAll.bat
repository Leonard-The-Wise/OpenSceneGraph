@echo off

SETLOCAL ENABLEDELAYEDEXPANSION

REM Capture all commandline parameters to a string
SET "additionalParams=%*"

REM Loop through every directory on current folder
FOR /D %%D in (*) DO (
    REM Enters directory
    cd "%%D"
    
    REM Execute conversion
	echo Convertendo %%~nxD
    osgconv file.osgjs %%~nxD.gltf !additionalParams!
	
	echo Creating output directory...
	mkdir %%~nxD-output
	move %%~nxD.gltf %%~nxD-output
	cd %%~nxD-output
	mkdir textures
	copy ..\textures\*.* textures /Y
	cd..
    
    REM Return to main folder
    cd ..
)

echo Conversions complete!
pause