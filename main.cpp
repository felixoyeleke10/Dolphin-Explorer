#include <QApplication>
#include <QScreen>
#include <QSettings>
#include <QStyleFactory>
#include <QDebug>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include "app/import/ImportLog.h"
#include "ui/bottom/RuntimeLogBridge.h"
#include "ui/mainwindow/MainWindow.h"
#include "ui/shell/AppInfo.h"
#include "pipeline/NodeRegistry.h"
#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#endif

using namespace dolphin::ui;

static FILE* g_log_file = nullptr;

#ifdef _WIN32
static void writeCrashStack(FILE* f)
{
    void* frames[64] = {};
    const USHORT count = CaptureStackBackTrace(0, 64, frames, nullptr);

    static char sym_buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    HANDLE proc = GetCurrentProcess();
    static bool sym_initialized = false;
    if (!sym_initialized) {
        SymInitialize(proc, nullptr, TRUE);
        sym_initialized = true;
    }

    fprintf(f, "=== call stack ===\n");
    for (USHORT i = 0; i < count; ++i) {
        DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(sym_buf);
        memset(sym, 0, sizeof(SYMBOL_INFO) + MAX_SYM_NAME);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = MAX_SYM_NAME;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, addr, &disp, sym))
            fprintf(f, "  [%2u] 0x%016llX  %s + 0x%llX\n", i, addr, sym->Name, (unsigned long long)disp);
        else
            fprintf(f, "  [%2u] 0x%016llX\n", i, addr);
    }
    fprintf(f, "=== end stack ===\n");
    fflush(f);
}
#endif

static void debugMessageHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    const QByteArray ba = msg.toLocal8Bit();
    if (g_log_file) {
        fprintf(g_log_file, "%s\n", ba.constData());
        fflush(g_log_file);
    }
    fprintf(stderr, "%s\n", ba.constData());
    fflush(stderr);
    dolphin::ui::RuntimeLogBridge::publish(type, msg);

    // Q_ASSERT → qFatal calls this handler, then calls abort() from inside Qt's DLL.
    // The DLL's CRT has a different signal table so SIGABRT handlers on the exe side
    // never fire.  Capture the stack here, while we're still alive, before abort() runs.
    if (type == QtFatalMsg) {
#ifdef _WIN32
        writeCrashStack(stderr);
        if (g_log_file) writeCrashStack(g_log_file);
#endif
    }
}

static void terminateHandler()
{
    fprintf(stderr, "\n=== FATAL: std::terminate ===\n");
    dolphin::app::ImportLog::dumpCrashTrace(stderr);
    if (g_log_file) {
        fprintf(g_log_file, "\n=== FATAL: std::terminate ===\n");
        dolphin::app::ImportLog::dumpCrashTrace(g_log_file);
    }
    std::abort();
}

#ifdef _WIN32
static LONG WINAPI sehCrashHandler(EXCEPTION_POINTERS* ep)
{
    // Write a minidump alongside the exe so it can be loaded in Visual Studio /
    // WinDbg to get the exact crash location and full call stack.
    HANDLE hFile = CreateFileA("dolphin_crash.dmp", GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{};
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;
        const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
            MiniDumpWithDataSegs | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
                          hFile, dumpType, &mei, nullptr, nullptr);
        CloseHandle(hFile);
        fprintf(stderr, "[crash] minidump written → dolphin_crash.dmp\n");
        if (g_log_file)
            fprintf(g_log_file, "[crash] minidump written → dolphin_crash.dmp\n");
    }

    const DWORD code = ep ? ep->ExceptionRecord->ExceptionCode : 0;
    fprintf(stderr, "\n=== FATAL: SEH 0x%08lX ===\n", code);
    dolphin::app::ImportLog::dumpCrashTrace(stderr);
    if (g_log_file) {
        fprintf(g_log_file, "\n=== FATAL: SEH 0x%08lX ===\n", code);
        dolphin::app::ImportLog::dumpCrashTrace(g_log_file);
        fflush(g_log_file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

int main(int argc, char* argv[])
{
    g_log_file = fopen("dolphin_debug.log", "w");
    qInstallMessageHandler(debugMessageHandler);
    std::set_terminate(terminateHandler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(sehCrashHandler);
#endif

    // Register all built-in processing nodes before any graph is loaded/created
    dolphin::pipeline::registerBuiltinNodes();

    QApplication app(argc, argv);
    app.setApplicationName(AppInfo::kSettingsApp);
    app.setApplicationVersion(AppInfo::kVersion);
    app.setOrganizationName(AppInfo::kOrgName);
    app.setStyle(QStyleFactory::create("Fusion"));

    // Dark palette — macOS system dark
    QPalette dark;
    dark.setColor(QPalette::Window,          QColor(0x1c, 0x1c, 0x1e));
    dark.setColor(QPalette::WindowText,      QColor(0xf2, 0xf2, 0xf7));
    dark.setColor(QPalette::Base,            QColor(0x1c, 0x1c, 0x1e));
    dark.setColor(QPalette::AlternateBase,   QColor(0x28, 0x28, 0x2a));
    dark.setColor(QPalette::ToolTipBase,     QColor(0x2c, 0x2c, 0x2e));
    dark.setColor(QPalette::ToolTipText,     QColor(0xf2, 0xf2, 0xf7));
    dark.setColor(QPalette::Text,            QColor(0xf2, 0xf2, 0xf7));
    dark.setColor(QPalette::Button,          QColor(0x2c, 0x2c, 0x2e));
    dark.setColor(QPalette::ButtonText,      QColor(0xf2, 0xf2, 0xf7));
    dark.setColor(QPalette::BrightText,      Qt::white);
    dark.setColor(QPalette::Link,            QColor(0x0a, 0x84, 0xff));
    dark.setColor(QPalette::Highlight,       QColor(0x0a, 0x84, 0xff));
    dark.setColor(QPalette::HighlightedText, Qt::white);
    dark.setColor(QPalette::Mid,             QColor(0x38, 0x38, 0x3a));
    dark.setColor(QPalette::Dark,            QColor(0x11, 0x11, 0x13));
    dark.setColor(QPalette::Shadow,          QColor(0x00, 0x00, 0x00));
    app.setPalette(dark);

    MainWindow window;

    // Restore geometry, but reset to centre if the saved rect is off every screen
    QSettings geom_settings(AppInfo::kOrgName, AppInfo::kSettingsApp);
    const QByteArray saved_geom = geom_settings.value("geometry").toByteArray();
    bool restored = false;
    if (!saved_geom.isEmpty()) {
        restored = window.restoreGeometry(saved_geom);
        if (restored) {
            const QRect frame = window.frameGeometry();
            bool on_screen = false;
            for (const QScreen* s : QApplication::screens())
                if (s->availableGeometry().intersects(frame)) { on_screen = true; break; }
            if (!on_screen) restored = false;
        }
    }
    if (!restored)
        window.move(QApplication::primaryScreen()->availableGeometry().center()
                    - window.rect().center());

    window.show();
    const int ret = app.exec();
    if (g_log_file) fclose(g_log_file);
    return ret;
}
