
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
#define SPY // Capture a picture with fswebcam when wrong password is typed (need fswebcam)

// Message option
#ifdef MESSAGE
#define MSGVALUE "Press ctrl + space to let a message"
#endif

// Spy options
#ifdef SPY
#define SPY_SUBDIR "spy/" // Used path will be $HOME/spy (will not be created so must exist)
#define FSWEBCAM "/usr/bin/fswebcam" // The complete path to use with
#endif

#endif

