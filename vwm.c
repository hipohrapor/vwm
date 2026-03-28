#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xproto.h>

#define BORDER_WIDTH  3
#define BORDER_COLOR  0x00cc44   /* green */
#define GAP           6
#define TERMINAL      "alacritty"


typedef struct Node Node;
struct Node {
    int x, y, w, h;

    int is_leaf;
    Window win;        

    int split_v;         

    Node *parent;
    Node *a, *b;         
};


static Display *dpy;
static Window   root;
static int      screen;
static int      sw, sh;           
static Node    *root_node = NULL; 
static Node    *focus_node = NULL;
static int      running = 1;
static Atom     wm_delete_window;
static Atom     wm_protocols;

static Node *node_new_leaf(Window w, int x, int y, int ww, int hh);
static Node *node_find_win(Node *n, Window w);
static void  node_apply(Node *n);
static void  node_remove(Node *n);
static void  set_focus(Node *n);
static void  spawn(const char *cmd);
static void  close_focused(void);
static void  handle_maprequest(XMapRequestEvent *e);
static void  handle_destroynotify(XDestroyWindowEvent *e);
static void  handle_enternotify(XCrossingEvent *e);
static void  handle_keypress(XKeyEvent *e);
static void  handle_configurerequest(XConfigureRequestEvent *e);
static int   xerror(Display *d, XErrorEvent *ee);

static Node *node_new_leaf(Window w, int x, int y, int ww, int hh) {
    Node *n = calloc(1, sizeof(Node));
    n->is_leaf = 1;
    n->win = w;
    n->x = x; n->y = y; n->w = ww; n->h = hh;
    return n;
}

static Node *node_find_win(Node *n, Window w) {
    if (!n) return NULL;
    if (n->is_leaf) return (n->win == w) ? n : NULL;
    Node *r = node_find_win(n->a, w);
    return r ? r : node_find_win(n->b, w);
}

static void node_apply(Node *n) {
    if (!n) return;
    if (n->is_leaf) {
        int bw = (focus_node == n) ? BORDER_WIDTH : 0;
        int x  = n->x + GAP;
        int y  = n->y + GAP;
        int w  = n->w - 2*GAP - 2*bw;
        int h  = n->h - 2*GAP - 2*bw;
        if (w < 1) w = 1;
        if (h < 1) h = 1;

        XSetWindowBorderWidth(dpy, n->win, bw);
        if (bw)
            XSetWindowBorder(dpy, n->win, BORDER_COLOR);
        XMoveResizeWindow(dpy, n->win, x, y, w, h);
        return;
    }
    if (n->split_v) {
        int half = n->w / 2;
        n->a->x = n->x;       n->a->y = n->y; n->a->w = half;       n->a->h = n->h;
        n->b->x = n->x+half;  n->b->y = n->y; n->b->w = n->w-half;  n->b->h = n->h;
    } else {
        int half = n->h / 2;
        n->a->x = n->x; n->a->y = n->y;       n->a->w = n->w; n->a->h = half;
        n->b->x = n->x; n->b->y = n->y+half;  n->b->w = n->w; n->b->h = n->h-half;
    }
    node_apply(n->a);
    node_apply(n->b);
}

static void node_remove(Node *n) {
    if (!n || !n->is_leaf) return;
    Node *p = n->parent;
    if (!p) {
        free(n);
        root_node  = NULL;
        focus_node = NULL;
        return;
    }
    Node *sibling = (p->a == n) ? p->b : p->a;
    Node *gp = p->parent;

    sibling->parent = gp;
    if (!gp) {
        root_node = sibling;
    } else {
        if (gp->a == p) gp->a = sibling;
        else            gp->b = sibling;
    }

    sibling->x = p->x; sibling->y = p->y;
    sibling->w = p->w; sibling->h = p->h;

    free(p);
    free(n);
}

static void set_focus(Node *n) {
    focus_node = n;
    if (n) {
        XSetInputFocus(dpy, n->win, RevertToPointerRoot, CurrentTime);
        XRaiseWindow(dpy, n->win);
    }
    node_apply(root_node);
}

static void spawn(const char *cmd) {
    if (fork() == 0) {
        if (dpy) close(ConnectionNumber(dpy));
        setsid();
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        exit(0);
    }
}

static void close_focused(void) {
    if (!focus_node) return;
    Window w = focus_node->win;

    Atom *protos;
    int n;
    if (XGetWMProtocols(dpy, w, &protos, &n)) {
        for (int i = 0; i < n; i++) {
            if (protos[i] == wm_delete_window) {
                XEvent ev = {0};
                ev.type = ClientMessage;
                ev.xclient.window       = w;
                ev.xclient.message_type = wm_protocols;
                ev.xclient.format       = 32;
                ev.xclient.data.l[0]    = wm_delete_window;
                ev.xclient.data.l[1]    = CurrentTime;
                XSendEvent(dpy, w, False, NoEventMask, &ev);
                XFree(protos);
                return;
            }
        }
        XFree(protos);
    }
    XKillClient(dpy, w);
}

static void handle_maprequest(XMapRequestEvent *e) {
    XSelectInput(dpy, e->window,
                 EnterWindowMask | FocusChangeMask | PropertyChangeMask);
    XMapWindow(dpy, e->window);

    if (!root_node) {
        root_node = node_new_leaf(e->window, 0, 0, sw, sh);
        root_node->parent = NULL;
        set_focus(root_node);
        return;
    }

    Node *target = focus_node ? focus_node : root_node;

    if (!target->is_leaf) target = root_node;

    int split_v = (target->w >= target->h);

    Node *branch = calloc(1, sizeof(Node));
    branch->x = target->x; branch->y = target->y;
    branch->w = target->w; branch->h = target->h;
    branch->split_v = split_v;
    branch->parent = target->parent;
    branch->is_leaf = 0;

    Node *old = calloc(1, sizeof(Node));
    *old = *target;          
    old->parent = branch;
    old->is_leaf = 1;

    Node *neo = node_new_leaf(e->window, 0, 0, 0, 0);
    neo->parent = branch;

    branch->a = old;
    branch->b = neo;

    if (!branch->parent) {
        root_node = branch;
    } else {
        if (branch->parent->a == target) branch->parent->a = branch;
        else                             branch->parent->b = branch;
    }

    free(target);

    set_focus(neo);
}

static void handle_destroynotify(XDestroyWindowEvent *e) {
    Node *n = node_find_win(root_node, e->window);
    if (!n) return;

    Node *next = NULL;
    Node *p = n->parent;
    if (p) {
        Node *sib = (p->a == n) ? p->b : p->a;
        while (sib && !sib->is_leaf) sib = sib->a;
        next = sib;
    }

    node_remove(n);
    set_focus(next);
}

static void handle_enternotify(XCrossingEvent *e) {
    if (e->mode != NotifyNormal || e->detail == NotifyInferior) return;
    Node *n = node_find_win(root_node, e->window);
    if (n && n != focus_node) set_focus(n);
}

static void handle_keypress(XKeyEvent *e) {
    KeySym sym = XLookupKeysym(e, 0);
    unsigned int mod = e->state & ~(LockMask);

    if (sym == XK_Return && mod == Mod1Mask) {
        spawn(TERMINAL);
        return;
    }
    if (sym == XK_q && mod == Mod1Mask) {
        close_focused();
        return;
    }
    if (sym == XK_q && mod == (Mod1Mask|ShiftMask)) {
        running = 0;
        return;
    }
}

static void handle_configurerequest(XConfigureRequestEvent *e) {
    Node *n = node_find_win(root_node, e->window);
    if (n) {
        XConfigureEvent ce = {0};
        ce.type   = ConfigureNotify;
        ce.event  = e->window;
        ce.window = e->window;
        ce.x = n->x + GAP;
        ce.y = n->y + GAP;
        int bw = (focus_node == n) ? BORDER_WIDTH : 0;
        ce.width  = n->w - 2*GAP - 2*bw;
        ce.height = n->h - 2*GAP - 2*bw;
        ce.border_width = bw;
        XSendEvent(dpy, e->window, False, StructureNotifyMask, (XEvent*)&ce);
        return;
    }
    XWindowChanges wc;
    wc.x            = e->x;
    wc.y            = e->y;
    wc.width        = e->width;
    wc.height       = e->height;
    wc.border_width = e->border_width;
    wc.sibling      = e->above;
    wc.stack_mode   = e->detail;
    XConfigureWindow(dpy, e->window, e->value_mask, &wc);
}

static int xerror(Display *d, XErrorEvent *ee) {
    (void)d;
    if (ee->error_code == BadWindow) return 0;
    if (ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) return 0;
    if (ee->request_code == X_PolyText8    && ee->error_code == BadDrawable) return 0;
    if (ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable) return 0;
    if (ee->request_code == X_ConfigureWindow   && ee->error_code == BadMatch)    return 0;
    if (ee->request_code == X_GrabButton        && ee->error_code == BadAccess)   return 0;
    if (ee->request_code == X_GrabKey           && ee->error_code == BadAccess)   return 0;
    char msg[256];
    XGetErrorText(d, ee->error_code, msg, sizeof(msg));
    fprintf(stderr, "vwm: X error: %s (req %d, err %d)\n",
            msg, ee->request_code, ee->error_code);
    return 0;
}

static void sigchld_handler(int s) {
    (void)s;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(void) {
    signal(SIGCHLD, sigchld_handler);

    dpy = XOpenDisplay(NULL);
    if (!dpy) { fputs("vwm: cannot open display\n", stderr); return 1; }

    XSetErrorHandler(xerror);

    screen = DefaultScreen(dpy);
    root   = RootWindow(dpy, screen);
    sw     = DisplayWidth(dpy, screen);
    sh     = DisplayHeight(dpy, screen);

    wm_protocols      = XInternAtom(dpy, "WM_PROTOCOLS",     False);
    wm_delete_window  = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

    XSelectInput(dpy, root,
                 SubstructureRedirectMask |
                 SubstructureNotifyMask   |
                 EnterWindowMask          |
                 KeyPressMask);

    KeyCode kret = XKeysymToKeycode(dpy, XK_Return);
    KeyCode kq   = XKeysymToKeycode(dpy, XK_q);
    XGrabKey(dpy, kret, Mod1Mask,              root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, kq,   Mod1Mask,              root, True, GrabModeAsync, GrabModeAsync);
    XGrabKey(dpy, kq,   Mod1Mask|ShiftMask,    root, True, GrabModeAsync, GrabModeAsync);

    XSetWindowBackground(dpy, root, BlackPixel(dpy, screen));
    XClearWindow(dpy, root);

    XSync(dpy, False);

    XEvent ev;
    while (running && !XNextEvent(dpy, &ev)) {
        switch (ev.type) {
        case MapRequest:       handle_maprequest    (&ev.xmaprequest);    break;
        case DestroyNotify:    handle_destroynotify (&ev.xdestroywindow); break;
        case EnterNotify:      handle_enternotify   (&ev.xcrossing);      break;
        case KeyPress:         handle_keypress      (&ev.xkey);           break;
        case ConfigureRequest: handle_configurerequest(&ev.xconfigurerequest); break;
        case MappingNotify:    XRefreshKeyboardMapping(&ev.xmapping);     break;
        }
    }

    XCloseDisplay(dpy);
    return 0;
}
