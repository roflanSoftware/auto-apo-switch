#define UNICODE
#define _UNICODE
#define NOMINMAX
#include "resource.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>
#include <objbase.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <algorithm>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(                                                                                   \
    linker,                                                                                        \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace fs = std::filesystem;
HWND win, list, status, processEdit, pathLabel;
bool paused = false, loading = false;
bool elevated = false;
std::wstring config, settings;
constexpr wchar_t elevatedSettingsKey[] = L"Software\\AutoAPO";
constexpr wchar_t elevatedSettingsValue[] = L"Preferences";
constexpr DWORD maxConfigBytes = 4 * 1024 * 1024;

struct FileHandle
{
    HANDLE value;

    explicit FileHandle(HANDLE handle) : value(handle)
    {
    }

    FileHandle(const FileHandle &) = delete;
    FileHandle &operator=(const FileHandle &) = delete;

    ~FileHandle()
    {
        close();
    }

    void close()
    {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
        value = INVALID_HANDLE_VALUE;
    }
};

// Keep the registered directory and its ancestors in place for the elevated session.
std::vector<HANDLE> protectedDirectories;
std::set<std::string> automatic;
std::vector<std::wstring> processes;

struct Row
{
    size_t line;
    std::string key;
    bool on, locked;
};

std::vector<Row> rows;
std::vector<std::string> lines;
std::string original;

std::wstring wide(const std::string &s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), 0, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string utf8(const std::wstring &w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), 0, 0, 0, 0);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, 0, 0);
    return s;
}

std::string trim(std::string s)
{
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == s.npos)
        return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::wstring lower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), towlower);
    return s;
}

std::wstring text(HWND h)
{
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, 0);
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(n);
    return s;
}

bool isLocalPath(const std::wstring &path)
{
    if (path.size() < 4 || path[1] != L':' || path[2] != L'\\' ||
        path.find_first_of(L":/*?\"<>|", 2) != path.npos)
        return false;
    return GetDriveTypeW(path.substr(0, 3).c_str()) == DRIVE_FIXED;
}

bool isTrustedSid(PSID sid, PSID installer)
{
    return IsWellKnownSid(sid, WinLocalSystemSid) ||
           IsWellKnownSid(sid, WinBuiltinAdministratorsSid) ||
           (installer && EqualSid(sid, installer));
}

bool hasProtectedPermissions(HANDLE handle)
{
    constexpr DWORD writeRights = FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                                  FILE_WRITE_ATTRIBUTES | FILE_DELETE_CHILD | DELETE | WRITE_DAC |
                                  WRITE_OWNER | GENERIC_WRITE | GENERIC_ALL | MAXIMUM_ALLOWED;
    PSID installer = nullptr;
    ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",
                           &installer);
    PSID owner = nullptr;
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    auto result = GetSecurityInfo(handle, SE_FILE_OBJECT,
                                  OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                                  nullptr, &dacl, nullptr, &descriptor);
    bool safe = result == ERROR_SUCCESS && owner && dacl && isTrustedSid(owner, installer);
    for (DWORD index = 0; safe && index < dacl->AceCount; ++index)
    {
        void *entry = nullptr;
        if (!GetAce(dacl, index, &entry))
        {
            safe = false;
            break;
        }
        auto header = static_cast<ACE_HEADER *>(entry);
        if (header->AceFlags & INHERIT_ONLY_ACE)
            continue;
        if (header->AceType == ACCESS_DENIED_ACE_TYPE)
            continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
        {
            safe = false;
            break;
        }
        auto ace = static_cast<ACCESS_ALLOWED_ACE *>(entry);
        if ((ace->Mask & writeRights) && !isTrustedSid(&ace->SidStart, installer))
            safe = false;
    }
    LocalFree(descriptor);
    LocalFree(installer);
    return safe;
}

bool protectElevatedConfigDirectory(const std::wstring &path)
{
    wchar_t programFiles[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT,
                                programFiles)) ||
        !isLocalPath(path))
        return false;

    const std::wstring root = programFiles;
    const auto directory = fs::path(path).parent_path();
    const auto target = directory.wstring();
    if (target.size() <= root.size() || target[root.size()] != L'\\' ||
        CompareStringOrdinal(target.c_str(), static_cast<int>(root.size()), root.c_str(),
                             static_cast<int>(root.size()), TRUE) != CSTR_EQUAL)
        return false;

    for (auto current = directory; current != current.root_path(); current = current.parent_path())
    {
        const auto part = current.filename().wstring();
        if (part.empty() || part.back() == L'.' || part.back() == L' ')
            return false;
    }

    std::vector<fs::path> directories;
    for (auto current = directory; current != current.root_path(); current = current.parent_path())
        directories.push_back(current);
    for (auto it = directories.rbegin(); it != directories.rend(); ++it)
    {
        HANDLE handle =
            CreateFileW(it->c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            return false;
        protectedDirectories.push_back(handle);
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(handle, &info) ||
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
            !hasProtectedPermissions(handle))
            return false;
    }
    return true;
}

bool readConfigHandle(HANDLE handle, std::string &bytes, BY_HANDLE_FILE_INFORMATION &info)
{
    if (handle == INVALID_HANDLE_VALUE || GetFileType(handle) != FILE_TYPE_DISK ||
        !GetFileInformationByHandle(handle, &info) || info.nFileSizeHigh ||
        info.nFileSizeLow > maxConfigBytes || info.nNumberOfLinks != 1 ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
        (elevated && !hasProtectedPermissions(handle)))
        return false;
    bytes.resize(info.nFileSizeLow);
    DWORD read = 0;
    return ReadFile(handle, bytes.data(), info.nFileSizeLow, &read, nullptr) &&
           read == info.nFileSizeLow;
}

HANDLE openConfigForRead()
{
    if (!isLocalPath(config))
        return INVALID_HANDLE_VALUE;
    return CreateFileW(config.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
}

void message(const wchar_t *s)
{
    MessageBoxW(win, s, L"AutoAPO", MB_OK | MB_ICONINFORMATION);
}

void saveSettings()
{
    if (elevated)
    {
        std::wstring prefs = config + L"\n" + text(processEdit) + L"\n";
        for (const auto &rule : automatic)
            prefs += wide(rule) + L"\n";
        if (prefs.size() > maxConfigBytes / sizeof(wchar_t) - 1 ||
            RegSetKeyValueW(HKEY_LOCAL_MACHINE, elevatedSettingsKey, elevatedSettingsValue, REG_SZ,
                            prefs.c_str(),
                            static_cast<DWORD>((prefs.size() + 1) * sizeof(wchar_t))) !=
                ERROR_SUCCESS)
            message(L"Could not save administrator preferences.");
        return;
    }
    std::ofstream f(fs::path(settings), std::ios::binary);
    f << utf8(config) << '\n' << utf8(text(processEdit)) << '\n';
    for (auto &k : automatic)
        f << k << '\n';
}

bool loadConfig()
{
    std::string bytes;
    FileHandle file(openConfigForRead());
    BY_HANDLE_FILE_INFORMATION info{};
    if (!readConfigHandle(file.value, bytes, info))
        return false;
    if (bytes.size() > 2 && (unsigned char)bytes[0] == 255 && (unsigned char)bytes[1] == 254)
        return false;
    original = bytes;
    lines.clear();
    rows.clear();
    size_t start = 0;
    while (start < bytes.size())
    {
        auto end = bytes.find('\n', start);
        if (end == bytes.npos)
            end = bytes.size();
        else
            ++end;
        lines.push_back(bytes.substr(start, end - start));
        start = end;
    }
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string s = lines[i];
        if (i == 0 && s.substr(0, 3) == "\xef\xbb\xbf")
            s.erase(0, 3);
        s = trim(s);
        bool on = true;
        if (!s.empty() && s[0] == '#')
        {
            on = false;
            s = trim(s.substr(1));
        }
        auto colon = s.find(':');
        if (colon == s.npos)
            continue;
        auto cmd = s.substr(0, colon);
        static const std::set<std::string> commands = {
            "VSTPlugin",   "Include", "Preamp",  "Filter", "GraphicEQ",
            "Convolution", "Device",  "Channel", "Copy",   "Delay",
            "Stage",       "If",      "Else",    "EndIf",  "Expression"};
        if (!commands.count(cmd) && cmd.rfind("Filter ", 0) != 0)
            continue;
        bool locked = cmd == "Device" || cmd == "Channel" || cmd == "Stage" || cmd == "If" ||
                      cmd == "Else" || cmd == "EndIf" || cmd == "Expression";
        rows.push_back({i, s, on, locked});
    }
    return true;
}

void render()
{
    loading = true;
    ListView_DeleteAllItems(list);
    std::multiset<std::string> keys;
    for (auto &r : rows)
        keys.insert(r.key);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto &r = rows[i];
        std::wstring label = wide(r.key);
        auto chunk = label.find(L" ChunkData");
        if (chunk != label.npos)
            label.resize(chunk);
        if (label.size() > 160)
            label = label.substr(0, 157) + L"...";
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = (int)i;
        item.pszText = label.data();
        ListView_InsertItem(list, &item);
        ListView_SetCheckState(list, (int)i, r.on);
        std::wstring mode = r.locked                 ? L"Protected"
                            : keys.count(r.key) > 1  ? L"Duplicate — unavailable"
                            : automatic.count(r.key) ? L"☑ Auto"
                                                     : L"☐ Auto";
        ListView_SetItemText(list, (int)i, 1, mode.data());
    }
    SetWindowTextW(pathLabel, config.c_str());
    loading = false;
}

DWORD writeNewFile(const std::wstring &path, const std::string &bytes)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES security{sizeof(security)};
    if (elevated)
    {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)", SDDL_REVISION_1, &descriptor, nullptr))
            return GetLastError();
        security.lpSecurityDescriptor = descriptor;
    }
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, elevated ? &security : nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD error = file == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    LocalFree(descriptor);
    if (error != ERROR_SUCCESS)
        return error;

    DWORD written = 0;
    const auto size = static_cast<DWORD>(bytes.size());
    if (!WriteFile(file, bytes.data(), size, &written, nullptr) || written != size ||
        !FlushFileBuffers(file))
        error = ERROR_WRITE_FAULT;
    CloseHandle(file);
    if (error != ERROR_SUCCESS)
        DeleteFileW(path.c_str());
    return error;
}

bool writeConfig(const std::vector<std::pair<size_t, bool>> &changes)
{
    if (changes.empty())
        return true;
    FileHandle current(openConfigForRead());
    BY_HANDLE_FILE_INFORMATION id{};
    std::string now;
    if (!readConfigHandle(current.value, now, id) || now != original)
    {
        SetWindowTextW(status, L"Configuration changed externally; refreshing before applying.");
        return false;
    }
    auto output = lines;
    for (auto change : changes)
    {
        auto &l = output[change.first];
        size_t pos = (change.first == 0 && l.substr(0, 3) == "\xef\xbb\xbf") ? 3 : 0;
        pos = l.find_first_not_of(" \t", pos);
        if (pos == l.npos)
            continue;
        if (change.second)
        {
            if (l[pos] == '#')
                l.erase(pos, (pos + 1 < l.size() && l[pos + 1] == ' ') ? 2 : 1);
        }
        else if (l[pos] != '#')
            l.insert(pos, "# ");
    }
    std::string result;
    for (auto &l : output)
        result += l;
    if (result.size() > maxConfigBytes)
    {
        SetWindowTextW(status, L"The updated configuration exceeds the 4 MB size limit.");
        return false;
    }
    const auto backup = config + L".AutoAPO.backup";
    const auto backupError = writeNewFile(backup, original);
    if (backupError != ERROR_SUCCESS && backupError != ERROR_FILE_EXISTS &&
        backupError != ERROR_ALREADY_EXISTS)
    {
        SetWindowTextW(status, L"Cannot create backup. Run as administrator if needed.");
        return false;
    }
    GUID unique{};
    wchar_t suffix[40]{};
    if (FAILED(CoCreateGuid(&unique)) || !StringFromGUID2(unique, suffix, ARRAYSIZE(suffix)))
        return false;
    const auto temp = config + L".AutoAPO-" + suffix + L".tmp";
    if (writeNewFile(temp, result) != ERROR_SUCCESS)
    {
        SetWindowTextW(status, L"Cannot create a temporary config. Check folder permissions.");
        return false;
    }

    FileHandle latest(openConfigForRead());
    BY_HANDLE_FILE_INFORMATION latestId{};
    if (!readConfigHandle(latest.value, now, latestId) || now != original ||
        latestId.dwVolumeSerialNumber != id.dwVolumeSerialNumber ||
        latestId.nFileIndexHigh != id.nFileIndexHigh || latestId.nFileIndexLow != id.nFileIndexLow)
    {
        DeleteFileW(temp.c_str());
        SetWindowTextW(status, L"Configuration changed externally; refreshing before applying.");
        return false;
    }

    const auto rollback = config + L".AutoAPO-" + suffix + L".rollback";
    // Release the read locks after the final check so Windows can replace the file.
    latest.close();
    current.close();
    if (!ReplaceFileW(config.c_str(), temp.c_str(), rollback.c_str(), 0, nullptr, nullptr))
    {
        if (GetLastError() == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)
        {
            if (!MoveFileExW(rollback.c_str(), config.c_str(), MOVEFILE_WRITE_THROUGH))
            {
                SetWindowTextW(status, L"Cannot restore the config. Check the AutoAPO backup and "
                                       L"rollback files.");
                return false;
            }
        }
        DeleteFileW(temp.c_str());
        SetWindowTextW(status, L"Cannot replace configuration; check permissions or file locks.");
        return false;
    }
    DeleteFileW(rollback.c_str());
    return true;
}

bool running(bool &ok)
{
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h == INVALID_HANDLE_VALUE)
    {
        ok = false;
        return false;
    }
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    bool found = false;
    ok = Process32FirstW(h, &e) != FALSE;
    if (ok)
        do
        {
            if (std::find(processes.begin(), processes.end(), lower(e.szExeFile)) !=
                processes.end())
            {
                found = true;
                break;
            }
        } while (Process32NextW(h, &e));
    CloseHandle(h);
    return found;
}

void tick()
{
    bool ok;
    bool active = running(ok);
    if (!ok)
    {
        SetWindowTextW(status, L"Cannot read process list; configuration left unchanged.");
        return;
    }
    if (!loadConfig())
    {
        SetWindowTextW(status, elevated ? L"Administrator mode requires a protected, readable "
                                          L"Equalizer APO config.txt."
                                        : L"Select a readable UTF-8 Equalizer APO config.txt.");
        return;
    }
    if (!paused)
    {
        std::vector<std::pair<size_t, bool>> changes;
        for (auto &r : rows)
            if (!r.locked && automatic.count(r.key) && r.on != active &&
                std::count_if(rows.begin(), rows.end(),
                              [&](const Row &x) { return x.key == r.key; }) == 1)
                changes.push_back({r.line, active});
        if (!writeConfig(changes))
        {
            render();
            return;
        }
        if (!changes.empty())
            loadConfig();
    }
    // Redraw only when configuration or rules change, not every polling interval.
    static std::string last;
    static std::set<std::string> lastRules;
    if (last != original || lastRules != automatic ||
        ListView_GetItemCount(list) != (int)rows.size())
    {
        render();
        last = original;
        lastRules = automatic;
    }
    SetWindowTextW(status, paused   ? L"Paused — automatic switching stopped."
                           : active ? L"Watched game running — automatic items ON."
                                    : L"No watched game running — automatic items OFF.");
}

void parseProcesses()
{
    processes.clear();
    std::wstring s = text(processEdit);
    std::replace(s.begin(), s.end(), L';', L',');
    std::wstringstream stream(s);
    std::wstring p;
    while (std::getline(stream, p, L','))
    {
        p = lower(wide(trim(utf8(p))));
        if (!p.empty())
        {
            if (p.find_first_of(L"\\/:*") != p.npos)
            {
                message(L"Enter executable names only, for example TslGame.exe, bf6.exe.");
                continue;
            }
            if (p.size() < 4 || p.substr(p.size() - 4) != L".exe")
                p += L".exe";
            processes.push_back(p);
        }
    }
}

void browse()
{
    if (elevated)
        return;
    wchar_t path[32768]{};
    OPENFILENAMEW o{sizeof(o)};
    o.hwndOwner = win;
    o.lpstrFile = path;
    o.nMaxFile = 32768;
    o.lpstrFilter = L"APO configuration (*.txt)\0*.txt\0\0";
    o.Flags = OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&o))
    {
        if (!isLocalPath(path))
        {
            message(L"Choose a config on a local fixed drive.");
            return;
        }
        config = path;
        automatic.clear();
        saveSettings();
        tick();
    }
}

constexpr int startWithWindowsId = 6;
constexpr wchar_t startupKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t startupValue[] = L"AutoAPO";
constexpr wchar_t startupArgument[] = L"--start-in-tray";

bool isStartupEnabled()
{
    DWORD size = 0;
    return RegGetValueW(HKEY_CURRENT_USER, startupKey, startupValue, RRF_RT_REG_SZ, nullptr,
                        nullptr, &size) == ERROR_SUCCESS;
}

LSTATUS setStartupEnabled(bool enabled)
{
    if (!enabled)
    {
        auto result = RegDeleteKeyValueW(HKEY_CURRENT_USER, startupKey, startupValue);
        return result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND ? ERROR_SUCCESS
                                                                                : result;
    }

    wchar_t executable[32768];
    DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    if (length == 0)
        return GetLastError();
    if (length == ARRAYSIZE(executable))
        return ERROR_INSUFFICIENT_BUFFER;

    std::wstring command = L"\"" + std::wstring(executable, length) + L"\" " + startupArgument;
    // Windows limits Run values to a 260-character command line.
    if (command.size() > 260)
        return ERROR_FILENAME_EXCED_RANGE;
    return RegSetKeyValueW(HKEY_CURRENT_USER, startupKey, startupValue, REG_SZ, command.c_str(),
                           static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m)
    {
    case WM_COMMAND:
        switch (LOWORD(w))
        {
        case 1:
            browse();
            break;
        case 2:
            parseProcesses();
            saveSettings();
            tick();
            break;
        case 3:
            paused = !paused;
            SetWindowTextW(GetDlgItem(h, 3), paused ? L"Resume" : L"Pause");
            tick();
            break;
        case 4:
            tick();
            render();
            break;
        case 5:
            DestroyWindow(h);
            break;
        case startWithWindowsId: {
            if (elevated)
                break;
            bool enabled = IsDlgButtonChecked(h, startWithWindowsId) == BST_CHECKED;
            if (setStartupEnabled(enabled) != ERROR_SUCCESS)
            {
                CheckDlgButton(h, startWithWindowsId,
                               isStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
                message(L"Could not update Windows startup. Check your permissions and make sure "
                        L"AutoAPO is stored in a folder with a short path.");
            }
            break;
        }
        }
        return 0;
    case WM_NOTIFY: {
        auto n = (NMHDR *)l;
        if (n->hwndFrom == list && n->code == NM_CLICK && !loading)
        {
            auto a = (NMITEMACTIVATE *)l;
            if (a->iItem >= 0 && a->iSubItem == 1)
            {
                auto r = rows[a->iItem];
                if (r.locked)
                {
                    message(L"Structural commands are protected. Included "
                            L"files are shown as a single switch.");
                    return 0;
                }
                if (std::count_if(rows.begin(), rows.end(),
                                  [&](const Row &x) { return x.key == r.key; }) != 1)
                    return 0;
                if (automatic.count(r.key))
                    automatic.erase(r.key);
                else
                    automatic.insert(r.key);
                saveSettings();
                tick();
                render();
            }
        }
        if (n->hwndFrom == list && n->code == LVN_ITEMCHANGED && !loading)
        {
            auto a = (NMLISTVIEW *)l;
            if (a->iItem >= 0 && (a->uChanged & LVIF_STATE) &&
                ((a->uOldState ^ a->uNewState) & LVIS_STATEIMAGEMASK))
            {
                auto r = rows[a->iItem];
                if (r.locked || (!paused && automatic.count(r.key)))
                {
                    PostMessageW(h, WM_APP + 2, 0, 0);
                    return 0;
                }
                bool on = ListView_GetCheckState(list, a->iItem) != 0;
                writeConfig({{r.line, on}});
                PostMessageW(h, WM_APP + 2, 0, 0);
            }
        }
        return 0;
    }
    case WM_APP + 2:
        tick();
        render();
        return 0;
    case WM_TIMER:
        tick();
        return 0;
    case WM_APP + 1:
        if (l == WM_LBUTTONUP)
        {
            ShowWindow(h, IsIconic(h) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(h);
        }
        else if (l == WM_RBUTTONUP)
        {
            POINT p{};
            if (!GetCursorPos(&p))
                return 0;

            UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON;
            MONITORINFO monitor{sizeof(monitor)};
            if (GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &monitor))
            {
                const RECT &work = monitor.rcWork;
                // The cursor is often on the taskbar, outside the usable desktop.
                p.x = std::clamp(p.x, work.left, work.right - 1);
                p.y = std::clamp(p.y, work.top, work.bottom - 1);
                if (p.y >= work.top + (work.bottom - work.top) / 2)
                    flags |= TPM_BOTTOMALIGN;
            }

            HMENU menu = CreatePopupMenu();
            if (!menu)
                return 0;
            AppendMenuW(menu, MF_STRING, 4, L"Open AutoAPO");
            AppendMenuW(menu, MF_STRING, 5, L"Exit (keep current APO state)");
            SetForegroundWindow(h);
            int cmd = TrackPopupMenu(menu, flags, p.x, p.y, 0, h, nullptr);
            PostMessageW(h, WM_NULL, 0, 0);
            DestroyMenu(menu);
            if (cmd == 4)
            {
                ShowWindow(h, SW_SHOW);
                SetForegroundWindow(h);
            }
            if (cmd == 5)
                DestroyWindow(h);
        }
        return 0;
    case WM_CLOSE:
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY: {
        NOTIFYICONDATAW n{sizeof(n)};
        n.hWnd = h;
        n.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &n);
        KillTimer(h, 1);
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

int selfTest()
{
    std::string sample = "\xef\xbb\xbf# Русский коментар\r\nInclude: peace.txt\r\n# VSTPlugin: "
                         "Library \"C:\\Plugins\\test.dll\" ChunkData \"abc\"\r\nPreamp: -3 dB\r\n";
    if (elevated)
    {
        if (writeNewFile(config, sample) != ERROR_SUCCESS)
            return 8;
    }
    else
    {
        wchar_t folder[MAX_PATH]{}, file[MAX_PATH]{};
        const DWORD length = GetTempPathW(ARRAYSIZE(folder), folder);
        if (!length || length >= ARRAYSIZE(folder) || !isLocalPath(folder) ||
            !GetTempFileNameW(folder, L"aap", 0, file))
            return 8;
        config = file;
        std::ofstream f(fs::path(config), std::ios::binary);
        f << sample;
    }
    int result = 0;
    if (!loadConfig() || rows.size() != 3 || rows[0].locked || rows[1].on)
        result = 1;
    if (!result &&
        (isLocalPath(L"\\\\server\\share\\config.txt") || isLocalPath(config + L":alternate") ||
         writeNewFile(config, "do not overwrite") != ERROR_FILE_EXISTS || !loadConfig() ||
         original != sample))
        result = 9;
    if (!result && (!writeConfig({{rows[1].line, true}}) || !loadConfig() || !rows[1].on))
        result = 2;
    if (!result && (!writeConfig({{rows[1].line, false}}) || !loadConfig() || original != sample))
        result = 3;
    if (!result && (!writeConfig({{rows[0].line, false}}) || !loadConfig() || rows[0].on))
        result = 6;
    if (!result && (!writeConfig({{rows[0].line, true}}) || !loadConfig() || original != sample))
        result = 7;
    {
        std::ofstream f(fs::path(config), std::ios::binary | std::ios::app);
        f << "# external change\r\n";
    }
    if (!result && writeConfig({{2, true}}))
        result = 4;
    std::string backup;
    std::ifstream backupFile(fs::path(config + L".AutoAPO.backup"), std::ios::binary);
    backup.assign(std::istreambuf_iterator<char>(backupFile), {});
    backupFile.close();
    if (backup != sample)
        result = 5;
    DeleteFileW(config.c_str());
    DeleteFileW((config + L".AutoAPO.backup").c_str());
    return result;
}

int elevatedSelfTest()
{
    wchar_t programFiles[MAX_PATH]{}, suffix[40]{};
    GUID id{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT,
                                programFiles)) ||
        FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, suffix, ARRAYSIZE(suffix)))
        return 10;

    const auto directory = std::wstring(programFiles) + L"\\AutoAPO-self-test-" + suffix;
    if (!CreateDirectoryW(directory.c_str(), nullptr))
        return 11;
    config = directory + L"\\config.txt";
    const int result = protectElevatedConfigDirectory(config) ? selfTest() : 12;
    for (HANDLE handle : protectedDirectories)
        CloseHandle(handle);
    if (!RemoveDirectoryW(directory.c_str()) && !result)
        return 13;
    return result;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR args, int)
{
    const std::wstring arguments(args);
    const bool startInTray = arguments == startupArgument;
    HANDLE processToken = nullptr;
    TOKEN_ELEVATION elevation{};
    DWORD elevationSize = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &processToken))
        return 1;
    FileHandle token(processToken);
    const BOOL queried = GetTokenInformation(processToken, TokenElevation, &elevation,
                                             sizeof(elevation), &elevationSize);
    if (!queried)
        return 1;
    elevated = elevation.TokenIsElevated != 0;
    token.close();
    if (arguments == L"--self-test")
        return elevated ? elevatedSelfTest() : selfTest();

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\AutoAPO.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (!startInTray)
            MessageBoxW(0, L"AutoAPO is already running. Open it from the system tray.", L"AutoAPO",
                        MB_OK);
        return 0;
    }

    INITCOMMONCONTROLSEX c{sizeof(c), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&c);
    SetProcessDPIAware();
    wchar_t cp[32768]{};
    DWORD size = sizeof(cp);
    const bool registered =
        RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\EqualizerAPO", L"ConfigPath",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, cp, &size) == ERROR_SUCCESS &&
        cp[0] != L'\0';
    if (registered)
        config = (fs::path(cp) / L"config.txt").wstring();
    else
        config = L"C:\\Program Files\\EqualizerAPO\\config\\config.txt";

    if (elevated && (!registered || !protectElevatedConfigDirectory(config)))
    {
        message(L"Administrator mode requires Equalizer APO's config in a protected Program "
                L"Files folder. For another location, run AutoAPO normally with write access "
                L"to that config.");
        return 1;
    }

    std::string savedPreferences;
    if (elevated)
    {
        DWORD bytes = 0;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, elevatedSettingsKey, elevatedSettingsValue,
                         RRF_RT_REG_SZ, nullptr, nullptr, &bytes) == ERROR_SUCCESS &&
            bytes >= sizeof(wchar_t) && bytes <= maxConfigBytes && bytes % sizeof(wchar_t) == 0)
        {
            std::wstring prefs(bytes / sizeof(wchar_t), L'\0');
            if (RegGetValueW(HKEY_LOCAL_MACHINE, elevatedSettingsKey, elevatedSettingsValue,
                             RRF_RT_REG_SZ, nullptr, prefs.data(), &bytes) == ERROR_SUCCESS)
                savedPreferences = utf8(prefs.c_str());
        }
    }
    else
    {
        wchar_t local[MAX_PATH]{};
        if (FAILED(
                SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local)))
            return 1;
        const auto settingsDirectory = fs::path(local) / L"AutoAPO";
        std::error_code error;
        fs::create_directories(settingsDirectory, error);
        settings = (settingsDirectory / L"settings.txt").wstring();
        std::ifstream prefs(fs::path(settings), std::ios::binary | std::ios::ate);
        if (prefs && prefs.tellg() <= maxConfigBytes)
        {
            prefs.seekg(0);
            savedPreferences.assign(std::istreambuf_iterator<char>(prefs), {});
        }
    }
    std::wstring watched = L"TslGame.exe";
    std::istringstream prefs(savedPreferences);
    std::string s;
    if (std::getline(prefs, s) && !s.empty() &&
        (elevated ? wide(s) == config : isLocalPath(wide(s))))
    {
        if (!elevated)
            config = wide(s);
        if (std::getline(prefs, s))
            watched = wide(s);
        while (std::getline(prefs, s))
            if (!s.empty())
                automatic.insert(s);
    }
    WNDCLASSEXW wc{sizeof(wc)};
    wc.hInstance = inst;
    wc.lpszClassName = L"AutoAPO.Window";
    wc.lpfnWndProc = proc;
    wc.hCursor = LoadCursorW(0, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_AUTOAPO));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_AUTOAPO), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);
    win = CreateWindowW(wc.lpszClassName, L"AutoAPO",
                        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                        CW_USEDEFAULT, 940, 632, 0, 0, inst, 0);
    auto control = [&](const wchar_t *cls, const wchar_t *title, DWORD style, int x, int y,
                       int width, int height, int id) {
        HWND v = CreateWindowW(cls, title, WS_CHILD | WS_VISIBLE | style, x, y, width, height, win,
                               (HMENU)(INT_PTR)id, inst, 0);
        SendMessageW(v, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        return v;
    };
    pathLabel = control(L"STATIC", config.c_str(), 0, 16, 16, 760, 25, 0);
    control(L"BUTTON", L"Choose config…", WS_TABSTOP | (elevated ? WS_DISABLED : 0), 790, 12, 120,
            30, 1);
    control(L"STATIC",
            L"Watched processes (comma separated). Any running process activates Auto items:", 0,
            16, 55, 890, 22, 0);
    processEdit = control(L"EDIT", watched.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 16, 82,
                          750, 28, 0);
    control(L"BUTTON", L"Save processes", WS_TABSTOP, 780, 80, 130, 30, 2);
    control(L"STATIC",
            L"Left checkbox: current state. Right checkbox: automatically ON in game / OFF outside "
            L"game.",
            0, 16, 123, 890, 23, 0);
    list = control(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | WS_BORDER | WS_TABSTOP, 16, 152,
                   894, 330, 0);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT |
                                                LVS_EX_DOUBLEBUFFER);
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 665;
    col.pszText = (LPWSTR)L"Enabled / Equalizer APO item";
    ListView_InsertColumn(list, 0, &col);
    col.cx = 205;
    col.pszText = (LPWSTR)L"Automatic switching (click)";
    ListView_InsertColumn(list, 1, &col);
    status = control(L"STATIC", L"", 0, 16, 492, 890, 22, 0);
    control(L"BUTTON", L"Pause", WS_TABSTOP, 16, 525, 100, 28, 3);
    control(L"BUTTON", L"Refresh", WS_TABSTOP, 130, 525, 100, 28, 4);
    control(L"BUTTON", L"Start with Windows",
            BS_AUTOCHECKBOX | WS_TABSTOP | (elevated ? WS_DISABLED : 0), 245, 525, 155, 28,
            startWithWindowsId);
    CheckDlgButton(win, startWithWindowsId, isStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
    control(L"STATIC", L"Closing the window keeps AutoAPO in the tray.", 0, 420, 531, 475, 25, 0);
    NOTIFYICONDATAW tray{sizeof(tray)};
    tray.hWnd = win;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = WM_APP + 1;
    tray.hIcon = wc.hIconSm ? wc.hIconSm : wc.hIcon;
    lstrcpyW(tray.szTip, L"AutoAPO — click to open");
    const bool trayAvailable = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE;
    parseProcesses();
    tick();
    render();
    SetTimer(win, 1, 2000, 0);
    if (!startInTray || !trayAvailable)
        ShowWindow(win, SW_SHOW);
    MSG msg;
    while (GetMessageW(&msg, 0, 0, 0) > 0)
    {
        if (!IsDialogMessageW(win, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    CloseHandle(mutex);
    for (HANDLE directory : protectedDirectories)
        CloseHandle(directory);
    return 0;
}
