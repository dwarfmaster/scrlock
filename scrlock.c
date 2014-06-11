
/* See LICENSE file for license details. */
#include "config.h"

#define _XOPEN_SOURCE 500
#if HAVE_SHADOW_H
#include <shadow.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <xcb/xcb.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

struct screen_t {
    xcb_screen_t* xcb;
    xcb_window_t win;
    xcb_gcontext_t fg;
    xcb_gcontext_t bg;
    xcb_font_t font;
    uint32_t text_height;
    uint32_t text_width;
    struct screen_t* next;
};

static void
die(const char *errstr, ...) {
	va_list ap;

	va_start(ap, errstr);
	vfprintf(stderr, errstr, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

#ifndef HAVE_BSD_AUTH
static const char *
getpw(void) { /* only run as root */
	const char *rval;
	struct passwd *pw;

	pw = getpwuid(getuid());
	if(!pw)
		die("slock: cannot retrieve password entry (make sure to suid or sgid slock)");
	endpwent();
	rval =  pw->pw_passwd;

#if HAVE_SHADOW_H
	if (strlen(rval) >= 1) { /* kludge, assumes pw placeholder entry has len >= 1 */
		struct spwd *sp;
		sp = getspnam(getenv("USER"));
		if(!sp)
			die("slock: cannot retrieve shadow entry (make sure to suid or sgid slock)\n");
		endspent();
		rval = sp->sp_pwdp;
	}
#endif

	/* drop privileges */
	if(setgid(pw->pw_gid) < 0 || setuid(pw->pw_uid) < 0)
		die("slock: cannot drop privileges");
	return rval;
}
#endif

static struct screen_t* load_screens(xcb_connection_t* c)
{
    struct screen_t* scr;
    struct screen_t* act;
    struct screen_t* first = NULL;
    xcb_screen_iterator_t it;

    it = xcb_setup_roots_iterator(xcb_get_setup(c));
    for(; it.rem; xcb_screen_next(&it)) {
        scr = malloc(sizeof(struct screen_t));
        scr->xcb = it.data;
        /* TODO load window and gcs. */

        if(!first)
            first = scr;
        else
            act->next = scr;
        scr->next = NULL;
        act = scr;
    }

    return first;
}

static void free_screens(struct screen_t* scrs)
{
    struct screen_t* act = scrs;
    struct screen_t* next;
    while(act) {
        next = act->next;
        free(act);
        act = next;
    }
}

int main(void)
{
    xcb_connection_t* c;
    struct screen_t* screens;

    /* Opening connection to X server. */
    c = xcb_connect(NULL, NULL);
    screens = load_screens(c);

    /* TODO mainloop */

    free_screens(screens);
    xcb_disconnect(c);
    return 0;
}


