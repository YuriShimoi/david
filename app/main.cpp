#include "app/App.h"
#include <SDL3/SDL_main.h>

#if defined(_WIN32) && defined(_DEBUG)
#include <windows.h>
#include <dbghelp.h>
#include <crtdbg.h>
#include <cstdio>
#pragma comment(lib, "dbghelp.lib")

// Debug builds: a failed CRT assert or a crash writes its message and a symbolized stack to %TEMP%\david-assert.txt
static FILE* openReport() {
  char path[MAX_PATH];
  GetTempPathA(MAX_PATH, path);
  strcat_s(path, "david-assert.txt");
  FILE* f = nullptr;
  return fopen_s(&f, path, "w") ? nullptr : f;
}

static void writeStack(FILE* f) {
  HANDLE proc = GetCurrentProcess();
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
  SymInitialize(proc, nullptr, TRUE);
  void* frames[48];
  USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
  alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 256];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
  for(USHORT i = 0; i < n; i++) {
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 addr = DWORD64(frames[i]), disp = 0;
    DWORD lineDisp = 0;
    IMAGEHLP_LINE64 line{ sizeof(IMAGEHLP_LINE64) };
    bool hasSym = SymFromAddr(proc, addr, &disp, sym);
    bool hasLine = SymGetLineFromAddr64(proc, addr, &lineDisp, &line);
    std::fprintf(f, "%2d %s  %s:%lu\n", i, hasSym ? sym->Name : "?", hasLine ? line.FileName : "", hasLine ? line.LineNumber : 0);
  }
}

static int assertHook(int, char* message, int*) {
  if(FILE* f = openReport()) {
    std::fprintf(f, "%s\n", message ? message : "");
    writeStack(f);
    std::fclose(f);
  }
  return FALSE;
}

static LONG WINAPI crashHook(EXCEPTION_POINTERS* e) {
  if(FILE* f = openReport()) {
    std::fprintf(f, "crash 0x%08lX at %p\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    writeStack(f);
    std::fclose(f);
  }
  return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char** argv) {
#if defined(_WIN32) && defined(_DEBUG)
  _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, assertHook);
  SetUnhandledExceptionFilter(crashHook);
#endif
  david::App app;
  if(!app.init(argc, argv)) return 1;
  app.run();
  app.shutdown();
  return 0;
}
