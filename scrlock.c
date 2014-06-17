
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
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_image.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

static uint8_t pixmap_data[1] = { 0 };

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
    xcb_pixmap_t pixmap;
    xcb_cursor_t cursor;
    uint32_t mask;
    uint32_t values[4];

    /* Create an empty cursor. */
    pixmap = xcb_create_pixmap_from_bitmap_data(scr->c, scr->win, pixmap_data,
            1, 1, 1,
            scr->xcb->black_pixel,
            scr->xcb->white_pixel,
            NULL);
    cursor = xcb_generate_id(scr->c);
    xcb_create_cursor(scr->c, cursor, pixmap, pixmap,
            scr->xcb->black_pixel, scr->xcb->black_pixel, scr->xcb->black_pixel,
            scr->xcb->black_pixel, scr->xcb->black_pixel, scr->xcb->black_pixel,
            0, 0);

    /* Create the window. */
    win = xcb_generate_id(scr->c);
    mask = XCB_CW_BACK_PIXEL
        | XCB_CW_OVERRIDE_REDIRECT
        | XCB_CW_EVENT_MASK
        | XCB_CW_CURSOR;
    values[0] = scr->xcb->black_pixel;
    values[1] = 1;
    values[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS;
    values[3] = cursor;
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

    /* Free the pixmap and the cursor. */
    xcb_free_pixmap(scr->c, pixmap);
    xcb_free_cursor(scr->c, cursor);

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
        scr->c = c;
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

static void mainloop(struct screen_t* scrs, const char* pws)
{
    xcb_connection_t* c = scrs->c;
    char passwd[256];
    unsigned int len;
    int running;
    xcb_generic_event_t* ev;
    xcb_key_press_event_t* keypress;
    uint8_t evtype;
    xcb_keysym_t symbol;
    xcb_key_symbols_t* syms;

    syms = xcb_key_symbols_alloc(c);

    len = 0;
    running = 1;
    while(running && (ev = xcb_wait_for_event(c))) {
        evtype = ev->response_type & ~0x80;
        if(evtype == XCB_EXPOSE) {
            /* TODO draw screens */
        } else if(evtype == XCB_KEY_PRESS) {
            keypress = (xcb_key_press_event_t*)ev;
            symbol   = xcb_key_press_lookup_keysym(syms, keypress, 0);

            /* TODO handle press. */
            if(xcb_is_keypad_key(symbol))
                running = 0;
        }
        free(ev);
    }

    xcb_key_symbols_free(syms);
}

int main(void)
{
    xcb_connection_t* c;
    struct screen_t* screens;
    const char* pws;

    /* Getting password. */
    if(!getpwuid(getuid()))
        die("scrlock: no password entry for you");
    pws = getpw();

    /* Opening connection to X server. */
    c = xcb_connect(NULL, NULL);
    screens = load_screens(c);
    grab_key(screens);
    xcb_flush(c);

    mainloop(screens, pws);

    ungrab_key(screens);
    free_screens(screens);
    xcb_disconnect(c);
    return 0;
}


