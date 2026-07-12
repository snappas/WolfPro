@echo off
rem ***************************************************************************
rem * This script will fetch:
rem * openssl, strawberry-perl (for openssl build)
rem * curl, and libjpeg-turbo
rem * and build openssl and curl with openssl support
rem ***************************************************************************

:begin
  if not "%~1" == "" (
    set vc_version=%~1
    goto calcProgramFiles
  )
  echo Compiler:
  echo.
  echo vc14.2    - Use Visual Studio 2019
  echo vc14.3    - Use Visual Studio 2022
  echo.
  echo Enter Compiler:
  set /p vc_version=
:calcProgramFiles
  rem Calculate the program files directory
  if defined PROGRAMFILES (
    set "PF=%PROGRAMFILES%"
    set OS_PLATFORM=x86
  )
  if defined PROGRAMFILES(x86) (
    rem Visual Studio was x86-only prior to 14.3
    if /i "%vc_version%" == "vc14.3" (
      set "PF=%PROGRAMFILES%"
    ) else (
      set "PF=%PROGRAMFILES(x86)%"
    )
    set OS_PLATFORM=x64
  )

:parseArgs
  if not "%vc_version%" == "" (
	if /i "%vc_version%" == "vc14.2" (
      set VC_VER=14.2
      set VC_DESC=VC14.20
	  set VC_generate=vc14.20
	  set "cmake_makefiles=Visual Studio 16 2019"
      rem Determine the VC14.2 path based on the installed edition in descending
      rem order (Enterprise, then Professional and finally Community)
      if exist "%PF%\Microsoft Visual Studio\2019\Enterprise" (
        set "VC_PATH=Microsoft Visual Studio\2019\Enterprise"
      ) else if exist "%PF%\Microsoft Visual Studio\2019\Professional" (
        set "VC_PATH=Microsoft Visual Studio\2019\Professional"
      ) else (
        set "VC_PATH=Microsoft Visual Studio\2019\Community"
      )
    ) else if /i "%vc_version%" == "vc14.3" (
      set VC_VER=14.3
      set VC_DESC=VC14.30
	  set VC_generate=vc14.30
	  set "cmake_makefiles=Visual Studio 17 2022"
      rem Determine the VC14.3 path based on the installed edition in descending
      rem order (Enterprise, then Professional and finally Community)
      if exist "%PF%\Microsoft Visual Studio\2022\Enterprise" (
        set "VC_PATH=Microsoft Visual Studio\2022\Enterprise"
      ) else if exist "%PF%\Microsoft Visual Studio\2022\Professional" (
        set "VC_PATH=Microsoft Visual Studio\2022\Professional"
      ) else (
        set "VC_PATH=Microsoft Visual Studio\2022\Community"
      )
    ) else (
		echo Unsupported compiler
		echo Recommend installing Visual Studio 2022 Community Edition - free
		pause
		exit
	)
  )

:checkEnvironment
	if not exist "README.txt" (
		echo Error: Change directory to the rtcwPro repository before running this script
		echo Recommend running this file by double clicking on it in the file explorer
		pause
		exit
	)
  
	if not exist "%PF%\%VC_PATH%\Common7\IDE\devenv.exe" (
		echo "%PF%\%VC_PATH%\Common7\IDE\devenv.exe"
		echo Error: could not find devenv.exe - Visual Studio
		echo Recommend installing Visual Studio 2022 Community Edition - free
		pause
		exit
	)
	
	where /q cmake
	if %errorlevel% neq 0 (
		echo Error: cmake not installed or not in the PATH environment variable
		echo Recommend installing cmake using the cmake installer
		pause
		exit
	)

:fetchDeps
	rem assume we are being called inside a project, do stuff inside a directory
	if not exist "deps64" (
		mkdir deps64
	)
	cd deps64

	echo Fetching Dependencies...

	if not exist "curl" (
		echo curl...
		call powershell "Invoke-WebRequest -Uri https://curl.se/windows/latest.cgi?p=win64-mingw.zip -Out curl.zip"
		call powershell "Expand-Archive -Path curl.zip -DestinationPath curl"
		call powershell "Get-ChildItem """curl\*\*""" | move-item -Destination """curl\""
		call powershell "rm curl.zip"
	) else (
		set SKIP_CURL=1
	)
	
	if not exist "libjpeg-turbo" (
		echo libjpeg-turbo...
		rem Pinned: don't auto-track "latest" here, it silently pulls in breaking
		rem releases (e.g. 3.2.0's SIMD dispatcher rewrite required a matching
		rem fix in tr_image.c). Bump deliberately and retest JPEG texture loading.
		call powershell "$source= (Invoke-RestMethod -Method GET -Uri https://api.github.com/repos/libjpeg-turbo/libjpeg-turbo/releases/tags/3.2.0).zipball_url;"^
						"Write-Host $source;"^
						"$file=$(Split-Path -Path $source -Leaf);"^
						"Invoke-WebRequest -Uri $source -Out $file;"^
						"Get-ChildItem $file | move-item -Destination libjpeg-turbo.zip"
		call powershell "Expand-Archive -Path """libjpeg-turbo*.zip""" -DestinationPath """libjpeg-turbo""""
		call powershell "Get-ChildItem """libjpeg-turbo\*\*""" | move-item -Destination """libjpeg-turbo\""
		call powershell "rm libjpeg-turbo.zip"
	) else (
		set SKIP_JPEG=1
	)
	
	if not exist "jansson" (
		echo jansson...
		call powershell "$source= (Invoke-RestMethod -Method GET -Uri https://api.github.com/repos/akheron/jansson/releases)[0].zipball_url;"^
						"Write-Host $source;"^
						"$file=$(Split-Path -Path $source -Leaf);"^
						"Invoke-WebRequest -Uri $source -Out $file;"^
						"Get-ChildItem $file | move-item -Destination jansson.zip"
		call powershell "Expand-Archive -Path """jansson.zip""" -DestinationPath """jansson""""
		call powershell "Get-ChildItem """jansson\*\*""" | move-item -Destination """jansson\""
		call powershell "rm jansson.zip"
	) else (
		set SKIP_JANSSON=1
	)

	if not exist "omni-bot" (
		echo omni-bot...
		call powershell "$source= (Invoke-RestMethod -Method GET -Uri https://api.github.com/repos/jswigart/omni-bot/releases)[0].zipball_url;"^
						"Write-Host $source;"^
						"$file=$(Split-Path -Path $source -Leaf);"^
						"Invoke-WebRequest -Uri $source -Out $file;"^
						"Get-ChildItem $file | move-item -Destination omni-bot.zip"
		call powershell "Expand-Archive -Path """omni-bot.zip""" -DestinationPath """omni-bot""""
		call powershell "Get-ChildItem """omni-bot\*\*""" | move-item -Destination """omni-bot\""
		call powershell "rm omni-bot.zip"
	) else (
		set SKIP_OMNIBOT=1
	)

:buildDeps
	call "%PF%\%VC_PATH%\VC\Auxiliary\Build\vcvars64.bat"
	set ROOT_DEP_DIR=%cd%
:buildCurl
	if defined SKIP_CURL (
		goto buildLibJPEG
	)
	cd "%ROOT_DEP_DIR%\curl"
	cd bin
	lib /def:libcurl-x64.def /OUT:libcurl-x64.lib /MACHINE:X64
	
:buildLibJPEG
	if defined SKIP_JPEG (
		goto buildJansson
	)
	cd "%ROOT_DEP_DIR%\libjpeg-turbo"
	set JPEG_SRC=%cd%
	mkdir build
	cd build
	rem call cmake -G"%cmake_makefiles%" -A x64  -DCMAKE_POLICY_VERSION_MINIMUM="3.5" -DCMAKE_BUILD_TYPE=Release %JPEG_SRC%
	rem call "%PF%\%VC_PATH%\Common7\IDE\devenv.exe" libjpeg-turbo.sln /Build Release
	call cmake -G"NMake Makefiles" -DCMAKE_POLICY_VERSION_MINIMUM="3.5" -DCMAKE_BUILD_TYPE=Release -DWITH_TURBOJPEG=OFF -DENABLE_SHARED=OFF %JPEG_SRC%
	nmake
	call powershell "Get-ChildItem """..\src\*.h""" | copy-item -Destination """..\""
	call powershell "Get-ChildItem """*.h""" | copy-item -Destination """..\""
	
:buildJansson
	if defined SKIP_JANSSON (
		goto harvest
	)
	cd "%ROOT_DEP_DIR%\jansson"
	set JANSON_SRC=%cd%
	mkdir build
	cd build
	call cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM="3.5" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DHAVE_GETTIMEOFDAY=0 -DHAVE_SCHED_YIELD=0 -DHAVE_SETLOCALE=0 -DHAVE_SYNC_BUILTINS=0 -DJANSSON_BUILD_DOCS=OFF -DCMAKE_USER_MAKE_RULES_OVERRIDE="%ROOT_DEP_DIR%/../cmake/CompilerOptions.cmake"
	
	call ninja
	
:harvest
	cd %ROOT_DEP_DIR%
	if not exist "bin" (
		mkdir bin
	)
	call powershell "Get-ChildItem """curl\bin\*.dll""" | copy-item -Destination """bin\""
	call powershell "Get-ChildItem """curl\bin\*.lib""" | copy-item -Destination """bin\""
	call powershell "Get-ChildItem """libjpeg-turbo\build\*.dll""" | copy-item -Destination """bin\""
	call powershell "Get-ChildItem """libjpeg-turbo\build\*.lib""" | copy-item -Destination """bin\""
	echo Copy the DLL files from deps/bin to your RtcwPro install location where wolfMP.exe is
	if "%~1" == "" (
		pause
	)
