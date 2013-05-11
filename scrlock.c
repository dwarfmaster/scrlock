
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
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#if HAVE_BSD_AUTH
#include <login_cap.h>
#include <bsd_auth.h>
#endif

#ifdef SPY
static int spys_count;
#endif

typedef struct {
	int screen;
	Window root, win;
	Pixmap pmap;
	unsigned long colors[2];
} Lock;

static Lock **locks;
static int nscreens;
static Bool running = True;

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

#ifdef MESSAGE
static void saveMsg(const char* msg)
{
	char buffer[1024];

	const char* home = getenv("HOME");
	if(home == NULL)
		return;
	snprintf(buffer, 1024, "%s/MESSAGES", home);

	time_t t = time(NULL);
	if(t < 0)
		return;

	struct tm* tm = localtime(&t);
	if(tm == NULL)
		return;

	FILE* file = fopen(buffer, "a");
	if(file == NULL)
		return;

	fprintf(file, "%d/%d/%d; %d:%d : \"%s\"\n", tm->tm_mday, tm->tm_mon+1, tm->tm_year + 1900, tm->tm_hour, tm->tm_min, msg);
	fclose(file);
}

static GC createGC(Display* dpy, int screen, Drawable w, unsigned long color)
{
	GC gc;
	XGCValues values;
	// XColor color, dummy;
	static Font font = 0;

	// XAllocNamedColor(dpy, DefaultColormap(dpy, screen), "Black", &color, &dummy);
	values.foreground = color;
	values.background = color;
	gc = XCreateGC(dpy, w, GCForeground | GCBackground, &values);

	if(font == 0)
		font = XLoadFont(dpy, FONT);
	XSetFont(dpy, gc, font);

	return gc;
}

static void blitMsg(Display* dpy, int screen, const char* msg, int len)
{
	GC gc;
	char buf[128];

	XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[1]);
	XClearWindow(dpy, locks[screen]->win);

	gc = createGC(dpy, screen, locks[screen]->win, locks[screen]->colors[0]);
	XDrawString(dpy, locks[screen]->win, gc,
			XDisplayWidth(dpy, screen)/2 - 200,
			XDisplayHeight(dpy, screen)/2,
			msg, len);

	sprintf(buf, "%i characters left.", 50-len);
	XDrawString(dpy, locks[screen]->win, gc,
			XDisplayWidth(dpy, screen)/2 - 30,
			XDisplayHeight(dpy, screen)/2 + 20,
			buf, strlen(buf));

	XDrawString(dpy, locks[screen]->win, gc, 
			XDisplayWidth(dpy, screen)/2 - 50,
			XDisplayHeight(dpy, screen)/2 + 50,
			"Press ctrl + space to cancel.", 29);
}
#endif

static void blitLocked(Display* dpy, int screen)
{
	XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[0]);
	XClearWindow(dpy, locks[screen]->win);

#ifdef MESSAGE
	GC gc;
	gc = createGC(dpy, screen, locks[screen]->win, locks[screen]->colors[1]);
	XDrawString(dpy, locks[screen]->win, gc, 
			XDisplayWidth(dpy, screen)/2 - 50,
			XDisplayHeight(dpy, screen)/2 - 10,
			"Press ctrl + space to let a message.", 35);
#endif
}

static void blitUnlock(Display* dpy, int screen)
{
	XSetWindowBackground(dpy, locks[screen]->win, locks[screen]->colors[1]);
	XClearWindow(dpy, locks[screen]->win);

#ifdef MESSAGE
	GC gc;
	gc = createGC(dpy, screen, locks[screen]->win, locks[screen]->colors[0]);
	XDrawString(dpy, locks[screen]->win, gc, 
			XDisplayWidth(dpy, screen)/2 - 50,
			XDisplayHeight(dpy, screen)/2 - 10,
			"Press ctrl + space to let a message.", 35);
#endif
}

#ifdef SPY
void takePicture()
{
	char* home = getenv("HOME");
	if(home == NULL)
		return;

	char fname[512];
	time_t t = time(NULL);
	if(t < 0)
		return;
	struct tm* tm = localtime(&t);
	if(tm == NULL)
		return;
	snprintf(fname, 512, "%d-%d_%d-%d-%d.png", tm->tm_mday, tm->tm_mon+1, tm->tm_hour, tm->tm_min, tm->tm_sec);

	char path[1024];
	snprintf(path, 1024, "%s/%s/%s", home, SPY_SUBDIR, fname);

	pid_t pid;
	do {
		pid = fork();
	} while(pid == -1 && errno == EAGAIN);

	if(pid == -1)
		return;
	else if(pid == 0)
	{
		char* argv[] = {FSWEBCAM, "-q", "-r", "1920x1080", path, NULL};
		execv(FSWEBCAM, argv);
		exit(EXIT_SUCCESS);
	}
	else
		++spys_count;
}
#endif

static void
#ifdef HAVE_BSD_AUTH
readpw(Display *dpy)
#else
readpw(Display *dpy, const char *pws)
#endif
{
	char buf[32], passwd[256];
	char* act;
	int num, screen;
	unsigned int len, llen;
	unsigned int* usedLen = NULL;
	unsigned int maxLen;
	KeySym ksym;
	XEvent ev;

#ifdef MESSAGE
	Bool inMsg;
	unsigned int msglen;
	char msg[51];
	msg[0] = 0;
	msglen = 0;
	inMsg = False;
#endif

#ifndef SPY
	spys_count = 0;
#endif

	len = llen = 0;
	usedLen = &len;
	act = passwd;
	maxLen = 255;
	running = True;

	for(screen = 0; screen < nscreens; screen++)
		blitLocked(dpy, screen);

	/* As "slock" stands for "Simple X display locker", the DPMS settings
	 * had been removed and you can set it with "xset" or some other
	 * utility. This way the user can easily set a customized DPMS
	 * timeout. */
	while(running && !XNextEvent(dpy, &ev)) {
		if(ev.type == KeyPress) {
#ifdef MESSAGE
			// Évènement ctrl + space
			if(ev.xkey.keycode == XKeysymToKeycode(dpy, XK_space)
					&& (ev.xkey.state & ControlMask))
			{
				inMsg = !inMsg;
				if(inMsg) {
					usedLen = &msglen;
					act = msg;
					maxLen = 50;

					for(screen = 0; screen < nscreens; screen++)
						blitMsg(dpy, screen, msg, msglen);
				}
				else {
					msglen = 0;
					msg[0] = 0;
					usedLen = &len;
					act = passwd;
					maxLen = 255;

					if(len != 0) {
						for(screen = 0; screen < nscreens; screen++)
							blitUnlock(dpy, screen);
					} else {
						for(screen = 0; screen < nscreens; screen++)
							blitLocked(dpy, screen);
					}
				}
			}
#endif

			buf[0] = 0;
			num = XLookupString(&ev.xkey, buf, sizeof buf, &ksym, 0);
			if(IsKeypadKey(ksym)) {
				if(ksym == XK_KP_Enter)
					ksym = XK_Return;
				else if(ksym >= XK_KP_0 && ksym <= XK_KP_9)
					ksym = (ksym - XK_KP_0) + XK_0;
			}
			if(IsFunctionKey(ksym) || IsKeypadKey(ksym)
					|| IsMiscFunctionKey(ksym) || IsPFKey(ksym)
					|| IsPrivateKeypadKey(ksym))
				continue;
			switch(ksym) {
				case XK_Return:
#ifdef MESSAGE
					if(inMsg) {
						msg[msglen] = 0;
						saveMsg(msg);

						msg[0] = 0;
						msglen = 0;
						usedLen = &len;
						act = passwd;
						maxLen = 255;
						inMsg = False;

						if(len != 0) {
							for(screen = 0; screen < nscreens; screen++)
								blitUnlock(dpy, screen);
						} else {
							for(screen = 0; screen < nscreens; screen++)
								blitLocked(dpy, screen);
						}
					}
					else {
#endif
						passwd[len] = 0;
#ifdef HAVE_BSD_AUTH
						running = !auth_userokay(getlogin(), NULL, "auth-xlock", passwd);
#else
						running = strcmp(crypt(passwd, pws), pws);
#endif
						if(running != False)
						{
							XBell(dpy, 100);
#ifdef SPY
							takePicture();
#endif
						}
						len = 0;
#ifdef MESSAGE
					}
#endif
					break;
				case XK_Escape:
					*usedLen = 0;
					break;
				case XK_BackSpace:
					if(*usedLen)
						--(*usedLen);
					break;
				default:
					if(num && !iscntrl((int) buf[0]) && (*usedLen + num < maxLen)) { 
						memcpy(act + *usedLen, buf, num);
						(*usedLen) += num;
					}
					break;
			}
#ifdef MESSAGE
			if(inMsg) {
				for(screen = 0; screen < nscreens; screen++)
					blitMsg(dpy, screen, msg, msglen);
			}
			else {
#endif
				if(llen == 0 && len != 0) {
					for(screen = 0; screen < nscreens; screen++)
						blitUnlock(dpy, screen);
				} else if(llen != 0 && len == 0) {
					for(screen = 0; screen < nscreens; screen++)
						blitLocked(dpy, screen);
				}
				llen = len;
#ifdef MESSAGE
			}
#endif
		}
		else for(screen = 0; screen < nscreens; screen++)
			XRaiseWindow(dpy, locks[screen]->win);
	}

#ifdef SPY
	for(; spys_count >= 0; --spys_count)
		wait(NULL);
#endif
}

static void
unlockscreen(Display *dpy, Lock *lock) {
	if(dpy == NULL || lock == NULL)
		return;

	XUngrabPointer(dpy, CurrentTime);
	XFreeColors(dpy, DefaultColormap(dpy, lock->screen), lock->colors, 2, 0);
	XFreePixmap(dpy, lock->pmap);
	XDestroyWindow(dpy, lock->win);

	free(lock);
}

static Lock *
lockscreen(Display *dpy, int screen) {
	char curs[] = {0, 0, 0, 0, 0, 0, 0, 0};
	unsigned int len;
	Lock *lock;
	XColor color, dummy;
	XSetWindowAttributes wa;
	Cursor invisible;

	if(dpy == NULL || screen < 0)
		return NULL;

	lock = malloc(sizeof(Lock));
	if(lock == NULL)
		return NULL;

	lock->screen = screen;

	lock->root = RootWindow(dpy, lock->screen);

	/* init */
	wa.override_redirect = 1;
	wa.background_pixel = BlackPixel(dpy, lock->screen);
	lock->win = XCreateWindow(dpy, lock->root, 0, 0, DisplayWidth(dpy, lock->screen), DisplayHeight(dpy, lock->screen),
			0, DefaultDepth(dpy, lock->screen), CopyFromParent,
			DefaultVisual(dpy, lock->screen), CWOverrideRedirect | CWBackPixel, &wa);
	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), COLOR2, &color, &dummy);
	lock->colors[1] = color.pixel;
	XAllocNamedColor(dpy, DefaultColormap(dpy, lock->screen), COLOR1, &color, &dummy);
	lock->colors[0] = color.pixel;
	lock->pmap = XCreateBitmapFromData(dpy, lock->win, curs, 8, 8);
	invisible = XCreatePixmapCursor(dpy, lock->pmap, lock->pmap, &color, &color, 0, 0);
	XDefineCursor(dpy, lock->win, invisible);
	XMapRaised(dpy, lock->win);
	for(len = 1000; len; len--) {
		if(XGrabPointer(dpy, lock->root, False, ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
					GrabModeAsync, GrabModeAsync, None, invisible, CurrentTime) == GrabSuccess)
			break;
		usleep(1000);
	}
	if(running && (len > 0)) {
		for(len = 1000; len; len--) {
			if(XGrabKeyboard(dpy, lock->root, True, GrabModeAsync, GrabModeAsync, CurrentTime)
					== GrabSuccess)
				break;
			usleep(1000);
		}
	}

	running &= (len > 0);
	if(!running) {
		unlockscreen(dpy, lock);
		lock = NULL;
	}
	else 
		XSelectInput(dpy, lock->root, SubstructureNotifyMask);

	return lock;
}

static void
usage(void) {
	fprintf(stderr, "usage: slock [-v]\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv) {
#ifndef HAVE_BSD_AUTH
	const char *pws;
#endif
	Display *dpy;
	int screen;

	if((argc == 2) && !strcmp("-v", argv[1]))
		die("slock-%s, © 2006-2012 Anselm R Garbe\n", VERSION);
	else if(argc != 1)
		usage();

	if(!getpwuid(getuid()))
		die("slock: no passwd entry for you");

#ifndef HAVE_BSD_AUTH
	pws = getpw();
#endif

	if(!(dpy = XOpenDisplay(0)))
		die("slock: cannot open display");
	/* Get the number of screens in display "dpy" and blank them all. */
	nscreens = ScreenCount(dpy);
	locks = malloc(sizeof(Lock *) * nscreens);
	if(locks == NULL)
		die("slock: malloc: %s", strerror(errno));
	int nlocks = 0;
	for(screen = 0; screen < nscreens; screen++) {
		if ( (locks[screen] = lockscreen(dpy, screen)) != NULL)
			nlocks++;
	}
	XSync(dpy, False);

	/* Did we actually manage to lock something? */
	if (nlocks == 0) { // nothing to protect
		free(locks);
		XCloseDisplay(dpy);
		return 1;
	}

	/* Everything is now blank. Now wait for the correct password. */
#ifdef HAVE_BSD_AUTH
	readpw(dpy);
#else
	readpw(dpy, pws);
#endif

	/* Password ok, unlock everything and quit. */
	for(screen = 0; screen < nscreens; screen++)
		unlockscreen(dpy, locks[screen]);

	free(locks);
	XCloseDisplay(dpy);

	return 0;
}
