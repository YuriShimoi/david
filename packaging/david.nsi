; David installer (NSIS). Built by tools/package.py with makensis (WSL or Windows):
;   makensis -DVERSION=1.0.0 -DSRCDIR=<staged app folder> -DOUTFILE=<setup.exe> david.nsi
; A per-user install (no admin rights): %LOCALAPPDATA%\Programs\David, Start menu shortcut, uninstaller.
Unicode true
SetCompressor /SOLID lzma
RequestExecutionLevel user

!include "MUI2.nsh"

Name "David ${VERSION}"
OutFile "${OUTFILE}"
InstallDir "$LOCALAPPDATA\Programs\David"
InstallDirRegKey HKCU "Software\David" "InstallDir"
BrandingText "David ${VERSION}"

!define MUI_ICON "${SRCDIR}\assets\icons\david.ico"
!define MUI_UNICON "${SRCDIR}\assets\icons\david.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\david.exe"

!insertmacro MUI_PAGE_LICENSE "${SRCDIR}\LICENSE.txt"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "David"
VIAddVersionKey "FileDescription" "David installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "MIT License"

Section "David" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  ; An update replaces the program's own folders; the user's settings live in %APPDATA%\David
  RMDir /r "$INSTDIR\assets"
  RMDir /r "$INSTDIR\docs"
  RMDir /r "$INSTDIR\lang"
  RMDir /r "$INSTDIR\patterns"
  RMDir /r "$INSTDIR\themes"
  RMDir /r "$INSTDIR\mods\examples"
  File /r "${SRCDIR}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortCut "$SMPROGRAMS\David.lnk" "$INSTDIR\david.exe" "" "$INSTDIR\david.exe" 0
  WriteRegStr HKCU "Software\David" "InstallDir" "$INSTDIR"
  !define UNINST "Software\Microsoft\Windows\CurrentVersion\Uninstall\David"
  WriteRegStr HKCU "${UNINST}" "DisplayName" "David"
  WriteRegStr HKCU "${UNINST}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "${UNINST}" "Publisher" "YuriShimoi"
  WriteRegStr HKCU "${UNINST}" "DisplayIcon" "$INSTDIR\david.exe"
  WriteRegStr HKCU "${UNINST}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "${UNINST}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST}" "NoRepair" 1
SectionEnd

Section "Uninstall"
  ; The program folder only: settings, themes and mods in %APPDATA%\David stay
  Delete "$SMPROGRAMS\David.lnk"
  RMDir /r "$INSTDIR"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\David"
  DeleteRegKey HKCU "Software\David"
SectionEnd
