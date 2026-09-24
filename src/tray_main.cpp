#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

constexpr wchar_t kWindowClass[] = L"RyzenAIServerTrayWindow";
constexpr wchar_t kMutexName[] = L"Local\\RyzenAIServerTray";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kStartMessage = WM_APP + 2;
constexpr UINT kStopMessage = WM_APP + 3;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kIconId = 1;
constexpr UINT kMenuStart = 100;
constexpr UINT kMenuStop = 101;
constexpr UINT kMenuHealth = 102;
constexpr UINT kMenuLog = 103;
constexpr UINT kMenuExit = 104;

std::wstring quoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'"') {
            result.append(slashes * 2 + 1, L'\\');
            result += ch;
            slashes = 0;
        } else {
            result.append(slashes, L'\\');
            result += ch;
            slashes = 0;
        }
    }
    result.append(slashes * 2, L'\\');
    return result + L'"';
}

fs::path executableDirectory() {
    std::vector<wchar_t> buffer(32768);
    DWORD count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count >= buffer.size()) return {};
    return fs::path(std::wstring(buffer.data(), count)).parent_path();
}

std::wstring readSetting(const fs::path& ini, const wchar_t* name, const wchar_t* fallback) {
    wchar_t buffer[32768] = {};
    GetPrivateProfileStringW(L"server", name, fallback, buffer,
                             static_cast<DWORD>(std::size(buffer)), ini.c_str());
    return buffer;
}

class TrayApp {
public:
    bool initialize(HINSTANCE instance) {
        directory_ = executableDirectory();
        if (directory_.empty()) return false;
        ini_ = directory_ / L"ryzenai-tray.ini";
        log_ = directory_ / L"ryzenai-server.log";
        taskbar_created_ = RegisterWindowMessageW(L"TaskbarCreated");

        WNDCLASSEXW window_class = {};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = windowProc;
        window_class.hInstance = instance;
        window_class.lpszClassName = kWindowClass;
        if (!RegisterClassExW(&window_class)) return false;

        window_ = CreateWindowExW(0, kWindowClass, L"Ryzen AI Server Tray", 0,
                                  0, 0, 0, 0, nullptr, nullptr, instance, this);
        if (!window_ || !addIcon()) return false;
        SetTimer(window_, kTimerId, 2000, nullptr);
        startServer();
        return true;
    }

    int run() {
        MSG message = {};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return static_cast<int>(message.wParam);
    }

private:
    enum class State { Stopped, Starting, Ready, External, Error };

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        if (message == WM_NCCREATE) {
            auto* created = reinterpret_cast<CREATESTRUCTW*>(lparam);
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(created->lpCreateParams));
        }
        auto* app = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!app) return DefWindowProcW(window, message, wparam, lparam);
        if (message == app->taskbar_created_) {
            app->addIcon();
            return 0;
        }
        switch (message) {
        case kTrayMessage:
            if (LOWORD(lparam) == WM_CONTEXTMENU || LOWORD(lparam) == WM_RBUTTONUP ||
                LOWORD(lparam) == NIN_SELECT || LOWORD(lparam) == WM_LBUTTONUP) {
                app->showMenu();
            }
            return 0;
        case WM_CONTEXTMENU:
            app->showMenu();
            return 0;
        case WM_COMMAND:
            app->handleCommand(LOWORD(wparam));
            return 0;
        case kStartMessage:
            app->startServer();
            return 0;
        case kStopMessage:
            app->stopServer();
            return 0;
        case WM_TIMER:
            if (wparam == kTimerId) app->refresh();
            return 0;
        case WM_CLOSE:
            DestroyWindow(window);
            return 0;
        case WM_ENDSESSION:
            if (wparam) app->stopServer();
            return 0;
        case WM_DESTROY:
            KillTimer(window, kTimerId);
            app->stopServer();
            Shell_NotifyIconW(NIM_DELETE, &app->icon_);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wparam, lparam);
        }
    }

    bool addIcon() {
        icon_ = {};
        icon_.cbSize = sizeof(icon_);
        icon_.hWnd = window_;
        icon_.uID = kIconId;
        icon_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        icon_.uCallbackMessage = kTrayMessage;
        icon_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(icon_.szTip, L"Ryzen AI Server: stopped");
        if (!Shell_NotifyIconW(NIM_ADD, &icon_)) return false;
        icon_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &icon_);
        updateIcon();
        return true;
    }

    void updateIcon() {
        const wchar_t* label = L"stopped";
        if (state_ == State::Starting) label = L"starting";
        if (state_ == State::Ready) label = L"ready";
        if (state_ == State::External) label = L"already running";
        if (state_ == State::Error) label = L"error";
        std::wstring tip = std::wstring(L"Ryzen AI Server: ") + label;
        wcscpy_s(icon_.szTip, tip.c_str());
        icon_.hIcon = LoadIconW(nullptr,
                                state_ == State::Error ? IDI_ERROR :
                                state_ == State::Ready || state_ == State::External ? IDI_INFORMATION :
                                IDI_APPLICATION);
        icon_.uFlags = NIF_ICON | NIF_TIP;
        Shell_NotifyIconW(NIM_MODIFY, &icon_);
    }

    void setState(State next) {
        if (state_ == next) return;
        state_ = next;
        updateIcon();
    }

    void notify(const wchar_t* title, const std::wstring& detail, DWORD flags) {
        NOTIFYICONDATAW notice = {};
        notice.cbSize = sizeof(notice);
        notice.hWnd = window_;
        notice.uID = kIconId;
        notice.uFlags = NIF_INFO;
        wcsncpy_s(notice.szInfoTitle, title, _TRUNCATE);
        wcsncpy_s(notice.szInfo, detail.c_str(), _TRUNCATE);
        notice.dwInfoFlags = flags;
        Shell_NotifyIconW(NIM_MODIFY, &notice);
    }

    bool loadConfig() {
        if (!fs::exists(ini_)) {
            notify(L"Ryzen AI Server", L"Create ryzenai-tray.ini beside ryzenai-tray.exe.", NIIF_ERROR);
            return false;
        }
        server_ = readSetting(ini_, L"server", L"ryzenai-server.exe");
        model_ = readSetting(ini_, L"model", L"");
        version_ = readSetting(ini_, L"ryzenai_version", L"");
        host_ = readSetting(ini_, L"host", L"127.0.0.1");
        port_ = GetPrivateProfileIntW(L"server", L"port", 8080, ini_.c_str());
        context_size_ = GetPrivateProfileIntW(L"server", L"ctx_size", 4096, ini_.c_str());
        if (server_.is_relative()) server_ = directory_ / server_;
        if (model_.is_relative()) model_ = directory_ / model_;
        if (model_.empty() || !fs::exists(server_) || !fs::exists(model_ / L"genai_config.json") ||
            port_ == 0 || port_ > 65535 || context_size_ < 2) {
            notify(L"Ryzen AI Server", L"Check server, model, port and ctx_size in ryzenai-tray.ini.", NIIF_ERROR);
            return false;
        }
        return true;
    }

    bool isHealthy() const {
        HINTERNET session = WinHttpOpen(L"RyzenAI-Tray/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return false;
        WinHttpSetTimeouts(session, 250, 250, 250, 250);
        HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", static_cast<INTERNET_PORT>(port_), 0);
        HINTERNET request = connection ? WinHttpOpenRequest(connection, L"GET", L"/health", nullptr,
                                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
        DWORD status = 0;
        DWORD length = sizeof(status);
        bool healthy = request && WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                       WinHttpReceiveResponse(request, nullptr) &&
                       WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                           WINHTTP_HEADER_NAME_BY_INDEX, &status, &length, WINHTTP_NO_HEADER_INDEX) &&
                       status == 200;
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return healthy;
    }

    void startServer() {
        if (process_) return;
        if (!loadConfig()) {
            setState(State::Error);
            return;
        }
        if (isHealthy()) {
            setState(State::External);
            notify(L"Ryzen AI Server", L"A server is already listening on this port.", NIIF_INFO);
            return;
        }

        SECURITY_ATTRIBUTES security = {sizeof(security), nullptr, TRUE};
        HANDLE output = CreateFileW(log_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (output == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
            if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
            if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
            setState(State::Error);
            notify(L"Ryzen AI Server", L"Cannot open server log or input handle.", NIIF_ERROR);
            return;
        }

        if (!version_.empty()) SetEnvironmentVariableW(L"RYZENAI_VERSION", version_.c_str());
        std::wstring command = quoteArgument(server_.wstring()) + L" -m " + quoteArgument(model_.wstring()) +
                               L" --ctx-size " + std::to_wstring(context_size_) +
                               L" --host " + quoteArgument(host_) +
                               L" --port " + std::to_wstring(port_);
        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = output;
        startup.hStdError = output;
        PROCESS_INFORMATION process_info = {};
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                         &limits, sizeof(limits))) {
                CloseHandle(job);
                job = nullptr;
            }
        }
        BOOL launched = CreateProcessW(server_.c_str(), command.data(), nullptr, nullptr, TRUE,
                                       CREATE_NO_WINDOW | (job ? CREATE_SUSPENDED : 0),
                                       nullptr, directory_.c_str(),
                                       &startup, &process_info);
        DWORD error = launched ? 0 : GetLastError();
        CloseHandle(input);
        CloseHandle(output);
        if (!launched) {
            if (job) CloseHandle(job);
            setState(State::Error);
            notify(L"Ryzen AI Server", L"CreateProcess failed: " + std::to_wstring(error), NIIF_ERROR);
            return;
        }
        if (job) {
            if (!AssignProcessToJobObject(job, process_info.hProcess)) {
                CloseHandle(job);
                job = nullptr;
            }
            if (ResumeThread(process_info.hThread) == static_cast<DWORD>(-1)) {
                if (job) CloseHandle(job);
                TerminateProcess(process_info.hProcess, 1);
                CloseHandle(process_info.hProcess);
                CloseHandle(process_info.hThread);
                setState(State::Error);
                notify(L"Ryzen AI Server", L"Could not resume server process.", NIIF_ERROR);
                return;
            }
        }
        job_ = job;
        process_ = process_info.hProcess;
        CloseHandle(process_info.hThread);
        setState(State::Starting);
    }

    void stopServer() {
        if (!process_) return;
        if (job_) {
            CloseHandle(job_);
            job_ = nullptr;
        } else {
            TerminateProcess(process_, 0);
        }
        WaitForSingleObject(process_, 5000);
        CloseHandle(process_);
        process_ = nullptr;
        setState(State::Stopped);
    }

    void refresh() {
        if (process_) {
            if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
                DWORD exit_code = 0;
                GetExitCodeProcess(process_, &exit_code);
                CloseHandle(process_);
                process_ = nullptr;
                if (job_) {
                    CloseHandle(job_);
                    job_ = nullptr;
                }
                setState(State::Error);
                notify(L"Ryzen AI Server", L"Server exited (code " + std::to_wstring(exit_code) +
                       L"). Open the log for details.", NIIF_ERROR);
            } else if (isHealthy()) {
                setState(State::Ready);
            }
        } else if (state_ == State::External && !isHealthy()) {
            setState(State::Stopped);
        }
    }

    void showMenu() {
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        AppendMenuW(menu, MF_STRING | (process_ || state_ == State::External ? MF_GRAYED : 0),
                    kMenuStart, L"Start");
        AppendMenuW(menu, MF_STRING | (!process_ ? MF_GRAYED : 0), kMenuStop, L"Stop");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (state_ != State::Ready && state_ != State::External ? MF_GRAYED : 0),
                    kMenuHealth, L"Open /health");
        AppendMenuW(menu, MF_STRING | (!fs::exists(log_) ? MF_GRAYED : 0), kMenuLog, L"Open log");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"Exit");
        POINT point = {};
        GetCursorPos(&point);
        SetForegroundWindow(window_);
        UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       point.x, point.y, 0, window_, nullptr);
        PostMessageW(window_, WM_NULL, 0, 0);
        DestroyMenu(menu);
        if (selected) handleCommand(selected);
    }

    void handleCommand(UINT command) {
        switch (command) {
        case kMenuStart: startServer(); break;
        case kMenuStop: stopServer(); break;
        case kMenuHealth: {
            std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port_) + L"/health";
            ShellExecuteW(window_, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case kMenuLog:
            ShellExecuteW(window_, L"open", L"notepad.exe", quoteArgument(log_.wstring()).c_str(),
                          nullptr, SW_SHOWNORMAL);
            break;
        case kMenuExit: DestroyWindow(window_); break;
        }
    }

    HWND window_ = nullptr;
    NOTIFYICONDATAW icon_ = {};
    HANDLE process_ = nullptr;
    HANDLE job_ = nullptr;
    UINT taskbar_created_ = 0;
    State state_ = State::Stopped;
    fs::path directory_;
    fs::path ini_;
    fs::path log_;
    fs::path server_;
    fs::path model_;
    std::wstring version_;
    std::wstring host_;
    UINT port_ = 8080;
    UINT context_size_ = 4096;
};

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring command = argc > 1 ? args[1] : L"";
    if (args) LocalFree(args);
    if (command == L"--quit" || command == L"--stop" || command == L"--start") {
        HWND existing = FindWindowW(kWindowClass, nullptr);
        if (existing) {
            PostMessageW(existing, command == L"--quit" ? WM_CLOSE :
                                   command == L"--stop" ? kStopMessage : kStartMessage, 0, 0);
            return 0;
        }
        if (command != L"--start") return 1;
    }

    HANDLE mutex = CreateMutexW(nullptr, FALSE, kMutexName);
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return 0;
    }
    TrayApp app;
    if (!app.initialize(instance)) {
        MessageBoxW(nullptr, L"Cannot create the Ryzen AI tray icon.", L"Ryzen AI Server", MB_ICONERROR);
        CloseHandle(mutex);
        return 1;
    }
    int result = app.run();
    CloseHandle(mutex);
    return result;
}
