; Minimal NSIS installer: only tests whether silent install (/S) works in this
; environment. Splits the problem in two: (a) environment cannot do silent,
; (b) our installer.nsi has a bug.
; NOTE: keep this file ASCII-only -- NSIS needs a BOM for any non-ASCII byte.
Unicode true
Name "MiniSilentTest"
OutFile "dist\mini-setup.exe"
InstallDir "$PROGRAMFILES64\MiniSilentTest"
RequestExecutionLevel admin

Section "install"
	SetOutPath "$INSTDIR"
	FileOpen $0 "$INSTDIR\marker.txt" w
	FileWrite $0 "silent-install-ok$\r$\n"
	FileClose $0
SectionEnd
