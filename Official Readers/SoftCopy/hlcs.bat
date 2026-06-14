REM  hlcs.bat
REM
REM  Launches the Bookshelf Organizer application.
REM
REM  Note:  Commands filled in during product install
REM
setlocal
set XERCESC_NLS_HOME=C:\PROGRA~2\IBM\SCR\sys
start C:\PROGRA~2\IBM\SCR\sys\hlcs.exe %1 %2 %3 %4 %5 %6 -d C:\PROGRA~2\IBM\SCR\sys
endlocal
