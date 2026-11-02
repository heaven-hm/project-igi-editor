@echo off
setlocal EnableExtensions
set "SCRIPT_DIR=%~dp0"

rem Easy smart-live syntax:
rem e2e_live_test --level 5 --category rigid --maximum 3

set "LEVEL=1"
set "ALL_LEVELS="
set "OBJECTS=All"
set "OBJECT_TYPE="
set "MODEL_ID="
set "EDITOR_EXE="
set "ARTIFACTS="
set "MAX=3"
set "VIEWS=10"
set "FLAGS="

:parse
if "%~1"=="" goto normalize
if /I "%~1"=="--level" set "LEVEL=%~2"&shift&shift&goto parse
if /I "%~1"=="--levels" set "LEVEL=%~2"&shift&shift&goto parse
if /I "%~1"=="-level" set "LEVEL=%~2"&shift&shift&goto parse
if /I "%~1"=="-levels" set "LEVEL=%~2"&shift&shift&goto parse
if /I "%~1"=="--all-levels" set "ALL_LEVELS=1"&shift&goto parse
if /I "%~1"=="-all-levels" set "ALL_LEVELS=1"&shift&goto parse
if /I "%~1"=="--category" set "OBJECTS=%~2"&shift&shift&goto parse
if /I "%~1"=="-category" set "OBJECTS=%~2"&shift&shift&goto parse
if /I "%~1"=="--categories" set "OBJECTS=%~2"&shift&shift&goto parse
if /I "%~1"=="-categories" set "OBJECTS=%~2"&shift&shift&goto parse
if /I "%~1"=="--objects" set "OBJECTS=%~2"&shift&shift&goto parse
if /I "%~1"=="-objects" set "OBJECTS=%~2"&shift&shift&goto parse
if /I "%~1"=="--ai" set "OBJECTS=AI"&shift&goto parse
if /I "%~1"=="-ai" set "OBJECTS=AI"&shift&goto parse
if /I "%~1"=="--rigid" set "OBJECTS=RigidObjects"&shift&goto parse
if /I "%~1"=="-rigid" set "OBJECTS=RigidObjects"&shift&goto parse
if /I "%~1"=="--rigidobjects" set "OBJECTS=RigidObjects"&shift&goto parse
if /I "%~1"=="--building" set "OBJECTS=Buildings"&shift&goto parse
if /I "%~1"=="--buildings" set "OBJECTS=Buildings"&shift&goto parse
if /I "%~1"=="-building" set "OBJECTS=Buildings"&shift&goto parse
if /I "%~1"=="-buildings" set "OBJECTS=Buildings"&shift&goto parse
if /I "%~1"=="--vehicle" set "OBJECTS=Vehicles"&shift&goto parse
if /I "%~1"=="--vehicles" set "OBJECTS=Vehicles"&shift&goto parse
if /I "%~1"=="-vehicle" set "OBJECTS=Vehicles"&shift&goto parse
if /I "%~1"=="-vehicles" set "OBJECTS=Vehicles"&shift&goto parse
if /I "%~1"=="--object-type" set "OBJECT_TYPE=%~2"&shift&shift&goto parse
if /I "%~1"=="--model" set "MODEL_ID=%~2"&shift&shift&goto parse
if /I "%~1"=="--models" set "MODEL_ID=%~2"&shift&shift&goto parse
if /I "%~1"=="-model" set "MODEL_ID=%~2"&shift&shift&goto parse
if /I "%~1"=="-models" set "MODEL_ID=%~2"&shift&shift&goto parse
if /I "%~1"=="--editor-exe" set "EDITOR_EXE=%~2"&shift&shift&goto parse
if /I "%~1"=="--artifacts" set "ARTIFACTS=%~2"&shift&shift&goto parse
if /I "%~1"=="--maximum" set "MAX=%~2"&shift&shift&goto parse
if /I "%~1"=="--max-objects" set "MAX=%~2"&shift&shift&goto parse
if /I "%~1"=="-max" set "MAX=%~2"&shift&shift&goto parse
if /I "%~1"=="--views" set "VIEWS=%~2"&shift&shift&goto parse
if /I "%~1"=="--all-objects" set "MAX=0"&shift&goto parse
if /I "%~1"=="--distinct-types" set "FLAGS=%FLAGS% -DistinctTypes"&shift&goto parse
if /I "%~1"=="-distinct-types" set "FLAGS=%FLAGS% -DistinctTypes"&shift&goto parse
if /I "%~1"=="--distinct-categories" set "FLAGS=%FLAGS% -DistinctCategories"&shift&goto parse
if /I "%~1"=="-distinct-categories" set "FLAGS=%FLAGS% -DistinctCategories"&shift&goto parse
if /I "%~1"=="--video" set "FLAGS=%FLAGS% -Video"&shift&goto parse
if /I "%~1"=="-video" set "FLAGS=%FLAGS% -Video"&shift&goto parse
if /I "%~1"=="--video-seconds" set "VIDEO_SECONDS=%~2"&shift&shift&goto parse
if /I "%~1"=="-video-seconds" set "VIDEO_SECONDS=%~2"&shift&shift&goto parse
if /I "%~1"=="--video-fps" set "VIDEO_FPS=%~2"&shift&shift&goto parse
if /I "%~1"=="-video-fps" set "VIDEO_FPS=%~2"&shift&shift&goto parse
if /I "%~1"=="--prepare-only" set "FLAGS=%FLAGS% -PrepareOnly"&shift&goto parse
if /I "%~1"=="--legacy-serial" set "FLAGS=%FLAGS% -LegacySerial"&shift&goto parse
echo Unknown option: %~1
echo.
echo Usage examples:
echo   e2e_live_test --level 5 --category ai --maximum 3
echo   e2e_live_test --level 5 --category buildings --maximum 3
echo   e2e_live_test --level 5 --category rigid --maximum 3
echo   e2e_live_test --level 5 --category vehicles --maximum 3
echo   e2e_live_test --level 1 --model 435_01_1 --maximum 1
echo   e2e_live_test --level 5 --distinct-types --maximum 3
echo   e2e_live_test --level 5 --distinct-categories --maximum 4 --video
echo   e2e_live_test --level 5 --video --video-seconds 5 --maximum 3
echo   e2e_live_test --all-levels --category ai --maximum 1
exit /b 2

:normalize
if /I "%LEVEL%"=="1-14" set "ALL_LEVELS=1"
if /I "%LEVEL%"=="1..14" set "ALL_LEVELS=1"
if /I "%OBJECTS%"=="rigid" set "OBJECTS=RigidObjects"
if /I "%OBJECTS%"=="rigidobjects" set "OBJECTS=RigidObjects"
if /I "%OBJECTS%"=="building" set "OBJECTS=Buildings"
if /I "%OBJECTS%"=="buildings" set "OBJECTS=Buildings"
if /I "%OBJECTS%"=="vehicle" set "OBJECTS=Vehicles"
if /I "%OBJECTS%"=="vehicles" set "OBJECTS=Vehicles"
if /I "%OBJECTS%"=="ai" set "OBJECTS=AI"
if /I "%OBJECTS%"=="any" set "OBJECTS=All"
if /I "%OBJECTS%"=="all" set "OBJECTS=All"

set "ARGS=-Category %OBJECTS% -MaxObjects %MAX% -ViewCount %VIEWS%"
if defined ALL_LEVELS set "ARGS=%ARGS% -AllLevels"
if not defined ALL_LEVELS set "ARGS=%ARGS% -Level %LEVEL%"
if defined OBJECT_TYPE set "ARGS=%ARGS% -ObjectTypes %OBJECT_TYPE%"
if defined MODEL_ID set "ARGS=%ARGS% -ModelIds %MODEL_ID%"
if defined EDITOR_EXE set "ARGS=%ARGS% -EditorExePath "%EDITOR_EXE%""
if defined ARTIFACTS set "ARGS=%ARGS% -ArtifactsRoot "%ARTIFACTS%""
if defined VIDEO_SECONDS call :add_video_flag
if defined VIDEO_SECONDS set "ARGS=%ARGS% -VideoSeconds %VIDEO_SECONDS%"
if defined VIDEO_FPS set "ARGS=%ARGS% -VideoFps %VIDEO_FPS%"
goto :after_video_flag
:add_video_flag
echo %FLAGS% | findstr /I /C:"-Video" >nul || set "FLAGS=%FLAGS% -Video"
goto :eof
:after_video_flag

echo Smart live test: level=%LEVEL% objects=%OBJECTS% max=%MAX% views=%VIEWS%
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%Run-SmartTest.ps1" %ARGS% %FLAGS%
exit /b %ERRORLEVEL%
