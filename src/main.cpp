// FCC - Falcon Cockpit Camera
// Plugin X-Plane 12 : sauvegarde / rappelle des positions de camera (tete du pilote
// en vue cockpit 3D) avec un raccourci clavier par position.
//
// Installation : Aircraft/<Falcon 8X>/plugins/FCC/64/win.xpl (lin.xpl / mac.xpl)

#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <cmath>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if APL
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "XPLMDefs.h"
#include "XPLMUtilities.h"
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMMenus.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"

static const int N_SLOTS = 8;
static const int WIN_W = 480;
static const int TITLE_H = 36;
static const int HEAD_H = 22;
static const int ROW_H = 34;
static const int FOOT_H = 52;
static const int WIN_H = TITLE_H + HEAD_H + N_SLOTS * ROW_H + 8 + FOOT_H;

struct Slot {
    std::string name;
    int vk = 0;               // 0 = pas de keybind
    int kflags = 0;           // modificateurs (Shift / Ctrl / Alt)
    bool saved = false;
    float x = 0, y = 0, z = 0, psi = 0, the = 0, fov = 0;
    XPLMHotKeyID hotkey = nullptr;
};

static Slot gSlots[N_SLOTS];
static std::string gCfgPath;

static XPLMDataRef drX, drY, drZ, drPsi, drThe, drFov, drViewType;
static XPLMCommandRef cmdLook3D;
static XPLMFlightLoopID gLoop = nullptr;
static int gPending = -1;

// transition douce entre deux vues
static bool gSmooth = true;
static const float ANIM_DUR = 0.6f;  // secondes
struct Anim {
    bool active = false;
    float t0 = 0;
    float from[6] = {0};  // x, y, z, psi, the, fov
    float to[6] = {0};
};
static Anim gAnim;

static XPLMMenuID gMenu = nullptr;
static XPLMWindowID gWin = nullptr;

// etat de l'interface
static int gCapture = -1;       // slot en attente d'une touche
static int gEdit = -1;          // slot dont on edite le nom
static bool gDrag = false;
static int gDX = 0, gDY = 0;
static int gMX = 0, gMY = 0;
static float gMouseT = -10.0f;
static int gFlashSlot = -1;
static float gFlashUntil = 0.0f;
static std::string gStatusMsg;

// ---------------------------------------------------------------- utilitaires

static void setStatus(const std::string& s) { gStatusMsg = s; }

static std::string keyName(int vk, int fl) {
    if (!vk) return "";
    std::string s;
    if (fl & xplm_ControlFlag) s += "Ctrl+";
    if (fl & xplm_ShiftFlag) s += "Shift+";
    if (fl & xplm_OptionAltFlag) s += "Alt+";
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) s += (char)vk;
    else if (vk >= XPLM_VK_F1 && vk <= XPLM_VK_F12) s += "F" + std::to_string(vk - XPLM_VK_F1 + 1);
    else if (vk >= XPLM_VK_NUMPAD0 && vk <= XPLM_VK_NUMPAD9) s += "Num" + std::to_string(vk - XPLM_VK_NUMPAD0);
    else if (vk == XPLM_VK_SPACE) s += "Space";
    else if (vk == XPLM_VK_TAB) s += "Tab";
    else if (vk == XPLM_VK_UP) s += "Up";
    else if (vk == XPLM_VK_DOWN) s += "Down";
    else if (vk == XPLM_VK_LEFT) s += "Left";
    else if (vk == XPLM_VK_RIGHT) s += "Right";
    else if (vk == XPLM_VK_HOME) s += "Home";
    else if (vk == XPLM_VK_END) s += "End";
    else if (vk == XPLM_VK_INSERT) s += "Insert";
    else if (vk == XPLM_VK_PRIOR) s += "PgUp";
    else if (vk == XPLM_VK_NEXT) s += "PgDn";
    else {
        char b[16];
        snprintf(b, sizeof b, "Key 0x%02X", vk);
        s += b;
    }
    return s;
}

// ---------------------------------------------------------------- persistance

static void buildCfgPath() {
    char fname[256] = {0}, fpath[1024] = {0};
    XPLMGetNthAircraftModel(0, fname, fpath);
    XPLMExtractFileAndPath(fpath);  // fpath devient le dossier de l'avion
    std::string sep = XPLMGetDirectorySeparator();
    gCfgPath = std::string(fpath) + sep + "plugins" + sep + "FCC" + sep + "cameras.cfg";
}

static void saveConfig() {
    if (gCfgPath.empty()) return;
    std::ofstream f(gCfgPath, std::ios::trunc);
    if (!f) {
        XPLMDebugString("FCC: impossible d'ecrire cameras.cfg\n");
        return;
    }
    f << "S|" << (gSmooth ? 1 : 0) << '\n';
    for (int i = 0; i < N_SLOTS; ++i) {
        Slot& s = gSlots[i];
        std::string n = s.name;
        std::replace(n.begin(), n.end(), '|', ' ');
        f << i << '|' << n << '|' << s.vk << '|' << s.kflags << '|' << (s.saved ? 1 : 0) << '|' << s.x
          << '|' << s.y << '|' << s.z << '|' << s.psi << '|' << s.the << '|' << s.fov << '\n';
    }
}

static void loadConfig() {
    for (int i = 0; i < N_SLOTS; ++i) {
        gSlots[i].name = "Camera " + std::to_string(i + 1);
        gSlots[i].vk = 0;
        gSlots[i].kflags = 0;
        gSlots[i].saved = false;
    }
    std::ifstream f(gCfgPath);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() > 1 && line[0] == 'S' && line[1] == '|') {
            gSmooth = atoi(line.c_str() + 2) != 0;
            continue;
        }
        std::vector<std::string> p;
        std::stringstream ss(line);
        std::string t;
        while (std::getline(ss, t, '|')) p.push_back(t);
        while (p.size() < 11) p.push_back("0");
        int i = atoi(p[0].c_str());
        if (i < 0 || i >= N_SLOTS) continue;
        Slot& s = gSlots[i];
        s.name = p[1].empty() ? "Camera " + std::to_string(i + 1) : p[1];
        s.vk = atoi(p[2].c_str());
        s.kflags = atoi(p[3].c_str());
        s.saved = atoi(p[4].c_str()) != 0;
        s.x = (float)atof(p[5].c_str());
        s.y = (float)atof(p[6].c_str());
        s.z = (float)atof(p[7].c_str());
        s.psi = (float)atof(p[8].c_str());
        s.the = (float)atof(p[9].c_str());
        s.fov = (float)atof(p[10].c_str());
    }
}

// ---------------------------------------------------------------- camera

static void applyCamera(int i) {
    Slot& s = gSlots[i];
    XPLMSetDataf(drX, s.x);
    XPLMSetDataf(drY, s.y);
    XPLMSetDataf(drZ, s.z);
    XPLMSetDataf(drPsi, s.psi);
    XPLMSetDataf(drThe, s.the);
    if (s.fov > 1.0f) XPLMSetDataf(drFov, s.fov);
}

static void setHead(const float* v) {
    XPLMSetDataf(drX, v[0]);
    XPLMSetDataf(drY, v[1]);
    XPLMSetDataf(drZ, v[2]);
    XPLMSetDataf(drPsi, v[3]);
    XPLMSetDataf(drThe, v[4]);
    if (v[5] > 1.0f) XPLMSetDataf(drFov, v[5]);
}

// demarre le deplacement vers la camera i (instantane ou anime)
static void beginMove(int i) {
    Slot& s = gSlots[i];
    if (!gSmooth) {
        gAnim.active = false;
        applyCamera(i);
        return;
    }
    gAnim.from[0] = XPLMGetDataf(drX);
    gAnim.from[1] = XPLMGetDataf(drY);
    gAnim.from[2] = XPLMGetDataf(drZ);
    gAnim.from[3] = XPLMGetDataf(drPsi);
    gAnim.from[4] = XPLMGetDataf(drThe);
    gAnim.from[5] = XPLMGetDataf(drFov);
    gAnim.to[0] = s.x;
    gAnim.to[1] = s.y;
    gAnim.to[2] = s.z;
    gAnim.to[3] = s.psi;
    gAnim.to[4] = s.the;
    gAnim.to[5] = (s.fov > 1.0f) ? s.fov : gAnim.from[5];
    gAnim.t0 = XPLMGetElapsedTime();
    gAnim.active = true;
    XPLMScheduleFlightLoop(gLoop, -1.0f, 1);  // des la prochaine frame
}

static float loopCb(float, float, int, void*) {
    if (gPending >= 0) {
        int i = gPending;
        gPending = -1;
        beginMove(i);
    }
    if (!gAnim.active) return 0.0f;  // desactive la boucle

    float t = (XPLMGetElapsedTime() - gAnim.t0) / ANIM_DUR;
    if (t >= 1.0f) {
        t = 1.0f;
        gAnim.active = false;
    }
    // easeInOutCubic
    float e = (t < 0.5f) ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;

    float v[6];
    for (int k = 0; k < 6; ++k) {
        float d = gAnim.to[k] - gAnim.from[k];
        if (k == 3) {  // cap : chemin le plus court
            while (d > 180.0f) d -= 360.0f;
            while (d < -180.0f) d += 360.0f;
        }
        v[k] = gAnim.from[k] + d * e;
    }
    setHead(v);
    return gAnim.active ? -1.0f : 0.0f;  // -1 = a chaque frame
}

static void recallCamera(int i) {
    Slot& s = gSlots[i];
    char dbg[128];
    snprintf(dbg, sizeof dbg, "FCC: recall camera %d (saved=%d)\n", i + 1, s.saved ? 1 : 0);
    XPLMDebugString(dbg);
    if (!s.saved) {
        setStatus("Camera " + std::to_string(i + 1) + " is empty - click SAVE first");
        return;
    }
    if (XPLMGetDatai(drViewType) != 1026) {
        // pas en cockpit 3D : on bascule, puis on applique a la frame suivante
        XPLMCommandOnce(cmdLook3D);
        gPending = i;
        XPLMScheduleFlightLoop(gLoop, -1.0f, 1);
    } else {
        beginMove(i);
    }
    setStatus("Moved to: " + s.name);
}

static void saveCamera(int i) {
    Slot& s = gSlots[i];
    s.x = XPLMGetDataf(drX);
    s.y = XPLMGetDataf(drY);
    s.z = XPLMGetDataf(drZ);
    s.psi = XPLMGetDataf(drPsi);
    s.the = XPLMGetDataf(drThe);
    s.fov = XPLMGetDataf(drFov);
    s.saved = true;
    saveConfig();
    gFlashSlot = i;
    gFlashUntil = XPLMGetElapsedTime() + 1.2f;
    setStatus("Camera " + std::to_string(i + 1) + " saved (" + s.name + ")");
}

// ---------------------------------------------------------------- raccourcis

static void hotkeyCb(void* refcon) {
    if (gCapture >= 0 || gEdit >= 0) return;
    recallCamera((int)(intptr_t)refcon);
}

static void registerHotkeys() {
    for (int i = 0; i < N_SLOTS; ++i) {
        Slot& s = gSlots[i];
        if (s.hotkey) {
            XPLMUnregisterHotKey(s.hotkey);
            s.hotkey = nullptr;
        }
        if (s.vk) {
            std::string d = "FCC " + s.name;
            s.hotkey = XPLMRegisterHotKey((char)s.vk, xplm_DownFlag | s.kflags, d.c_str(), hotkeyCb,
                                          (void*)(intptr_t)i);
        }
    }
}

// Commandes X-Plane : FCC/recall_N et FCC/save_N (bindables aussi dans Reglages > Clavier/Joystick)
static int cmdHandler(XPLMCommandRef, XPLMCommandPhase phase, void* refcon) {
    if (phase != xplm_CommandBegin) return 1;
    int v = (int)(intptr_t)refcon;
    int i = v / 2;
    if (v % 2) saveCamera(i);
    else recallCamera(i);
    return 1;
}

// ---------------------------------------------------------------- dessin

struct Cell { int l, b, r, t; };

static Cell cellBadge(int wl, int wt, int i) { int t = wt - TITLE_H - HEAD_H - i * ROW_H - 4; return {wl + 12, t - 26, wl + 38, t}; }
static Cell cellName(int wl, int wt, int i)  { int t = wt - TITLE_H - HEAD_H - i * ROW_H - 4; return {wl + 46, t - 26, wl + 226, t}; }
static Cell cellKey(int wl, int wt, int i)   { int t = wt - TITLE_H - HEAD_H - i * ROW_H - 4; return {wl + 234, t - 26, wl + 374, t}; }
static Cell cellSave(int wl, int wt, int i)  { int t = wt - TITLE_H - HEAD_H - i * ROW_H - 4; return {wl + 382, t - 26, wl + 468, t}; }
static Cell cellClose(int wl, int wt)        { return {wl + WIN_W - 32, wt - 30, wl + WIN_W - 8, wt - 8}; }
static Cell cellSmooth(int wl, int wt)       { return {wl + WIN_W - 142, wt - 30, wl + WIN_W - 40, wt - 8}; }

static bool ptIn(const Cell& c, int x, int y) { return x >= c.l && x <= c.r && y >= c.b && y <= c.t; }

static const float C_BG[4]       = {0.07f, 0.08f, 0.10f, 0.97f};
static const float C_TITLE[4]    = {0.11f, 0.13f, 0.18f, 1.0f};
static const float C_ACCENT[4]   = {0.96f, 0.64f, 0.14f, 1.0f};
static const float C_ROW[4]      = {0.115f, 0.125f, 0.15f, 1.0f};
static const float C_ROWALT[4]   = {0.095f, 0.105f, 0.13f, 1.0f};
static const float C_CELL[4]     = {0.19f, 0.21f, 0.26f, 1.0f};
static const float C_CELLHOV[4]  = {0.26f, 0.29f, 0.35f, 1.0f};
static const float C_BLUE[4]     = {0.17f, 0.44f, 0.84f, 1.0f};
static const float C_BLUEHOV[4]  = {0.25f, 0.55f, 0.95f, 1.0f};
static const float C_GREEN[4]    = {0.19f, 0.70f, 0.40f, 1.0f};
static const float C_GREY[4]     = {0.30f, 0.32f, 0.37f, 1.0f};
static const float C_REDHOV[4]   = {0.85f, 0.25f, 0.25f, 1.0f};
static const float C_WHITE[3]    = {0.93f, 0.94f, 0.96f};
static const float C_DIM[3]      = {0.58f, 0.61f, 0.67f};
static const float C_DARK[3]     = {0.05f, 0.06f, 0.08f};
static const float C_AMBER[3]    = {0.96f, 0.64f, 0.14f};

static void rect(int l, int b, int r, int t, const float* c) {
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glColor4f(c[0], c[1], c[2], c[3]);
    // GL_QUADS n'est pas supporte par la couche de compatibilite d'X-Plane 12 : triangles
    glBegin(GL_TRIANGLES);
    glVertex2f((float)l, (float)b);
    glVertex2f((float)r, (float)b);
    glVertex2f((float)r, (float)t);
    glVertex2f((float)l, (float)b);
    glVertex2f((float)r, (float)t);
    glVertex2f((float)l, (float)t);
    glEnd();
}

static void outline(int l, int b, int r, int t, const float* c) {
    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    glColor4f(c[0], c[1], c[2], c[3]);
    glBegin(GL_LINE_LOOP);
    glVertex2f((float)l + 0.5f, (float)b + 0.5f);
    glVertex2f((float)r - 0.5f, (float)b + 0.5f);
    glVertex2f((float)r - 0.5f, (float)t - 0.5f);
    glVertex2f((float)l + 0.5f, (float)t - 0.5f);
    glEnd();
}

static void text(int x, int y, const std::string& s, const float* c) {
    float col[3] = {c[0], c[1], c[2]};
    XPLMDrawString(col, x, y, const_cast<char*>(s.c_str()), nullptr, xplmFont_Proportional);
}

static void textCentered(const Cell& c, const std::string& s, const float* col) {
    float w = XPLMMeasureString(xplmFont_Proportional, s.c_str(), (int)s.size());
    text((int)((c.l + c.r) / 2.0f - w / 2.0f), c.b + 9, s, col);
}

static bool hovered(const Cell& c, float now) { return (now - gMouseT) < 0.25f && ptIn(c, gMX, gMY); }

static void drawCb(XPLMWindowID w, void*) {
    int wl, wt, wr, wb;
    XPLMGetWindowGeometry(w, &wl, &wt, &wr, &wb);
    float now = XPLMGetElapsedTime();

    // fond + cadre
    rect(wl, wb, wl + WIN_W, wt, C_BG);
    outline(wl, wb, wl + WIN_W, wt, C_GREY);

    // barre de titre
    rect(wl, wt - TITLE_H, wl + WIN_W, wt, C_TITLE);
    rect(wl, wt - TITLE_H, wl + 5, wt, C_ACCENT);
    text(wl + 16, wt - 24, "FCC", C_AMBER);
    text(wl + 52, wt - 24, "Falcon Cockpit Camera", C_WHITE);

    Cell cl = cellClose(wl, wt);
    rect(cl.l, cl.b, cl.r, cl.t, hovered(cl, now) ? C_REDHOV : C_CELL);
    textCentered(cl, "X", C_WHITE);

    Cell cm = cellSmooth(wl, wt);
    rect(cm.l, cm.b, cm.r, cm.t, hovered(cm, now) ? C_CELLHOV : C_CELL);
    textCentered(cm, gSmooth ? "Smooth: ON" : "Smooth: OFF", gSmooth ? C_AMBER : C_DIM);

    // en-tetes de colonnes
    int hy = wt - TITLE_H - 16;
    text(wl + 18, hy, "#", C_DIM);
    text(wl + 50, hy, "NAME", C_DIM);
    text(wl + 238, hy, "KEYBIND", C_DIM);

    // lignes
    for (int i = 0; i < N_SLOTS; ++i) {
        Slot& s = gSlots[i];
        Cell cb = cellBadge(wl, wt, i), cn = cellName(wl, wt, i), ck = cellKey(wl, wt, i), cs = cellSave(wl, wt, i);

        rect(wl + 6, cb.b - 4, wl + WIN_W - 6, cb.t + 4, (i % 2) ? C_ROWALT : C_ROW);

        // pastille : verte si une position est enregistree
        rect(cb.l, cb.b, cb.r, cb.t, s.saved ? C_GREEN : C_GREY);
        textCentered(cb, std::to_string(i + 1), C_DARK);

        // nom (editable)
        bool editing = (gEdit == i);
        rect(cn.l, cn.b, cn.r, cn.t, editing ? C_CELLHOV : (hovered(cn, now) ? C_CELLHOV : C_CELL));
        if (editing) outline(cn.l, cn.b, cn.r, cn.t, C_ACCENT);
        std::string nm = s.name;
        if (editing && ((int)(now * 2.0f) % 2 == 0)) nm += "|";
        text(cn.l + 8, cn.b + 9, nm, C_WHITE);

        // keybind
        bool waiting = (gCapture == i);
        rect(ck.l, ck.b, ck.r, ck.t, waiting ? C_ACCENT : (hovered(ck, now) ? C_CELLHOV : C_CELL));
        if (waiting) {
            textCentered(ck, "Waiting for keybind...", C_DARK);
        } else if (s.vk) {
            textCentered(ck, keyName(s.vk, s.kflags), C_WHITE);
        } else {
            textCentered(ck, "Click to bind", C_DIM);
        }

        // SAVE
        bool flash = (gFlashSlot == i && now < gFlashUntil);
        const float* sc = flash ? C_GREEN : (hovered(cs, now) ? C_BLUEHOV : C_BLUE);
        rect(cs.l, cs.b, cs.r, cs.t, sc);
        textCentered(cs, flash ? "SAVED" : "SAVE", C_WHITE);
    }

    // pied de page
    int fy = wb + FOOT_H;
    rect(wl, wb, wl + WIN_W, fy - 4, C_TITLE);
    text(wl + 14, wb + 32, "Click a keybind, then press a key.  Esc = cancel   Backspace = clear", C_DIM);
    text(wl + 14, wb + 12, gStatusMsg.empty() ? "Ready" : gStatusMsg, C_AMBER);
}

// ---------------------------------------------------------------- entrees

static void cancelInput() {
    if (gEdit >= 0) {
        gEdit = -1;
        registerHotkeys();
        saveConfig();
    }
    gCapture = -1;
    XPLMTakeKeyboardFocus(0);
}

static int mouseCb(XPLMWindowID w, int x, int y, XPLMMouseStatus st, void*) {
    int wl, wt, wr, wb;
    XPLMGetWindowGeometry(w, &wl, &wt, &wr, &wb);

    if (st == xplm_MouseDown) {
        if (ptIn(cellClose(wl, wt), x, y)) {
            cancelInput();
            XPLMSetWindowIsVisible(w, 0);
            return 1;
        }
        if (ptIn(cellSmooth(wl, wt), x, y)) {
            gSmooth = !gSmooth;
            saveConfig();
            setStatus(gSmooth ? "Smooth transition enabled" : "Smooth transition disabled");
            return 1;
        }
        if (y > wt - TITLE_H) {
            gDrag = true;
            gDX = x - wl;
            gDY = wt - y;
            return 1;
        }
        for (int i = 0; i < N_SLOTS; ++i) {
            if (ptIn(cellSave(wl, wt, i), x, y)) {
                cancelInput();
                saveCamera(i);
                return 1;
            }
            if (ptIn(cellKey(wl, wt, i), x, y)) {
                cancelInput();
                gCapture = i;
                XPLMTakeKeyboardFocus(w);
                setStatus("Press the key for camera " + std::to_string(i + 1));
                return 1;
            }
            if (ptIn(cellName(wl, wt, i), x, y)) {
                cancelInput();
                gEdit = i;
                XPLMTakeKeyboardFocus(w);
                setStatus("Type a name, then press Enter");
                return 1;
            }
        }
        cancelInput();
    } else if (st == xplm_MouseDrag) {
        if (gDrag) {
            int nl = x - gDX, nt = y + gDY;
            XPLMSetWindowGeometry(w, nl, nt, nl + WIN_W, nt - WIN_H);
        }
    } else if (st == xplm_MouseUp) {
        gDrag = false;
    }
    return 1;
}

static int rightClickCb(XPLMWindowID, int, int, XPLMMouseStatus, void*) { return 1; }
static int wheelCb(XPLMWindowID, int, int, int, int, void*) { return 1; }
static XPLMCursorStatus cursorCb(XPLMWindowID, int x, int y, void*) {
    gMX = x;
    gMY = y;
    gMouseT = XPLMGetElapsedTime();
    return xplm_CursorDefault;
}

static void keyCb(XPLMWindowID, char key, XPLMKeyFlags flags, char vkey, void*, int losingFocus) {
    if (losingFocus) {
        gCapture = -1;
        if (gEdit >= 0) {
            gEdit = -1;
            saveConfig();
            registerHotkeys();
        }
        return;
    }
    if (!(flags & xplm_DownFlag)) return;
    int vk = (unsigned char)vkey;

    if (gCapture >= 0) {
        int i = gCapture;
        if (vk == XPLM_VK_ESCAPE) {
            setStatus("Keybind cancelled");
        } else if (vk == XPLM_VK_BACK || vk == XPLM_VK_DELETE) {
            gSlots[i].vk = 0;
            gSlots[i].kflags = 0;
            setStatus("Keybind cleared for camera " + std::to_string(i + 1));
        } else if (vk >= 0x10 && vk <= 0x12) {
            return;  // modificateur seul : on attend la vraie touche
        } else {
            int kf = flags & (xplm_ShiftFlag | xplm_ControlFlag | xplm_OptionAltFlag);
            std::string moved;
            for (int j = 0; j < N_SLOTS; ++j)
                if (j != i && gSlots[j].vk == vk && gSlots[j].kflags == kf) {
                    gSlots[j].vk = 0;
                    gSlots[j].kflags = 0;
                    moved = " (removed from camera " + std::to_string(j + 1) + ")";
                }
            gSlots[i].vk = vk;
            gSlots[i].kflags = kf;
            setStatus("Camera " + std::to_string(i + 1) + " bound to " + keyName(vk, kf) + moved);
        }
        gCapture = -1;
        XPLMTakeKeyboardFocus(0);
        registerHotkeys();
        saveConfig();
        return;
    }

    if (gEdit >= 0) {
        std::string& n = gSlots[gEdit].name;
        if (vk == XPLM_VK_RETURN || vk == XPLM_VK_ENTER || vk == XPLM_VK_ESCAPE) {
            if (n.empty()) n = "Camera " + std::to_string(gEdit + 1);
            gEdit = -1;
            XPLMTakeKeyboardFocus(0);
            registerHotkeys();
            saveConfig();
            setStatus("Name saved");
        } else if (vk == XPLM_VK_BACK) {
            if (!n.empty()) n.pop_back();
        } else if ((unsigned char)key >= 32 && (unsigned char)key < 127 && key != '|' && n.size() < 22) {
            n += key;
        }
    }
}

static void showWindow() {
    if (!gWin) {
        int sl, st, sr, sb;
        XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
        int left = sl + 120, top = st - 120;

        XPLMCreateWindow_t p;
        memset(&p, 0, sizeof p);
        p.structSize = sizeof(p);
        p.left = left;
        p.top = top;
        p.right = left + WIN_W;
        p.bottom = top - WIN_H;
        p.visible = 1;
        p.drawWindowFunc = drawCb;
        p.handleMouseClickFunc = mouseCb;
        p.handleKeyFunc = keyCb;
        p.handleCursorFunc = cursorCb;
        p.handleMouseWheelFunc = wheelCb;
        p.handleRightClickFunc = rightClickCb;
        p.refcon = nullptr;
        p.decorateAsFloatingWindow = xplm_WindowDecorationSelfDecorated;
        p.layer = xplm_WindowLayerFloatingWindows;
        gWin = XPLMCreateWindowEx(&p);
    } else {
        XPLMSetWindowIsVisible(gWin, 1);
        XPLMBringWindowToFront(gWin);
    }
}

static void menuHandler(void*, void*) { showWindow(); }

// ---------------------------------------------------------------- cycle de vie

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
    strcpy(outName, "FCC - Falcon Cockpit Camera");
    strcpy(outSig, "fr.jb.fcc");
    strcpy(outDesc, "Sauvegarde et rappel de positions de camera cockpit avec raccourcis clavier.");

    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    drX = XPLMFindDataRef("sim/graphics/view/pilots_head_x");
    drY = XPLMFindDataRef("sim/graphics/view/pilots_head_y");
    drZ = XPLMFindDataRef("sim/graphics/view/pilots_head_z");
    drPsi = XPLMFindDataRef("sim/graphics/view/pilots_head_psi");
    drThe = XPLMFindDataRef("sim/graphics/view/pilots_head_the");
    drFov = XPLMFindDataRef("sim/graphics/view/field_of_view_deg");
    drViewType = XPLMFindDataRef("sim/graphics/view/view_type");
    cmdLook3D = XPLMFindCommand("sim/view/3d_cockpit_cmnd_look");

    for (int i = 0; i < N_SLOTS; ++i) {
        std::string n = std::to_string(i + 1);
        std::string rn = "FCC/recall_" + n, sn = "FCC/save_" + n;
        std::string rd = "FCC : aller a la camera " + n, sd = "FCC : sauvegarder la camera " + n;
        XPLMCommandRef rc = XPLMCreateCommand(rn.c_str(), rd.c_str());
        XPLMCommandRef sc = XPLMCreateCommand(sn.c_str(), sd.c_str());
        XPLMRegisterCommandHandler(rc, cmdHandler, 1, (void*)(intptr_t)(i * 2));
        XPLMRegisterCommandHandler(sc, cmdHandler, 1, (void*)(intptr_t)(i * 2 + 1));
    }

    XPLMCreateFlightLoop_t fl;
    fl.structSize = sizeof(fl);
    fl.phase = xplm_FlightLoop_Phase_BeforeFlightModel;
    fl.callbackFunc = loopCb;
    fl.refcon = nullptr;
    gLoop = XPLMCreateFlightLoop(&fl);

    int item = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "FCC", nullptr, 1);
    gMenu = XPLMCreateMenu("FCC", XPLMFindPluginsMenu(), item, menuHandler, nullptr);
    XPLMAppendMenuItem(gMenu, "Open window", (void*)1, 1);
    return 1;
}

PLUGIN_API void XPluginStop(void) {
    for (int i = 0; i < N_SLOTS; ++i)
        if (gSlots[i].hotkey) XPLMUnregisterHotKey(gSlots[i].hotkey);
    if (gLoop) XPLMDestroyFlightLoop(gLoop);
    if (gWin) XPLMDestroyWindow(gWin);
    if (gMenu) XPLMDestroyMenu(gMenu);
}

PLUGIN_API int XPluginEnable(void) {
    buildCfgPath();
    loadConfig();
    registerHotkeys();
    return 1;
}

PLUGIN_API void XPluginDisable(void) { saveConfig(); }

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void*) {}
