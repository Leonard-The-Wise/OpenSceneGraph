@echo off

SETLOCAL ENABLEDELAYEDEXPANSION

REM Captura todos os parâmetros da linha de comando como uma string
SET "additionalParams=%*"

REM Loop através de cada diretório na pasta principal
FOR /D %%D in (*) DO (
    REM Entra no diretório
    cd "%%D"
    
    REM Executa o comando usando o nome do diretório e os parâmetros adicionais da linha de comando
	echo Convertendo %%~nxD
    osgconv file.osgjs %%~nxD.fbx !additionalParams!
    
    REM Retorna para a pasta principal
    cd ..
)

echo Processo concluído.
pause