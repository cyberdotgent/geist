REM  hlcb.bat
REM
REM  Launches the Book Reader application.
REM
REM  Note:  Commands filled in during product install
REM
setlocal
set XERCESC_NLS_HOME=C:\PROGRA~2\IBM\SCR\sys
start C:\PROGRA~2\IBM\SCR\sys\hlcb.exe %1 %2 %3 %4 %5 %6 -d C:\PROGRA~2\IBM\SCR\sys
endlocal
