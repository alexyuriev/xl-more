/*
 *  XL-more version 1.00
 *
 *  github.com/alexyuriev/xl-more
 *
 *  Derived from XL by Danny Dulai <github.com/dannydulai/xl>
 *  PAM support, state signaling and resource management by Alex Yuriev <github.com/alexyriev>
 *
 *  Copyright (c) 1997 Danni Dulai
 *  Copyright (c) 2018 Alexander O. Yuriev
 *  Copyright (c) 2002 Networks Associates Technology, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 *  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 *  OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 *  SUCH DAMAGE.
 *
 */

#include <pwd.h>
#include <security/pam_appl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xresource.h>
#include <X11/Xutil.h>

#define MAX_PASSWORD_LEN                1024

#define DEFAULT_LOCKED_IGNORE_STR       "#000000"
#define DEFAULT_LOCKED_STORE_STR        "#0000ff"

#define XRESOURCE_LOAD_STRING(NAME, DST)                  \
    XrmGetResource(db, NAME, "String", &type, &ret);      \
    if (ret.addr != NULL && !strncmp("String", type, 64)) \
        DST = ret.addr;                                   \
    else                                                  \
        DST = NULL;

#define XL_LOCK_SCREEN_COLOR(NAME)                                \
    syslog(LOG_ERR, "DEBUG: screen lock color set to %lu", NAME); \
    XSetWindowBackground( display, w, NAME );                     \
    XUnmapWindow( display, w);                                    \
    XMapWindow( display, w);


static int pam_converse(int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
    struct   pam_response *responses;
    char     *password = data;
    unsigned i;

    if (n <= 0 || n > PAM_MAX_NUM_MSG) {
        syslog(LOG_ERR, "Internal PAM error: num_msg <= 0 or great than %d", PAM_MAX_NUM_MSG);
        return (PAM_CONV_ERR);
    }
    if ((responses = calloc(n, sizeof *responses)) == NULL) {
        syslog(LOG_ERR, "Internal PAM error: out of memory");
        return (PAM_BUF_ERR);
    }
    for (i = 0; i < n; ++i) {
        responses[i].resp_retcode = 0;
        responses[i].resp = NULL;
        char *m_style = NULL;
        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF: m_style = "PAM_PROMPT_ECHO_OFF"; break;
            case PAM_PROMPT_ECHO_ON:  m_style = "PAM_PROMPT_ECHO_ON";  break;
            case PAM_ERROR_MSG:       m_style = "PAM_ERROR_MSG";       break;
            case PAM_TEXT_INFO:       m_style = "PAM_TEXT_INFO";       break;
            default: syslog(LOG_ERR, "Internal PAM error: uknown message type %d", msg[i]->msg_style);
                     goto fail;
        }

        switch (msg[i]->msg_style) {
            case PAM_PROMPT_ECHO_OFF:   if ((responses[i].resp = strdup(password)) == NULL) {
                                            goto fail;
                                        }
                                        break;
            default: syslog(LOG_ERR, "Internal error: message style %s not implemented", m_style);
                     goto fail;
        }
    }
    *resp = responses;
    return (PAM_SUCCESS);

 fail:

    for (i = 0; i < n; ++i) {
        if (responses[i].resp != NULL) {
            memset(responses[i].resp, 0, strlen(responses[i].resp));
            free(responses[i].resp);
        }
    }
    memset(responses, 0, n * sizeof *responses);
    *resp = NULL;
    return (PAM_CONV_ERR);
}

static int authenticate_using_pam (const char* service_name, const char* username, char* password)
{
    struct pam_conv pam_conversation = { pam_converse, password };
    pam_handle_t    *pamh;
    int             ret;

    ret = pam_start(service_name, username, &pam_conversation, &pamh);
    if (ret != PAM_SUCCESS) {
        syslog(LOG_ERR, "PAM Initialization failed: %s", pam_strerror(pamh, ret));
        return 1;
    }

    ret = pam_authenticate(pamh, 0);
    if (ret != PAM_SUCCESS) {
        return 1;
    }

    ret = pam_end(pamh, ret);
    if (ret != PAM_SUCCESS) {
        syslog(LOG_ERR, "PAM termination failed after successful valdation: %s", pam_strerror(pamh, ret));
        return 1;
    }

    return 0;
}

static char *return_null_or_dup(char *check) {
    if (!check || !strlen(check)) {
        return NULL;
    }
    return strdup(check);
}

static char *return_dup_if_null_or_self(char *check, char *dup)
{
    if (!check) {
        return strdup(dup);
    }
    return check;
}

static int load_resources( Display *display, char **resourceStrPAMService, char **resourceStrColorIgnore, char **resourceStrColorStore )
{
    XrmDatabase  db;                    /* Xresources database */
    XrmValue     ret;                   /* structure that holds pointer to string */
    char        *resource_manager;
    char        *type;                  /* class of returned variable */
    char        *var;                   /* pointer to the resource */

    XrmInitialize();
    if ( !(resource_manager = XResourceManagerString( display )) ) {
        return -1;
    }
    if ( !(db = XrmGetStringDatabase(resource_manager)) ) {
        return -2;
    }

    XRESOURCE_LOAD_STRING("XL-more.color.ignore", var);
    *resourceStrColorIgnore = return_null_or_dup(var);

    XRESOURCE_LOAD_STRING("XL-more.color.store", var);
    *resourceStrColorStore = return_null_or_dup(var);

    XRESOURCE_LOAD_STRING("XL-more.pam_service", var);
    *resourceStrPAMService = return_null_or_dup(var);

    return 1;
}

int main(int argc, char *argv[])
{
    int ret = 0;

    uid_t uid = getuid();

    struct passwd *pw;
    pw = getpwuid(uid);
    if ( !pw ) {
        fprintf(stderr, "Could not get a user name of active user\n");
        exit(1);
    }

    char *user_name = NULL;
    if ( !(user_name = strdup(pw->pw_name)) ) {
        fprintf(stderr, "Could not process a user name of the active user: out of memory\n");
        exit(1);
    }

    Display *display;
    if ( !(display = XOpenDisplay(NULL)) ) {
        fprintf(stderr, "Could not connect to DISPLAY\n");
        exit(1);
    }

    char *defaultStrPAMService, *defaultStrColorIgnore, *defaultStrColorStore;
    if ( (ret = load_resources( display, &defaultStrPAMService, &defaultStrColorIgnore, &defaultStrColorStore )) < 0) {
        fprintf(stderr, "Failed to process Xresouces\n");
        exit(1);
    }

    char *service_name = NULL;
    char *ptr = NULL;

    if ( !defaultStrPAMService) {
        if ( (ptr = getenv("PAM_SERVICE")) ) {
            service_name = strdup(ptr);
        }

        if ( !service_name ) {
            fprintf(stderr, "Could not get lock service name for PAM\n");
            exit(1);
        }
    } else {
        service_name = defaultStrPAMService;
    }

    char *strColorLockedIgnore = return_dup_if_null_or_self( defaultStrColorIgnore, DEFAULT_LOCKED_IGNORE_STR );
    if ( !strColorLockedIgnore) {
        fprintf(stderr, "Failed to obtain locked ignore color: Out of memory\n");
        exit(1);
    }

    char *strColorLockedStore = return_dup_if_null_or_self( defaultStrColorStore, DEFAULT_LOCKED_STORE_STR );
    if ( !strColorLockedStore) {
        fprintf(stderr, "Failed to obtain locked store color: Out of memory\n");
        exit(1);
    }

    int screen = DefaultScreen( display );

    Window root = DefaultRootWindow(display);
    if ( root < 0 ) {
        fprintf(stderr, "Failed to get a root window ID of a default screen\n");
        exit(1);
    }

    Colormap cmap = DefaultColormap( display, screen );
    Status retStatus;

    XColor  colorLockedIgnore;
    if ( !XParseColor( display, cmap, strColorLockedIgnore, &colorLockedIgnore )) {
        fprintf(stderr, "Failed to parse a locked ignore color %s\n", strColorLockedIgnore);
        exit(1);
    }
    retStatus = XAllocColor( display, cmap, &colorLockedIgnore );
    if (retStatus == BadColor) {
        fprintf(stderr, "Failed to allocate a locked ignore color %s\n", strColorLockedIgnore);
        exit(1);
    }
    unsigned long pixelColorLockedIgnore = colorLockedIgnore.pixel;

    XColor  colorLockedStore;
    if ( !XParseColor( display, cmap, strColorLockedStore, &colorLockedStore )) {
        fprintf(stderr, "Failed to parse a locked store color %s\n", strColorLockedStore);
        exit(1);
    }
    retStatus = XAllocColor ( display, cmap, &colorLockedStore );
    if (retStatus == BadColor) {
        fprintf(stderr, "Failed to allocate a locked store color %s\n", strColorLockedStore);
        exit(1);
    }
    unsigned long pixelColorLockedStore = colorLockedStore.pixel;

    XWindowAttributes xwRootWinAttr;
    XGetWindowAttributes( display, root, &xwRootWinAttr );

    Window w = XCreateSimpleWindow( display, root, 0, 0, xwRootWinAttr.width, xwRootWinAttr.height, 0, 0, pixelColorLockedIgnore );

    openlog("XL-more", LOG_PID, LOG_AUTHPRIV);
    syslog(LOG_INFO, "User `%s` locked X screen", user_name);

    static char noData[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    Pixmap emptyBitMap = XCreateBitmapFromData( display, w, noData, 8, 8);

    Cursor invisibleCursor = XCreatePixmapCursor( display, emptyBitMap, emptyBitMap, &colorLockedIgnore, &colorLockedIgnore, 0, 0);
    XDefineCursor( display, w, invisibleCursor );
    XFreeCursor( display, invisibleCursor );
    XFreePixmap( display, emptyBitMap );

    /* Attributes for the locked window */

    XSetWindowAttributes xwWindowSetAttr;
    xwWindowSetAttr.save_under        = True;
    xwWindowSetAttr.override_redirect = True;

    XChangeWindowAttributes( display, w, CWOverrideRedirect | CWSaveUnder, &xwWindowSetAttr );

//    XMapWindow( display, w);

    XL_LOCK_SCREEN_COLOR(pixelColorLockedIgnore);

    XGrabPointer(display, root, 1, ButtonPress, GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
    XGrabKeyboard(display, root, 0, GrabModeAsync, GrabModeAsync, CurrentTime);
    XSelectInput(display, root, KeyPressMask);

    char input_buffer[MAX_PASSWORD_LEN];
    char passwd_buffer[MAX_PASSWORD_LEN];
    memset( passwd_buffer, 0x00, MAX_PASSWORD_LEN );

    unsigned    current_offset = 0;
    unsigned    remember_keys = 0;
    XEvent      ev;

    while (XNextEvent(display, &ev), 1) {
        if (ev.type == KeyPress) {
            KeySym keysym;
            XComposeStatus compose;

            int i = XLookupString(&ev.xkey, input_buffer, 10, &keysym, &compose);
            if ( keysym == XK_Return ) {
                if ( !remember_keys ) {
                    // start recording key presses

                    XL_LOCK_SCREEN_COLOR( pixelColorLockedStore );

                    memset( passwd_buffer, 0x00, MAX_PASSWORD_LEN );
                    remember_keys = 1;
                    current_offset = 0;

                } else {

                    XL_LOCK_SCREEN_COLOR( pixelColorLockedIgnore );
                    remember_keys  = 0;

                    if ( current_offset && !authenticate_using_pam(service_name, user_name, passwd_buffer) ) {
                        // success - password correct

                        memset(passwd_buffer, 0x00, MAX_PASSWORD_LEN);

                        syslog(LOG_INFO, "User `%s` unlocked X screen", user_name);

                        XUngrabKeyboard(display, CurrentTime);
                        XUngrabPointer(display, CurrentTime);

                        exit(0);

                    }
                    current_offset = 0;
                    memset( passwd_buffer, 0x00, MAX_PASSWORD_LEN );
                    syslog( LOG_ERR, "User `%s` failed to unlocked X screen", user_name );
                }
            } else {

                if ( current_offset + i > MAX_PASSWORD_LEN - 1 ) {
                    // This password is too long so pretend that it failed and reset

                    XL_LOCK_SCREEN_COLOR( pixelColorLockedIgnore );
                    remember_keys = 0;
                    current_offset = 0;


                    memset( passwd_buffer, 0x00, MAX_PASSWORD_LEN );
                    syslog( LOG_ERR, "User `%s` failed to unlocked X screen", user_name );
                } else {
                    if ( remember_keys ) {
                        /* Not only key pressed, but we are in a record keypress to assemble password mode */

                        memcpy( passwd_buffer + current_offset, input_buffer, i );
                        current_offset += i;
                    }
                    memset( input_buffer, 0x00, i );
                }
            }
        }
    }
}
