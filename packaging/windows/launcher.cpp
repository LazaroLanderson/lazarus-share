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
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    wchar_t tmp[32768]; if (!GetTempPathW(32768, tmp)) return 1;
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
