// Eat App shell: Win32 window + WebView2 (local resources only) + named-pipe
// bridge to the local Agent. No HTTP port is opened by the App.
//
// Build: see client/app/CMakeLists.txt (requires the WebView2 SDK).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <WebView2.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <wrl.h>

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kWindowClass[] = L"EatAppShell";
constexpr wchar_t kWindowTitle[] = L"今天你吃了吗";
constexpr UINT kIpcMessage = WM_APP + 1;
constexpr UINT kTrayMessage = WM_APP + 2;
constexpr int kTrayId = 1;
constexpr int kMenuShow = 1001;
constexpr int kMenuExit = 1002;
constexpr std::size_t kMaxIpcPayload = 1024 * 1024;

// MinGW's WRL only provides ComPtr, not Callback; these small handlers keep the
// shell buildable with both MSVC and MinGW.
template <typename Interface, const IID* kIid>
class ComCallbackBase : public Interface {
 public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) return E_POINTER;
        if (riid == IID_IUnknown || riid == *kIid) {
            *object = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG value = --refs_;
        if (value == 0) delete this;
        return value;
    }

 protected:
    virtual ~ComCallbackBase() = default;

 private:
    std::atomic<ULONG> refs_{1};
};

class EnvironmentCallback final
    : public ComCallbackBase<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                            &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler> {
 public:
    using Fn = std::function<HRESULT(HRESULT, ICoreWebView2Environment*)>;
    explicit EnvironmentCallback(Fn fn) : fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result,
                                     ICoreWebView2Environment* environment) override {
        return fn_(result, environment);
    }

 private:
    Fn fn_;
};

class ControllerCallback final
    : public ComCallbackBase<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                            &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler> {
 public:
    using Fn = std::function<HRESULT(HRESULT, ICoreWebView2Controller*)>;
    explicit ControllerCallback(Fn fn) : fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result,
                                     ICoreWebView2Controller* controller) override {
        return fn_(result, controller);
    }

 private:
    Fn fn_;
};

class WebMessageCallback final
    : public ComCallbackBase<ICoreWebView2WebMessageReceivedEventHandler,
                            &IID_ICoreWebView2WebMessageReceivedEventHandler> {
 public:
    using Fn = std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)>;
    explicit WebMessageCallback(Fn fn) : fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE Invoke(
        ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        return fn_(sender, args);
    }

 private:
    Fn fn_;
};

std::wstring Widen(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &out[0], size);
    return out;
}

std::wstring ExecutableDir() {
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::wstring path(buffer);
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

bool ReadExact(HANDLE pipe, void* buffer, std::size_t size) {
    auto* cursor = static_cast<char*>(buffer);
    while (size > 0) {
        DWORD read = 0;
        if (!ReadFile(pipe, cursor, static_cast<DWORD>(size), &read, nullptr) || read == 0) {
            return false;
        }
        cursor += read;
        size -= read;
    }
    return true;
}

class IpcClient {
 public:
    ~IpcClient() { Stop(); }

    bool Start(const std::wstring& pipe_name, HWND window) {
        window_ = window;
        for (int attempt = 0; attempt < 30; ++attempt) {
            pipe_ = CreateFileW(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                OPEN_EXISTING, 0, nullptr);
            if (pipe_ != INVALID_HANDLE_VALUE) break;
            Sleep(100);
        }
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        running_.store(true);
        reader_ = std::thread([this] { ReadLoop(); });
        return true;
    }

    bool Send(const std::string& json) {
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        const std::uint32_t length = static_cast<std::uint32_t>(json.size());
        char header[4] = {
            static_cast<char>(length & 0xFF),
            static_cast<char>((length >> 8) & 0xFF),
            static_cast<char>((length >> 16) & 0xFF),
            static_cast<char>((length >> 24) & 0xFF),
        };
        std::lock_guard<std::mutex> lock(write_mutex_);
        return WriteAll(header, sizeof(header)) && WriteAll(json.data(), json.size());
    }

    void Stop() {
        if (!running_.exchange(false)) return;
        if (pipe_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(pipe_, nullptr);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        if (reader_.joinable()) reader_.join();
    }

 private:
    bool WriteAll(const void* data, std::size_t size) {
        auto* cursor = static_cast<const char*>(data);
        while (size > 0) {
            DWORD written = 0;
            if (!WriteFile(pipe_, cursor, static_cast<DWORD>(size), &written, nullptr) ||
                written == 0) {
                return false;
            }
            cursor += written;
            size -= written;
        }
        return true;
    }

    void ReadLoop() {
        while (running_.load()) {
            char header[4] = {};
            if (!ReadExact(pipe_, header, sizeof(header))) break;
            const std::uint32_t length =
                static_cast<std::uint32_t>(static_cast<unsigned char>(header[0])) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(header[1])) << 8) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(header[2])) << 16) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(header[3])) << 24);
            if (length > kMaxIpcPayload) break;
            std::string payload(length, '\0');
            if (length > 0 && !ReadExact(pipe_, &payload[0], length)) break;
            auto* heap = new std::string(std::move(payload));
            if (!PostMessageW(window_, kIpcMessage, 0, reinterpret_cast<LPARAM>(heap))) {
                delete heap;
                break;
            }
        }
    }

    HWND window_ = nullptr;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::thread reader_;
    std::mutex write_mutex_;
    std::atomic<bool> running_{false};
};

class ShellApp {
 public:
    bool Initialize(HINSTANCE instance) {
        instance_ = instance;
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &ShellApp::WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);

        window_ = CreateWindowExW(0, kWindowClass, kWindowTitle, WS_OVERLAPPEDWINDOW,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820, nullptr, nullptr,
                                  instance, this);
        if (window_ == nullptr) return false;
        ShowWindow(window_, SW_SHOW);
        UpdateWindow(window_);
        return true;
    }

    int Run() {
        MSG message = {};
        while (GetMessageW(&message, nullptr, 0, 0)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return 0;
    }

    void OnIpcPayload(const std::string& json) {
        if (controller_ == nullptr) return;
        ComPtr<ICoreWebView2> webview;
        if (FAILED(controller_->get_CoreWebView2(&webview)) || webview == nullptr) return;
        webview->PostWebMessageAsJson(Widen(json).c_str());
    }

 private:
    static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        ShellApp* self = nullptr;
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<ShellApp*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->window_ = window;
        } else {
            self = reinterpret_cast<ShellApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        if (self == nullptr) return DefWindowProcW(window, message, wparam, lparam);

        switch (message) {
            case WM_CREATE:
                self->Setup();
                return 0;
            case WM_SIZE:
                self->Resize();
                return 0;
            case WM_CLOSE:
                self->HideToTray();
                return 0;
            case WM_DESTROY:
                self->Shutdown();
                PostQuitMessage(0);
                return 0;
            case kIpcMessage:
                if (auto* payload = reinterpret_cast<std::string*>(lparam)) {
                    self->OnIpcPayload(*payload);
                    delete payload;
                }
                return 0;
            case kTrayMessage:
                if (lparam == WM_RBUTTONUP) self->ShowTrayMenu();
                else if (lparam == WM_LBUTTONDBLCLK) self->ShowWindowFromTray();
                return 0;
            case WM_COMMAND:
                if (LOWORD(wparam) == kMenuShow) self->ShowWindowFromTray();
                else if (LOWORD(wparam) == kMenuExit) DestroyWindow(window);
                return 0;
            default:
                return DefWindowProcW(window, message, wparam, lparam);
        }
    }

    void Setup() {
        AddTrayIcon();
        StartAgent();
        CreateWebView();
    }

    void AddTrayIcon() {
        tray_ = {};
        tray_.cbSize = sizeof(tray_);
        tray_.hWnd = window_;
        tray_.uID = kTrayId;
        tray_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        tray_.uCallbackMessage = kTrayMessage;
        tray_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(tray_.szTip, kWindowTitle);
        Shell_NotifyIconW(NIM_ADD, &tray_);
    }

    void StartAgent() {
        const std::wstring dir = ExecutableDir();
        const std::wstring agent_path = dir + L"\\eat-agent.exe";
        SetEnvironmentVariableW(L"EAT_APP_SID", L"local");
        SetEnvironmentVariableW(L"EAT_APP_INSTANCE", L"1");

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        std::wstring command = L"\"" + agent_path + L"\" --ipc";
        if (CreateProcessW(agent_path.c_str(), &command[0], nullptr, nullptr, FALSE, 0, nullptr,
                           dir.c_str(), &startup, &process)) {
            CloseHandle(process.hThread);
            agent_process_ = process.hProcess;
        }
        ipc_.Start(L"\\\\.\\pipe\\EatAppAgent-local-1", window_);
    }

    void CreateWebView() {
        const std::wstring data_dir = ExecutableDir() + L"\\webview-data";
        auto* environment_callback = new EnvironmentCallback(
            [this](HRESULT, ICoreWebView2Environment* environment) -> HRESULT {
                if (environment == nullptr) return E_FAIL;
                auto* controller_callback = new ControllerCallback(
                    [this](HRESULT, ICoreWebView2Controller* controller) -> HRESULT {
                        if (controller == nullptr) return E_FAIL;
                        controller_ = controller;
                        controller_->AddRef();
                        Resize();
                        ComPtr<ICoreWebView2> webview;
                        controller_->get_CoreWebView2(&webview);
                        auto* message_callback = new WebMessageCallback(
                            [this](ICoreWebView2*,
                                   ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                LPWSTR json = nullptr;
                                if (SUCCEEDED(args->get_WebMessageAsJson(&json)) &&
                                    json != nullptr) {
                                    const int size = WideCharToMultiByte(
                                        CP_UTF8, 0, json, -1, nullptr, 0, nullptr, nullptr);
                                    std::string utf8(static_cast<std::size_t>(size), '\0');
                                    WideCharToMultiByte(CP_UTF8, 0, json, -1, &utf8[0], size,
                                                        nullptr, nullptr);
                                    if (!utf8.empty() && utf8.back() == '\0') {
                                        utf8.pop_back();
                                    }
                                    ipc_.Send(utf8);
                                    CoTaskMemFree(json);
                                }
                                return S_OK;
                            });
                        webview->add_WebMessageReceived(message_callback, nullptr);
                        message_callback->Release();
                        const std::wstring url =
                            L"file:///" + ExecutableDir() + L"/web/index.html";
                        webview->Navigate(url.c_str());
                        return S_OK;
                    });
                environment->CreateCoreWebView2Controller(window_, controller_callback);
                controller_callback->Release();
                return S_OK;
            });
        CreateCoreWebView2EnvironmentWithOptions(nullptr, data_dir.c_str(), nullptr,
                                                 environment_callback);
        environment_callback->Release();
    }

    void Resize() {
        if (controller_ == nullptr) return;
        RECT bounds = {};
        GetClientRect(window_, &bounds);
        controller_->put_Bounds(bounds);
    }

    void HideToTray() {
        ShowWindow(window_, SW_HIDE);
        Shell_NotifyIconW(NIM_MODIFY, &tray_);
    }

    void ShowWindowFromTray() {
        ShowWindow(window_, SW_SHOW);
        SetForegroundWindow(window_);
    }

    void ShowTrayMenu() {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kMenuShow, L"打开");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kMenuExit, L"退出");
        POINT point = {};
        GetCursorPos(&point);
        SetForegroundWindow(window_);
        TrackPopupMenu(menu, TPM_RIGHTBUTTON, point.x, point.y, 0, window_, nullptr);
        DestroyMenu(menu);
    }

    void Shutdown() {
        ipc_.Stop();
        if (agent_process_ != nullptr) {
            ipc_.Send("{\"type\":\"shutdown\"}");
            WaitForSingleObject(agent_process_, 2000);
            CloseHandle(agent_process_);
            agent_process_ = nullptr;
        }
        if (controller_ != nullptr) {
            controller_->Close();
            controller_->Release();
            controller_ = nullptr;
        }
        Shell_NotifyIconW(NIM_DELETE, &tray_);
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    NOTIFYICONDATAW tray_ = {};
    ICoreWebView2Controller* controller_ = nullptr;
    IpcClient ipc_;
    HANDLE agent_process_ = nullptr;
};

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\EatApp.Instance");
    if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND existing = FindWindowW(kWindowClass, nullptr)) {
            ShowWindow(existing, SW_SHOW);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ShellApp app;
    if (!app.Initialize(instance)) return 1;
    const int code = app.Run();

    CoUninitialize();
    if (mutex != nullptr) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}
