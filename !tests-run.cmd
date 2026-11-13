@if not exist tests.temp mkdir tests.temp
@if "%CONFIGURATION%"=="" set CONFIGURATION=Debug
%CONFIGURATION%\testrunner.exe %*
