// Overlay: Dear ImGui drawn in the system opengl32!wglSwapBuffers, i.e. after ReShade's effects.
// Shows the "Mods" button on the main menu and the Mods screen; routes hotkeys; blocks game
// input (window messages + DirectInput) while the overlay has focus.
#include "loader.h"
#include <MinHook.h>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <backends/imgui_impl_opengl2.h>
#include <backends/imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

typedef BOOL(WINAPI* SwapBuffers_t)(HDC);
static SwapBuffers_t o_Swap;
static HWND s_hwnd;
static HGLRC s_glrc;
static bool s_open;          // Mods screen visible
static bool s_capture;       // overlay owns input this frame
static int s_selected = -1;  // -1 = WormsLoader page
static Hotkey* s_rebinding;
static Mod* s_rebinding_mod;
static unsigned s_screen;
static std::string s_block_dismissed;
static char s_relay_buf[128];
static const unsigned kScreenMain = djb2x("ScreenMain");

static unsigned read_screen_hash() {
    __try {
        int depth = *(int*)kScreenDepth;
        if (depth <= 0 || depth > 64) return 0;
        BYTE* obj = *(BYTE**)(kScreenStack + 8 * depth);
        return obj ? *(unsigned*)(obj + 8) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

static std::string key_name(int vk) {
    if (!vk) return "(unbound)";
    char buf[64] = {};
    LONG sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC) << 16;
    if (vk >= VK_PRIOR && vk <= VK_HELP) sc |= 1 << 24; // extended keys (arrows, ins/del, ...)
    if (!GetKeyNameTextA(sc, buf, sizeof buf)) sprintf_s(buf, "VK 0x%02X", vk);
    return buf;
}

static LRESULT CALLBACK h_WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
        bool repeat = (lp >> 30) & 1;
        if (s_rebinding && !repeat) {
            s_rebinding->vk = wp == VK_ESCAPE ? 0 : (int)wp;
            config_save(s_rebinding_mod);
            s_rebinding = nullptr;
            return 0;
        }
        if (s_open && wp == VK_ESCAPE) { s_open = false; return 0; }
        if (!s_open && !repeat) api_dispatch_hotkey((int)wp);
    }
    if (ImGui::GetCurrentContext()) {
        // Mouse position and buttons come from the game's cursor and DirectInput (see h_Swap). Windows mouse
        // messages carry the Windows cursor position, which is elsewhere, so ImGui never sees them.
        bool mouse = (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_MOUSELEAVE ||
                     (msg >= WM_NCMOUSEMOVE && msg <= WM_NCMBUTTONDBLCLK) || msg == WM_SETCURSOR;
        if (!mouse) ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp);
        // Mouse moves always reach the game (it hides the Windows cursor on them); clicks, wheel and keys don't
        // while the overlay has input.
        bool blocked = (msg > WM_MOUSEMOVE && msg <= WM_MOUSELAST) || (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
        if (blocked && s_capture) return 0;
    }
    return CallWindowProcA((WNDPROC)GetPropA(h, "wl_wndproc"), h, msg, wp, lp);
}

static void draw_setting(Mod* m, Setting* s) {
    bool changed = false, edited = false;
    // Numbers get a slider plus a typed input box; both save on release.
    float w = ImGui::CalcItemWidth();
    ImGui::PushID(s);
    switch (s->type) {
    case SettingType::Bool: changed = ImGui::Checkbox(s->label.c_str(), &s->value.b); break;
    case SettingType::Int:
        ImGui::SetNextItemWidth(w * .7f);
        ImGui::SliderInt("##slider", &s->value.i, s->imin, s->imax);
        edited = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w * .3f - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputInt(s->label.c_str(), &s->value.i, 0, 0);
        s->value.i = s->value.i < s->imin ? s->imin : s->value.i > s->imax ? s->imax : s->value.i;
        break;
    case SettingType::Float:
        ImGui::SetNextItemWidth(w * .7f);
        ImGui::SliderFloat("##slider", &s->value.f, s->fmin, s->fmax);
        edited = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w * .3f - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputFloat(s->label.c_str(), &s->value.f, 0, 0, "%.2f");
        s->value.f = s->value.f < s->fmin ? s->fmin : s->value.f > s->fmax ? s->fmax : s->value.f;
        break;
    case SettingType::Enum: {
        std::vector<const char*> items;
        for (auto& o : s->options) items.push_back(o.c_str());
        changed = ImGui::Combo(s->label.c_str(), &s->value.i, items.data(), (int)items.size());
        break;
    }
    }
    // Sliders fire every drag step; saving on release keeps the ini writes cheap.
    if (changed || edited || ImGui::IsItemDeactivatedAfterEdit()) config_save(m);
    ImGui::PopID();
}

static void draw_loader_page() {
    ImGui::TextWrapped("WormsLoader loads the mods in <game>\\Mods. Online, everyone in a lobby must run the same "
                       "game build and the same mods (client-only mods excepted); other lobbies are refused.");
    ImGui::Separator();
    ImGui::Text("Game build: %s", g_known_build ? "01.83.31.37 (supported)" : "UNKNOWN - mods disabled");
    ImGui::Text("Network: %s", g_net_status.c_str());
    ImGui::Text("Mod set: %08x  %s", g_modset_hash, g_modset.empty() ? "(none)" : g_modset.c_str());
    ImGui::Separator();
    ImGui::TextWrapped("Relay server (host:port). Used for lobbies YOU host; joiners follow the host. Empty = Steam P2P.");
    if (ImGui::InputText("Relay", s_relay_buf, sizeof s_relay_buf, ImGuiInputTextFlags_EnterReturnsTrue) ||
        ImGui::IsItemDeactivatedAfterEdit())
        loader_ini_set("relay", s_relay_buf);
}

static void draw_mod_page(Mod* m) {
    ImGui::Text("%s  v%s", m->name.c_str(), m->version.c_str());
    if (!m->author.empty()) ImGui::TextDisabled("by %s", m->author.c_str());
    if (m->client_only) ImGui::TextDisabled("client-only (does not affect other players)");
    if (!m->error.empty()) { ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "Error: %s", m->error.c_str()); return; }
    ImGui::Separator();
    ImGui::TextWrapped("%s", m->description.c_str());
    if (!m->settings.empty()) {
        ImGui::SeparatorText("Settings");
        for (Setting* s : m->settings) draw_setting(m, s);
    }
    if (!m->hotkeys.empty()) {
        ImGui::SeparatorText("Hotkeys");
        for (Hotkey& h : m->hotkeys) {
            ImGui::PushID(&h);
            std::string label = s_rebinding == &h ? "Press a key... (Esc = unbind)" : key_name(h.vk);
            if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetFontSize() * 14, 0))) { s_rebinding = &h; s_rebinding_mod = m; }
            ImGui::SameLine();
            ImGui::TextUnformatted(h.label.c_str());
            ImGui::PopID();
        }
    }
}

static void draw() {
    ImGuiIO& io = ImGui::GetIO();
    unsigned screen = g_known_build ? read_screen_hash() : 0;
    if (screen != s_screen) { wl_log("screen -> %08x", screen); s_screen = screen; }

    if (!g_known_build && ImGui::GetTime() < 20) {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::Begin("##wlwarn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs);
        ImGui::TextColored(ImVec4(1, .6f, .2f, 1), "WormsLoader: unknown game build, mods disabled.");
        ImGui::End();
    }

    if (!g_block_reason.empty() && g_block_reason != s_block_dismissed) {
        ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_Always, ImVec2(.5f, .5f));
        ImGui::Begin("WormsLoader: lobby refused", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted(g_block_reason.c_str());
        ImGui::TextDisabled("Leave this lobby; the match cannot connect.");
        if (ImGui::Button("OK")) s_block_dismissed = g_block_reason;
        ImGui::End();
    }

    if (screen == kScreenMain && !s_open) {
        // Bottom-left, above the game's news ticker (which sits at ~88-93% of the height).
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .02f, io.DisplaySize.y * .86f), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::Begin("##wlbtn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
        ImGui::SetWindowFontScale(1.5f);
        if (ImGui::Button("Mods", ImVec2(io.DisplaySize.x * .1f, 0))) s_open = true;
        ImGui::End();
    }

    if (s_open) {
        io.MouseDrawCursor = true;
        ImGui::SetNextWindowPos(io.DisplaySize * 0.5f, ImGuiCond_Appearing, ImVec2(.5f, .5f));
        ImGui::SetNextWindowSize(io.DisplaySize * ImVec2(.6f, .65f), ImGuiCond_Appearing);
        if (ImGui::Begin("Mods", &s_open, ImGuiWindowFlags_NoCollapse)) {
            ImGui::BeginChild("list", ImVec2(ImGui::GetContentRegionAvail().x * .3f, 0), ImGuiChildFlags_Borders);
            if (ImGui::Selectable("WormsLoader", s_selected == -1)) s_selected = -1;
            ImGui::Separator();
            for (int i = 0; i < (int)g_mods.size(); i++) {
                Mod* m = g_mods[i];
                std::string label = m->name + "  v" + m->version + (m->error.empty() ? "" : "  (error)");
                if (ImGui::Selectable(label.c_str(), s_selected == i)) s_selected = i;
            }
            if (g_mods.empty()) ImGui::TextDisabled(g_known_build ? "No mods installed" : "Mods disabled");
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("detail", ImVec2(0, 0));
            if (s_selected < 0 || s_selected >= (int)g_mods.size()) draw_loader_page(); else draw_mod_page(g_mods[s_selected]);
            ImGui::EndChild();
        }
        ImGui::End();
    } else {
        io.MouseDrawCursor = false;
        s_rebinding = nullptr;
    }
    s_capture = s_open || io.WantCaptureMouse;
}

static void create_context(HWND hwnd) {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // Designed at 720p; scale fonts and sizes to the window height.
    RECT rc; GetClientRect(hwnd, &rc);
    float scale = (rc.bottom - rc.top) / 720.0f;
    if (scale < 1) scale = 1;
    ImFontConfig font;
    font.SizePixels = 13.0f * scale;
    io.Fonts->AddFontDefault(&font);
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = st.FrameRounding = st.ChildRounding = 6;
    st.FramePadding = ImVec2(8, 4);
    ImVec4* c = st.Colors; // the game's brown/cream menu palette
    c[ImGuiCol_WindowBg] = ImVec4(.24f, .16f, .10f, .96f);
    c[ImGuiCol_ChildBg] = ImVec4(.18f, .12f, .07f, .60f);
    c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] = ImVec4(.45f, .28f, .15f, 1);
    c[ImGuiCol_Button] = c[ImGuiCol_FrameBg] = c[ImGuiCol_Header] = ImVec4(.55f, .36f, .20f, 1);
    c[ImGuiCol_ButtonHovered] = c[ImGuiCol_FrameBgHovered] = c[ImGuiCol_HeaderHovered] = ImVec4(.70f, .48f, .26f, 1);
    c[ImGuiCol_ButtonActive] = c[ImGuiCol_FrameBgActive] = c[ImGuiCol_HeaderActive] = ImVec4(.85f, .60f, .30f, 1);
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = c[ImGuiCol_SliderGrabActive] = ImVec4(1, .85f, .35f, 1);
    c[ImGuiCol_Border] = c[ImGuiCol_Separator] = ImVec4(.75f, .55f, .30f, .6f);
    c[ImGuiCol_Text] = ImVec4(.98f, .93f, .80f, 1);
    st.ScaleAllSizes(scale);
    ImGui_ImplOpenGL2_Init();
    strncpy_s(s_relay_buf, loader_ini("relay", "").c_str(), _TRUNCATE);
}

// Alt-tab / focus loss makes the game recreate its window and GL context: follow it.
static void attach(HWND hwnd, HGLRC glrc) {
    if (!ImGui::GetCurrentContext()) create_context(hwnd);
    if (hwnd != s_hwnd) {
        if (s_hwnd) ImGui_ImplWin32_Shutdown();
        ImGui_ImplWin32_Init(hwnd);
        if (!GetPropA(hwnd, "wl_wndproc"))
            SetPropA(hwnd, "wl_wndproc", (HANDLE)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)h_WndProc));
    }
    // New context = our font texture is gone. Not deleted first: in the new context that id may be a game texture.
    if (s_glrc && glrc != s_glrc) ImGui_ImplOpenGL2_CreateFontsTexture();
    wl_log("overlay attached to window %p, GL context %p", hwnd, glrc);
    s_hwnd = hwnd;
    s_glrc = glrc;
}

// The game may own the mouse through DirectInput, in which case no WM_LBUTTON* arrive:
// the button state is also taken from the DirectInput hooks below.
static volatile bool s_di_lmb;

// The game draws its own cursor, moved by DirectInput deltas only, so it drifts away from the Windows cursor.
// Each local player has a cursor slot (float x, y in GUI space, centre 0,0, Y up). Which slot the mouse drives
// differs between launches, so the overlay follows the slot that moved last. GUI bounds sit after the slots.
static bool game_cursor(ImVec2 display, float* x, float* y) {
    if (!g_known_build) return false;
    const float* bd = (const float*)kCursorBounds; // min x, min y, max x, max y
    float w = bd[2] - bd[0], h = bd[3] - bd[1];
    if (!(w > 1 && h > 1)) return false;
    static float last[kCursorSlots][2];
    static int active = -1;
    for (int k = 0; k < kCursorSlots; k++) {
        const float* c = (const float*)(kCursorSlot0 + k * kCursorStride);
        if (c[0] == last[k][0] && c[1] == last[k][1]) continue;
        bool inside = c[0] >= bd[0] && c[0] <= bd[2] && c[1] >= bd[1] && c[1] <= bd[3];
        if (inside && (last[k][0] != 0 || last[k][1] != 0)) active = k; // a move, not the first read
        last[k][0] = c[0]; last[k][1] = c[1];
    }
    if (active < 0) return false;
    *x = (last[active][0] - bd[0]) / w * display.x;
    *y = (bd[3] - last[active][1]) / h * display.y;
    return true;
}

static bool s_lmb_sent;

static BOOL WINAPI h_Swap(HDC dc) {
    HWND hwnd = WindowFromDC(dc);
    HGLRC glrc = wglGetCurrentContext();
    if (hwnd && glrc && (hwnd != s_hwnd || glrc != s_glrc)) attach(hwnd, glrc);
    if (hwnd && hwnd == s_hwnd) {
        api_run_frame(); // before t0: mods like FpsCap wait in here, which is not overlay cost
        double t0 = wl_time();
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        POINT p;
        float gx, gy;
        if (game_cursor(io.DisplaySize, &gx, &gy)) io.AddMousePosEvent(gx, gy);
        else if (GetForegroundWindow() == hwnd && GetCursorPos(&p) && ScreenToClient(hwnd, &p)) io.AddMousePosEvent((float)p.x, (float)p.y);
        if (s_di_lmb != s_lmb_sent) { io.AddMouseButtonEvent(0, s_di_lmb); s_lmb_sent = s_di_lmb; }
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());

        // Cost report every 60 s, to tell overlay cost apart from other slowdowns.
        static double spent, since = t0;
        static int frames;
        spent += wl_time() - t0;
        frames++;
        if (t0 - since > 60) {
            wl_log("perf: %.1f fps, overlay %.3f ms/frame", frames / (t0 - since), spent * 1000 / frames);
            spent = 0; frames = 0; since = t0;
        }
    }
    return o_Swap(dc);
}

void ui_install() {
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\opengl32.dll"); // the real one; <game>\opengl32.dll is ReShade
    HMODULE gl = LoadLibraryA(path);
    void* swap = gl ? (void*)GetProcAddress(gl, "wglSwapBuffers") : nullptr;
    if (!swap || g_api.hook(swap, (void*)h_Swap, (void**)&o_Swap)) wl_log("overlay: cannot hook wglSwapBuffers");
}

// ---- DirectInput: hide input from the game while the overlay has it -------------------
typedef HRESULT(WINAPI* CreateDevice_t)(void*, REFGUID, void**, void*);
typedef HRESULT(WINAPI* GetDeviceState_t)(void*, DWORD, void*);
typedef HRESULT(WINAPI* GetDeviceData_t)(void*, DWORD, void*, DWORD*, DWORD);
static CreateDevice_t o_CreateDevice;
static GetDeviceState_t o_GetDeviceState;
static GetDeviceData_t o_GetDeviceData;
static void* s_mouse;
static const GUID kSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

// While the overlay has input, the game sees no keys, buttons or wheel. Mouse MOVEMENT still reaches the game,
// because the game's own cursor (which the overlay follows, see game_cursor) only moves on DirectInput deltas.
static HRESULT WINAPI h_GetDeviceState(void* self, DWORD n, void* out) {
    HRESULT hr = o_GetDeviceState(self, n, out);
    if (FAILED(hr) || !out) return hr;
    if (self == s_mouse && (n == 16 || n == 20)) { // DIMOUSESTATE(2): lX, lY, lZ, rgbButtons[]
        s_di_lmb = ((BYTE*)out)[12] & 0x80;
        if (s_capture) memset((BYTE*)out + 8, 0, n - 8);
    } else if (s_capture) memset(out, 0, n);
    return hr;
}
static HRESULT WINAPI h_GetDeviceData(void* self, DWORD sz, void* out, DWORD* count, DWORD flags) {
    HRESULT hr = o_GetDeviceData(self, sz, out, count, flags);
    if (FAILED(hr) || !out || !count) return hr;
    if (self != s_mouse) { if (s_capture) *count = 0; return hr; }
    DWORD kept = 0;
    for (DWORD i = 0; i < *count; i++) {
        BYTE* item = (BYTE*)out + i * sz; // DIDEVICEOBJECTDATA: dwOfs, dwData, ...
        DWORD ofs = *(DWORD*)item;
        if (ofs == 12) s_di_lmb = ((DWORD*)item)[1] & 0x80; // DIMOFS_BUTTON0
        if (s_capture && ofs >= 8) continue;                 // drop wheel and buttons, keep X/Y movement
        if (kept != i) memmove((BYTE*)out + kept * sz, item, sz);
        kept++;
    }
    *count = kept;
    return hr;
}
static HRESULT WINAPI h_CreateDevice(void* self, REFGUID g, void** dev, void* outer) {
    HRESULT hr = o_CreateDevice(self, g, dev, outer);
    if (SUCCEEDED(hr) && dev && *dev && IsEqualGUID(g, kSysMouse)) s_mouse = *dev;
    if (SUCCEEDED(hr) && dev && *dev && !o_GetDeviceState) { // one implementation shared by all devices
        void** vt = *(void***)*dev;
        g_api.hook(vt[9], (void*)h_GetDeviceState, (void**)&o_GetDeviceState);
        g_api.hook(vt[10], (void*)h_GetDeviceData, (void**)&o_GetDeviceData);
    }
    return hr;
}
void dinput_wrap(void* di) {
    if (!o_CreateDevice) g_api.hook((*(void***)di)[3], (void*)h_CreateDevice, (void**)&o_CreateDevice);
}
