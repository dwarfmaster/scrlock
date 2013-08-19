
#ifndef DEF_CONFIG_H
#define DEF_CONFIG_H

// the program version
#define VERSION 0.1

// Use shadow header
#define HAVE_SHADOW_H 1

// Colors
#define COLOR1 "black"
#define COLOR2 "#006600"

// Font
#define FONT "-*-lucida-bold-r-*-*-20-140-*-*-*-*-iso8859-15"

// Fonctions (undef to disable)
#define MESSAGE // Allow to let message
#define SCRIPT // Execute a script when a wrong password is typed
#define USEDPMS // Shutdown the screen with DPMS when not typing message nor password

// Message option
#ifdef MESSAGE
#define MSGVALUE "Press ctrl + space to let a message"
#endif

// Spy options
#ifdef SCRIPT
#define SCRIPTPATH "Prog/error.sh"
#endif

// DPMS options
#ifdef USEDPMS
#define DPMSTIMEOUT 1 // Timeout in seconds
#endif

#endif

