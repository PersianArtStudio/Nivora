#define UNICODE
#define _UNICODE
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#include <winsock2.h>
#include <windows.h>
#include "core.h"
#include <commdlg.h>
#include <process.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <strsafe.h>

#define KEY L"Software\\Nivora"
#define RUNKEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define TRAY (WM_APP + 1)
#define RESULT (WM_APP + 2)
#define NAVY RGB(13, 18, 34)
#define INK RGB(234, 238, 255)
#define ACCENT RGB(105, 93, 245)
enum {
  HOME = 101,
  SETTINGS,
  LOGS,
  LAUNCH,
  STOP,
  PATH,
  CHOOSE,
  MODE,
  PORT,
  HIDE,
  MINIM,
  NOTIFY,
  DISK,
  STARTUP,
  SAVE,
  RESET,
  VIEW,
  FILTER,
  COPY,
  EXPORT,
  CLEAR,
  QUIT,
  RESTART,
  STATUS,
  DETAIL,
  TARGET
};
static HWND w, controls[260];
static int ids[100], count, page = HOME, scale = 96, state, filter;
static HFONT font, title;
static HBRUSH bg;
static HICON icons[5];
static NOTIFYICONDATAW tray;
static HANDLE job, child, worker;
static UINT taskbar;
static wchar_t path[MAX_PATH], detected[MAX_PATH],
    portText[12] = L"10808", logPath[MAX_PATH],
    message[180] = L"Ready to launch";
static int mode, hideAfter = 1, minTray = 1, notifyUser = 1, logDisk = 1;
static wchar_t lines[256][210];
static int levels[256], used;
static const wchar_t *names[] = {L"Ready", L"Checking", L"Running",
                                 L"Disconnected", L"Error"};
static const wchar_t *severity[] = {L"Info", L"Success", L"Warning", L"Error"};
static int px(int v) { return MulDiv(v, scale, 96); }
static DWORD setting(const wchar_t *name, DWORD fallback) {
  DWORD v = 0, size = sizeof(v);
  return RegGetValueW(HKEY_CURRENT_USER, KEY, name, RRF_RT_REG_DWORD, NULL, &v,
                      &size) == ERROR_SUCCESS
             ? v
             : fallback;
}
static void readText(const wchar_t *key, wchar_t *buf, DWORD size) {
  RegGetValueW(HKEY_CURRENT_USER, KEY, key, RRF_RT_REG_SZ, NULL, buf, &size);
}
static BOOL writeSetting(const wchar_t *key, DWORD type, const void *data,
                         DWORD size) {
  return RegSetKeyValueW(HKEY_CURRENT_USER, KEY, key, type, data, size) ==
         ERROR_SUCCESS;
}
static void refreshLog(void);
static void logLine(int level, const wchar_t *text) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  if (used == 256) {
    memmove(lines, lines + 1, 255 * sizeof(lines[0]));
    memmove(levels, levels + 1, 255 * sizeof(int));
    used--;
  }
  levels[used] = level;
  StringCchPrintfW(
      lines[used], 210, L"%04u-%02u-%02u %02u:%02u:%02u  [%ls]  %ls", t.wYear,
      t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, severity[level], text);
  if (logDisk && *logPath) {
    WIN32_FILE_ATTRIBUTE_DATA a;
    if (GetFileAttributesExW(logPath, GetFileExInfoStandard, &a) &&
        a.nFileSizeLow > 262144) {
      wchar_t old[MAX_PATH];
      StringCchPrintfW(old, MAX_PATH, L"%ls.1", logPath);
      MoveFileExW(logPath, old, MOVEFILE_REPLACE_EXISTING);
    }
    FILE *f = _wfopen(logPath, L"a, ccs=UTF-8");
    if (f) {
      fwprintf(f, L"%ls\n", lines[used]);
      fclose(f);
    }
  }
  used++;
  if (page == LOGS)
    refreshLog();
}
static void syncStatus(void) {
  if (controls[STATUS])
    SetWindowTextW(controls[STATUS], names[state]);
  if (controls[DETAIL])
    SetWindowTextW(controls[DETAIL], message);
  if (controls[LAUNCH])
    EnableWindow(controls[LAUNCH], !child && !worker);
  if (controls[TARGET])
    EnableWindow(controls[TARGET], !worker && !child);
  if (controls[STOP])
    EnableWindow(controls[STOP], child != NULL);
  tray.hIcon = icons[state];
  StringCchPrintfW(tray.szTip, 128, L"Nivora  |  %ls", names[state]);
  Shell_NotifyIconW(NIM_MODIFY, &tray);
}
static void status(int s, const wchar_t *msg, int level) {
  state = s;
  StringCchCopyW(message, 180, msg);
  logLine(level, msg);
  syncStatus();
  if (notifyUser && !IsWindowVisible(w)) {
    tray.uFlags |= NIF_INFO;
    StringCchCopyW(tray.szInfo, 256, msg);
    StringCchCopyW(tray.szInfoTitle, 64, L"Nivora");
    Shell_NotifyIconW(NIM_MODIFY, &tray);
    tray.uFlags &= ~NIF_INFO;
  }
}
enum { APP, IDE, CLI, EDITOR, TARGET_COUNT };
static DWORD target;
static const wchar_t *targetNames[] = {L"Antigravity App", L"Antigravity IDE",
                                       L"Antigravity CLI",
                                       L"Editor with Antigravity extension"};
static const wchar_t *targetKeys[] = {L"AppPath", L"IdePath", L"CliPath",
                                      L"EditorPath"};
static const wchar_t *executables[] = {
    L"Antigravity.exe", L"Antigravity IDE.exe", L"agy.exe", NULL};
static wchar_t cliDir[MAX_PATH];
static void loadPath(void) {
  path[0] = 0;
  DWORD bytes = sizeof(path);
  if (RegGetValueW(HKEY_CURRENT_USER, KEY, targetKeys[target], RRF_RT_REG_SZ,
                   NULL, path, &bytes) == ERROR_FILE_NOT_FOUND &&
      target < CLI) {
    wchar_t legacy[MAX_PATH] = {0};
    readText(L"Path", legacy, sizeof(legacy));
    const wchar_t *base = wcsrchr(legacy, L'\\');
    if (base && !_wcsicmp(base + 1, executables[target]))
      StringCchCopyW(path, MAX_PATH, legacy);
  }
}
static BOOL validExe(const wchar_t *p) {
  DWORD attr = GetFileAttributesW(p);
  const wchar_t *base = wcsrchr(p, L'\\');
  return wcslen(p) > 3 && p[1] == L':' && p[2] == L'\\' && base &&
         (target == EDITOR
              ? (wcslen(base) > 4 && !_wcsicmp(p + wcslen(p) - 4, L".exe"))
              : !_wcsicmp(base + 1, executables[target])) &&
         attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
static void detect(void) {
  detected[0] = 0;
  if (*path) {
    if (validExe(path))
      StringCchCopyW(detected, MAX_PATH, path);
    return;
  }
  if (target == EDITOR)
    return;
  if (target == CLI) {
    const wchar_t *places[] = {
        L"%LOCALAPPDATA%\\Programs\\Antigravity CLI\\agy.exe",
        L"%USERPROFILE%\\.local\\bin\\agy.exe",
        L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Links\\agy.exe"};
    wchar_t candidate[MAX_PATH];
    for (int i = 0; i < ARRAYSIZE(places); i++) {
      DWORD n = ExpandEnvironmentStringsW(places[i], candidate, MAX_PATH);
      if (n && n <= MAX_PATH && validExe(candidate)) {
        StringCchCopyW(detected, MAX_PATH, candidate);
        return;
      }
    }
    DWORD size = GetEnvironmentVariableW(L"PATH", NULL, 0);
    if (size && size <= 32767) {
      wchar_t *dirs = calloc(size, sizeof(wchar_t));
      DWORD n = dirs ? GetEnvironmentVariableW(L"PATH", dirs, size) : 0;
      if (n && n < size) {
        wchar_t *ctx = NULL;
        for (wchar_t *dir = wcstok_s(dirs, L";", &ctx); dir;
             dir = wcstok_s(NULL, L";", &ctx)) {
          size_t len = wcslen(dir);
          if (len >= 2 && dir[0] == L'"' && dir[len - 1] == L'"') {
            dir[len - 1] = 0;
            dir++;
          }
          if (SUCCEEDED(StringCchPrintfW(candidate, MAX_PATH, L"%ls\\agy.exe",
                                         dir)) &&
              validExe(candidate)) {
            StringCchCopyW(detected, MAX_PATH, candidate);
            break;
          }
        }
      }
      free(dirs);
    }
    return;
  }
  const wchar_t *roots[] = {L"LOCALAPPDATA", L"ProgramFiles",
                            L"ProgramFiles(x86)", L"USERPROFILE"};
  const wchar_t *apps[] = {L"Antigravity", L"Antigravity IDE"};
  for (int i = 0; i < 4; i++) {
    int j = (int)target;
    wchar_t root[MAX_PATH], p[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(roots[i], root, MAX_PATH);
    if (!n || n >= MAX_PATH)
      continue;
    if (i == 3)
      StringCchPrintfW(p, MAX_PATH, L"%ls\\scoop\\apps\\%ls\\current\\%ls.exe",
                       root, j ? L"antigravity-ide" : L"antigravity", apps[j]);
    else
      StringCchPrintfW(p, MAX_PATH, L"%ls\\%ls%ls\\%ls.exe", root,
                       i == 0 ? L"Programs\\" : L"", apps[j], apps[j]);
    if (validExe(p)) {
      StringCchCopyW(detected, MAX_PATH, p);
      return;
    }
  }
}
typedef struct {
  int type, port;
} Endpoint;
static Endpoint selected;
static BOOL probe(Endpoint e) {
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET)
    return FALSE;
  DWORD timeout = 600;
  setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
  setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((u_short)e.port);
  u_long nb = 1;
  ioctlsocket(s, FIONBIO, &nb);
  BOOL ok = connect(s, (struct sockaddr *)&addr, sizeof(addr)) == 0;
  if (!ok && WSAGetLastError() == WSAEWOULDBLOCK) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    struct timeval tv = {0, 350000};
    int err = 1, len = sizeof(err);
    ok = select(0, NULL, &set, NULL, &tv) > 0 &&
         getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len) == 0 && !err;
  }
  nb = 0;
  ioctlsocket(s, FIONBIO, &nb);
  if (ok) {
    char buf[16] = {0};
    if (e.type == 2) {
      const char hello[] = {5, 1, 0};
      ok = send(s, hello, 3, 0) == 3 && recv(s, buf, 2, MSG_WAITALL) == 2 &&
           buf[0] == 5 && buf[1] == 0;
    } else {
      const char q[] =
          "OPTIONS * HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
      ok = send(s, q, sizeof(q) - 1, 0) == sizeof(q) - 1 &&
           recv(s, buf, 5, MSG_WAITALL) == 5 && memcmp(buf, "HTTP/", 5) == 0;
    }
  }
  closesocket(s);
  return ok;
}
static unsigned __stdcall check(void *unused) {
  Endpoint autoList[] = {{2, 10808}, {1, 10809}, {2, 2080},
                         {1, 2081},  {2, 7890},  {1, 7890},
                         {2, 1080},  {1, 8888},  {1, 8080}};
  Endpoint manual = {mode, parse_port(portText)};
  int total = mode ? 1 : ARRAYSIZE(autoList);
  for (int i = 0; i < total; i++) {
    Endpoint e = mode ? manual : autoList[i];
    if (probe(e)) {
      PostMessageW(w, RESULT, e.type, e.port);
      return 0;
    }
  }
  PostMessageW(w, RESULT, 0, 0);
  return 0;
}
static void beginLaunch(void) {
  if (worker || child)
    return;
  detect();
  if (!*detected) {
    status(4, L"Choose the executable for the selected target in Settings.", 3);
    return;
  }
  if (target == CLI) {
    BROWSEINFOW bi = {0};
    bi.hwndOwner = w;
    bi.lpszTitle = L"Choose the working folder for Antigravity CLI";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE picked = SHBrowseForFolderW(&bi);
    if (!picked)
      return;
    BOOL ok = SHGetPathFromIDListW(picked, cliDir);
    CoTaskMemFree(picked);
    if (!ok) {
      status(4, L"Select a local project folder.", 3);
      return;
    }
  }
  worker = (HANDLE)_beginthreadex(NULL, 0, check, NULL, 0, NULL);
  if (worker)
    status(1, L"Checking the local proxy listener...", 0);
  else
    status(4, L"Could not start the network check. Try again.", 3);
}
/* Construct a private child environment. Keep the launcher's environment
 * untouched. */
static int byName(const void *a, const void *b) {
  return _wcsicmp(*(const wchar_t *const *)a, *(const wchar_t *const *)b);
}
static wchar_t *childEnvironment(int type, int port) {
  wchar_t *original = GetEnvironmentStringsW();
  if (!original)
    return NULL;
  size_t count = 0;
  for (wchar_t *v = original; *v; v += wcslen(v) + 1)
    count++;
  wchar_t **rows = calloc(count + 3, sizeof(*rows));
  wchar_t *result = NULL;
  if (rows) {
    wchar_t replacement[3][100];
    const wchar_t *keys[] = {L"HTTP_PROXY=", L"HTTPS_PROXY=", L"ALL_PROXY="};
    size_t n = 0, chars = 1;
    for (wchar_t *v = original; *v; v += wcslen(v) + 1) {
      BOOL replace = FALSE;
      for (int k = 0; k < 3; k++)
        if (!_wcsnicmp(v, keys[k], wcslen(keys[k])))
          replace = TRUE;
      if (!replace)
        rows[n++] = v;
    }
    for (int k = 0; k < 3; k++) {
      StringCchPrintfW(replacement[k], 100, L"%ls%ls://127.0.0.1:%d", keys[k],
                       type == 2 ? L"socks5" : L"http", port);
      rows[n++] = replacement[k];
    }
    qsort(rows, n, sizeof(*rows), byName);
    for (size_t k = 0; k < n; k++)
      chars += wcslen(rows[k]) + 1;
    result = calloc(chars, sizeof(wchar_t));
    if (result) {
      wchar_t *dst = result;
      for (size_t k = 0; k < n; k++) {
        size_t len = wcslen(rows[k]) + 1;
        memcpy(dst, rows[k], len * sizeof(wchar_t));
        dst += len;
      }
    }
    free(rows);
  }
  FreeEnvironmentStringsW(original);
  return result;
}
static void finishLaunch(int type, int port) {
  if (worker) {
    CloseHandle(worker);
    worker = NULL;
  }
  if (!type) {
    status(3, L"Start your local proxy client or check its port in Settings.",
           2);
    return;
  }
  wchar_t cmd[1024];
  StringCchPrintfW(cmd, 1024, L"\"%ls\" --proxy-server=%ls://127.0.0.1:%d",
                   detected, type == 2 ? L"socks5" : L"http", port);
  if (target >= CLI)
    StringCchPrintfW(cmd, 1024, L"\"%ls\"", detected);
  job = CreateJobObjectW(NULL, NULL);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {0};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi = {0};
  wchar_t dir[MAX_PATH];
  StringCchCopyW(dir, MAX_PATH, detected);
  *wcsrchr(dir, L'\\') = 0;
  wchar_t *env = childEnvironment(type, port);
  if (!env || !job ||
      !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                               sizeof(limits)) ||
      !CreateProcessW(detected, cmd, NULL, NULL, FALSE,
                      CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                          (target == CLI ? CREATE_NEW_CONSOLE : 0),
                      env, target == CLI ? cliDir : dir, &si, &pi) ||
      !AssignProcessToJobObject(job, pi.hProcess) ||
      ResumeThread(pi.hThread) == (DWORD)-1) {
    free(env);
    if (pi.hProcess) {
      TerminateProcess(pi.hProcess, 1);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    }
    if (job)
      CloseHandle(job);
    job = NULL;
    status(4,
           L"Windows could not start a managed session. Check the installation "
           L"and permissions.",
           3);
    return;
  }
  free(env);
  CloseHandle(pi.hThread);
  child = pi.hProcess;
  selected = (Endpoint){type, port};
  status(1, L"Proxy listener responded. Waiting for Antigravity...", 0);
  SetTimer(w, 1, 1400, NULL);
}
static void closeSession(void) {
  if (job) {
    CloseHandle(job);
    job = NULL;
  }
  if (child) {
    CloseHandle(child);
    child = NULL;
  }
  KillTimer(w, 1);
}
static BOOL confirmStop(void) {
  return MessageBoxW(w,
                     L"Close this Antigravity session and its child processes? "
                     L"Save your work first.",
                     L"Nivora", MB_YESNO | MB_ICONWARNING) == IDYES;
}
static HWND add(int id, const wchar_t *cls, const wchar_t *txt, DWORD style,
                int x, int y, int width, int height) {
  HWND h =
      CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style, px(x), px(y),
                      px(width), px(height), w, (HMENU)(INT_PTR)id, NULL, NULL);
  SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
  controls[id] = h;
  ids[count++] = id;
  return h;
}
static void label(int id, const wchar_t *s, int x, int y, int width) {
  add(id, L"STATIC", s, 0, x, y, width, 25);
}
static void button(int id, const wchar_t *s, int x, int y, int width) {
  add(id, L"BUTTON", s, BS_OWNERDRAW | WS_TABSTOP, x, y, width, 36);
}
static void tick(int id, const wchar_t *s, int y, int checked) {
  HWND h = add(id, L"BUTTON", s, BS_AUTOCHECKBOX | WS_TABSTOP, 34, y, 490, 25);
  SendMessageW(h, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
}
static int checked(int id) {
  return SendMessageW(controls[id], BM_GETCHECK, 0, 0) == BST_CHECKED;
}
static void refreshLog(void) {
  if (!controls[VIEW])
    return;
  static wchar_t buffer[56000];
  buffer[0] = 0;
  for (int i = 0; i < used; i++)
    if (!filter || levels[i] == filter - 1) {
      StringCchCatW(buffer, ARRAYSIZE(buffer), lines[i]);
      StringCchCatW(buffer, ARRAYSIZE(buffer), L"\r\n");
    }
  SetWindowTextW(controls[VIEW], buffer);
  SendMessageW(controls[VIEW], EM_SETSEL, -1, -1);
  SendMessageW(controls[VIEW], EM_SCROLLCARET, 0, 0);
}
static void render(int p) {
  for (int i = 0; i < count; i++) {
    DestroyWindow(controls[ids[i]]);
    controls[ids[i]] = NULL;
  }
  count = 0;
  page = p;
  button(HOME, L"Dashboard", 30, 74, 128);
  button(SETTINGS, L"Settings", 168, 74, 110);
  button(LOGS, L"Activity", 288, 74, 110);
  if (p == HOME) {
    label(201, L"CONNECTION", 34, 152, 240);
    label(STATUS, names[state], 32, 185, 520);
    SetWindowPos(controls[STATUS], NULL, 0, 0, px(530), px(58),
                 SWP_NOMOVE | SWP_NOZORDER);
    SendMessageW(controls[STATUS], WM_SETFONT, (WPARAM)title, TRUE);
    add(DETAIL, L"STATIC", message, 0, 34, 254, 530, 60);
    HWND picker = add(TARGET, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP,
                      34, 307, 520, 160);
    for (int i = 0; i < TARGET_COUNT; i++)
      SendMessageW(picker, CB_ADDSTRING, 0, (LPARAM)targetNames[i]);
    SendMessageW(picker, CB_SETCURSEL, target, 0);
    detect();
    label(202,
          *detected ? L"Application     •     Selected executable detected"
                    : L"Application     •     Installation required",
          34, 351, 525);
    wchar_t info[150];
    if (child)
      StringCchPrintfW(info, 150, L"Proxy     •     %ls  /  127.0.0.1:%d",
                       selected.type == 2 ? L"SOCKS5" : L"HTTP", selected.port);
    else
      StringCchCopyW(info, 150,
                     mode ? L"Proxy     •     Custom loopback listener"
                          : L"Proxy     •     Automatic local detection");
    label(203, info, 34, 388, 525);
    button(LAUNCH, L"Launch selected target", 34, 444, 230);
    button(STOP, L"Stop session", 278, 444, 150);
    syncStatus();
  } else if (p == SETTINGS) {
    label(202, targetNames[target], 34, 131, 530);
    add(PATH, L"EDIT", path, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 34, 161,
        425, 28);
    button(CHOOSE, L"Browse", 468, 157, 90);
    label(203, L"Local proxy", 34, 207, 250);
    label(204, L"Port", 348, 207, 150);
    HWND m = add(MODE, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 34, 237,
                 289, 180);
    const wchar_t *opts[] = {L"Automatic detection", L"HTTP on 127.0.0.1",
                             L"SOCKS5 on 127.0.0.1"};
    for (int i = 0; i < 3; i++)
      SendMessageW(m, CB_ADDSTRING, 0, (LPARAM)opts[i]);
    SendMessageW(m, CB_SETCURSEL, mode, 0);
    add(PORT, L"EDIT", portText, ES_NUMBER | WS_BORDER | WS_TABSTOP, 348, 237,
        100, 28);
    EnableWindow(controls[PORT], mode != 0);
    tick(HIDE, L"Hide after a successful launch", 286, hideAfter);
    tick(MINIM, L"Minimize to the notification area", 316, minTray);
    tick(NOTIFY, L"Show background notifications", 346, notifyUser);
    tick(DISK, L"Save activity to rotating log files", 376, logDisk);
    DWORD bytes = 0;
    tick(STARTUP, L"Open Nivora when I sign in to Windows", 406,
         RegGetValueW(HKEY_CURRENT_USER, RUNKEY, L"Nivora", RRF_RT_REG_SZ, NULL,
                      NULL, &bytes) == ERROR_SUCCESS);
    button(SAVE, L"Save settings", 34, 463, 150);
    button(RESET, L"Restore defaults", 196, 463, 160);
  } else {
    HWND m = add(FILTER, L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP, 34,
                 132, 180, 150);
    SendMessageW(m, CB_ADDSTRING, 0, (LPARAM)L"All events");
    for (int i = 0; i < 4; i++)
      SendMessageW(m, CB_ADDSTRING, 0, (LPARAM)severity[i]);
    SendMessageW(m, CB_SETCURSEL, filter, 0);
    add(VIEW, L"EDIT", L"",
        ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 34, 178, 524,
        260);
    refreshLog();
    button(COPY, L"Copy", 34, 464, 110);
    button(EXPORT, L"Export", 156, 464, 110);
    button(CLEAR, L"Clear", 278, 464, 110);
  }
}
static void save(BOOL defaults) {
  if (worker) {
    MessageBoxW(w, L"Wait for the proxy check to finish.", L"Nivora", MB_OK);
    return;
  }
  wchar_t p[MAX_PATH] = {0}, digits[12] = L"10808";
  DWORD m = 0, values[4] = {1, 1, 1, 1};
  int startup = 0;
  if (!defaults) {
    GetWindowTextW(controls[PATH], p, MAX_PATH);
    GetWindowTextW(controls[PORT], digits, 12);
    m = SendMessageW(controls[MODE], CB_GETCURSEL, 0, 0);
    if ((*p && !validExe(p)) || (m && !parse_port(digits))) {
      MessageBoxW(w,
                  L"Choose an installed Antigravity executable and a port from "
                  L"1 to 65535.",
                  L"Check settings", MB_OK);
      return;
    }
    values[0] = checked(HIDE);
    values[1] = checked(MINIM);
    values[2] = checked(NOTIFY);
    values[3] = checked(DISK);
    startup = checked(STARTUP);
  }
  BOOL ok = writeSetting(targetKeys[target], REG_SZ, p, (wcslen(p) + 1) * 2) &&
            writeSetting(L"Port", REG_SZ, digits, (wcslen(digits) + 1) * 2) &&
            writeSetting(L"Mode", REG_DWORD, &m, 4);
  const wchar_t *keys[] = {L"Hide", L"Tray", L"Notify", L"Disk"};
  for (int i = 0; i < 4; i++)
    ok = writeSetting(keys[i], REG_DWORD, &values[i], 4) && ok;
  if (startup) {
    wchar_t exe[MAX_PATH], arg[MAX_PATH + 4];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    StringCchPrintfW(arg, ARRAYSIZE(arg), L"\"%ls\"", exe);
    ok = (RegSetKeyValueW(HKEY_CURRENT_USER, RUNKEY, L"Nivora", REG_SZ, arg,
                          (wcslen(arg) + 1) * 2) == ERROR_SUCCESS) &&
         ok;
  } else {
    LSTATUS r = RegDeleteKeyValueW(HKEY_CURRENT_USER, RUNKEY, L"Nivora");
    ok = ok && (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
  }
  if (!ok) {
    MessageBoxW(
        w,
        L"Windows could not save all preferences. Check your user permissions.",
        L"Nivora", MB_OK);
    return;
  }
  StringCchCopyW(path, MAX_PATH, p);
  StringCchCopyW(portText, 12, digits);
  mode = m;
  hideAfter = values[0];
  minTray = values[1];
  notifyUser = values[2];
  logDisk = values[3];
  logLine(1, L"Preferences saved.");
  render(defaults ? SETTINGS : HOME);
}
static void openWindow(int p) {
  ShowWindow(w, SW_RESTORE);
  SetForegroundWindow(w);
  render(p);
}
static void command(int id) {
  if (id == HOME || id == SETTINGS || id == LOGS) {
    openWindow(id);
    return;
  }
  if (id == LAUNCH) {
    beginLaunch();
    return;
  }
  if (id == STOP || id == RESTART) {
    if (child && !confirmStop())
      return;
    closeSession();
    status(0, L"Session stopped.", 0);
    render(HOME);
    if (id == RESTART)
      beginLaunch();
    return;
  }
  if (id == QUIT) {
    if (child && !confirmStop())
      return;
    DestroyWindow(w);
    return;
  }
  if (id == SAVE || id == RESET) {
    save(id == RESET);
    return;
  }
  if (id == CHOOSE || id == EXPORT) {
    wchar_t file[MAX_PATH] = L"";
    OPENFILENAMEW of = {sizeof(of)};
    of.hwndOwner = w;
    of.lpstrFile = file;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_NOCHANGEDIR;
    if (id == CHOOSE) {
      of.lpstrFilter = L"Antigravity executables\0*.exe\0";
      of.Flags |= OFN_FILEMUSTEXIST;
      if (GetOpenFileNameW(&of))
        SetWindowTextW(controls[PATH], file);
    } else {
      of.lpstrFilter = L"Text log\0*.txt\0";
      of.lpstrDefExt = L"txt";
      of.Flags |= OFN_OVERWRITEPROMPT;
      if (GetSaveFileNameW(&of)) {
        FILE *f = _wfopen(file, L"w, ccs=UTF-8");
        BOOL ok = FALSE;
        if (f) {
          for (int i = 0; i < used; i++)
            if (!filter || levels[i] == filter - 1)
              fwprintf(f, L"%ls\n", lines[i]);
          ok = fclose(f) == 0;
        }
        if (!ok)
          MessageBoxW(w, L"Could not write the selected file.", L"Nivora",
                      MB_OK);
      }
    }
  } else if (id == COPY) {
    SendMessageW(controls[VIEW], EM_SETSEL, 0, -1);
    SendMessageW(controls[VIEW], WM_COPY, 0, 0);
  } else if (id == CLEAR &&
             MessageBoxW(w, L"Clear activity and delete saved Nivora logs?",
                         L"Nivora", MB_YESNO) == IDYES) {
    used = 0;
    DeleteFileW(logPath);
    wchar_t old[MAX_PATH];
    StringCchPrintfW(old, MAX_PATH, L"%ls.1", logPath);
    DeleteFileW(old);
    refreshLog();
  }
}
static void selectTarget(int index) {
  if (index < 0 || index >= TARGET_COUNT || child || worker)
    return;
  target = index;
  writeSetting(L"Target", REG_DWORD, &target, sizeof(target));
  loadPath();
  status(0, L"Ready to launch", 0);
  render(HOME);
}
static void menu(void) {
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_GRAYED, 0, names[state]);
  AppendMenuW(m, 0, HOME, L"Open Nivora");
  HMENU targets = CreatePopupMenu();
  for (int i = 0; i < TARGET_COUNT; i++)
    AppendMenuW(targets, i == (int)target ? MF_CHECKED : 0, 300 + i,
                targetNames[i]);
  AppendMenuW(m, MF_POPUP | (worker || child ? MF_GRAYED : 0),
              (UINT_PTR)targets, L"Launch");
  AppendMenuW(m, child ? 0 : MF_GRAYED, RESTART, L"Restart session");
  AppendMenuW(m, child ? 0 : MF_GRAYED, STOP, L"Stop session");
  AppendMenuW(m, 0, SETTINGS, L"Settings");
  AppendMenuW(m, 0, LOGS, L"View logs");
  AppendMenuW(m, 0, QUIT, L"Exit Nivora");
  POINT pos;
  GetCursorPos(&pos);
  SetForegroundWindow(w);
  int choice = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pos.x, pos.y,
                              0, w, NULL);
  DestroyMenu(m);
  if (choice >= 300 && choice < 300 + TARGET_COUNT) {
    openWindow(HOME);
    selectTarget(choice - 300);
    beginLaunch();
  } else if (choice)
    command(choice);
}
static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (taskbar && msg == taskbar) {
    Shell_NotifyIconW(NIM_ADD, &tray);
    return 0;
  }
  switch (msg) {
  case WM_COMMAND:
    if (LOWORD(wp) == TARGET && HIWORD(wp) == CBN_SELCHANGE && !worker &&
        !child) {
      int index = SendMessageW(controls[TARGET], CB_GETCURSEL, 0, 0);
      selectTarget(index);
    } else if (LOWORD(wp) == MODE && HIWORD(wp) == CBN_SELCHANGE)
      EnableWindow(controls[PORT],
                   SendMessageW(controls[MODE], CB_GETCURSEL, 0, 0) != 0);
    else if (LOWORD(wp) == FILTER && HIWORD(wp) == CBN_SELCHANGE) {
      filter = SendMessageW(controls[FILTER], CB_GETCURSEL, 0, 0);
      refreshLog();
    } else if (HIWORD(wp) == BN_CLICKED)
      command(LOWORD(wp));
    return 0;
  case RESULT:
    finishLaunch((int)wp, (int)lp);
    return 0;
  case WM_TIMER:
    if (child && WaitForSingleObject(child, 0) == WAIT_OBJECT_0) {
      closeSession();
      status(3,
             L"Launch process ended. If Antigravity was already open, close it "
             L"before relaunching.",
             2);
      if (page == HOME)
        render(HOME);
    } else if (child && state == 1) {
      status(2, L"The local proxy responded and Antigravity is running.", 1);
      if (page == HOME)
        render(HOME);
      if (hideAfter)
        ShowWindow(h, SW_HIDE);
    }
    return 0;
  case TRAY:
    if (lp == WM_LBUTTONDBLCLK)
      openWindow(HOME);
    if (lp == WM_RBUTTONUP)
      menu();
    return 0;
  case WM_SIZE:
    if (wp == SIZE_MINIMIZED && minTray)
      ShowWindow(h, SW_HIDE);
    break;
  case WM_CLOSE:
    if (minTray)
      ShowWindow(h, SW_HIDE);
    else
      command(QUIT);
    return 0;
  case WM_QUERYENDSESSION:
    return TRUE;
  case WM_ENDSESSION:
    if (wp)
      DestroyWindow(h);
    return 0;
  case WM_DESTROY:
    closeSession();
    Shell_NotifyIconW(NIM_DELETE, &tray);
    PostQuitMessage(0);
    return 0;
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORBTN:
    SetTextColor((HDC)wp, INK);
    SetBkColor((HDC)wp, NAVY);
    return (LRESULT)bg;
  case WM_DRAWITEM: {
    DRAWITEMSTRUCT *d = (void *)lp;
    RECT r = d->rcItem;
    BOOL active = d->CtlID == page || d->CtlID == LAUNCH || d->CtlID == SAVE;
    HBRUSH b = CreateSolidBrush(active ? ACCENT : RGB(33, 42, 67));
    HGDIOBJ prev = SelectObject(d->hDC, b),
            pen = SelectObject(d->hDC, GetStockObject(NULL_PEN));
    RoundRect(d->hDC, 0, 0, r.right, r.bottom, px(12), px(12));
    SelectObject(d->hDC, pen);
    SelectObject(d->hDC, prev);
    DeleteObject(b);
    SetBkMode(d->hDC, TRANSPARENT);
    SetTextColor(d->hDC,
                 d->itemState & ODS_DISABLED ? RGB(154, 154, 177) : INK);
    wchar_t s[80];
    GetWindowTextW(d->hwndItem, s, 80);
    DrawTextW(d->hDC, s, -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    if (d->itemState & ODS_FOCUS)
      DrawFocusRect(d->hDC, &r);
    return TRUE;
  }
  case WM_PAINT: {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, INK);
    SelectObject(dc, title);
    TextOutW(dc, px(79), px(10), L"Nivora", 6);
    DrawIconEx(dc, px(30), px(17), icons[0], px(38), px(38), 0, NULL,
               DI_NORMAL);
    SelectObject(dc, font);
    SetTextColor(dc, RGB(155, 164, 202));
    TextOutW(dc, px(488), px(30), L"v1.1.0", 6);
    EndPaint(h, &ps);
    return 0;
  }
  }
  return DefWindowProcW(h, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, LPWSTR args, int show) {
  HANDLE lock = CreateMutexW(NULL, FALSE, L"Local\\Nivora-Launcher-v1");
  if (!lock || GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND other = FindWindowW(L"NivoraDesktop", NULL);
    if (other) {
      ShowWindow(other, SW_RESTORE);
      SetForegroundWindow(other);
    }
    return 0;
  }
  SetProcessDPIAware();
  HDC screen = GetDC(NULL);
  scale = GetDeviceCaps(screen, LOGPIXELSY);
  ReleaseDC(NULL, screen);
  WSADATA ws;
  if (WSAStartup(MAKEWORD(2, 2), &ws))
    return 1;
  OleInitialize(NULL);
  target = setting(L"Target", APP);
  if (target >= TARGET_COUNT)
    target = APP;
  loadPath();
  readText(L"Port", portText, sizeof(portText));
  mode = setting(L"Mode", 0);
  if (mode < 0 || mode > 2)
    mode = 0;
  hideAfter = setting(L"Hide", 1);
  minTray = setting(L"Tray", 1);
  notifyUser = setting(L"Notify", 1);
  logDisk = setting(L"Disk", 1);
  wchar_t local[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, local)) &&
      SUCCEEDED(StringCchCatW(local, MAX_PATH, L"\\Nivora"))) {
    CreateDirectoryW(local, NULL);
    StringCchPrintfW(logPath, MAX_PATH, L"%ls\\activity.log", local);
  }
  bg = CreateSolidBrush(NAVY);
  font = CreateFontW(-px(16), 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, CLEARTYPE_QUALITY,
                     0, L"Segoe UI");
  title = CreateFontW(-px(37), 0, 0, 0, 650, 0, 0, 0, 1, 0, 0,
                      CLEARTYPE_QUALITY, 0, L"Segoe UI");
  for (int i = 0; i < 5; i++)
    icons[i] = LoadIconW(inst, MAKEINTRESOURCEW(10 + i));
  WNDCLASSW c = {0};
  c.lpfnWndProc = proc;
  c.hInstance = inst;
  c.hbrBackground = bg;
  c.hCursor = LoadCursor(NULL, IDC_ARROW);
  c.hIcon = icons[0];
  c.lpszClassName = L"NivoraDesktop";
  RegisterClassW(&c);
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                WS_CLIPCHILDREN;
  RECT r = {0, 0, px(592), px(536)};
  AdjustWindowRect(&r, style, FALSE);
  w = CreateWindowW(c.lpszClassName, L"Nivora", style, CW_USEDEFAULT,
                    CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL,
                    NULL, inst, NULL);
  if (!w)
    return 1;
  tray.cbSize = sizeof(tray);
  tray.hWnd = w;
  tray.uID = 1;
  tray.uCallbackMessage = TRAY;
  tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
  tray.hIcon = icons[0];
  Shell_NotifyIconW(NIM_ADD, &tray);
  taskbar = RegisterWindowMessageW(L"TaskbarCreated");
  logLine(0, L"Nivora opened.");
  render(HOME);
  ShowWindow(w, SW_SHOW);
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    if (!IsDialogMessageW(w, &msg)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
  }
  OleUninitialize();
  CloseHandle(lock);
  return 0;
}
