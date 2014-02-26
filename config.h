
#ifndef DEF_CONFIG_H
#define DEF_CONFIG_H

/* The program version. */
#define VERSION 0.1

/* Use shadow header. */
#define HAVE_SHADOW_H 1

/* Colors : COLOR1 is the default color and COLOR2 the active color. */
#define COLOR1 "black"
#define COLOR2 "#006600"

/* The font used when typing the message. */
#define FONT "-*-lucida-bold-r-*-*-20-140-*-*-*-*-iso8859-15"

/* Fonctions (undef to disable). */
#define MESSAGE /* Allow to let message. */
#define SCRIPT  /* Execute a script when a wrong password is typed. */

/* Message option. */
#ifdef MESSAGE
#define MSGVALUE "Press ctrl + space to let a message"
#endif

/* Script options. */
#ifdef SCRIPT
#define SCRIPTPATH "Prog/error.sh"
#endif

#endif

