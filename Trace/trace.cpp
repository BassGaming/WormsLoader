// Trace: reverse-engineering tool, not a gameplay mod. Counts calls to every function listed in
// targets.txt (generated offline: all call targets + vtable entries of the exe). Press "arm" in a match,
// then "mark" between actions; each mark appends the call counts since the previous mark to trace.txt.
// Diffing the windows shows which functions run for one action (select weapon, fire, explode, ...).
#include <windows.h>
#include <stdio.h>
#include <vector>
#include <intrin.h>
#include <MinHook.h>
#include "wormsloader.h"

static const WL_Api* api;
static char s_dir[MAX_PATH];
static std::vector<DWORD> s_va;
static DWORD* s_cnt;
static void** s_orig;
static int s_window;

static const WL_ModInfo kInfo = {
    "Trace", "1.0.0", "WormsLoader",
    "Developer tool for reverse engineering. Arm hooks every function in Mods\\Trace\\targets.txt with a call "
    "counter (the game gets slower); Mark writes the counts since the last mark to Mods\\Trace\\trace.txt.",
    true, "WormsClanWars",
};

static void arm(void*) {
    if (s_cnt) return;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\targets.txt", s_dir);
    FILE* f = fopen(path, "r");
    if (!f) { api->log("[Trace] no %s", path); return; }
    unsigned va;
    while (fscanf(f, "%x", &va) == 1) s_va.push_back(va);
    fclose(f);
    size_t n = s_va.size();
    s_cnt = (DWORD*)calloc(n, sizeof(DWORD));
    s_orig = (void**)calloc(n, sizeof(void*));
    BYTE* thunks = (BYTE*)VirtualAlloc(nullptr, n * 12, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    MH_Initialize(); // our own MinHook copy; functions already hooked by others start with a jmp and are skipped
    int ok = 0, skipped = 0;
    for (size_t i = 0; i < n; i++) {
        BYTE* t = (BYTE*)(uintptr_t)s_va[i];
        BYTE* k = thunks + i * 12;
        k[0] = 0xFF; k[1] = 0x05; *(DWORD**)(k + 2) = &s_cnt[i];   // inc dword [cnt]
        k[6] = 0xFF; k[7] = 0x25; *(void***)(k + 8) = &s_orig[i];  // jmp [orig]
        if (t[0] == 0xE9 || t[0] == 0xEB || MH_CreateHook(t, k, &s_orig[i]) != MH_OK || MH_QueueEnableHook(t) != MH_OK) {
            skipped++;
            continue;
        }
        ok++;
    }
    MH_STATUS st = MH_ApplyQueued();
    api->log("[Trace] armed %d functions (%d skipped), apply=%d", ok, skipped, st);
    sprintf_s(path, "%s\\trace.txt", s_dir);
    if ((f = fopen(path, "w"))) fclose(f);
    s_window = 0;
}

static void mark(void*) {
    if (!s_cnt) { api->log("[Trace] not armed"); return; }
    char path[MAX_PATH];
    sprintf_s(path, "%s\\trace.txt", s_dir);
    FILE* f = fopen(path, "a");
    if (!f) return;
    fprintf(f, "=== %d %.2f\n", s_window, api->time());
    for (size_t i = 0; i < s_va.size(); i++) {
        DWORD c = s_cnt[i]; // read then subtract: increments in between land in the next window
        if (c) { fprintf(f, "%x %u\n", s_va[i], c); s_cnt[i] -= c; }
    }
    fclose(f);
    api->log("[Trace] window %d written", s_window++);
}


// ---- Probe: hardware write-watchpoints on the selected worm's weapon fields -------------------------
// Worm::SetEquip (0x7da080, stdcall(worm, equip id)) tells us the worm. A helper thread puts data breakpoints
// (DR0-DR3) on that worm's fields on the game thread; a vectored handler logs who wrote them.
typedef void(__stdcall* SetEquip_t)(BYTE* worm, int id);
static SetEquip_t o_SetEquip;
static DWORD s_game_tid;
static BYTE* volatile s_worm;
static BYTE* s_armed_worm;
static const DWORD kWatch[] = {0x1c4, 0x1c8, 0x188, 0x18c}; // current equip, pending equip, flag, alt pending
static const unsigned kExeLo = 0x401000, kExeHi = 0xc00000;

static void stack_text(DWORD* sp, char* out, size_t n) {
    out[0] = 0;
    int found = 0;
    for (int i = 0; i < 64 && found < 8; i++) {
        DWORD v;
        __try { v = sp[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (v >= kExeLo && v < kExeHi) { size_t l = strlen(out); sprintf_s(out + l, n - l, " %x", v); found++; }
    }
}

static void __stdcall h_SetEquip(BYTE* worm, int id) {
    char st[160];
    stack_text((DWORD*)_AddressOfReturnAddress(), st, sizeof st);
    api->log("[Probe] SetEquip worm=%p id=%d (cur %d, pending %d) stack:%s", worm, id, *(int*)(worm + 0x1c4),
             *(int*)(worm + 0x1c8), st);
    s_game_tid = GetCurrentThreadId();
    s_worm = worm;
    o_SetEquip(worm, id);
}

static LONG CALLBACK veh(EXCEPTION_POINTERS* e) {
    if (e->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT* c = e->ContextRecord;
    if (!(c->Dr6 & 0xF)) return EXCEPTION_CONTINUE_SEARCH;
    static int logged;
    if (logged++ < 400) {
        char st[160];
        stack_text((DWORD*)c->Esp, st, sizeof st);
        for (int i = 0; i < 4; i++)
            if (c->Dr6 & (1u << i))
                api->log("[Probe] write worm+%x = %d at eip %x (ecx=%x esi=%x) stack:%s", kWatch[i],
                         *(int*)(s_armed_worm + kWatch[i]), c->Eip, c->Ecx, c->Esi, st);
    }
    c->Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

static DWORD WINAPI watcher(void*) {
    for (;;) {
        Sleep(20);
        BYTE* w = s_worm;
        if (!w || w == s_armed_worm || !s_game_tid) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, s_game_tid);
        if (!t) continue;
        SuspendThread(t);
        CONTEXT c = {};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(t, &c)) {
            c.Dr0 = (DWORD)(w + kWatch[0]); c.Dr1 = (DWORD)(w + kWatch[1]);
            c.Dr2 = (DWORD)(w + kWatch[2]); c.Dr3 = (DWORD)(w + kWatch[3]);
            // L0-L3 on; RW = 01 (write); LEN = 11 (4 bytes) for dwords, 00 (1 byte) for the 0x188 flag
            c.Dr7 = 0x55 | (0xD << 16) | (0xD << 20) | (0x1 << 24) | (0xD << 28);
            c.Dr6 = 0;
            SetThreadContext(t, &c);
            s_armed_worm = w;
            api->log("[Probe] watching worm %p", w);
        }
        ResumeThread(t);
        CloseHandle(t);
    }
}

// Numpad /: dump the per-weapon data objects (0x19b7290[id]) and the weapon table entry for a few weapons.
static void dump_weapons(void*) {
    static const int ids[] = {1, 3, 4, 5, 13};
    for (int id : ids) {
        BYTE* o = *(BYTE**)(0x19b7290 + 4 * id);
        if (!o) { api->log("[Probe] weapondata[%d] = null", id); continue; }
        char line[1024];
        int n = sprintf_s(line, "[Probe] weapondata[%d] %p vt=%x:", id, o, *(DWORD*)o);
        for (int k = 0; k < 64 && n < 900; k++) {
            DWORD v = ((DWORD*)o)[k];
            float f = *(float*)&v;
            n += (f > 1e-4f && f < 1e6f) || (f < -1e-4f && f > -1e6f) ? sprintf_s(line + n, sizeof line - n, " %g", f)
                                                                   : sprintf_s(line + n, sizeof line - n, " %x", v);
        }
        api->log("%s", line);
    }
}


// ---- Call log: every call to the functions in calllog.txt, in order, with registers and stack args ----------
// Thunk: pushad; push esp; push idx; call log_call; add esp, 8; popad; jmp [orig]
static std::vector<DWORD> s_log_va;
static void** s_log_orig;
static int* s_log_n;
static void __cdecl log_call(int idx, DWORD* r) { // r: edi esi ebp esp ebx edx ecx eax, ret, args...
    if (s_log_n[idx]++ >= 40) return;
    api->log("[Call] %x n=%d ret=%x ecx=%x edx=%x eax=%x esi=%x edi=%x ebx=%x args %x %x %x %x %x", s_log_va[idx],
             s_log_n[idx], r[8], r[6], r[5], r[7], r[1], r[0], r[4], r[9], r[10], r[11], r[12], r[13]);
}
static void calllog_init() {
    char path[MAX_PATH];
    sprintf_s(path, "%s\\calllog.txt", s_dir);
    FILE* f = fopen(path, "r");
    if (!f) return;
    unsigned va;
    while (fscanf(f, "%x", &va) == 1) s_log_va.push_back(va);
    fclose(f);
    size_t n = s_log_va.size();
    s_log_orig = (void**)calloc(n, sizeof(void*));
    s_log_n = (int*)calloc(n, sizeof(int));
    BYTE* th = (BYTE*)VirtualAlloc(nullptr, n * 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    int ok = 0;
    for (size_t i = 0; i < n; i++) {
        BYTE* k = th + i * 32;
        int p = 0;
        k[p++] = 0x60;                                         // pushad
        k[p++] = 0x54;                                         // push esp
        k[p++] = 0x68; *(DWORD*)(k + p) = (DWORD)i; p += 4;    // push idx
        k[p++] = 0xE8; *(int*)(k + p) = (int)((BYTE*)log_call - (k + p + 4)); p += 4; // call log_call
        k[p++] = 0x83; k[p++] = 0xC4; k[p++] = 0x08;           // add esp, 8
        k[p++] = 0x61;                                         // popad
        k[p++] = 0xFF; k[p++] = 0x25; *(void***)(k + p) = &s_log_orig[i]; p += 4; // jmp [orig]
        if (!api->hook((void*)(uintptr_t)s_log_va[i], k, &s_log_orig[i])) ok++;
    }
    api->log("[Call] logging %d of %d functions", ok, (int)n);
}

static void probe_init() {
    AddVectoredExceptionHandler(1, veh);
    api->hook(api->exe(0x7da080), (void*)h_SetEquip, (void**)&o_SetEquip);
    CreateThread(nullptr, 0, watcher, nullptr, 0, nullptr);
    api->hotkey("dump", "Probe: log weapon data objects", VK_DIVIDE, dump_weapons, nullptr);
}

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }

extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api* a) {
    api = a;
    HMODULE self;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&kInfo, &self);
    GetModuleFileNameA(self, s_dir, MAX_PATH);
    *strrchr(s_dir, '\\') = 0;
    api->hotkey("arm", "Arm: hook all functions in targets.txt", VK_PAUSE, arm, nullptr);
    api->hotkey("mark", "Mark: write call counts since the last mark", VK_SCROLL, mark, nullptr);
    probe_init();
    calllog_init();
}
