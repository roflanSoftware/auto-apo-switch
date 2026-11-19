#define UNICODE
#define _UNICODE
#define NOMINMAX
#include "resource.h"
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <aclapi.h>
#include <sddl.h>
#include <objbase.h>
#include <exdisp.h>
#include <shldisp.h>
#include <shlguid.h>
#include <wrl/client.h>
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
#pragma comment(lib, "oleaut32.lib")
#pragma comment(                                                                                   \
    linker,                                                                                        \
    "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
namespace fs = std::filesystem;
HWND win, list, status, processEdit, pathLabel;
// Tooltip over blocked Enabled checkboxes, and the row it currently points at (-1: none).
HWND checkboxTip = nullptr;
int checkboxTipRow = -1;
bool loading = false;
bool elevated = false;
std::wstring config, settings;
constexpr wchar_t elevatedSettingsKey[] = L"Software\\AutoAPO";
constexpr wchar_t elevatedSettingsValue[] = L"Preferences";
constexpr DWORD maxConfigBytes = 4 * 1024 * 1024;

// ---- Translations -------------------------------------------------------------------------
enum Language
{
    English,
    Russian,
    Ukrainian,
    LanguageCount
};

enum class T
{
    AdminUnavailable,
    AlreadyRunning,
    SaveAdminPrefsFailed,
    Protected,
    Duplicate,
    AutoOn,
    AutoOff,
    ChangedExternally,
    TooLarge,
    BackupFailed,
    TempFailed,
    RestoreFailed,
    ReplaceFailed,
    ProcessListFailed,
    AdminConfigUnreadable,
    ConfigUnreadable,
    GameRunning,
    NoGame,
    ExeNamesOnly,
    FileFilterName,
    LocalDriveOnly,
    StartupFailed,
    StructuralProtected,
    TrayOpen,
    TrayExit,
    ChooseConfig,
    WatchedLabel,
    SaveProcesses,
    Hint,
    ColumnItem,
    ColumnAuto,
    Refresh,
    StartWithWindows,
    TrayNote,
    TrayTip,
    Stop,
    AutoCheckboxTip,
    ProtectedCheckboxTip,
    Count
};

// Columns: English, Russian, Ukrainian. Rows follow the order of enum T.
const wchar_t *const translations[][LanguageCount] = {
    {L"AutoAPO was started as administrator, but Equalizer APO's config is not in a protected "
     L"Program Files folder, so administrator mode is not available. Start AutoAPO normally, "
     L"without \"Run as administrator\".",
     L"AutoAPO запущен от имени администратора, но конфигурация Equalizer APO находится не в "
     L"защищённой папке Program Files, поэтому режим администратора недоступен. Запустите AutoAPO "
     L"обычным способом, без «Запуск от имени администратора».",
     L"AutoAPO запущено від імені адміністратора, але конфігурація Equalizer APO розташована не в "
     L"захищеній папці Program Files, тому режим адміністратора недоступний. Запустіть AutoAPO "
     L"звичайним способом, без «Запуск від імені адміністратора»."},
    {L"AutoAPO is already running. Open it from the system tray.",
     L"AutoAPO уже запущен. Откройте его из области уведомлений.",
     L"AutoAPO вже запущено. Відкрийте його з області сповіщень."},
    {L"Could not save administrator preferences.",
     L"Не удалось сохранить настройки режима администратора.",
     L"Не вдалося зберегти налаштування режиму адміністратора."},
    {L"Protected", L"Защищено", L"Захищено"},
    {L"Duplicate — unavailable", L"Дубликат — недоступно", L"Дублікат — недоступно"},
    {L"☑ Auto", L"☑ Авто", L"☑ Авто"},
    {L"☐ Auto", L"☐ Авто", L"☐ Авто"},
    {L"Configuration changed externally; refreshing before applying.",
     L"Конфигурация изменена извне; обновление перед применением.",
     L"Конфігурацію змінено ззовні; оновлення перед застосуванням."},
    {L"The updated configuration exceeds the 4 MB size limit.",
     L"Обновлённая конфигурация превышает лимит 4 МБ.",
     L"Оновлена конфігурація перевищує ліміт 4 МБ."},
    {L"Cannot create backup. Run as administrator if needed.",
     L"Не удалось создать резервную копию. При необходимости запустите от имени администратора.",
     L"Не вдалося створити резервну копію. За потреби запустіть від імені адміністратора."},
    {L"Cannot create a temporary config. Check folder permissions.",
     L"Не удалось создать временный файл конфигурации. Проверьте права доступа к папке.",
     L"Не вдалося створити тимчасовий файл конфігурації. Перевірте права доступу до папки."},
    {L"Cannot restore the config. Check the AutoAPO backup and rollback files.",
     L"Не удалось восстановить конфигурацию. Проверьте файлы резервной копии и отката AutoAPO.",
     L"Не вдалося відновити конфігурацію. Перевірте файли резервної копії та відкату AutoAPO."},
    {L"Cannot replace configuration; check permissions or file locks.",
     L"Не удалось заменить конфигурацию; проверьте права доступа или блокировку файла.",
     L"Не вдалося замінити конфігурацію; перевірте права доступу або блокування файлу."},
    {L"Cannot read process list; configuration left unchanged.",
     L"Не удалось получить список процессов; конфигурация не изменена.",
     L"Не вдалося отримати список процесів; конфігурацію не змінено."},
    {L"Administrator mode requires a protected, readable Equalizer APO config.txt.",
     L"Режиму администратора нужен защищённый и доступный для чтения config.txt Equalizer APO.",
     L"Режиму адміністратора потрібен захищений і доступний для читання config.txt Equalizer APO."},
    {L"Select a readable UTF-8 Equalizer APO config.txt.",
     L"Выберите доступный для чтения config.txt Equalizer APO в кодировке UTF-8.",
     L"Виберіть доступний для читання config.txt Equalizer APO у кодуванні UTF-8."},
    {L"Watched game running — automatic items ON.",
     L"Отслеживаемая игра запущена — автоматические элементы ВКЛ.",
     L"Відстежувану гру запущено — автоматичні елементи УВІМК."},
    {L"No watched game running — automatic items OFF.",
     L"Отслеживаемые игры не запущены — автоматические элементы ВЫКЛ.",
     L"Відстежувані ігри не запущено — автоматичні елементи ВИМК."},
    {L"Enter executable names only, for example TslGame.exe, bf6.exe.",
     L"Введите только имена исполняемых файлов, например TslGame.exe, bf6.exe.",
     L"Введіть лише імена виконуваних файлів, наприклад TslGame.exe, bf6.exe."},
    {L"APO configuration", L"Конфигурация APO", L"Конфігурація APO"},
    {L"Choose a config on a local fixed drive.",
     L"Выберите конфигурацию на локальном несъёмном диске.",
     L"Виберіть конфігурацію на локальному незнімному диску."},
    {L"Could not update Windows startup. Check your permissions and make sure AutoAPO is stored "
     L"in a folder with a short path.",
     L"Не удалось изменить автозапуск Windows. Проверьте права доступа и убедитесь, что AutoAPO "
     L"находится в папке с коротким путём.",
     L"Не вдалося змінити автозапуск Windows. Перевірте права доступу й переконайтеся, що AutoAPO "
     L"розташовано в папці з коротким шляхом."},
    {L"Structural commands are protected. Included files are shown as a single switch.",
     L"Структурные команды защищены. Подключённые файлы показаны как один переключатель.",
     L"Структурні команди захищені. Підключені файли показано як один перемикач."},
    {L"Open AutoAPO", L"Открыть AutoAPO", L"Відкрити AutoAPO"},
    {L"Exit (keep current APO state)", L"Выход (сохранить текущее состояние APO)",
     L"Вихід (зберегти поточний стан APO)"},
    {L"Choose config…", L"Выбрать конфиг…", L"Вибрати конфіг…"},
    {L"Watched processes (comma separated). Any running process activates Auto items:",
     L"Отслеживаемые процессы (через запятую). Любой запущенный процесс включает элементы «Авто»:",
     L"Відстежувані процеси (через кому). Будь-який запущений процес вмикає елементи «Авто»:"},
    {L"Save processes", L"Сохранить процессы", L"Зберегти процеси"},
    {L"Left checkbox: current state. Right checkbox: automatically ON in game / OFF outside game.",
     L"Левый флажок: текущее состояние. Правый флажок: автоматически ВКЛ в игре / ВЫКЛ вне игры.",
     L"Лівий прапорець: поточний стан. Правий прапорець: автоматично УВІМК у грі / ВИМК поза "
     L"грою."},
    {L"Enabled / Equalizer APO item", L"Вкл. / элемент Equalizer APO",
     L"Увімк. / елемент Equalizer APO"},
    {L"Automatic switching (click)", L"Автопереключение (щёлкните)",
     L"Автоперемикання (клацніть)"},
    {L"Refresh config", L"Обновить конфиг", L"Оновити конфіг"},
    {L"Start with Windows", L"Запускать с Windows", L"Запускати з Windows"},
    {L"Closing the window keeps AutoAPO in the tray. If you want to stop AutoAPO entirely, click "
     L"the \"Stop AutoAPO\" button.",
     L"При закрытии окна приложение остаётся запущенным. Чтобы полностью остановить AutoAPO, "
     L"нажмите кнопку «Остановить AutoAPO».",
     L"Після закриття вікна програма залишається запущеною. Щоб повністю зупинити AutoAPO, "
     L"натисніть кнопку «Зупинити AutoAPO»."},
    {L"AutoAPO — click to open", L"AutoAPO — нажмите, чтобы открыть",
     L"AutoAPO — натисніть, щоб відкрити"},
    {L"Stop AutoAPO", L"Остановить AutoAPO", L"Зупинити AutoAPO"},
    {L"These config items are controlled by AutoAPO. If you want to select them, turn Auto off.",
     L"Эти элементы конфигурации управляются AutoAPO. Чтобы выбрать их вручную, отключите «Авто».",
     L"Цими елементами конфігурації керує AutoAPO. Щоб вибрати їх вручну, вимкніть «Авто»."},
    {L"Structural commands are protected and cannot be switched.",
     L"Структурные команды защищены, их нельзя переключать.",
     L"Структурні команди захищені, їх не можна перемикати."},
};
static_assert(ARRAYSIZE(translations) == static_cast<size_t>(T::Count),
              "Every translatable string needs a row in translations");

// Language names are shown in their own language in the selector.
const wchar_t *const languageNames[LanguageCount] = {L"English", L"Русский", L"Українська"};
const wchar_t *const languageCodes[LanguageCount] = {L"en", L"ru", L"uk"};
constexpr wchar_t languageKey[] = L"Software\\AutoAPO";
constexpr wchar_t languageValue[] = L"Language";
Language language = English;

const wchar_t *tr(T id)
{
    return translations[static_cast<int>(id)][language];
}

// Uses the saved choice, otherwise the Windows display language.
Language detectLanguage()
{
    wchar_t code[16]{};
    DWORD size = sizeof(code);
    if (RegGetValueW(HKEY_CURRENT_USER, languageKey, languageValue, RRF_RT_REG_SZ, nullptr, code,
                     &size) == ERROR_SUCCESS)
        for (int i = 0; i < LanguageCount; ++i)
            if (lstrcmpiW(code, languageCodes[i]) == 0)
                return static_cast<Language>(i);
    switch (PRIMARYLANGID(GetUserDefaultUILanguage()))
    {
    case LANG_RUSSIAN:
        return Russian;
    case LANG_UKRAINIAN:
        return Ukrainian;
    default:
        return English;
    }
}

void saveLanguage()
{
    const wchar_t *code = languageCodes[language];
    RegSetKeyValueW(HKEY_CURRENT_USER, languageKey, languageValue, REG_SZ, code,
                    static_cast<DWORD>((lstrlenW(code) + 1) * sizeof(wchar_t)));
}

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

// Identity of a command for Auto rules. Equalizer APO's Configuration Editor may write a
// plugin's parameters in a different order when it saves, so a VSTPlugin command is reduced
// to its name/value pairs sorted by name, each quoted the same way. Other commands keep their
// text. The result is itself a valid command, so applying this twice gives the same key.
std::wstring wide(const std::string &s);
std::string utf8(const std::wstring &w);
std::wstring lower(std::wstring s);

std::string canonicalKey(const std::string &command)
{
    static const std::string prefix = "VSTPlugin:";
    if (command.compare(0, prefix.size(), prefix) != 0)
        return command;
    std::vector<std::string> tokens;
    size_t i = prefix.size();
    while (true)
    {
        i = command.find_first_not_of(" \t", i);
        if (i == command.npos)
            break;
        if (command[i] == '"')
        {
            const size_t end = command.find('"', i + 1);
            if (end == command.npos)
                return command;
            tokens.push_back(command.substr(i + 1, end - i - 1));
            i = end + 1;
        }
        else
        {
            const size_t end = command.find_first_of(" \t", i);
            tokens.push_back(command.substr(i, end == command.npos ? command.npos : end - i));
            i = end;
            if (i == command.npos)
                break;
        }
    }
    if (tokens.empty() || tokens.size() % 2 != 0)
        return command;
    std::vector<std::pair<std::string, std::string>> pairs;
    for (size_t t = 0; t < tokens.size(); t += 2)
    {
        std::string value = tokens[t + 1];
        // The editor and the installer may write the same DLL as "..\config\vst\x.dll" or as a
        // full path. Equalizer APO resolves relative paths from the config file's folder, so do
        // the same and ignore letter case (Windows paths are case-insensitive).
        if (_stricmp(tokens[t].c_str(), "Library") == 0 && !value.empty())
        {
            fs::path library(wide(value));
            if (library.is_relative() && !config.empty())
                library = fs::path(config).parent_path() / library;
            value = utf8(lower(library.lexically_normal().wstring()));
        }
        pairs.push_back({tokens[t], value});
    }
    std::sort(pairs.begin(), pairs.end());
    std::string key = prefix;
    for (const auto &pair : pairs)
        key += " \"" + pair.first + "\" \"" + pair.second + "\"";
    return key;
}

struct Row
{
    size_t line;
    std::string text; // The command as written in config.txt, shown in the list.
    std::string key;  // canonicalKey(text), used for Auto rules and duplicate checks.
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
            message(tr(T::SaveAdminPrefsFailed));
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
        rows.push_back({i, s, canonicalKey(s), on, locked});
    }
    return true;
}

void render()
{
    loading = true;
    // Rows may change, so the tooltip is re-evaluated on the next mouse move.
    checkboxTipRow = -2;
    ListView_DeleteAllItems(list);
    std::multiset<std::string> keys;
    for (auto &r : rows)
        keys.insert(r.key);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto &r = rows[i];
        std::wstring label = wide(r.text);
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
        std::wstring mode = r.locked                 ? tr(T::Protected)
                            : keys.count(r.key) > 1  ? tr(T::Duplicate)
                            : automatic.count(r.key) ? tr(T::AutoOn)
                                                     : tr(T::AutoOff);
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
        SetWindowTextW(status, tr(T::ChangedExternally));
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
        SetWindowTextW(status, tr(T::TooLarge));
        return false;
    }
    const auto backup = config + L".AutoAPO.backup";
    const auto backupError = writeNewFile(backup, original);
    if (backupError != ERROR_SUCCESS && backupError != ERROR_FILE_EXISTS &&
        backupError != ERROR_ALREADY_EXISTS)
    {
        SetWindowTextW(status, tr(T::BackupFailed));
        return false;
    }
    GUID unique{};
    wchar_t suffix[40]{};
    if (FAILED(CoCreateGuid(&unique)) || !StringFromGUID2(unique, suffix, ARRAYSIZE(suffix)))
        return false;
    const auto temp = config + L".AutoAPO-" + suffix + L".tmp";
    if (writeNewFile(temp, result) != ERROR_SUCCESS)
    {
        SetWindowTextW(status, tr(T::TempFailed));
        return false;
    }

    FileHandle latest(openConfigForRead());
    BY_HANDLE_FILE_INFORMATION latestId{};
    if (!readConfigHandle(latest.value, now, latestId) || now != original ||
        latestId.dwVolumeSerialNumber != id.dwVolumeSerialNumber ||
        latestId.nFileIndexHigh != id.nFileIndexHigh || latestId.nFileIndexLow != id.nFileIndexLow)
    {
        DeleteFileW(temp.c_str());
        SetWindowTextW(status, tr(T::ChangedExternally));
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
                SetWindowTextW(status, tr(T::RestoreFailed));
                return false;
            }
        }
        DeleteFileW(temp.c_str());
        SetWindowTextW(status, tr(T::ReplaceFailed));
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
        SetWindowTextW(status, tr(T::ProcessListFailed));
        return;
    }
    if (!loadConfig())
    {
        SetWindowTextW(status, elevated ? tr(T::AdminConfigUnreadable) : tr(T::ConfigUnreadable));
        return;
    }
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
    SetWindowTextW(status, active ? tr(T::GameRunning) : tr(T::NoGame));
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
                message(tr(T::ExeNamesOnly));
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
    std::wstring filter = std::wstring(tr(T::FileFilterName)) + L" (*.txt)";
    filter.push_back(L'\0');
    filter += L"*.txt";
    filter.push_back(L'\0');
    filter.push_back(L'\0');
    o.lpstrFilter = filter.c_str();
    o.Flags = OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&o))
    {
        if (!isLocalPath(path))
        {
            message(tr(T::LocalDriveOnly));
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

constexpr int watchedLabelId = 10;
constexpr int hintLabelId = 11;
constexpr int trayNoteId = 12;
constexpr int languageId = 13;
// Also the tray menu's Exit command.
constexpr int stopId = 5;

constexpr COLORREF autoRowColor = RGB(214, 245, 214);

// Rows the watcher controls (shown with a light green background).
bool isAutoRow(int index)
{
    if (index < 0 || index >= static_cast<int>(rows.size()))
        return false;
    const Row &r = rows[index];
    return !r.locked && automatic.count(r.key) &&
           std::count_if(rows.begin(), rows.end(),
                         [&](const Row &x) { return x.key == r.key; }) == 1;
}

// Rows whose Enabled checkbox cannot be changed by hand (the list reverts such clicks).
bool isCheckboxBlocked(int index)
{
    if (index < 0 || index >= static_cast<int>(rows.size()))
        return false;
    return rows[index].locked || automatic.count(rows[index].key) != 0;
}

// Returns the row whose blocked Enabled checkbox is under the list-client point, or -1.
// The checkbox area (left of the first column's text) is stored in box.
int blockedCheckboxAt(HWND h, POINT pt, RECT &box)
{
    LVHITTESTINFO hit{};
    hit.pt = pt;
    RECT label{};
    // Full-row selection reports the state-icon flag for other columns too, so also require
    // the pointer to be left of the first column's text, where the checkbox is drawn.
    if (ListView_SubItemHitTest(h, &hit) < 0 || hit.iSubItem != 0 ||
        !(hit.flags & LVHT_ONITEMSTATEICON) || !isCheckboxBlocked(hit.iItem) ||
        !ListView_GetItemRect(h, hit.iItem, &box, LVIR_BOUNDS) ||
        !ListView_GetItemRect(h, hit.iItem, &label, LVIR_LABEL) || pt.x >= label.left)
        return -1;
    box.right = label.left;
    return hit.iItem;
}

// Points the tooltip at the given row's checkbox, or hides it when row is -1.
void updateCheckboxTip(int row, const RECT &box)
{
    if (!checkboxTip || row == checkboxTipRow)
        return;
    checkboxTipRow = row;
    SendMessageW(checkboxTip, TTM_POP, 0, 0);
    TOOLINFOW tool{sizeof(tool)};
    tool.hwnd = list;
    tool.uId = 1;
    tool.rect = row >= 0 ? box : RECT{};
    SendMessageW(checkboxTip, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    if (row >= 0)
    {
        tool.lpszText = const_cast<LPWSTR>(rows[row].locked ? tr(T::ProtectedCheckboxTip)
                                                            : tr(T::AutoCheckboxTip));
        SendMessageW(checkboxTip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
}

void createCheckboxTip(HINSTANCE inst)
{
    checkboxTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                  WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT,
                                  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, win, nullptr, inst,
                                  nullptr);
    if (!checkboxTip)
        return;
    TOOLINFOW tool{sizeof(tool)};
    tool.uFlags = TTF_SUBCLASS;
    tool.hwnd = list;
    tool.uId = 1;
    tool.lpszText = const_cast<LPWSTR>(L"");
    SendMessageW(checkboxTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool));
    SendMessageW(checkboxTip, TTM_SETMAXTIPWIDTH, 0, 320);
}

// Shows the "not allowed" cursor and a tooltip over checkboxes that cannot be changed.
LRESULT CALLBACK listSubclass(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR)
{
    if (m == WM_SETCURSOR && LOWORD(l) == HTCLIENT)
    {
        POINT pt{};
        RECT box{};
        if (GetCursorPos(&pt) && ScreenToClient(h, &pt) && blockedCheckboxAt(h, pt, box) >= 0)
        {
            SetCursor(LoadCursorW(nullptr, IDC_NO));
            return TRUE;
        }
    }
    if (m == WM_MOUSEMOVE)
    {
        RECT box{};
        const int row = blockedCheckboxAt(h, {GET_X_LPARAM(l), GET_Y_LPARAM(l)}, box);
        updateCheckboxTip(row, box);
    }
    return DefSubclassProc(h, m, w, l);
}

// The Stop button is owner-drawn because themed buttons ignore custom background colors.
void drawStopButton(const DRAWITEMSTRUCT &item)
{
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    HBRUSH fill = CreateSolidBrush(pressed ? RGB(160, 24, 24) : RGB(200, 32, 32));
    FillRect(item.hDC, &item.rcItem, fill);
    DeleteObject(fill);
    FrameRect(item.hDC, &item.rcItem, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    wchar_t label[64]{};
    GetWindowTextW(item.hwndItem, label, ARRAYSIZE(label));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, RGB(255, 255, 255));
    RECT text = item.rcItem;
    DrawTextW(item.hDC, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item.itemState & ODS_FOCUS)
    {
        RECT focus = item.rcItem;
        InflateRect(&focus, -3, -3);
        DrawFocusRect(item.hDC, &focus);
    }
}

void setColumnText(int column, T id)
{
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT;
    col.pszText = const_cast<LPWSTR>(tr(id));
    ListView_SetColumn(list, column, &col);
}

void setTrayTip()
{
    NOTIFYICONDATAW n{sizeof(n)};
    n.hWnd = win;
    n.uID = 1;
    n.uFlags = NIF_TIP;
    lstrcpynW(n.szTip, tr(T::TrayTip), ARRAYSIZE(n.szTip));
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

// Re-labels every control after the language changes.
void applyLanguage()
{
    SetDlgItemTextW(win, 1, tr(T::ChooseConfig));
    SetDlgItemTextW(win, watchedLabelId, tr(T::WatchedLabel));
    SetDlgItemTextW(win, 2, tr(T::SaveProcesses));
    SetDlgItemTextW(win, hintLabelId, tr(T::Hint));
    SetDlgItemTextW(win, 4, tr(T::Refresh));
    SetDlgItemTextW(win, stopId, tr(T::Stop));
    SetDlgItemTextW(win, startWithWindowsId, tr(T::StartWithWindows));
    SetDlgItemTextW(win, trayNoteId, tr(T::TrayNote));
    setColumnText(0, T::ColumnItem);
    setColumnText(1, T::ColumnAuto);
    setTrayTip();
    render();
    tick();
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
        case 4:
            tick();
            render();
            break;
        case stopId:
            DestroyWindow(h);
            break;
        case languageId:
            if (HIWORD(w) == CBN_SELCHANGE)
            {
                const auto selected = SendDlgItemMessageW(h, languageId, CB_GETCURSEL, 0, 0);
                if (selected >= 0 && selected < LanguageCount && selected != language)
                {
                    language = static_cast<Language>(selected);
                    saveLanguage();
                    applyLanguage();
                }
            }
            break;
        case startWithWindowsId: {
            if (elevated)
                break;
            bool enabled = IsDlgButtonChecked(h, startWithWindowsId) == BST_CHECKED;
            if (setStartupEnabled(enabled) != ERROR_SUCCESS)
            {
                CheckDlgButton(h, startWithWindowsId,
                               isStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
                message(tr(T::StartupFailed));
            }
            break;
        }
        }
        return 0;
    case WM_NOTIFY: {
        auto n = (NMHDR *)l;
        if (n->hwndFrom == list && n->code == NM_CUSTOMDRAW)
        {
            auto draw = reinterpret_cast<NMLVCUSTOMDRAW *>(l);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            const int item = static_cast<int>(draw->nmcd.dwItemSpec);
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT && isAutoRow(item))
            {
                draw->clrTextBk = autoRowColor;
                return CDRF_NOTIFYPOSTPAINT;
            }
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT && isAutoRow(item))
            {
                // clrTextBk only covers the text cells. Fill the checkbox area green after the
                // row is drawn, then draw the checkbox again on top of it.
                RECT bounds{}, label{};
                if (ListView_GetItemRect(list, item, &bounds, LVIR_BOUNDS) &&
                    ListView_GetItemRect(list, item, &label, LVIR_LABEL))
                {
                    bounds.right = label.left;
                    HBRUSH fill = CreateSolidBrush(autoRowColor);
                    FillRect(draw->nmcd.hdc, &bounds, fill);
                    DeleteObject(fill);
                    HIMAGELIST checks = ListView_GetImageList(list, LVSIL_STATE);
                    const UINT state = ListView_GetItemState(list, item, LVIS_STATEIMAGEMASK);
                    int width = 0, height = 0;
                    if (checks && state && ImageList_GetIconSize(checks, &width, &height))
                        ImageList_Draw(checks, static_cast<int>(state >> 12) - 1,
                                       draw->nmcd.hdc,
                                       bounds.left + (bounds.right - bounds.left - width) / 2,
                                       bounds.top + (bounds.bottom - bounds.top - height) / 2,
                                       ILD_TRANSPARENT);
                }
            }
            return CDRF_DODEFAULT;
        }
        if (n->hwndFrom == list && n->code == NM_CLICK && !loading)
        {
            auto a = (NMITEMACTIVATE *)l;
            if (a->iItem >= 0 && a->iSubItem == 1)
            {
                auto r = rows[a->iItem];
                if (r.locked)
                {
                    message(tr(T::StructuralProtected));
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
                if (r.locked || automatic.count(r.key))
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
    case WM_DRAWITEM: {
        auto item = reinterpret_cast<const DRAWITEMSTRUCT *>(l);
        if (item->CtlID == stopId)
        {
            drawStopButton(*item);
            return TRUE;
        }
        break;
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
            AppendMenuW(menu, MF_STRING, 4, tr(T::TrayOpen));
            AppendMenuW(menu, MF_STRING, 5, tr(T::TrayExit));
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
    {
        const std::string a = "VSTPlugin: Library \"x.dll\" \"Band 1 State\" 1 \"Band 1 Range\" 0.3";
        const std::string b = "VSTPlugin: Library x.dll \"Band 1 Range\" 0.3  \"Band 1 State\" 1";
        const std::string c = "VSTPlugin: Library \"x.dll\" \"Band 1 State\" 1 \"Band 1 Range\" 0.4";
        const std::string broken = "VSTPlugin: Library \"x.dll";
        // The same DLL written relative to the config folder and as a full path.
        const auto folder = fs::path(config).parent_path();
        const std::string relative =
            "VSTPlugin: Library \"..\\" + utf8(folder.filename().wstring()) + "\\vst\\X.dll\" A 1";
        const std::string absolute =
            "VSTPlugin: Library \"" + utf8(folder.wstring()) + "\\vst\\x.dll\" A 1";
        if (canonicalKey(relative) != canonicalKey(absolute) ||
            canonicalKey(canonicalKey(relative)) != canonicalKey(relative) ||
            canonicalKey(a) != canonicalKey(b) || canonicalKey(a) == canonicalKey(c) ||
            canonicalKey(canonicalKey(a)) != canonicalKey(a) ||
            canonicalKey("Include: peace.txt") != "Include: peace.txt" ||
            canonicalKey(broken) != broken)
            result = 14;
    }
    if (!result && (!loadConfig() || rows.size() != 3 || rows[0].locked || rows[1].on))
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

enum class ShellElevation
{
    Unknown,
    Limited,
    Elevated
};

// Unknown is never treated as limited so a relaunch can never loop back into administrator mode.
ShellElevation desktopShellElevation()
{
    DWORD processId = 0;
    const HWND shell = GetShellWindow();
    if (!shell || !GetWindowThreadProcessId(shell, &processId))
        return ShellElevation::Unknown;
    const HANDLE shellProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!shellProcess)
        return ShellElevation::Unknown;
    FileHandle process(shellProcess);
    HANDLE shellToken = nullptr;
    if (!OpenProcessToken(process.value, TOKEN_QUERY, &shellToken))
        return ShellElevation::Unknown;
    FileHandle token(shellToken);
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    if (!GetTokenInformation(shellToken, TokenElevation, &elevation, sizeof(elevation), &size))
        return ShellElevation::Unknown;
    return elevation.TokenIsElevated ? ShellElevation::Elevated : ShellElevation::Limited;
}

// Starts this executable through the desktop shell, which runs with the user's normal token.
bool relaunchWithoutElevation(const std::wstring &arguments)
{
    if (desktopShellElevation() != ShellElevation::Limited)
        return false;
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    if (!length || length == ARRAYSIZE(executable))
        return false;

    const HRESULT initialized =
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool launched = false;
    {
        using Microsoft::WRL::ComPtr;
        ComPtr<IShellWindows> windows;
        ComPtr<IDispatch> desktop, background, application;
        ComPtr<IServiceProvider> provider;
        ComPtr<IShellBrowser> browser;
        ComPtr<IShellView> view;
        ComPtr<IShellFolderViewDual> folderView;
        ComPtr<IShellDispatch2> shell;
        VARIANT empty{};
        long desktopWindow = 0;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER,
                                       IID_PPV_ARGS(&windows))) &&
            windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &desktopWindow,
                                  SWFO_NEEDDISPATCH, &desktop) == S_OK &&
            desktop && SUCCEEDED(desktop.As(&provider)) &&
            SUCCEEDED(provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser))) &&
            SUCCEEDED(browser->QueryActiveShellView(&view)) &&
            SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&background))) &&
            SUCCEEDED(background.As(&folderView)) &&
            SUCCEEDED(folderView->get_Application(&application)) &&
            SUCCEEDED(application.As(&shell)))
        {
            BSTR file = SysAllocString(executable);
            VARIANT parameters{}, show{};
            parameters.vt = VT_BSTR;
            parameters.bstrVal = SysAllocString(arguments.c_str());
            show.vt = VT_I4;
            show.lVal = SW_SHOWNORMAL;
            launched = file && parameters.bstrVal &&
                       SUCCEEDED(shell->ShellExecute(file, parameters, empty, empty, show));
            SysFreeString(parameters.bstrVal);
            SysFreeString(file);
        }
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return launched;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR args, int)
{
    // Some launchers (for example PowerShell's Start-Process) append a trailing space.
    std::wstring arguments(args);
    arguments.erase(0, arguments.find_first_not_of(L" \t"));
    arguments.erase(arguments.find_last_not_of(L" \t") + 1);
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
    language = detectLanguage();

    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\AutoAPO.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (!startInTray)
            MessageBoxW(0, tr(T::AlreadyRunning), L"AutoAPO", MB_OK);
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
        // Equalizer APO normally lets users edit its config folder, so administrator rights are
        // unnecessary there and unsafe to use. Continue as the normal user instead.
        for (HANDLE handle : protectedDirectories)
            CloseHandle(handle);
        protectedDirectories.clear();
        CloseHandle(mutex);
        if (relaunchWithoutElevation(arguments))
            return 0;
        if (desktopShellElevation() != ShellElevation::Elevated)
        {
            message(tr(T::AdminUnavailable));
            return 1;
        }
        // The desktop itself runs as administrator (UAC is off, or this is the built-in
        // Administrator account), so no normal-rights session exists to return to and none can
        // be put at risk. Continue in normal mode with this token.
        elevated = false;
        mutex = CreateMutexW(nullptr, FALSE, L"Local\\AutoAPO.SingleInstance");
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            if (!startInTray)
                MessageBoxW(0, tr(T::AlreadyRunning), L"AutoAPO", MB_OK);
            return 0;
        }
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
    // Also strip line endings written by other tools: the PUBG installer writes this file
    // with CRLF, and a UTF-8 byte-order mark may come from editing it by hand.
    if (savedPreferences.compare(0, 3, "\xef\xbb\xbf") == 0)
        savedPreferences.erase(0, 3);
    std::istringstream prefs(savedPreferences);
    std::string s;
    auto readLine = [&] {
        if (!std::getline(prefs, s))
            return false;
        if (!s.empty() && s.back() == '\r')
            s.pop_back();
        return true;
    };
    if (readLine() && !s.empty() && (elevated ? wide(s) == config : isLocalPath(wide(s))))
    {
        if (!elevated)
            config = wide(s);
        if (readLine())
            watched = wide(s);
        while (readLine())
            if (!s.empty())
                automatic.insert(canonicalKey(s)); // Also upgrades rules saved as raw lines.
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
    constexpr DWORD windowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    // The lowest control (the tray note) ends at y = 585; keep a 12 px margin below it.
    RECT frame{0, 0, 0, 585 + 12};
    AdjustWindowRect(&frame, windowStyle, FALSE);
    win = CreateWindowW(wc.lpszClassName, L"AutoAPO", windowStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                        940, frame.bottom - frame.top, 0, 0, inst, 0);
    auto control = [&](const wchar_t *cls, const wchar_t *title, DWORD style, int x, int y,
                       int width, int height, int id) {
        HWND v = CreateWindowW(cls, title, WS_CHILD | WS_VISIBLE | style, x, y, width, height, win,
                               (HMENU)(INT_PTR)id, inst, 0);
        SendMessageW(v, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
        return v;
    };
    pathLabel = control(L"STATIC", config.c_str(), 0, 16, 16, 584, 25, 0);
    control(L"BUTTON", tr(T::ChooseConfig), WS_TABSTOP | (elevated ? WS_DISABLED : 0), 610, 12,
            160, 30, 1);
    HWND languageBox = control(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, 780,
                               14, 130, 120, languageId);
    for (const wchar_t *name : languageNames)
        SendMessageW(languageBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(languageBox, CB_SETCURSEL, language, 0);
    control(L"STATIC", tr(T::WatchedLabel), 0, 16, 55, 890, 22, watchedLabelId);
    processEdit = control(L"EDIT", watched.c_str(), WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 16, 82,
                          714, 28, 0);
    control(L"BUTTON", tr(T::SaveProcesses), WS_TABSTOP, 740, 80, 170, 30, 2);
    control(L"STATIC", tr(T::Hint), 0, 16, 123, 890, 23, hintLabelId);
    list = control(WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | WS_BORDER | WS_TABSTOP, 16, 152,
                   894, 330, 0);
    ListView_SetExtendedListViewStyle(list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT |
                                                LVS_EX_DOUBLEBUFFER);
    SetWindowSubclass(list, listSubclass, 1, 0);
    createCheckboxTip(inst);
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH;
    col.cx = 630;
    col.pszText = const_cast<LPWSTR>(tr(T::ColumnItem));
    ListView_InsertColumn(list, 0, &col);
    col.cx = 240;
    col.pszText = const_cast<LPWSTR>(tr(T::ColumnAuto));
    ListView_InsertColumn(list, 1, &col);
    status = control(L"STATIC", L"", 0, 16, 492, 890, 22, 0);
    control(L"BUTTON", tr(T::Refresh), WS_TABSTOP, 16, 525, 140, 28, 4);
    control(L"BUTTON", tr(T::StartWithWindows),
            BS_AUTOCHECKBOX | WS_TABSTOP | (elevated ? WS_DISABLED : 0), 170, 525, 185, 28,
            startWithWindowsId);
    CheckDlgButton(win, startWithWindowsId, isStartupEnabled() ? BST_CHECKED : BST_UNCHECKED);
    control(L"STATIC", tr(T::TrayNote), 0, 16, 563, 894, 22, trayNoteId);
    control(L"BUTTON", tr(T::Stop), BS_OWNERDRAW | WS_TABSTOP, 755, 525, 155, 28, stopId);
    NOTIFYICONDATAW tray{sizeof(tray)};
    tray.hWnd = win;
    tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = WM_APP + 1;
    tray.hIcon = wc.hIconSm ? wc.hIconSm : wc.hIcon;
    lstrcpynW(tray.szTip, tr(T::TrayTip), ARRAYSIZE(tray.szTip));
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
