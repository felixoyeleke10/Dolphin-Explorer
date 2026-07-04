#include <QApplication>
#include <QFileInfo>
#include <QScreen>
#include <QSettings>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QDebug>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include "app/import/ImportLog.h"
#include "ui/bottom/RuntimeLogBridge.h"
#include "ui/mainwindow/MainWindow.h"
#include "ui/shell/AppInfo.h"
#include "ui/shell/AppStyle.h"
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

    // Benign Qt Windows platform-plugin noise — Qt auto-corrects these and the UI is
    // unaffected, so keep them in the debug log + stderr above but don't surface them
    // in the in-app runtime log as user-facing problems:
    //   - "Unable to obtain handle for monitor … defaulting to 96 DPI" — transient
    //     monitor-handle / DPI fallback while a window maps.
    //   - "QWindowsWindow::setGeometry: Unable to set geometry …" — the platform
    //     plugin's initial geometry request for a dialog is below its minimum, so Qt
    //     clamps it up to the correct size (the "Resulting geometry" in the message).
    //     This fires for built-in dialogs too (QInputDialog/QMessageBox), so it can't
    //     be fixed per-dialog; the windows still display correctly.
    static const char* kBenignPlatformNoise[] = {
        "Unable to obtain handle for monitor",
        "Unable to set geometry",
    };
    bool benign = false;
    for (const char* needle : kBenignPlatformNoise)
        if (msg.contains(QLatin1String(needle))) { benign = true; break; }
    if (!benign)
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

    // OpenGL setup MUST happen before QApplication so the top-level window is
    // GL-capable from its first show. Otherwise, introducing the 3D QOpenGLWidget
    // later (first 2D→3D switch) forces Windows to recreate the native top-level
    // window — the whole app visibly disappears and reappears. A shared global
    // context + a default format matching MapView3D (3.3 core) initialises the GL
    // compositor up front, so no window recreation occurs on the first switch.
    {
        QSurfaceFormat fmt;
        fmt.setVersion(3, 3);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        fmt.setDepthBufferSize(24);
        fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
        QSurfaceFormat::setDefaultFormat(fmt);
    }
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts, true);

    QApplication app(argc, argv);

    // Point GDAL/PROJ at the CRS + driver data bundled next to the exe (deployed
    // from vcpkg). Set before any GDAL call (raster import lazily registers GDAL).
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString gdalData = appDir + "/gdal-data";
        const QString projData = appDir + "/proj-data";
        if (QFileInfo::exists(gdalData)) qputenv("GDAL_DATA", gdalData.toLocal8Bit());
        if (QFileInfo::exists(projData)) {
            qputenv("PROJ_LIB",  projData.toLocal8Bit());
            qputenv("PROJ_DATA", projData.toLocal8Bit());
        }
    }

    app.setApplicationName(AppInfo::kSettingsApp);
    app.setApplicationVersion(AppInfo::kVersion);
    app.setOrganizationName(AppInfo::kOrgName);
    app.setStyle(QStyleFactory::create("Fusion"));

    // Theme (palette + stylesheet) from the persisted setting — 0=Dark, 1=Light.
    // AppStyle owns both palettes and the QSS; see ui/shell/AppStyle.cpp.
    {
        QSettings theme_settings(AppInfo::kOrgName, AppInfo::kSettingsApp);
        const int theme_mode = theme_settings.value("app/theme", 0).toInt();
        dolphin::ui::AppStyle::apply(theme_mode == 1 ? dolphin::ui::Theme::Mode::Light
                                                     : dolphin::ui::Theme::Mode::Dark);
    }

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
