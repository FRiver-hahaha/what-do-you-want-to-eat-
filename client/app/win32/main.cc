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

#include "client/transport/TcpTransport.h"
#include "shared/protocol/JsonValue.h"
#include "shared/protocol/StrictJson.h"

using Microsoft::WRL::ComPtr;
using namespace eat;

namespace {

constexpr wchar_t kWindowClass[] = L"EatAppShell";
constexpr wchar_t kWindowTitle[] = L"泛饭";
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

std::wstring LocalAppDataDir() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    return size > 0 ? std::wstring(buffer, size) : ExecutableDir();
}

std::string ReadUtf8File(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::string();
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 || size.QuadPart > 1024 * 1024) {
        CloseHandle(file);
        return std::string();
    }
    std::string content(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool ok = ReadFile(file, &content[0], static_cast<DWORD>(content.size()), &read, nullptr) &&
                    read == content.size();
    CloseHandle(file);
    return ok ? content : std::string();
}

bool WriteUtf8File(const std::wstring& path, const std::string& content) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written,
                              nullptr) && written == content.size();
    CloseHandle(file);
    return ok;
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
    struct LoginConfig {
        bool present = false;
        std::string host = "127.0.0.1";
        std::uint16_t port = 9443;
        std::string login_id;
        std::string password;
        std::string display_name;
        std::string captcha_id;
        std::string captcha_code;
        bool register_account = false;
    };

    struct Session {
        std::string host = "127.0.0.1";
        std::uint16_t port = 9443;
        std::string user_key;
        std::string access_token;
    };

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

    void OnIpcPayload(const std::string& json) { PostToWeb(json); }

    void PostToWeb(const std::string& json) {
        if (controller_ == nullptr) return;
        ComPtr<ICoreWebView2> webview;
        if (FAILED(controller_->get_CoreWebView2(&webview)) || webview == nullptr) return;
        webview->PostWebMessageAsJson(Widen(json).c_str());
    }

    void RouteWebMessage(const std::string& utf8) {
        const auto parsed = protocol::ParseStrictJson(utf8);
        if (parsed.ok && parsed.value.IsObject()) {
            const protocol::JsonValue* type = parsed.value.Find("type");
            if (type != nullptr && type->IsString() && type->AsString() == "service.login") {
                HandleServiceLogin(parsed.value);
                return;
            }
            if (type != nullptr && type->IsString() && type->AsString() == "service.captcha") {
                HandleServiceCaptcha(parsed.value);
                return;
            }
            if (type != nullptr && type->IsString() && type->AsString() == "service.logout") {
                HandleServiceLogout(parsed.value);
                return;
            }
            if (type != nullptr && type->IsString() && type->AsString() == "service.quit") {
                PostMessageW(window_, WM_CLOSE, 0, 0);
                return;
            }
        }
        ipc_.Send(utf8);
    }

    static std::string StringField(const protocol::JsonValue& object, const char* key) {
        const protocol::JsonValue* value = object.Find(key);
        return value != nullptr && value->IsString() ? value->AsString() : std::string();
    }

    void HandleServiceLogin(const protocol::JsonValue& message) {
        LoginConfig config;
        config.host = StringField(message, "host");
        config.login_id = StringField(message, "login_id");
        config.password = StringField(message, "password");
        config.display_name = StringField(message, "display_name");
        config.captcha_id = StringField(message, "captcha_id");
        config.captcha_code = StringField(message, "captcha_code");
        config.present = true;
        const protocol::JsonValue* port_value = message.Find("port");
        if (port_value != nullptr && port_value->IsInt() && port_value->AsInt() > 0 &&
            port_value->AsInt() <= 65535) {
            config.port = static_cast<std::uint16_t>(port_value->AsInt());
        }
        const protocol::JsonValue* register_value = message.Find("register");
        config.register_account = register_value != nullptr && register_value->AsBool();
        if (config.host.empty() || config.login_id.empty() || config.password.empty()) {
            ReplyLogin(message, false, "host/login_id/password are required");
            return;
        }

        std::string error;
        if (!LoginWithCredentials(config, error)) {
            ReplyLogin(message, false, error);
            return;
        }
        NotifyAgentService();
        ReplyLogin(message, true, "");
    }

    bool LoginWithCredentials(const LoginConfig& config, std::string& error) {
        client::TransportConfig transport_config;
        transport_config.host = config.host;
        transport_config.port = config.port;
        transport_config.login_id = config.login_id;
        transport_config.password = config.password;
        transport_config.display_name =
            config.display_name.empty() ? config.login_id : config.display_name;
        transport_config.register_account = config.register_account;
        transport_config.captcha_id = config.captcha_id;
        transport_config.captcha_code = config.captcha_code;
        transport_config.device_id = "eat-app";
        transport_config.client_version = "windows-app/1";

        client::TcpTransport transport(transport_config);
        std::string transport_error;
        if (!transport.Start(transport_error)) {
            error = "login_failed:" + transport_error;
            return false;
        }
        session_.host = config.host;
        session_.port = config.port;
        session_.user_key = transport.user_key();
        session_.access_token = transport.access_token();
        transport.Stop();
        if (session_.access_token.empty() || session_.user_key.empty()) {
            error = "login_returned_empty_session";
            return false;
        }
        SaveSession();
        ApplySessionEnvironment();
        error.clear();
        return true;
    }

    LoginConfig LoadLoginConfig() const {
        LoginConfig config;
        const std::wstring candidates[] = {
            ExecutableDir() + L"\\login.json",
            LocalAppDataDir() + L"\\EatApp\\login.json",
        };
        for (const auto& path : candidates) {
            const std::string raw = ReadUtf8File(path);
            if (raw.empty()) continue;
            const auto parsed = protocol::ParseStrictJson(raw);
            if (!parsed.ok || !parsed.value.IsObject()) continue;
            config.host = StringField(parsed.value, "host");
            config.login_id = StringField(parsed.value, "login_id");
            config.password = StringField(parsed.value, "password");
            config.display_name = StringField(parsed.value, "display_name");
            config.captcha_id = StringField(parsed.value, "captcha_id");
            config.captcha_code = StringField(parsed.value, "captcha_code");
            config.present = true;
            const protocol::JsonValue* port_value = parsed.value.Find("port");
            if (port_value != nullptr && port_value->IsInt() && port_value->AsInt() > 0 &&
                port_value->AsInt() <= 65535) {
                config.port = static_cast<std::uint16_t>(port_value->AsInt());
            }
            const protocol::JsonValue* register_value = parsed.value.Find("register");
            config.register_account =
                register_value != nullptr && register_value->AsBool();
            return config;
        }
        return config;
    }

    void EnsureSession() {
        if (!session_.access_token.empty()) return;
        const LoginConfig config = LoadLoginConfig();
        if (!config.present) return;
        if (config.host.empty() || config.login_id.empty() || config.password.empty()) return;
        std::string error;
        LoginWithCredentials(config, error);
    }

    void ReplyLogin(const protocol::JsonValue& message, bool ok, const std::string& error) {
        protocol::JsonValue response = protocol::JsonValue::Object();
        response.Set("type", protocol::JsonValue::String("service.login.response"));
        const protocol::JsonValue* id = message.Find("id");
        if (id != nullptr) response.Set("id", *id);
        response.Set("ok", protocol::JsonValue::Bool(ok));
        response.Set("user_key", protocol::JsonValue::String(session_.user_key));
        response.Set("message", protocol::JsonValue::String(error));
        PostToWeb(response.Dump());
    }

    void HandleServiceCaptcha(const protocol::JsonValue& message) {
        const std::string host = StringField(message, "host");
        const protocol::JsonValue* port_value = message.Find("port");
        std::uint16_t port = 9443;
        if (port_value != nullptr && port_value->IsInt() && port_value->AsInt() > 0 &&
            port_value->AsInt() <= 65535) {
            port = static_cast<std::uint16_t>(port_value->AsInt());
        }
        if (host.empty()) {
            ReplyCaptcha(message, false, "", "", "host is required");
            return;
        }
        client::TransportConfig config;
        config.host = host;
        config.port = port;
        config.device_id = "eat-app";
        config.client_version = "windows-app/1";
        client::TcpTransport transport(config);
        std::string transport_error;
        if (!transport.StartForCaptcha(transport_error)) {
            ReplyCaptcha(message, false, "", "", "captcha_connect_failed:" + transport_error);
            return;
        }
        protocol::JsonValue params = protocol::JsonValue::Object();
        protocol::JsonValue data;
        protocol::ErrorCode error_code = protocol::ErrorCode::Ok;
        std::string error_message;
        const bool ok = transport.Request("auth.captcha", params, data, error_code,
                                          error_message, 3000);
        transport.Stop();
        if (!ok) {
            ReplyCaptcha(message, false, "", "",
                         error_message.empty() ? "captcha_failed" : error_message);
            return;
        }
        ReplyCaptcha(message, true, StringField(data, "captcha_id"),
                     StringField(data, "svg"), "");
    }

    void ReplyCaptcha(const protocol::JsonValue& message, bool ok,
                      const std::string& captcha_id, const std::string& svg,
                      const std::string& error) {
        protocol::JsonValue response = protocol::JsonValue::Object();
        response.Set("type", protocol::JsonValue::String("service.captcha.response"));
        const protocol::JsonValue* id = message.Find("id");
        if (id != nullptr) response.Set("id", *id);
        response.Set("ok", protocol::JsonValue::Bool(ok));
        response.Set("captcha_id", protocol::JsonValue::String(captcha_id));
        response.Set("svg", protocol::JsonValue::String(svg));
        response.Set("message", protocol::JsonValue::String(error));
        PostToWeb(response.Dump());
    }

    void HandleServiceLogout(const protocol::JsonValue& message) {
        protocol::JsonValue notice = protocol::JsonValue::Object();
        notice.Set("type", protocol::JsonValue::String("service.logout"));
        ipc_.Send(notice.Dump());

        session_ = Session{};
        SetEnvironmentVariableW(L"EAT_TCP_HOST", nullptr);
        SetEnvironmentVariableW(L"EAT_TCP_PORT", nullptr);
        SetEnvironmentVariableW(L"EAT_USER_KEY", nullptr);
        SetEnvironmentVariableW(L"EAT_SERVER_TOKEN", nullptr);
        SetEnvironmentVariableW(L"EAT_SERVICE_REQUIRED", nullptr);
        DeleteFileW(SessionPath().c_str());

        protocol::JsonValue response = protocol::JsonValue::Object();
        response.Set("type", protocol::JsonValue::String("service.logout.response"));
        const protocol::JsonValue* id = message.Find("id");
        if (id != nullptr) response.Set("id", *id);
        response.Set("ok", protocol::JsonValue::Bool(true));
        PostToWeb(response.Dump());
    }

    void ApplySessionEnvironment() {
        if (session_.access_token.empty()) return;
        SetEnvironmentVariableW(L"EAT_TCP_HOST", Widen(session_.host).c_str());
        SetEnvironmentVariableW(L"EAT_TCP_PORT", std::to_wstring(session_.port).c_str());
        SetEnvironmentVariableW(L"EAT_USER_KEY", Widen(session_.user_key).c_str());
        SetEnvironmentVariableW(L"EAT_SERVER_TOKEN", Widen(session_.access_token).c_str());
        SetEnvironmentVariableW(L"EAT_SERVICE_REQUIRED", L"1");
    }

    void NotifyAgentService() {
        if (session_.access_token.empty()) return;
        protocol::JsonValue configure = protocol::JsonValue::Object();
        configure.Set("type", protocol::JsonValue::String("service.configure"));
        configure.Set("host", protocol::JsonValue::String(session_.host));
        configure.Set("port", protocol::JsonValue::Int(session_.port));
        configure.Set("user_key", protocol::JsonValue::String(session_.user_key));
        configure.Set("access_token", protocol::JsonValue::String(session_.access_token));
        ipc_.Send(configure.Dump());
    }

    std::wstring SessionPath() const {
        return LocalAppDataDir() + L"\\EatApp\\session.json";
    }

    void LoadSession() {
        const std::string raw = ReadUtf8File(SessionPath());
        if (raw.empty()) return;
        const auto parsed = protocol::ParseStrictJson(raw);
        if (!parsed.ok || !parsed.value.IsObject()) return;
        session_.host = StringField(parsed.value, "host");
        session_.user_key = StringField(parsed.value, "user_key");
        session_.access_token = StringField(parsed.value, "access_token");
        const protocol::JsonValue* port_value = parsed.value.Find("port");
        if (port_value != nullptr && port_value->IsInt() && port_value->AsInt() > 0 &&
            port_value->AsInt() <= 65535) {
            session_.port = static_cast<std::uint16_t>(port_value->AsInt());
        }
    }

    void SaveSession() {
        const std::wstring dir = LocalAppDataDir() + L"\\EatApp";
        CreateDirectoryW(dir.c_str(), nullptr);
        protocol::JsonValue root = protocol::JsonValue::Object();
        root.Set("host", protocol::JsonValue::String(session_.host));
        root.Set("port", protocol::JsonValue::Int(session_.port));
        root.Set("user_key", protocol::JsonValue::String(session_.user_key));
        root.Set("access_token", protocol::JsonValue::String(session_.access_token));
        WriteUtf8File(SessionPath(), root.Dump());
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
        LoadSession();
        EnsureSession();
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
        const std::wstring app_dir = LocalAppDataDir() + L"\\EatApp";
        const std::wstring data_dir = app_dir + L"\\data";
        CreateDirectoryW(app_dir.c_str(), nullptr);
        CreateDirectoryW(data_dir.c_str(), nullptr);
        SetEnvironmentVariableW(L"EAT_AGENT_DATA_DIR", data_dir.c_str());
        ApplySessionEnvironment();

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
        NotifyAgentService();
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
                                    RouteWebMessage(utf8);
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


    Session session_;
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
