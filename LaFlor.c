#define WIN32_LEAN_AND_MEAN
#define NTDDI_VERSION 0x05010000 /* NTDDI_WINXP */
#define _WIN32_WINNT 0x0501      /* _WIN32_WINNT_WINXP */
#define WINVER 0x0501

#include <windows.h>

#include <shellapi.h>
#include <shlwapi.h>

#include <assert.h>
#include <stdbool.h>

/* define for the custom message that's sent when some user input is directed at
 * our application's notification icon. since the message identifier namespace
 * is shared with standard Windows messages, this cannot just be any number :
 * application-specific private messages should use the range from WM_APP to
 * 0xBFFF. */
#define NOTIFYICON_ID WM_APP

/* command identifiers for menu items. the IDM_ prefix is customary, since the
 * Resource Compiler uses them when defining resource-based menus. this
 * convention is preserved here for readability. */
#define IDM_ENABLED 1
#define IDM_QUIT 2

/* the "interval" and "delta" menu entires are generated dynamically based on
 * the number of entires in respective arrays : therefore, a pair of _START and
 * _END defines is created for each of these instead of a hardcoded set. */
#define IDM_INTERVAL_START 3
static const int predefIntervals[] = {1000, 5000, 10000, 30000, 60000};
#define IDM_INTERVAL_END (IDM_INTERVAL_START + ARRAYSIZE(predefIntervals))
#define IDM_INTERVAL_CUSTOM IDM_INTERVAL_END

#define SEEK_PREDEF_MACRO(arr, val)                                            \
  for (int i__ = 0; i__ < ARRAYSIZE(arr); ++i__) {                             \
    if (arr[i__] == val) {                                                     \
      return i__;                                                              \
    }                                                                          \
  }                                                                            \
  return -1

static int seekPredefInterval(int val) {
  SEEK_PREDEF_MACRO(predefIntervals, val);
}

#define IDM_DELTA_START (IDM_INTERVAL_CUSTOM + 1)
static const int predefDeltas[] = {1, 5, 10, 30, 60};
#define IDM_DELTA_END (IDM_DELTA_START + ARRAYSIZE(predefDeltas))
#define IDM_DELTA_CUSTOM IDM_DELTA_END

static int seekPredefDelta(int val) { SEEK_PREDEF_MACRO(predefDeltas, val); }

/* the timer ID of the main timer that's created when a timer is associated with
 * the application's main (and only) window handle. */
#define TIMER_EVENT_ID 1

/* the main application state struct that's associated with the given window
 * handle. */
struct AppState {
  HINSTANCE app;
  HWND wnd;
  UINT_PTR timerId;
  int interval;
  int delta;
  int currentDeltaX;
  int currentDeltaY;
  bool active;
};

static HICON getNotificationIcon(HINSTANCE app, bool active) {
  return LoadIconW(app, active ? L"IDI_ICON1" : L"IDI_ICON1_BW");
}

static void notifyIconDataCommonInit(NOTIFYICONDATAW *out, HWND wnd) {
  /* in order to keep compatibility with Windows XP, we use
   * NOTIFYICONDATAW_V2_SIZE and NOTIFYICON_VERSION instead of the newer values
   * (_V3_SIZE and _VERSION_4 respectively) available since Vista. in the older
   * version, the custom message's wparam specifies the icon ID (specified as
   * the uID member of NOTIFYICONDATA), and its lparam specifies the actual
   * event that occurred on the notification icon.
   *
   * the newer version is admittedly a bit easier to use, as it encodes the
   * actual event and the icon ID in the lparam, and X/Y coordinates of the
   * related event in the wparam. this means that it's not necessary to call
   * GetCursorPos() when processing the event, whose return value might be
   * different to the location where the event actually occurred.
   */
  memset(out, 0, sizeof(*out));
  out->cbSize = NOTIFYICONDATAW_V2_SIZE;
  out->uVersion = NOTIFYICON_VERSION;
  out->hWnd = wnd;
  out->uID = NOTIFYICON_ID;
}

static void changeNotificationIcon(HINSTANCE app, HWND wnd, bool active) {
  NOTIFYICONDATAW data;
  notifyIconDataCommonInit(&data, wnd);
  data.uFlags |= NIF_ICON; /* the only thing we want to change is the icon and
                              leave the rest untouched. */
  data.hIcon = getNotificationIcon(app, active);
  Shell_NotifyIconW(NIM_MODIFY, &data);
}

static void getNewDelta(struct AppState *state, int newval, int maxval,
                        int *delta) {
  const int wantedDelta = state->delta;
  if (newval >= maxval) {
    *delta = (-1 * wantedDelta);
  } else if (newval <= 0) {
    *delta = wantedDelta;
  }
}

static void getNewDeltas(struct AppState *state) {
  POINT p;
  GetCursorPos(&p);
  const int newx = p.x + state->currentDeltaX;
  const int newy = p.y + state->currentDeltaY;

  const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
  const int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

  getNewDelta(state, newx, width, &state->currentDeltaX);
  getNewDelta(state, newy, height, &state->currentDeltaY);
}

static void setNewDelta(struct AppState *state, int wantedDelta) {
  state->delta = wantedDelta;
  if (state->currentDeltaX < 0) {
    state->currentDeltaX = state->currentDeltaY = (-1 * wantedDelta);
  } else {
    state->currentDeltaX = state->currentDeltaY = wantedDelta;
  }
  getNewDeltas(state);
}

static void setTimerEnabled(struct AppState *state, bool enabled) {
  /* enable or disable the timer, according to the value of the "enabled"
   * parameter. TimerProc is not used here, which means that the timer ticks
   * will be sent to the main message loop as WM_TIMER messages and reach the
   * window function, where the state struct pointer can be accessed via the
   * associated window handle. */
  setNewDelta(state, state->delta);
  if (enabled) {
    assert(state->timerId == 0);
    state->timerId = SetTimer(state->wnd, TIMER_EVENT_ID, state->interval, 0);
  } else {
    assert(state->timerId);
    KillTimer(state->wnd, TIMER_EVENT_ID);
    state->timerId = 0;
  }
}

static void restartTimer(struct AppState *state) {
  assert(state->timerId);
  state->timerId = SetTimer(state->wnd, TIMER_EVENT_ID, state->interval, 0);
}

static void setNewInterval(struct AppState *state, int interval) {
  state->interval = interval;
  if (state->timerId) {
    restartTimer(state);
  }
}

static void moveMouse(struct AppState *state) {
  /* just synthesize user input moving the mouse by the given number of pixels :
   * currentDelta{X,Y} are updated in order to bounce back by helper functions
   * when reaching the horizontal/vertical end of the screen. */
  INPUT inp;
  memset(&inp, 0, sizeof(inp));
  inp.type = INPUT_MOUSE;
  inp.mi.dx = state->currentDeltaX;
  inp.mi.dy = state->currentDeltaY;
  inp.mi.dwFlags = MOUSEEVENTF_MOVE;
  getNewDeltas(state);
  SendInput(1, &inp, sizeof(inp));
}

static void commonAppendMenuItem(HMENU menu, int extraFlags,
                                 UINT_PTR menuItemId, const wchar_t *label) {
  AppendMenuW(menu, MF_STRING | extraFlags, menuItemId, label);
}

static HMENU commonCreateMenu(const int *vals, int numVals, int extraFlags,
                              int currentSelected, int idmStart,
                              void (*formatFn)(wchar_t *, int, int)) {
  HMENU rv = CreatePopupMenu();
  for (int i = 0; i < numVals; ++i) {
    wchar_t buf[256];
    formatFn(buf, ARRAYSIZE(buf), vals[i]);
    const int checkedFlag = (currentSelected == i) ? MF_CHECKED : MF_UNCHECKED;
    commonAppendMenuItem(rv, extraFlags | checkedFlag, (UINT_PTR)idmStart + i,
                         buf);
  }
  const int checkedFlag = (currentSelected == -1) ? MF_CHECKED : MF_UNCHECKED;
  commonAppendMenuItem(rv, checkedFlag, (UINT_PTR)idmStart + numVals,
                       L"Custom...");
  return rv;
}

static void intervalFormat(wchar_t *buf, int bufLen, int value) {
  wnsprintfW(buf, bufLen, L"%d s", value / 1000);
}

static HMENU createIntervalsMenu(const struct AppState *state, int extraFlags) {
  int selectedIntervalIdx = seekPredefInterval(state->interval);
  return commonCreateMenu(predefIntervals, ARRAYSIZE(predefIntervals),
                          extraFlags, selectedIntervalIdx, IDM_INTERVAL_START,
                          intervalFormat);
}

static void deltaFormat(wchar_t *buf, int bufLen, int value) {
  wnsprintfW(buf, bufLen, L"%d px", value);
}

static HMENU createDeltasMenu(const struct AppState *state, int extraFlags) {
  int selectedDeltaIdx = seekPredefDelta(state->delta);
  return commonCreateMenu(predefDeltas, ARRAYSIZE(predefDeltas), extraFlags,
                          selectedDeltaIdx, IDM_DELTA_START, deltaFormat);
}

static HMENU createMenu(const struct AppState *state) {
  HMENU rv = CreatePopupMenu();
  AppendMenuW(rv, (state->active ? MF_CHECKED : MF_UNCHECKED) | MF_STRING,
              IDM_ENABLED, L"Enabled");
  AppendMenuW(rv, MF_SEPARATOR, 0, 0);

  /* the top-level menu takes ownership of submenus : see the comment around
   * DestroyMenu() about that. */
  const int grayFlag = state->active ? 0 : MF_GRAYED;
  HMENU intervalSubmenu = createIntervalsMenu(state, grayFlag);
  AppendMenuW(rv, MF_POPUP | MF_STRING | grayFlag, (UINT_PTR)intervalSubmenu,
              L"Interval");

  HMENU deltaSubmenu = createDeltasMenu(state, grayFlag);
  AppendMenuW(rv, MF_POPUP | MF_STRING | grayFlag, (UINT_PTR)deltaSubmenu,
              L"Delta");

  AppendMenuW(rv, MF_SEPARATOR, 0, 0);
  AppendMenuW(rv, MF_STRING, IDM_QUIT, L"Quit");
  return rv;
}

#define ID_HELP 150
#define ID_TEXT 200

static BOOL CALLBACK DialogProc(HWND hwndDlg, UINT message, WPARAM wParam,
                                LPARAM lParam) {
  switch (message) {
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDOK:
    case IDCANCEL:
      EndDialog(hwndDlg, wParam);
      return TRUE;
    }
  }
  return FALSE;
}

static void *dwordAlign(void *ptr) {
  uintptr_t p = (uintptr_t)ptr;
  p += 3;
  p >>= 2;
  p <<= 2;
  return (void *)p;
}

static void *memcpy_incr(void *dst, const void *src, size_t len) {
  memcpy(dst, src, len);
  return ((char *)dst) + len;
}

static void *initDLGTEMPLATEEX(unsigned char *buf, DWORD helpID, DWORD exStyle,
                               DWORD style, WORD cDlgItems, short x, short y,
                               short cx, short cy, const wchar_t *title) {
  const WORD dlgVer = 1;
  const WORD signature = 0xFFFF;
  style |= (DS_SETFONT | DS_SHELLFONT);
  buf = memcpy_incr(buf, &dlgVer, sizeof(dlgVer));
  buf = memcpy_incr(buf, &signature, sizeof(signature));
  buf = memcpy_incr(buf, &helpID, sizeof(helpID));
  buf = memcpy_incr(buf, &exStyle, sizeof(exStyle));
  buf = memcpy_incr(buf, &style, sizeof(style));
  buf = memcpy_incr(buf, &cDlgItems, sizeof(cDlgItems));
  buf = memcpy_incr(buf, &x, sizeof(x));
  buf = memcpy_incr(buf, &y, sizeof(y));
  buf = memcpy_incr(buf, &cx, sizeof(cx));
  buf = memcpy_incr(buf, &cy, sizeof(cy));
  memset(buf, 0, 4); /* no menu, default window class */
  buf += 4;
  buf = memcpy_incr(buf, title, (wcslen(title) + 1) * sizeof(*title));

  /* the values here are based on the default values when creating dialogs via
   * MSVC's resource editor. not using DS_SETFONT and DS_SHELLFONT and not
   * appending font data to the dialog template results in an ugly Fixedsys-like
   * font being used, which makes it look like Windows 3.1. */
  const WORD pointsize = 8;
  const WORD weight = FW_NORMAL;
  const BYTE italic = 0;
  const BYTE charset = DEFAULT_CHARSET;
  const wchar_t typeface[] = L"MS Shell Dlg";
  buf = memcpy_incr(buf, &pointsize, sizeof(pointsize));
  buf = memcpy_incr(buf, &weight, sizeof(weight));
  buf = memcpy_incr(buf, &italic, sizeof(italic));
  buf = memcpy_incr(buf, &charset, sizeof(charset));
  buf = memcpy_incr(buf, typeface, sizeof(typeface));
  return dwordAlign(buf);
}

static void *initDLGITEMTEMPLATEEX(unsigned char *buf, DWORD helpID,
                                   DWORD exStyle, DWORD style, short x, short y,
                                   short cx, short cy, DWORD id,
                                   WORD windowClass, const wchar_t *title) {
  buf = memcpy_incr(buf, &helpID, sizeof(helpID));
  buf = memcpy_incr(buf, &exStyle, sizeof(exStyle));
  buf = memcpy_incr(buf, &style, sizeof(style));
  buf = memcpy_incr(buf, &x, sizeof(x));
  buf = memcpy_incr(buf, &y, sizeof(y));
  buf = memcpy_incr(buf, &cx, sizeof(cx));
  buf = memcpy_incr(buf, &cy, sizeof(cy));
  buf = memcpy_incr(buf, &id, sizeof(id));

  memset(buf, 0xFF, 2);
  memcpy(buf + 2, &windowClass, sizeof(windowClass));
  buf += 4;

  buf = memcpy_incr(buf, title, wcslen(title) * sizeof(*title));
  memset(buf, 0, 4); /* title terminator + zero size of extra data */
  buf += 4;
  return dwordAlign(buf);
}

static LRESULT DisplayMyMessage(HINSTANCE hinst, HWND hwndOwner,
                                const wchar_t *label) {
  unsigned char buf[1024] = {0};

  void *dlg = initDLGTEMPLATEEX(buf, 0, 0,
                                WS_POPUP | WS_SYSMENU | WS_CAPTION |
                                    DS_MODALFRAME | DS_CENTERMOUSE,
                                3, 10, 10, 100, 100, L"My Dialog");
  dlg =
      initDLGITEMTEMPLATEEX(dlg, 0, 0, WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                            10, 70, 80, 20, IDOK, 0x0080, L"OK");
  dlg = initDLGITEMTEMPLATEEX(dlg, 0, 0, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                              55, 10, 40, 20, ID_HELP, 0x0080, L"dupa");
  dlg = initDLGITEMTEMPLATEEX(dlg, 0, 0, WS_CHILD | WS_VISIBLE | SS_LEFT, 10,
                              10, 40, 20, ID_TEXT, 0x0082, label);

  return DialogBoxIndirectW(hinst, (const DLGTEMPLATE *)buf, hwndOwner,
                            DialogProc);
}

static int getCustomInterval(struct AppState *state) {
  DisplayMyMessage(state->app, state->wnd, L"foobar");
  return 2222;
}

static int getCustomDelta(struct AppState *state) { return 26; }

static void toggleEnabled(struct AppState *state) {
  state->active ^= 1;
  setTimerEnabled(state, state->active);
  changeNotificationIcon(state->app, state->wnd, state->active);
}

static void onMenuItemClicked(int itemId, struct AppState *state, HWND wnd) {
  if (itemId == IDM_QUIT) {
    /* this seems to be the most "proper" way of asking the application to
     * terminate itself :
     * - using any standard C facilities is out of the question, since they kill
     * the process instantly and don't let the message queue terminate properly,
     * - PostQuitMessage() will lead to memory leaks since the window is never
     * actually destroyed, as the message queue processing instantly ends after
     * receiving such a message, having no chance to actually process any
     * destruction-related messages.
     * - sending a window a WM_DESTROY message is "is like prank calling
     * somebody pretending to be the police" according to Raymond Chen, whom I
     * dare not doubt
     * (https://devblogs.microsoft.com/oldnewthing/20110926-00/?p=9553)
     * - the article mentioned above also mentions that the window functions's
     * default behaviour (i.e. DefWindowProc's) is to call DestroyWindow() on a
     * WM_CLOSE, which makes everything come together nicely.
     */
    SendMessage(wnd, WM_CLOSE, 0, 0);
  } else if (itemId == IDM_ENABLED) {
    toggleEnabled(state);
  } else if (itemId >= IDM_INTERVAL_START && itemId < IDM_INTERVAL_END) {
    setNewInterval(state, predefIntervals[itemId - IDM_INTERVAL_START]);
  } else if (itemId == IDM_INTERVAL_CUSTOM) {
    int interval = getCustomInterval(state);
    if (interval != -1) {
      setNewInterval(state, interval);
    }
  } else if (itemId >= IDM_DELTA_START && itemId < IDM_DELTA_END) {
    setNewDelta(state, predefDeltas[itemId - IDM_DELTA_START]);
  } else if (itemId == IDM_DELTA_CUSTOM) {
    int delta = getCustomDelta(state);
    if (delta != -1) {
      setNewDelta(state, delta);
    }
  }
}

static LRESULT onTaskbarIconEvent(struct AppState *state, HWND wnd, UINT msg) {
  switch (msg) {
  case WM_RBUTTONUP: {
    /* the version of Shell_NotifyIcon that we use does not support passing any
     * extra information about the location of the related event, so we need to
     * call GetCursorPos() ourselves. see further comments about this. */
    POINT position;
    GetCursorPos(&position);

    /* the documentation for TrackPopupMenuEx() notes in its "Remarks" section
     * that it will be impossible for the user to dismiss the menu by clicking
     * away from it, unless the window associated with the menu is the current
     * foreground window. I actually observed this as far back as Windows XP,
     * and even Wine.
     *
     * to be truthfully honest, I have no idea if this is okay to just call this
     * function blindly like this, but our window isn't visible anyway : it
     * doesn't even have any graphical representation due to being message-only,
     * so it cannot take any input events as far as I'm aware. however, setting
     * it as foreground seems to work correctly with regard to how the
     * notification icon menu behaves. */
    SetForegroundWindow(wnd);

    HMENU menu = createMenu(state);
    /* TrackPopupMenuEx() doesn't return until the menu is closed - the message
     * loop is not blocked, so it must run its own loop inside. this means that
     * it's possible to "wait" for the user's input : we leverage this by
     * setting TPM_RETURNCMD, which conveniently returns the selected item ID as
     * the function's return value. */
    int menuRv = TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                  position.x, position.y, wnd, 0);
    if (menuRv != 0) {
      onMenuItemClicked(menuRv, state, wnd);
    }

    /* any menus not associated with a window must be explicitly destroyed.
     * DestroyMenu() works recursively, i.e. it destroys any submenus as well.
     */
    DestroyMenu(menu);
    break;
  }
  case WM_LBUTTONDBLCLK:
    toggleEnabled(state);
    break;
  }
  return 0;
}

static LRESULT CALLBACK windowProc(HWND wnd, UINT msg, WPARAM wparam,
                                   LPARAM lparam) {
  /* get back the state struct pointer, saved when processing WM_NCCREATE. it
   * points to the original state struct created in main() and is the central
   * point of storing any state variables.
   *
   * using GWLP_USERDATA means that it's not necessary to specify any values in
   * the cbWndExtra member of the WNDCLASSEX structure passed to RegisterClassEx
   * : GWLP_USERDATA is "always there". using cbWndExtra would be necessary if
   * one wanted to use positive offsets in {Set,Get}WindowLongPtr functions, but
   * that's just not necessary in this case.
   *
   * I originally thought that it might be possible to set cbWndExtra to
   * sizeof(struct AppData), which would essentially make the struct "embedded"
   * into the system-allocated window structure : this would mean one level of
   * indirection less. however, it seems like there's no way to get a pointer to
   * the "start of cbWndExtra area", and the window structure is obviously
   * opaque, so there's no way to get an offset to its last member. this means
   * that this is the best that we're left with, not including weird hackish
   * stuff that I'm sure is possible either way, but totally unnecessary here.
   */
  struct AppState *state = (void *)GetWindowLongPtrW(wnd, GWLP_USERDATA);
  switch (msg) {
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  case NOTIFYICON_ID:
    /* see comments in notifyIconDataCommonInit() */
    assert(wparam == NOTIFYICON_ID);
    return onTaskbarIconEvent(state, wnd, LOWORD(lparam));
  case WM_TIMER:
    moveMouse(state);
    return 0;
  case WM_NCCREATE: {
    /* this message is sent to the window procedure before any other messages.
     * the lpCreateParams member of CREATESTRUCT is the last parameter
     * originally passed to CreateWindow(Ex), which in our case is a pointer to
     * the state struct. */
    CREATESTRUCT *create = (void *)lparam;
    SetWindowLongPtrW(wnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
  }
  }
  return DefWindowProcW(wnd, msg, wparam, lparam);
}

static int createNotificationIcon(HWND wnd, HICON icon) {
  NOTIFYICONDATAW notifyIconData;
  notifyIconDataCommonInit(&notifyIconData, wnd);
  notifyIconData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  notifyIconData.uCallbackMessage = NOTIFYICON_ID;
  notifyIconData.hIcon = icon;

  const wchar_t tooltipText[] = L"La Flor";
  memcpy(notifyIconData.szTip, tooltipText, sizeof(tooltipText));

  return Shell_NotifyIconW(NIM_ADD, &notifyIconData) == TRUE;
}

static int removeNotificationIcon(HWND wnd) {
  NOTIFYICONDATAW notifyIconData;
  notifyIconDataCommonInit(&notifyIconData, wnd);
  return Shell_NotifyIconW(NIM_DELETE, &notifyIconData) == TRUE;
}

static const wchar_t LaFlorRegistryKey[] = L"SOFTWARE\\xavery\\LaFlor";

static int registryReadInteger(HKEY key, const wchar_t *subkey, int *rv) {
  unsigned int type;
  unsigned char buf[sizeof(int)];
  unsigned int bufsize = sizeof(buf);

  const LSTATUS stat = RegQueryValueExW(key, subkey, 0, &type, buf, &bufsize);
  if (stat == ERROR_SUCCESS && type == REG_DWORD) {
    memcpy(rv, buf, sizeof(*rv));
    return 0;
  } else {
    return 1;
  }
}

static void stateReadFromRegistry(struct AppState *state) {
  HKEY key;
  const LSTATUS ok =
      RegOpenKeyExW(HKEY_CURRENT_USER, LaFlorRegistryKey, 0, KEY_READ, &key);
  if (ok != ERROR_SUCCESS) {
    return;
  }

  int value;
  if (registryReadInteger(key, L"delta", &value) == 0) {
    setNewDelta(state, value);
  }
  if (registryReadInteger(key, L"interval", &value) == 0) {
    setNewInterval(state, value);
  }
  if (registryReadInteger(key, L"active", &value) == 0 && value) {
    toggleEnabled(state);
  }

  RegCloseKey(key);
}

static void stateSaveToRegistry(const struct AppState *state) {
  HKEY key;
  const LSTATUS ok = RegCreateKeyExW(HKEY_CURRENT_USER, LaFlorRegistryKey, 0, 0,
                                     0, KEY_WRITE, 0, &key, 0);
  if (ok != ERROR_SUCCESS) {
    return;
  }

  RegSetValueExW(key, L"delta", 0, REG_DWORD, (const BYTE *)&state->delta,
                 sizeof(state->delta));
  RegSetValueExW(key, L"interval", 0, REG_DWORD, (const BYTE *)&state->interval,
                 sizeof(state->interval));
  int value = state->active;
  RegSetValueExW(key, L"active", 0, REG_DWORD, (const BYTE *)&value,
                 sizeof(value));
  RegCloseKey(key);
}

static void initAppState(struct AppState *state, HINSTANCE hInstance) {
  memset(state, 0, sizeof(*state));
  state->app = hInstance;
  state->interval = predefIntervals[0];
  state->delta = predefDeltas[0];
}

int WINAPI wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
                    _In_ PWSTR pCmdLine, _In_ int nCmdShow) {
  struct AppState state;
  initAppState(&state, hInstance);

  int rv = 1;
  WNDCLASSEXW wndClass;
  memset(&wndClass, 0, sizeof(wndClass));
  wndClass.cbSize = sizeof(wndClass);
  wndClass.hInstance = hInstance;
  wndClass.lpszClassName = L"LaFlor Root Window Class";
  wndClass.lpfnWndProc = windowProc;
  ATOM classAtom = RegisterClassExW(&wndClass);
  if (classAtom == 0) {
    goto beach;
  }

  /* since we have actually no use for any actual (visible) windows, the one and
   * only window structure in the program is created as a "message-only" window
   * (https://docs.microsoft.com/en-us/windows/win32/winmsg/window-features#message-only-windows)
   * by specifying HWND_MESSAGE as its parent. the only thing that we need a
   * window structure for is dispatching and processing messages, and this is
   * exactly what this special kind of window is for. */
  HWND wnd = CreateWindowExW(0, MAKEINTATOM(classAtom), L"LaFlor Root Window",
                             0, 0, 0, 0, 0, HWND_MESSAGE, 0, hInstance, &state);
  if (wnd == 0) {
    goto beach;
  }
  state.wnd = wnd;

  /* the icon is always created as inactive. if needed, this will be changed
   * after reading registry values : this is done in order not to unnecessarily
   * call notification icon related functions with a null icon ID. */
  if (!createNotificationIcon(wnd, getNotificationIcon(hInstance, false))) {
    goto beach;
  }

  /* reading values from registry might start the timer, which needs a valid
   * window handle. */
  stateReadFromRegistry(&state);

  MSG msg;
  BOOL getMsgRv;
  /* as the documentation for GetMessageW() notes, the return value of this
   * function is actually an integer and not a boolean : the value can be either
   * nonzero, zero, or -1, thus the commonly seen
   *
   * while (GetMessage( lpMsg, hWnd, 0, 0))
   *
   * is slightly wrong though has no real consequences as it works out to the
   * exact same thing. */
  while ((getMsgRv = GetMessageW(&msg, 0, 0, 0)) != 0) {
    if (getMsgRv == -1) {
      goto beach2;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  /* as noted in PostQuitMessage() documentation, the return value of the
   * program should be the wParam of a WM_QUIT message, which is also the value
   * passed to PostQuitMessage(). */
  rv = LOWORD(msg.wParam);
  stateSaveToRegistry(&state);

beach2:
  removeNotificationIcon(wnd);

beach:
  return rv;
}
