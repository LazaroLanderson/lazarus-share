// Portable wrapper: unpack bundled binaries, wait for app exit, then remove files.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <filesystem>
#include <string>
#include <vector>
static std::wstring quote(std::wstring value) {
    std::wstring out = L"'";
    for (auto c : value) { out += c; if (c == L'\'') out += c; }
    return out + L"'";
}
static DWORD run(std::wstring command, const std::wstring &directory, bool hidden) {
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, hidden ? CREATE_NO_WINDOW : 0, nullptr, directory.c_str(), &startup, &process)) return 1;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 1; GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); return code;
}
static bool forwardInvite(const std::wstring &tmp, const std::wstring &args) {
    std::wstring link = args;
    while (!link.empty() && (link.front() == L' ' || link.front() == L'\"' || link.front() == L'\'')) link.erase(link.begin());
    while (!link.empty() && (link.back() == L' ' || link.back() == L'\"' || link.back() == L'\'')) link.pop_back();
    if (link.empty()) return false;

    int len = WideCharToMultiByte(CP_UTF8, 0, link.c_str(), (int)link.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return false;
    std::string linkUtf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, link.c_str(), (int)link.size(), &linkUtf8[0], len, nullptr, nullptr);

    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(tmp, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        auto fname = entry.path().filename().wstring();
        if (fname.rfind(L"lazarus-share-", 0) == 0 && fname.rfind(L".endpoint") == fname.size() - 9) {
            HANDLE f = CreateFileW(entry.path().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (f == INVALID_HANDLE_VALUE) continue;
            char buf[256] = {0};
            DWORD read = 0;
            ReadFile(f, buf, sizeof(buf) - 1, &read, nullptr);
            CloseHandle(f);
            std::string name(buf);
            while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ')) name.pop_back();
            if (name.empty()) continue;

            std::wstring pipePath = L"\\\\.\\pipe\\" + std::wstring(name.begin(), name.end());
            HANDLE pipe = CreateFileW(pipePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe != INVALID_HANDLE_VALUE) {
                std::string escaped;
                for (char c : linkUtf8) {
                    if (c == '\"') escaped += "\\\"";
                    else if (c == '\\') escaped += "\\\\";
                    else escaped += c;
                }
                std::string msg = "{\"invite\":\"" + escaped + "\"}\n";
                DWORD written = 0;
                if (WriteFile(pipe, msg.data(), (DWORD)msg.size(), &written, nullptr)) {
                    char resp[32] = {0};
                    DWORD respRead = 0;
                    ReadFile(pipe, resp, sizeof(resp) - 1, &respRead, nullptr);
                    CloseHandle(pipe);
                    if (std::string(resp).find("ok") != std::string::npos) {
                        AllowSetForegroundWindow(ASFW_ANY);
                        return true;
                    }
                } else {
                    CloseHandle(pipe);
                }
            }
        }
    }
    return false;
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    // Join the updater-owned job before creating children, so rollback owns the full tree.
    wchar_t jobName[256];
    if (GetEnvironmentVariableW(L"LAZARUS_UPDATE_JOB", jobName, 256)) {
        HANDLE job = OpenJobObjectW(JOB_OBJECT_ASSIGN_PROCESS, FALSE, jobName);
        if (!job || !AssignProcessToJobObject(job, GetCurrentProcess())) { if (job) CloseHandle(job); return 1; }
        CloseHandle(job);
        SetEnvironmentVariableW(L"LAZARUS_UPDATE_JOB", nullptr);
    }
    SetEnvironmentVariableW(L"LAZARUS_LAUNCHER_PID", std::to_wstring(GetCurrentProcessId()).c_str());
    wchar_t launcher[32768];
    if (!GetModuleFileNameW(nullptr, launcher, 32768)) return 1;
    SetEnvironmentVariableW(L"LAZARUS_LAUNCHER_PATH", launcher);
    wchar_t tmp[32768]; if (!GetTempPathW(32768, tmp)) return 1;
    if (arguments && arguments[0] != L'\0' && forwardInvite(tmp, arguments)) return 0;
    unsigned char random[16];
    if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return 1;
    std::wstring dir = tmp; dir += L"LazarusShare-";
    for (auto c : random) { dir += L"0123456789abcdef"[c >> 4]; dir += L"0123456789abcdef"[c & 15]; }
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;OW)", SDDL_REVISION_1, &sd, nullptr)) return 1;
    SECURITY_ATTRIBUTES security{sizeof(security), sd, FALSE};
    bool created = CreateDirectoryW(dir.c_str(), &security); LocalFree(sd); if (!created) return 1;
    auto cleanup = [&] { std::error_code e; std::filesystem::remove_all(dir, e); };
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(1), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(instance, resource) : nullptr;
    void *data = loaded ? LockResource(loaded) : nullptr;
    if (!data) { cleanup(); return 1; }
    std::wstring archive = dir + L"\\payload.zip";
    HANDLE file = CreateFileW(archive.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD size = SizeofResource(instance, resource), written = 0;
    bool ok = file != INVALID_HANDLE_VALUE && WriteFile(file, data, size, &written, nullptr) && written == size;
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    wchar_t system[32768]; GetSystemDirectoryW(system, 32768);
    std::wstring powershell = std::wstring(system) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    std::wstring extract = L"\"" + powershell + L"\" -NoLogo -NoProfile -NonInteractive -Command \"Expand-Archive -LiteralPath " + quote(archive) + L" -DestinationPath " + quote(dir + L"\\app") + L"\"";
    if (!ok || run(extract, dir, true)) {
        cleanup(); MessageBoxW(nullptr, L"Não foi possível abrir o pacote portátil.", L"Lazarus Share", MB_ICONERROR); return 1;
    }
    std::wstring appdir = dir + L"\\app";
    SetEnvironmentVariableW(L"GST_PLUGIN_SYSTEM_PATH_1_0", L"");
    SetEnvironmentVariableW(L"GST_PLUGIN_PATH_1_0", (appdir + L"\\gstreamer-1.0").c_str());
    if (std::filesystem::exists(appdir + L"\\gst-plugin-scanner.exe")) SetEnvironmentVariableW(L"GST_PLUGIN_SCANNER_1_0", (appdir + L"\\gst-plugin-scanner.exe").c_str());
    SetEnvironmentVariableW(L"GST_DEBUG", L"0");
    DWORD result = run(L"\"" + appdir + L"\\lazarus-share.exe\" " + arguments, appdir, false);
    cleanup(); return int(result);
}
