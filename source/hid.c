// zeebo pad over IHID/IHIDDevice, fed from the 3ds buttons
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "aee.h"

#define UID_JOYSTICK 0x0106c3fd
#define HID_CONNECTED 1

// pad button uids. the game turns uid into its own bit inside its GetNextButtonEvent wrapper:
// c40a b0, c40b b1 (confirm), c40c b2, c40d b3, c406 b4, c408 b5, c403 b8, dpad b12-15
enum {
    B_UP = 0x0106c3fe, B_LEFT, B_DOWN, B_RIGHT, B_START, B_BACK, B_LTHUMB, B_RTHUMB,
    B_LSHU, B_LSHL, B_RSHU, B_RSHL, B_2, B_1, B_3, B_4,
};

static const struct { u32 key; u32 uid; } map[] = {
    { KEY_A, B_1 },      { KEY_B, B_2 },      { KEY_X, B_4 },      { KEY_Y, B_3 },
    { KEY_L, B_LSHU },   { KEY_R, B_RSHU },   { KEY_ZL, B_LSHL },  { KEY_ZR, B_RSHL },
    { KEY_START, B_BACK }, { KEY_SELECT, B_START },
    { KEY_DUP, B_UP },   { KEY_DDOWN, B_DOWN }, { KEY_DLEFT, B_LEFT }, { KEY_DRIGHT, B_RIGHT },
};
#define NMAP (sizeof map / sizeof *map)

typedef struct { u32 uid; bool down; } ev_t;
static ev_t q[32];
static int qh, qt;

static asig_t *sig_btn, *sig_pos, *sig_conn;
static u32 held;
static int axes[4] = { 128, 128, 128, 128 };
static int touch_x0, touch_y0;
static bool touching;

static void *vt_hid[8];
static void *vt_dev[19];
static aee_obj hid = { vt_hid };
static aee_obj dev = { vt_dev };

static int one(void) { return 1; }
static int zero(void) { return 0; }

static int hid_qi(void *o, u32 iid, void **out) { *out = o; return SUCCESS; }

static int hid_devices(void *h, u32 type, int *handles, int len, int *needed)
{
    int n = type == UID_JOYSTICK;
    if (needed) *needed = n;
    if (n && handles && len > 0) handles[0] = 1;
    return SUCCESS;
}

static int hid_create(void *h, int handle, void **out) { *out = &dev; return SUCCESS; }

static void dev_info_fill(u8 *p)
{
    *(u32 *)p = UID_JOYSTICK;
    *(u16 *)(p + 4) = 0x0135;
    *(u16 *)(p + 6) = 0x1eaa;
    *(u32 *)(p + 8) = 0;
}

static int hid_info(void *h, int handle, u8 *p) { dev_info_fill(p); return SUCCESS; }
static int hid_nextconn(void *h, u32 *a, u32 *b, u32 *c) { if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; return EFAILED; }
static int hid_regconn(void *h, asig_t *s) { sig_conn = s; return SUCCESS; }

static int dev_info(void *d, u8 *p) { dev_info_fill(p); return SUCCESS; }
static int dev_status(void *d, u32 *st) { if (st) *st = HID_CONNECTED; return SUCCESS; }
static int dev_regstatus(void *d, asig_t *s) { return SUCCESS; }
static int dev_nbuttons(void *d, int *n) { if (n) *n = 16; return SUCCESS; }

static u32 id_for(u32 uid)
{
    // thumb clicks fall through the game's switch and keep this id as is
    if (uid == B_LTHUMB) return 6;
    if (uid == B_RTHUMB) return 7;
    return uid - B_UP;
}

static bool uid_down(u32 uid)
{
    for (unsigned i = 0; i < NMAP; i++) if (map[i].uid == uid && (held & map[i].key)) return true;
    return false;
}

static int dev_buttoninfo(void *d, u32 uid, u32 *info)
{
    if (uid < B_UP || uid > B_4) return EBADPARM;
    info[0] = id_for(uid); info[1] = uid_down(uid); info[2] = uid; info[3] = 0; info[4] = 1;
    return SUCCESS;
}

static int dev_regbtn(void *d, asig_t *s) { sig_btn = s; return SUCCESS; }

static int dev_nextbtn(void *d, u32 *info, u32 *ts, u32 *dropped)
{
    if (ts) *ts = aee_uptime();
    if (dropped) *dropped = 0;
    if (qh == qt) { memset(info, 0, 20); return EFAILED; }
    ev_t e = q[qh];
    qh = (qh + 1) & 31;
    info[0] = id_for(e.uid); info[1] = e.down; info[2] = e.uid; info[3] = 0; info[4] = 1;
    return SUCCESS;
}

static void pos_fill(u32 *p, u32 v)
{
    for (int i = 0; i < 25; i++) p[i] = v;
    p[0] = 0;
}

static int dev_pos(void *d, u32 *p)
{
    pos_fill(p, 128);
    p[1] = axes[0]; p[2] = axes[1]; p[3] = axes[2]; p[6] = axes[3];
    return SUCCESS;
}

static int dev_min(void *d, u32 *p) { pos_fill(p, 0); return SUCCESS; }
static int dev_max(void *d, u32 *p) { pos_fill(p, 255); return SUCCESS; }

static int dev_axes(void *d, u32 *p)
{
    memset(p, 0, 25 * 4);
    p[1] = 0x0106c4d0; p[2] = 0x0106c4d1; p[3] = 0x0106c4ce; p[6] = 0x0106c4cf;
    return SUCCESS;
}

static int dev_regpos(void *d, asig_t *s)
{
    sig_pos = s;
    // real hw reports the resting position right away, game waits on it
    sig_raise(s);
    return SUCCESS;
}

static int dev_excl(void *d, u32 *v) { if (v) *v = 0; return SUCCESS; }

void *hid_new(void)
{
    static bool done;
    if (!done) {
        void **v = vt_hid;
        vt_fill(v, 8);
        v[0] = one; v[1] = zero; v[2] = hid_qi; v[3] = hid_create; v[4] = hid_info;
        v[5] = hid_nextconn; v[6] = hid_regconn; v[7] = hid_devices;
        v = vt_dev;
        vt_fill(v, 19);
        v[0] = one; v[1] = zero; v[2] = hid_qi; v[3] = dev_info; v[4] = dev_status; v[5] = dev_regstatus;
        v[6] = dev_buttoninfo; v[7] = dev_nbuttons; v[8] = dev_regbtn; v[9] = dev_nextbtn;
        v[10] = dev_pos; v[11] = dev_min; v[12] = dev_max; v[13] = dev_axes; v[14] = dev_regpos;
        v[15] = zero; v[16] = dev_excl; v[17] = zero; v[18] = dev_excl;
        done = true;
    }
    return &hid;
}

static int stick(int v, int range)
{
    int a = 128 + v * 127 / range;
    return a < 0 ? 0 : a > 255 ? 255 : a;
}

// ------------------------------------------------------------------ dodge (ios style)
// while aiming, flick the stick to the edge, back to centre, and to the edge again in the same
// direction -> step that way. the game already has "Step forward/right/left/back" under
// "with weapon ready", so this only feeds it the input it already understands.
#define DODGE_EDGE_ON   0.80f   // |stick| to count as pushed to the edge
#define DODGE_EDGE_OFF  0.35f   // |stick| to count as back at centre
#define DODGE_FLICK_MS  260     // centre -> edge must be quicker than this
#define DODGE_GAP_MS    380     // the whole double flick must fit in this
#define DODGE_COOL_MS   700     // after a step, ignore the stick this long
#define DODGE_HOLD_MS   180     // how long the step input is held
#define DODGE_AIM_KEYS  (KEY_R | KEY_ZR)   // ZR on a new 3ds, R on an old one
#define DODGE_STEP_BTN  B_2     // the "{2}" of "Step forward: {^}+{2}"
#define DODGE_USE_DPAD  1       // also push B_UP.. ; the text says "{^}+{2}", so cover both
                                // the d-pad buttons and the stick axis, one of them is what
                                // the game reads for "Step forward/right/left/back"

enum { DG_IDLE, DG_FLICK1, DG_WAIT2 };

static struct {
    int state;
    float dx, dy;      // direction of the first flick
    u32 t_edge;        // when the first edge was hit
    u32 t_centre;      // when it came back to centre
    u32 t_cool;        // cooldown deadline
    u32 t_hold;        // injection start, 0 = not injecting
    int dir;           // 0 up 1 down 2 left 3 right
} dg = { DG_IDLE, 0, 0, 0, 0, 0, 0, 0 };

// push a button event the game will read back through GetNextButtonEvent
static void dg_push(u32 uid, bool down)
{
    int nt = (qt + 1) & 31;
    if (nt == qh) return;              // queue full, drop it
    q[qt] = (ev_t){ uid, down };
    qt = nt;
    sig_raise(sig_btn);
}

static const u32 dg_dpad[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
static const float dg_vec[4][2] = { { 0, 1 }, { 0, -1 }, { -1, 0 }, { 1, 0 } };

static void dg_step_start(int dir, u32 t)
{
    dg.dir = dir;
    dg.t_hold = t;
    dg.t_cool = t + DODGE_COOL_MS;
    if (DODGE_USE_DPAD) dg_push(dg_dpad[dir], true);
    if (DODGE_STEP_BTN) dg_push(DODGE_STEP_BTN, true);
    dbg("dodge: step dir %d", dir);
}

static void dg_step_end(void)
{
    if (DODGE_USE_DPAD) dg_push(dg_dpad[dg.dir], false);
    if (DODGE_STEP_BTN) dg_push(DODGE_STEP_BTN, false);
    dg.t_hold = 0;
}

// one poll of the gesture. x/y are the left stick, -1..1, y up
static void dg_feed(float x, float y, bool aim, u32 t)
{
    float m = sqrtf(x * x + y * y);

    // release the step input once its hold is over, whatever the state machine is doing
    if (dg.t_hold && t - dg.t_hold >= DODGE_HOLD_MS) dg_step_end();

    if (!aim) { dg.state = DG_IDLE; return; }
    if (dg.t_cool && t < dg.t_cool) { dg.state = DG_IDLE; return; }

    switch (dg.state) {
    case DG_IDLE:
        if (m >= DODGE_EDGE_ON) {
            dg.dx = x; dg.dy = y; dg.t_edge = t;
            dg.state = DG_FLICK1;
        }
        break;

    case DG_FLICK1:
        if (t - dg.t_edge > DODGE_GAP_MS) { dg.state = DG_IDLE; break; }
        if (m <= DODGE_EDGE_OFF) { dg.t_centre = t; dg.state = DG_WAIT2; }
        break;

    case DG_WAIT2: {
        if (t - dg.t_centre > DODGE_FLICK_MS || t - dg.t_edge > DODGE_GAP_MS) {
            dg.state = DG_IDLE;
            break;
        }
        if (m < DODGE_EDGE_ON) break;
        // same direction as the first flick?
        float l1 = sqrtf(dg.dx * dg.dx + dg.dy * dg.dy), l2 = m;
        float dot = l1 > 0.001f ? (x * dg.dx + y * dg.dy) / (l1 * l2) : 0;
        if (dot < 0.70f) { dg.state = DG_IDLE; break; }
        int dir = fabsf(x) >= fabsf(y) ? (x >= 0 ? 3 : 2) : (y >= 0 ? 0 : 1);
        dg.state = DG_IDLE;
        dg_step_start(dir, t);
        break;
    }
    }
}

// while a step is being injected the stick is forced to that direction, so the game sees a
// clean push and the player's own wobble can't fight it
static bool dg_override(float *x, float *y)
{
    if (!dg.t_hold) return false;
    *x = dg_vec[dg.dir][0] * 0.9f;
    *y = dg_vec[dg.dir][1] * 0.9f;
    return true;
}

void hid_poll(void)
{
    hidScanInput();
    u32 now = hidKeysHeld();
    // l+r+x/y/a/b are debug keys (main.c), keep them away from the game meanwhile
    if ((now & (KEY_L | KEY_R)) == (KEY_L | KEY_R)) now &= ~(KEY_X | KEY_Y | KEY_A | KEY_B);
    u32 changed = now ^ held;
    held = now;

    bool any = false;
    for (unsigned i = 0; i < NMAP; i++) {
        if (!(changed & map[i].key)) continue;
        int nt = (qt + 1) & 31;
        if (nt == qh) break;
        q[qt] = (ev_t){ map[i].uid, (now & map[i].key) != 0 };
        qt = nt;
        any = true;
    }
    if (any) sig_raise(sig_btn);

    circlePosition cp, cs;
    hidCircleRead(&cp);
    hidCstickRead(&cs);
    // the dodge gesture reads the raw stick, before any override or deadzone
    float nx = cp.dx / 150.0f, ny = -cp.dy / 150.0f;
    if (nx > 1) nx = 1; else if (nx < -1) nx = -1;
    if (ny > 1) ny = 1; else if (ny < -1) ny = -1;
    dg_feed(nx, ny, (now & DODGE_AIM_KEYS) != 0, aee_uptime());

    int na[4];
    na[0] = stick(cp.dx, 150);
    na[1] = stick(-cp.dy, 150);
    na[2] = stick(cs.dx, 150);
    na[3] = stick(-cs.dy, 150);

    float ox = nx, oy = ny;
    if (dg_override(&ox, &oy)) {
        na[0] = 128 + (int)(ox * 127);
        na[1] = 128 + (int)(oy * 127);
    }

    // o3ds has no right stick, dragging on the touch screen stands in for it
    if (now & KEY_TOUCH) {
        touchPosition tp;
        hidTouchRead(&tp);
        if (!touching) { touch_x0 = tp.px; touch_y0 = tp.py; touching = true; }
        na[2] = stick(tp.px - touch_x0, 60);
        na[3] = stick(tp.py - touch_y0, 60);
    } else {
        touching = false;
    }

    // small deadzone so a resting nub doesnt spam position events
    bool moved = false;
    for (int i = 0; i < 4; i++) {
        if (abs(na[i] - 128) < 10) na[i] = 128;
        if (na[i] != axes[i]) { axes[i] = na[i]; moved = true; }
    }
    if (moved) sig_raise(sig_pos);
}
