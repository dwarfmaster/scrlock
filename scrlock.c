
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
    xcb_connection_t* c;
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

static void grab_key(struct screen_t* scr)
{
    xcb_grab_keyboard_cookie_t cookie;
    xcb_grab_keyboard_reply_t* reply;

    cookie = xcb_grab_keyboard(scr->c, 1, scr->win, XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_SYNC);
    reply  = xcb_grab_keyboard_reply(scr->c, cookie, NULL);
    free(reply);
}

static void ungrab_key(struct screen_t* scr)
{
    xcb_ungrab_keyboard(scr->c, XCB_CURRENT_TIME);
    xcb_ungrab_pointer (scr->c, XCB_CURRENT_TIME);
}

static xcb_font_t load_font(xcb_connection_t* c)
{
    static xcb_font_t font = 0;
    if(font > 0)
        return font;
    font = xcb_generate_id(c);
    xcb_open_font(c, font, strlen(FONT), FONT);
    return font;
}

static void load_gcs(struct screen_t* scr)
{
    xcb_connection_t* c = scr->c;
    xcb_gcontext_t gc;
    uint32_t mask;
    uint32_t values[3];
    char* word = DISPLAYED_MSG;
    xcb_char2b_t xcbword[strlen(DISPLAYED_MSG)];
    xcb_query_text_extents_reply_t* reply;
    xcb_query_text_extents_cookie_t cookie;
    uint32_t i;

    /* FG */
    gc = xcb_generate_id(c);
    mask = XCB_GC_FOREGROUND
        | XCB_GC_BACKGROUND
        | XCB_GC_FONT;
    values[0] = scr->xcb->white_pixel;
    values[1] = scr->xcb->black_pixel;
    values[2] = load_font(c);
    xcb_create_gc(c, gc, scr->xcb->root, mask, values);
    scr->fg = gc;

    /* BG */
    gc = xcb_generate_id(c);
    xcb_create_gc(c, gc, scr->xcb->root, mask, values);
    scr->bg = gc;

    /* Font */
    scr->font = values[2];
    for(i = 0; i < strlen(word); ++i) {
        xcbword[i].byte1 = word[i];
        xcbword[i].byte2 = 0;
    }
    cookie = xcb_query_text_extents(c, scr->font, strlen(word), xcbword);
    reply  = xcb_query_text_extents_reply(c, cookie, NULL);
    scr->text_width  = reply->overall_width;
    scr->text_height = reply->overall_ascent + reply->overall_descent;
    free(reply);
}

static void close_gcs(struct screen_t* scr)
{
    /* Nothing to do. */
}

static void open_window(struct screen_t* scr)
{
    xcb_window_t win;
    uint32_t mask;
    uint32_t values[3];

    win = xcb_generate_id(scr->c);
    mask = XCB_CW_BACK_PIXEL
        | XCB_CW_OVERRIDE_REDIRECT
        | XCB_CW_EVENT_MASK;
    values[0] = scr->xcb->black_pixel;
    values[1] = 1;
    values[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;
    xcb_create_window(scr->c,
            XCB_COPY_FROM_PARENT,
            win,
            scr->xcb->root,
            0, 0,
            scr->xcb->width_in_pixels,
            scr->xcb->height_in_pixels,
            1,
            XCB_WINDOW_CLASS_INPUT_OUTPUT,
            scr->xcb->root_visual,
            mask, values
            );

    /* TODO hide cursor */
    /* TODO todo grab input and cursor */

    scr->win = win;
    xcb_map_window(scr->c, win);
}

static void close_window(struct screen_t* scr)
{
    xcb_destroy_window(scr->c, scr->win);
}

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
        load_gcs(scr);
        open_window(scr);

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
        close_gcs(act);
        close_window(act);
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
    grab_key(screens);

    /* TODO mainloop */

    ungrab_key(screens);
    free_screens(screens);
    xcb_disconnect(c);
    return 0;
}


