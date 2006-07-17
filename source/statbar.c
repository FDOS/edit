/*  Status Bar

    Part of the FreeDOS Editor

*/

#include "dflat.h"

extern char time_string[];

int StatusBarProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    char *statusbar;

    switch (msg)
        {
        case CREATE_WINDOW:
            SendMessage(wnd, CAPTURE_CLOCK, 0, 0);
            break;
        case KEYBOARD:
            if ((int)p1 == CTRL_F4)
                return TRUE;

            break;
        case PAINT: 
            if (!isVisible(wnd))
                break;

            statusbar = DFcalloc(1, WindowWidth(wnd)+1);
            memset(statusbar, ' ', WindowWidth(wnd));
            *(statusbar+WindowWidth(wnd)) = '\0';
            strncpy(statusbar+1, "F1=Help Ý", 9); /* 9 */
            if (wnd->text)
                {
                int len = min(strlen(wnd->text),
                    WindowWidth(wnd)-(strlen(time_string)+9+1+1+1) ); /* 9 */
                    /* if len < strlen(wnd->text), we cannot display all the text */

                if (len > 3) /* space left for min. 3 char more than time */
                             /* and "F1=Help " and 2 block graphics chars */
                    {
                    int off=(WindowWidth(wnd)-
                      (strlen(time_string)+len+1+1) );
                      /* right-aligned, next to time display */
                    if (len < strlen(wnd->text))
                        {
                        strncpy(statusbar+off, wnd->text, len-3);
                        strncpy(statusbar+off+len-3, "...", 3);
                        }
                    else
                        strncpy(statusbar+off, wnd->text, len);
                    } /* could display anything */

                }

            strncpy(statusbar+WindowWidth(wnd)-(strlen(time_string)+2),
                "³ ", 2);
            if (wnd->TimePosted)
                *(statusbar+WindowWidth(wnd)-strlen(time_string)) = '\0';
            else
                strncpy(statusbar+WindowWidth(wnd)-strlen(time_string),
                    time_string, strlen(time_string));

            SetStandardColor(wnd);
            PutWindowLine(wnd, statusbar, 0, 0);
            free(statusbar);
            return TRUE;
        case BORDER:
            return TRUE;
        case CLOCKTICK:
            SetStandardColor(wnd);
            PutWindowLine(wnd, (char *)p1, WindowWidth(wnd)-strlen(time_string), 0);
            wnd->TimePosted = TRUE;
            SendMessage(wnd->PrevClock, msg, p1, p2);
            return TRUE;
        case CLOSE_WINDOW:
            SendMessage(wnd, RELEASE_CLOCK, 0, 0);
            break;
        default:
            break;
        }

    return BaseWndProc(STATUSBAR, wnd, msg, p1, p2);

}
