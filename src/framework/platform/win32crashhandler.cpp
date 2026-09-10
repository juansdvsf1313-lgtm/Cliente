/*
 * Copyright (c) 2010-2026 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "framework/core/application.h"
#if defined(WIN32) && defined(CRASH_HANDLER)

#include <windows.h>

#ifdef _MSC_VER

#pragma warning (push)
#pragma warning (disable:4091) // warning C4091: 'typedef ': ignored on left of '' when no variable is declared
#include <imagehlp.h>
#pragma warning (pop)

#else

#include <imagehlp.h>

#endif

#include <framework/core/graphicalapplication.h>

const char* getExceptionName(const DWORD exceptionCode)
{
    switch (exceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:         return "Access violation";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "Datatype misalignment";
        case EXCEPTION_BREAKPOINT:               return "Breakpoint";
        case EXCEPTION_SINGLE_STEP:              return "Single step";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "Array bounds exceeded";
        case EXCEPTION_FLT_DENORMAL_OPERAND:     return "Float denormal operand";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "Float divide by zero";
        case EXCEPTION_FLT_INEXACT_RESULT:       return "Float inexact result";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "Float invalid operation";
        case EXCEPTION_FLT_OVERFLOW:             return "Float overflow";
        case EXCEPTION_FLT_STACK_CHECK:          return "Float stack check";
        case EXCEPTION_FLT_UNDERFLOW:            return "Float underflow";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "Integer divide by zero";
        case EXCEPTION_INT_OVERFLOW:             return "Integer overflow";
        case EXCEPTION_PRIV_INSTRUCTION:         return "Privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR:            return "In page error";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "Illegal instruction";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "Noncontinuable exception";
        case EXCEPTION_STACK_OVERFLOW:           return "Stack overflow";
        case EXCEPTION_INVALID_DISPOSITION:      return "Invalid disposition";
        case EXCEPTION_GUARD_PAGE:               return "Guard page";
        case EXCEPTION_INVALID_HANDLE:           return "Invalid handle";
    }
    return "Unknown exception";
}

void Stacktrace(LPEXCEPTION_POINTERS e, std::stringstream& ss)
{
    STACKFRAME sf;
    HANDLE process, thread;
    ULONG_PTR dwModBase, Disp;
    BOOL more = FALSE;
    DWORD machineType;
    int count = 0;
    char modname[MAX_PATH];
    char symBuffer[sizeof(IMAGEHLP_SYMBOL) + 255];

    auto* pSym = (PIMAGEHLP_SYMBOL)symBuffer;

    ZeroMemory(&sf, sizeof(sf));
#ifdef _WIN64
    sf.AddrPC.Offset = e->ContextRecord->Rip;
    sf.AddrStack.Offset = e->ContextRecord->Rsp;
    sf.AddrFrame.Offset = e->ContextRecord->Rbp;
    machineType = IMAGE_FILE_MACHINE_AMD64;
#else
    sf.AddrPC.Offset = e->ContextRecord->Eip;
    sf.AddrStack.Offset = e->ContextRecord->Esp;
    sf.AddrFrame.Offset = e->ContextRecord->Ebp;
    machineType = IMAGE_FILE_MACHINE_I386;
#endif

    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrStack.Mode = AddrModeFlat;
    sf.AddrFrame.Mode = AddrModeFlat;

    process = GetCurrentProcess();
    thread = GetCurrentThread();

    while (true) {
        more = StackWalk(machineType, process, thread, &sf, e->ContextRecord, nullptr, SymFunctionTableAccess, SymGetModuleBase, nullptr);
        if (!more || sf.AddrFrame.Offset == 0)
            break;

        dwModBase = SymGetModuleBase(process, sf.AddrPC.Offset);
        if (dwModBase)
            GetModuleFileName(reinterpret_cast<HINSTANCE>(dwModBase), modname, MAX_PATH);
        else {
#ifdef _MSC_VER
            strcpy_s(modname, sizeof(modname), "Unknown");
#else
            strncpy(modname, "Unknown", sizeof(modname));
            modname[sizeof(modname) - 1] = '\0';
#endif
        }

        Disp = 0;
        pSym->SizeOfStruct = sizeof(symBuffer);
        pSym->MaxNameLength = 254;

        if (SymGetSymFromAddr(process, sf.AddrPC.Offset, &Disp, pSym))
            ss << fmt::format("    {}: {}({}+%#0lx) [0x%016lX]\n", count, modname, pSym->Name, Disp, sf.AddrPC.Offset);
        else
            ss << fmt::format("    {}: {} [0x%016lX]\n", count, modname, sf.AddrPC.Offset);
        ++count;
    }
    GlobalFree(pSym);
}

LONG CALLBACK ExceptionHandler(const LPEXCEPTION_POINTERS e)
{
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    std::string crashReport = fmt::format(
        "== application crashed\n"
        "app name: {}\n"
        "app version: {}\n"
        "build compiler: {} - {}\n"
        "build date: {}\n"
        "build type: {}\n"
        "build revision: {} ({})\n"
        "crash date: {}\n"
        "exception: {} (0x{:08X})\n"
        "exception address: 0x{:08X}\n"
        "  backtrace:\n",
        g_app.getName(),
        g_app.getVersion(),
        g_app.getBuildCompiler(), g_app.getBuildArch(),
        g_app.getBuildDate(),
        g_app.getBuildType(),
        g_app.getBuildRevision(), g_app.getBuildCommit(),
        stdext::date_time_string(),
        getExceptionName(e->ExceptionRecord->ExceptionCode), e->ExceptionRecord->ExceptionCode,
        reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)
    );

    std::stringstream oss;
    oss << crashReport;
    Stacktrace(e, oss);
    oss << "\n";

    SymCleanup(GetCurrentProcess());

    g_logger.info(oss.str());

    char dir[MAX_PATH];
    DWORD len = GetCurrentDirectory(sizeof(dir), dir);
    if (len == 0 || len >= sizeof(dir)) {
        g_logger.error("Failed to get current directory for crash report");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::string fileName = fmt::format("{}\\crashreport.log", dir);

    std::ofstream fout(fileName, std::ios::out | std::ios::app);
    if (fout.is_open()) {
        fout << oss.str();
        fout.close();
        g_logger.info("Crash report saved to file {}", fileName);
    } else {
        g_logger.error("Failed to save crash report to {}", fileName);
    }

    std::string msg = fmt::format(
        "The application has crashed.\n\n"
        "A crash report has been written to:\n{}",
        fileName
    );
    MessageBoxA(nullptr, msg.c_str(), "Application crashed", MB_OK | MB_ICONERROR);

    return EXCEPTION_CONTINUE_SEARCH;
}

// [DIAGNOSTICO] El verificador de heap (PageHeap) mata el proceso con
// STATUS_ASSERTION_FAILURE sin pasar por SetUnhandledExceptionFilter, asi que no
// llegabamos a escribir nada. Un manejador vectorizado si ve la excepcion en
// primera instancia, y con la pila en ese momento tenemos al culpable.
static LONG CALLBACK VigilanteDeExcepciones(LPEXCEPTION_POINTERS e)
{
    const DWORD codigo = e->ExceptionRecord->ExceptionCode;
    if (codigo != 0xC0000005 &&  // acceso invalido
        codigo != 0xC0000421 &&  // assert del verificador
        codigo != 0xC0000374 &&  // heap corrompido
        codigo != 0x80000003)    // breakpoint del verificador
        return EXCEPTION_CONTINUE_SEARCH;

    static bool yaEscrito = false;
    if (yaEscrito)
        return EXCEPTION_CONTINUE_SEARCH;
    yaEscrito = true;

    std::stringstream oss;
    oss << "=== excepcion 0x" << std::hex << codigo << std::dec
        << " en 0x" << std::hex
        << reinterpret_cast<std::uintptr_t>(e->ExceptionRecord->ExceptionAddress)
        << std::dec << " ===" << std::endl;
    Stacktrace(e, oss);
    oss << std::endl;

    if (std::ofstream fout("crash_pila.log", std::ios::out | std::ios::app); fout.is_open()) {
        fout << oss.str();
        fout.flush();
        fout.close();
    }
    g_logger.error(oss.str());

    return EXCEPTION_CONTINUE_SEARCH;
}

void installCrashHandler()
{
    SetUnhandledExceptionFilter(ExceptionHandler);
    AddVectoredExceptionHandler(1, VigilanteDeExcepciones);
}


// ===================== GUARDAS_MEMORIA [DIAGNOSTICO] =====================
// El verificador de Windows detecta la corrupcion pero mata el proceso sin
// dejarnos escribir nada. Aqui envolvemos cada reserva de C++ con una cabecera
// y dos franjas centinela: al liberar se comprueba todo y, si algo esta roto,
// escribimos en el acto la pila de DONDE SE RESERVO el bloque (que es lo que
// identifica al culpable) y la de donde se estaba liberando.
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {

    constexpr unsigned int MAGIA_VIVA = 0xC0FFEE01u;
    constexpr unsigned int MAGIA_MUERTA = 0xDEADBEEFu;
    constexpr size_t DESPLAZAMIENTO = 128;   // cabecera + franja delantera
    constexpr size_t FRANJA = 32;            // franja trasera
    constexpr unsigned char PATRON = 0xAB;
    constexpr int FRAMES = 8;

    struct Cabecera
    {
        unsigned int magia;
        unsigned int estado;      // 1 vivo, 0 liberado -> pilla la doble liberacion
        size_t tam;
        unsigned short nPila;
        void* pila[FRAMES];
    };

    std::atomic_bool s_yaVolcado{ false };
    std::atomic_bool s_simbolosListos{ false };

    void simbolizar(std::stringstream& ss, void* const* pila, int n)
    {
        const HANDLE proceso = GetCurrentProcess();
        if (!s_simbolosListos.exchange(true)) {
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
            SymInitialize(proceso, nullptr, TRUE);
        }

        char bufer[sizeof(SYMBOL_INFO) + 512];
        auto* simbolo = reinterpret_cast<SYMBOL_INFO*>(bufer);
        simbolo->SizeOfStruct = sizeof(SYMBOL_INFO);
        simbolo->MaxNameLen = 500;

        for (int i = 0; i < n; ++i) {
            const auto direccion = reinterpret_cast<DWORD64>(pila[i]);
            if (!direccion) continue;
            DWORD64 desplaza = 0;
            ss << "    ";
            if (SymFromAddr(proceso, direccion, &desplaza, simbolo))
                ss << simbolo->Name;
            else
                ss << "0x" << std::hex << direccion << std::dec;
            IMAGEHLP_LINE64 linea;
            ZeroMemory(&linea, sizeof(linea));
            linea.SizeOfStruct = sizeof(linea);
            DWORD despLinea = 0;
            if (SymGetLineFromAddr64(proceso, direccion, &despLinea, &linea))
                ss << "  (" << linea.FileName << ":" << linea.LineNumber << ")";
            ss << std::endl;
        }
    }

    void volcarFallo(const char* motivo, const Cabecera* cab)
    {
        if (s_yaVolcado.exchange(true))
            return;

        std::stringstream ss;
        ss << "=== MEMORIA PISADA: " << motivo << " ===" << std::endl;
        if (cab) {
            ss << "  tamano del bloque: " << cab->tam << " bytes" << std::endl;
            ss << "  estado: " << (cab->estado == 1 ? "vivo" : "ya liberado") << std::endl;
            ss << "  RESERVADO EN:" << std::endl;
            simbolizar(ss, cab->pila, cab->nPila);
        }

        void* aqui[24];
        const unsigned short n = RtlCaptureStackBackTrace(1, 24, aqui, nullptr);
        ss << "  DETECTADO AL LIBERAR EN:" << std::endl;
        simbolizar(ss, aqui, n);
        ss << std::endl;

        if (std::ofstream fout("crash_guardas.log", std::ios::out | std::ios::app); fout.is_open()) {
            fout << ss.str();
            fout.flush();
            fout.close();
        }
    }

    void* reservar(size_t tam)
    {
        if (tam == 0) tam = 1;
        auto* base = static_cast<unsigned char*>(std::malloc(DESPLAZAMIENTO + tam + FRANJA));
        if (!base) return nullptr;

        auto* cab = reinterpret_cast<Cabecera*>(base);
        cab->magia = MAGIA_VIVA;
        cab->estado = 1;
        cab->tam = tam;
        cab->nPila = RtlCaptureStackBackTrace(2, FRAMES, cab->pila, nullptr);

        std::memset(base + sizeof(Cabecera), PATRON, DESPLAZAMIENTO - sizeof(Cabecera));
        std::memset(base + DESPLAZAMIENTO + tam, PATRON, FRANJA);
        return base + DESPLAZAMIENTO;
    }

    void soltar(void* p)
    {
        if (!p) return;
        auto* base = static_cast<unsigned char*>(p) - DESPLAZAMIENTO;
        auto* cab = reinterpret_cast<Cabecera*>(base);

        if (cab->magia == MAGIA_MUERTA) {
            volcarFallo("liberacion DOBLE del mismo bloque", cab);
            return;
        }
        if (cab->magia != MAGIA_VIVA) {
            volcarFallo("cabecera destruida o puntero que no salio de operator new", nullptr);
            return;
        }

        const auto* delante = base + sizeof(Cabecera);
        for (size_t i = 0; i < DESPLAZAMIENTO - sizeof(Cabecera); ++i) {
            if (delante[i] != PATRON) { volcarFallo("escritura ANTES del bloque", cab); break; }
        }
        const auto* detras = base + DESPLAZAMIENTO + cab->tam;
        for (size_t i = 0; i < FRANJA; ++i) {
            if (detras[i] != PATRON) { volcarFallo("escritura DESPUES del bloque", cab); break; }
        }

        cab->magia = MAGIA_MUERTA;
        cab->estado = 0;
        std::free(base);
    }

}  // namespace

void* operator new(size_t tam) { void* p = reservar(tam); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](size_t tam) { void* p = reservar(tam); if (!p) throw std::bad_alloc(); return p; }
void* operator new(size_t tam, const std::nothrow_t&) noexcept { return reservar(tam); }
void* operator new[](size_t tam, const std::nothrow_t&) noexcept { return reservar(tam); }
void operator delete(void* p) noexcept { soltar(p); }
void operator delete[](void* p) noexcept { soltar(p); }
void operator delete(void* p, size_t) noexcept { soltar(p); }
void operator delete[](void* p, size_t) noexcept { soltar(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { soltar(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { soltar(p); }
// =================== fin GUARDAS_MEMORIA ===================

#endif