/* NetHackDCC engine driver -- v0 (Phase 0, task 2)
 *
 * Boots libnethack.a headless, registers the shim graphics callback, and
 * streams every window/UI event the engine emits as newline-delimited JSON
 * (NDJSON) on stdout.  Input is deliberately minimal at this stage: raw
 * keystrokes are read from stdin (or the DCC_KEYS env var) to answer the
 * engine's blocking nhgetch(); every other blocking prompt is auto-answered
 * with a safe "cancel / default" so the game boots unattended to the first
 * map.  Later Phase-0 tasks add glyph decoding, cmdq injection, and full
 * state snapshots (which will pull in hack.h); this file stays header-free.
 *
 * Protocol note: this is NOT the real Phase-1 protocol (which frames both
 * directions as NDJSON).  v0 uses raw-key stdin purely to drive a boot dump.
 *
 * Build: see server/driver/Makefile (links ../../src/libnh.a + liblua).
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/stat.h>

/* --- libnethack.a public API (no header is shipped; declare inline, per
 *     sys/libnh/README.md) --- */
int nhmain(int argc, char *argv[]);
typedef void (*shim_callback_t)(const char *name, void *ret_ptr,
                                const char *fmt, ...);
void shim_graphics_set_callback(shim_callback_t cb);

/* Phase-0 task 5: command-queue injection. cmdq_add_ec() appends an
 * extended-command function pointer to CQ_CANNED (queue index 0, per
 * include/hack.h's cmdq_cmdtypes); rhack() drains that queue at the top of
 * every player turn, ahead of any real keystroke read (src/cmd.c:3643) --
 * this is the same mechanism the engine's own canned command sequences use
 * (e.g. act_on_act() at src/cmd.c:4688). ddoinv is extcmdlist[]'s "inventory"
 * entry's ef_funct (src/cmd.c, key 'i'); calling it via the queue instead of
 * a real 'i' keypress proves the injection path end to end. */
void cmdq_add_ec(int q, int (*fn)(void));
int ddoinv(void);
#define CQ_CANNED 0

/* Phase-0 task 4: glyph decoding. Implemented in glyphdecode.c (the one
 * translation unit in this driver that includes hack.h -- see its file
 * comment) rather than here, so main.c can stay header-free while still
 * turning each print_glyph's opaque glyph_info* into {ch, color, monIdx}
 * via the engine's own glyph-band macros. */
void dcc_decode_glyph(const void *glyphinfo_ptr, int *ch, int *color,
                      int *monIdx);

/* NetHack type facts we depend on (verified against include/global.h and
 * include/wintype.h at build commit):
 *   winid   = int          WIN_ERR = (winid)-1  -> valid ids are >= 0
 *   coordxy = int16_t
 *   boolean = unsigned char (1 byte)
 * Under the C varargs ABI, char/short/boolean/coordxy all arrive int-promoted,
 * so scalar args are read with va_arg(ap,int); 's' args are char*, 'p' are
 * void*.  Return values are written back through ret_ptr, sized by fmt[0]. */
typedef int16_t coordxy_t;

/* ------------------------------------------------------------------ */
/* JSON output helpers                                                 */
/* ------------------------------------------------------------------ */

/* Escape a C string as a JSON string (with surrounding quotes), or emit the
 * literal null for a NULL pointer. */
static void
json_puts_escaped(const char *s)
{
    if (!s) {
        fputs("null", stdout);
        return;
    }
    putchar('"');
    for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n", stdout); break;
        case '\r': fputs("\\r", stdout); break;
        case '\t': fputs("\\t", stdout); break;
        case '\b': fputs("\\b", stdout); break;
        case '\f': fputs("\\f", stdout); break;
        default:
            if (c < 0x20)
                printf("\\u%04x", c);
            else
                putchar(c);
        }
    }
    putchar('"');
}

/* ------------------------------------------------------------------ */
/* Scripted input (v0): DCC_KEYS env var, then stdin, then EOF -> exit  */
/* ------------------------------------------------------------------ */

static const char *g_keys = NULL; /* cursor into DCC_KEYS */

static void emit_simple(const char *cb);

static int
read_key(void)
{
    if (g_keys && *g_keys)
        return (unsigned char) *g_keys++;

    int c = getchar();
    if (c == EOF) {
        /* v0: out of scripted input -- end the run cleanly so an automated
         * test terminates after the boot/first-map dump. */
        emit_simple("__eof");
        fflush(stdout);
        exit(0);
    }
    return c;
}

/* ------------------------------------------------------------------ */
/* The shim callback: decode args by fmt, emit NDJSON, supply returns   */
/* ------------------------------------------------------------------ */

#define MAXARG 16
struct arg {
    char t;             /* 'i' scalar, 's' string, 'p' pointer */
    long i;
    const char *s;
    void *p;
};

static int g_next_winid = 1;

static int
winid_next(void)
{
    return g_next_winid++;
}

/* Phase-0 task 5: when set, queue a canned "inventory" extcmd the first time
 * the engine asks for a real keystroke, so it fires on the *next* player
 * turn (the current ask is still answered normally from DCC_KEYS/stdin). */
static int g_inject_inventory = 0;
static int g_injected_inventory = 0;

/* Emit one NDJSON line for a decoded callback:
 *   {"cb":NAME,"fmt":FMT,"args":[...] [,"ret":N]} */
static void
emit_event(const char *name, const char *fmt, const struct arg *args,
           int nargs, int have_ret, long ret_val)
{
    fputs("{\"cb\":", stdout);
    json_puts_escaped(name);
    fputs(",\"fmt\":", stdout);
    json_puts_escaped(fmt ? fmt : "");
    fputs(",\"args\":[", stdout);
    for (int i = 0; i < nargs; i++) {
        if (i) putchar(',');
        switch (args[i].t) {
        case 's':
            json_puts_escaped(args[i].s);
            break;
        case 'p':
            if (args[i].p)
                printf("\"0x%lx\"", (unsigned long) (uintptr_t) args[i].p);
            else
                fputs("null", stdout);
            break;
        default:
            printf("%ld", args[i].i);
            break;
        }
    }
    putchar(']');
    if (have_ret)
        printf(",\"ret\":%ld", ret_val);
    fputs("}\n", stdout);
    fflush(stdout);
}

static void
dcc_cb(const char *name, void *ret_ptr, const char *fmt, ...)
{
    struct arg args[MAXARG] = { { 0, 0, NULL, NULL } };
    int nargs = 0;
    va_list ap;

    va_start(ap, fmt);
    for (const char *c = (fmt && *fmt) ? fmt + 1 : ""; *c && nargs < MAXARG;
         c++) {
        struct arg *a = &args[nargs++];
        switch (*c) {
        case 's':
            a->t = 's';
            a->s = va_arg(ap, const char *);
            break;
        case 'p':
            a->t = 'p';
            a->p = va_arg(ap, void *);
            break;
        default: /* i,0,1,2,b,c,n : all int-promoted in varargs */
            a->t = 'i';
            a->i = va_arg(ap, int);
            break;
        }
    }
    va_end(ap);

    /* Input callbacks block on stdin; emit the "ask" event BEFORE reading so
     * the prompt is visible even when input hits EOF (read_key exits there).
     * Then supply the key/out-params.  Non-input returns are computed below
     * and echoed inline as "ret". */
    if (!strcmp(name, "shim_nhgetch") || !strcmp(name, "shim_nh_poskey")) {
        if (g_inject_inventory && !g_injected_inventory) {
            /* Queue onto CQ_CANNED now, answer this ask normally; rhack()
             * will drain the queue at the start of the *next* turn instead
             * of prompting for a real key. */
            cmdq_add_ec(CQ_CANNED, ddoinv);
            g_injected_inventory = 1;
            emit_simple("__inject_inventory");
        }
        emit_event(name, fmt, args, nargs, 0, 0);
        if (!strcmp(name, "shim_nh_poskey") && nargs >= 3) {
            /* x(p) y(p) mod(p) -- report a keystroke, no map click */
            if (args[0].p) *(coordxy_t *) args[0].p = 0;
            if (args[1].p) *(coordxy_t *) args[1].p = 0;
            if (args[2].p) *(int *) args[2].p = 0;
        }
        int key = read_key(); /* may exit(0) on EOF, after the ask above */
        if (ret_ptr) *(int *) ret_ptr = key;
        printf("{\"cb\":\"%s.answer\",\"ret\":%d}\n", name, key);
        fflush(stdout);
        return;
    }

    /* Phase-0 task 4: decode the glyph_info batch instead of emitting raw
     * pointers -- args: w(i) x(1) y(1) glyphinfo(p) bkglyphinfo(p). */
    if (!strcmp(name, "shim_print_glyph") && nargs >= 4) {
        int ch, color, monIdx;
        dcc_decode_glyph(args[3].p, &ch, &color, &monIdx);
        printf("{\"cb\":\"shim_print_glyph\",\"w\":%ld,\"x\":%ld,\"y\":%ld,"
               "\"ch\":%d,\"color\":%d,\"monIdx\":%d}\n",
               args[0].i, args[1].i, args[2].i, ch, color, monIdx);
        fflush(stdout);
        return;
    }

    /* ---- compute return value / out-params for non-blocking calls ---- */
    long ret_val = 0;      /* for numeric ret types, and to echo in JSON */
    int have_ret = 0;      /* 1 if we set a numeric ret */

    if (!strcmp(name, "shim_create_nhwindow")) {
        ret_val = winid_next();
        if (ret_ptr) *(int *) ret_ptr = (int) ret_val;
        have_ret = 1;
    } else if (!strcmp(name, "shim_yn_function")) {
        /* args: query(s) resp(s) def(i=char) ; return the default, else the
         * first listed valid response, else ESC. */
        char def = (nargs >= 3) ? (char) args[2].i : 0;
        const char *resp = (nargs >= 2) ? args[1].s : NULL;
        char r = def ? def : (resp && *resp ? *resp : '\033');
        ret_val = (unsigned char) r;
        if (ret_ptr) *(char *) ret_ptr = r;
        have_ret = 1;
    } else if (!strcmp(name, "shim_message_menu")) {
        /* args: let(i=char) how(i) mesg(s) ; return the default letter. */
        char let = (nargs >= 1) ? (char) args[0].i : '\033';
        ret_val = (unsigned char) let;
        if (ret_ptr) *(char *) ret_ptr = let;
        have_ret = 1;
    } else if (!strcmp(name, "shim_select_menu")) {
        /* args: window(i) how(i) menu_list(p). v0 makes no selection. The
         * menu convention is: return -1 to signal the user cancelled (ESC),
         * which yes/no-style prompts treat as the default/"no"; returning 0
         * means "0 items selected", which some callers re-prompt on. Leave
         * *menu_list untouched (NULL); the core won't dereference it. */
        int how = (nargs >= 2) ? (int) args[1].i : 0;
        ret_val = (how == 0 /* PICK_NONE */) ? 0 : -1;
        if (ret_ptr) *(int *) ret_ptr = (int) ret_val;
        have_ret = 1;
    } else if (!strcmp(name, "shim_getlin")) {
        /* args: query(s) bufp(p) ; write ESC to signal a cancelled entry
         * (bufp is an uninitialised caller buffer, so we MUST write it). */
        if (nargs >= 2 && args[1].p) {
            char *bufp = (char *) args[1].p;
            bufp[0] = '\033';
            bufp[1] = '\0';
        }
    }
    /* All other return-valued callbacks (get_ext_cmd, doprev_message,
     * getmsghistory, ctrl_nhwindow, ...) keep the shim's pre-zeroed ret
     * (0 / NULL), which is the safe "none / cancel" answer. */

    emit_event(name, fmt, args, nargs, have_ret, ret_val);
}

static void
emit_simple(const char *cb)
{
    fputs("{\"cb\":", stdout);
    json_puts_escaped(cb);
    fputs(",\"fmt\":\"\",\"args\":[]}\n", stdout);
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/* Playground setup + boot                                             */
/* ------------------------------------------------------------------ */

/* SYSCF_FILE is compiled to an absolute path (HACKDIR/sysconf) and is read
 * regardless of the per-session -d dir, so ensure it exists there. */
#ifndef DCC_SYSCF_FILE
#define DCC_SYSCF_FILE "/home/user/NetHackDCC/playground/sysconf"
#endif

static void
ensure_symlink(const char *target, const char *linkpath)
{
    struct stat st;
    if (lstat(linkpath, &st) == 0)
        return; /* already present */
    if (symlink(target, linkpath) != 0 && errno != EEXIST)
        fprintf(stderr, "nhdcc-driver: symlink %s -> %s: %s\n",
                linkpath, target, strerror(errno));
}

static void
write_sysconf(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return;
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "nhdcc-driver: cannot write sysconf %s: %s\n",
                path, strerror(errno));
        return;
    }
    /* Minimal sysconf: allow anyone into wizard/explore, no shell escape.
     * (MAXPLAYERS is capped at 25 by the engine.) */
    fputs("WIZARDS=*\nEXPLORERS=*\nSHELLERS=\nMAXPLAYERS=25\n", f);
    fclose(f);
}

/* mkdir -p for a single path (creates missing parent components). */
static int
mkdir_p(const char *path)
{
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof tmp)
        return -1;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return (mkdir(tmp, 0755) == 0 || errno == EEXIST) ? 0 : -1;
}

int
main(int argc, char *argv[])
{
    const char *session_dir = NULL;
    const char *data_dir = getenv("NHDCC_DATA");
    const char *plname = "DCCbot";

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--session") && i + 1 < argc)
            session_dir = argv[++i];
        else if (!strcmp(argv[i], "--data") && i + 1 < argc)
            data_dir = argv[++i];
        else if (!strcmp(argv[i], "--name") && i + 1 < argc)
            plname = argv[++i];
        else {
            fprintf(stderr,
                    "usage: %s --session <dir> [--data <datdir>] "
                    "[--name <plname>]\n", argv[0]);
            return 2;
        }
    }
    if (!session_dir) {
        fprintf(stderr, "nhdcc-driver: --session <dir> is required\n");
        return 2;
    }
    if (!data_dir)
        data_dir = "dat";

    setvbuf(stdout, NULL, _IOLBF, 0);
    g_keys = getenv("DCC_KEYS");
    g_inject_inventory = getenv("DCC_INJECT_INVENTORY") != NULL;

    /* Build the per-session playground. */
    if (mkdir_p(session_dir) != 0) {
        fprintf(stderr, "nhdcc-driver: mkdir %s: %s\n", session_dir,
                strerror(errno));
        return 1;
    }
    char save_dir[1024], link[1024], target[1024], scratch[1088];
    snprintf(save_dir, sizeof save_dir, "%s/save", session_dir);
    mkdir(save_dir, 0755);

    /* getlock() and the scorefile code require these to already exist (a
     * normal `make install` creates them); create empty ones if missing. */
    const char *touch_files[] = { "perm", "record", "logfile", NULL };
    for (int i = 0; touch_files[i]; i++) {
        snprintf(scratch, sizeof scratch, "%s/%s", session_dir,
                 touch_files[i]);
        int fd = open(scratch, O_WRONLY | O_CREAT, 0644);
        if (fd >= 0)
            close(fd);
    }

    /* Symlink the DLB bundle + loose data files the engine opens relative to
     * its working dir (nhdat, symbols, license). data_dir may be relative to
     * the current cwd; resolve to absolute for a stable symlink target. */
    char data_abs[PATH_MAX]; /* realpath() requires a PATH_MAX-sized buffer */
    if (!realpath(data_dir, data_abs)) {
        fprintf(stderr, "nhdcc-driver: cannot resolve data dir %s: %s\n",
                data_dir, strerror(errno));
        return 1;
    }
    const char *data_files[] = { "nhdat", "symbols", "license", NULL };
    for (int i = 0; data_files[i]; i++) {
        snprintf(target, sizeof target, "%s/%s", data_abs, data_files[i]);
        snprintf(link, sizeof link, "%s/%s", session_dir, data_files[i]);
        ensure_symlink(target, link);
    }

    /* SYSCF_FILE is at a compiled absolute path; make sure it exists. */
    {
        char syscf_dir[1024];
        snprintf(syscf_dir, sizeof syscf_dir, "%s", DCC_SYSCF_FILE);
        char *slash = strrchr(syscf_dir, '/');
        if (slash) {
            *slash = '\0';
            mkdir_p(syscf_dir);
        }
        write_sysconf(DCC_SYSCF_FILE);
    }

    /* This libnh build is compiled without CHDIR, so nhmain never changes to
     * the playground itself (and the -d option is inert -- its value would
     * just break the option scan before -u). Change into the per-session dir
     * ourselves so every relative path (nhdat, symbols, perm, record, save/)
     * resolves here; also set NETHACKDIR in case a CHDIR build is used later. */
    setenv("NETHACKDIR", session_dir, 1);
    if (chdir(session_dir) != 0) {
        fprintf(stderr, "nhdcc-driver: chdir %s: %s\n", session_dir,
                strerror(errno));
        return 1;
    }

    /* Register the callback, then hand control to the engine's blocking
     * moveloop. */
    shim_graphics_set_callback(dcc_cb);

    char uarg[128];
    snprintf(uarg, sizeof uarg, "-u%s", plname);
    char *nh_argv[] = { "nethackdcc", uarg, NULL };
    int nh_argc = 2;

    emit_simple("__boot");
    nhmain(nh_argc, nh_argv);
    emit_simple("__exit");
    return 0;
}
