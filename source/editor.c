/* ------------- editor.c ------------ */
#include "dflat.h"

/*
 * Hint: TAB_TOGGLING mode is not used in EDIT 0.6* anyway,
 * but if it is on, char values 0x89 and 0x8c have special
 * meanings. They and 0x8d and 0xa0 are called whitespace
 * in this mode! To use, hook functions to expand and
 * collapse tabs in the tab size selection and in load/save!?
 */

#ifdef TAB_TOGGLING
#define pTab ('\t' + 0x80) /* the tab itself plus a special flag */
#define sTab ('\f' + 0x80) /* space padding for the tabby looks  */
#endif

/* ---------- SETTEXT Message ------------ */
static int SetTextMsg(WINDOW wnd, char *Buf)
{
    unsigned char *tp, *ep, *ttp;
    unsigned int x = 0; /* column */
    unsigned int sz = 0; /* buffer size */
    int rtn;

	tp = Buf;
    /* --- compute the buffer size based on tabs in the text --- */
    while (*tp)
        {
        if (*tp == '\t')
            {
            /* --- tab, adjust the buffer length --- */
            int sps = cfg.Tabs - (x % cfg.Tabs); /* even valid in non-tab mode */
            sz += sps;
            x += sps;
            }
        else
            {
            /* --- not a tab, count the character --- */
            sz++;
            x++;
            }

        if (*tp == '\n')
            x = 0;    /* newline, reset x --- */

        tp++;
        } /* while */

    ep = DFcalloc(1, sz+1);             /* allocate a buffer */
    /* --- detab the input file --- */
    tp = Buf;
    ttp = ep;
    x = 0;
    while (*tp)
        {
        /* --- put the character (\t, too) into the buffer --- */
        x++;
        /* --- expand tab into subst tab (\f + 0x80)
               and expansions (\t + 0x80) --- */
        if (*tp == '\t')
            {
            if (cfg.Tabs > 1)	/* normal tab-expansion mode */
                {
#ifdef TAB_TOGGLING
                *ttp++ = sTab;  /* --- substitute tab character --- */
#else
		*ttp++ = ' ';
#endif
                while ((x % cfg.Tabs) != 0)
                    {
#ifdef TAB_TOGGLING
                    *ttp++ = pTab;
#else
		    *ttp++ = ' ';
#endif
                    x++;
                    }
                }
            else
                *ttp++ = '\t';	/* leave-tab-as-char mode -ea */
            }
	else
            {
	        *ttp++ = *tp;
        	if (*tp == '\n')
            	    x = 0;
            }

        tp++;
        } /* while *tp */

    *ttp = '\0';
    rtn = BaseWndProc(EDITOR, wnd, SETTEXT, (PARAM) ep, 0);
    free(ep);
	return rtn;
}

#ifdef TAB_TOGGLING /* currently (0.6*) unused. Interesting to know... */
void CollapseTabs(WINDOW wnd)
{
	unsigned char *cp = wnd->text;
	unsigned char *cp2;

	while (*cp)
        {
        if (*cp == pTab)	/* substituted tab -> tab again */
            {
		*cp = '\t';
		cp2 = cp;
            while (*++cp2 == sTab); /* space padding after tab -> zap */
		memmove(cp+1, cp2, strlen(cp2)+1);
            }
    	cp++;
        }

}
#endif

#ifdef TAB_TOGGLING /* currently (0.6*) unused. Interesting to know... */
void ExpandTabs(WINDOW wnd)
{
    int Holdwtop = wnd->wtop;
    int Holdwleft = wnd->wleft;
    int HoldRow = wnd->CurrLine;
    int HoldCol = wnd->CurrCol;
    int HoldwRow = wnd->WndRow;

	SendMessage(wnd, SETTEXT, (PARAM) wnd->text, 0);
	wnd->wtop = Holdwtop;
	wnd->wleft = Holdwleft;
	wnd->CurrLine = HoldRow;
	wnd->CurrCol = HoldCol;
	wnd->WndRow = HoldwRow;
	SendMessage(wnd, PAINT, 0, 0);
	SendMessage(wnd, KEYBOARD_CURSOR, 0, wnd->WndRow);
}
#endif /* TAB_TOGGLING */

/* --- When inserting or deleting, adjust next following tab, same line --- */
/* updates current TAB expansion lengths */
#ifdef TAB_TOGGLING
static void AdjustTab(WINDOW wnd)
{
    int col = wnd->CurrCol;

    while (*CurrChar && *CurrChar != '\n')
        {
        if (*CurrChar == sTab)
            {
            int exp = (cfg.Tabs - 1) - (wnd->CurrCol % cfg.Tabs);

            wnd->CurrCol++;
            while (*CurrChar == pTab)
                BaseWndProc(EDITOR, wnd, KEYBOARD, DEL, 0);

            while (exp--)
                BaseWndProc(EDITOR, wnd, KEYBOARD, pTab, 0);

            break;
            }
        wnd->CurrCol++;
        }
    wnd->CurrCol = col;
}
#endif

static void TurnOffDisplay(WINDOW wnd)
{
	SendMessage(NULL, HIDE_CURSOR, 0, 0);
    ClearVisible(wnd);
}

static void TurnOnDisplay(WINDOW wnd)
{
    SetVisible(wnd);
	SendMessage(NULL, SHOW_CURSOR, 0, 0);
}

static void RepaintLine(WINDOW wnd)
{
	SendMessage(wnd, KEYBOARD_CURSOR, WndCol, wnd->WndRow);
	WriteTextLine(wnd, NULL, wnd->CurrLine, FALSE);
}

/* --------- KEYBOARD Message ---------- */
static int KeyboardMsg(WINDOW wnd, PARAM p1, PARAM p2)
{
    int c = (int) p1;
	BOOL delnl;
#ifdef TAB_TOGGLING
	PARAM pn = p1;
#endif
    if (WindowMoving || WindowSizing || ((int)p2 & ALTKEY))
        return FALSE;
    switch (c)    {
		case PGUP:
		case PGDN:
		case UP:
		case DN:
#ifdef TAB_TOGGLING
			pn = (PARAM) BS; /* !?!? */
#endif
#ifdef HOOKKEYB
		case FWD: /* right arrow */
#else
		case LARROW: /* hope this makes sense */
		case RARROW: /* formerly called FWD */
#endif
		case BS:
		    TurnOffDisplay(wnd);
		    BaseWndProc(EDITOR, wnd, KEYBOARD, p1, p2);
#ifdef TAB_TOGGLING
		    while (*CurrChar == pTab) /* zap adjacent tabby-spaces */
			BaseWndProc(EDITOR, wnd, KEYBOARD, pn, p2);
#endif
		    TurnOnDisplay(wnd);
		    return TRUE;
		case DEL:
		    TurnOffDisplay(wnd);
		    delnl = *CurrChar == '\n' || TextBlockMarked(wnd);
		    BaseWndProc(EDITOR, wnd, KEYBOARD, p1, p2);
#ifdef TAB_TOGGLING
		    while (*CurrChar == pTab) /* zap adjacent tabby-spaces */
			BaseWndProc(EDITOR, wnd, KEYBOARD, p1, p2);
		    AdjustTab(wnd);
#endif
		    TurnOnDisplay(wnd);
		    RepaintLine(wnd);
		    if (delnl)
			SendMessage(wnd, PAINT, 0, 0);
		    return TRUE;
		case '\t': /* type tab as tab-substitute plus tabby-space-substitutes */
		    if (cfg.Tabs > 1) /* unless in tab-type through mode -ea */
		        {
		        TurnOffDisplay(wnd);
#ifdef TAB_TOGGLING
		        BaseWndProc(EDITOR, wnd, KEYBOARD, (PARAM) sTab, p2);
		        while ((wnd->CurrCol % cfg.Tabs) != 0)
			    BaseWndProc(EDITOR, wnd, KEYBOARD, pTab, p2);
#else
		        BaseWndProc(EDITOR, wnd, KEYBOARD, (PARAM) ' ', p2);
		        while ((wnd->CurrCol % cfg.Tabs) != 0)
			    BaseWndProc(EDITOR, wnd, KEYBOARD, ' ', p2);
#endif
		        TurnOnDisplay(wnd);
		        RepaintLine(wnd);
		        return TRUE;
		        } /* else fall through to default! */
		default: /* simply a typed char */
		    if ( (c < 256) /* *** && ( isprint(c) || c == '\r' ) *** */
		       )
		    {
			TurnOffDisplay(wnd);
			BaseWndProc(EDITOR, wnd, KEYBOARD, p1, p2);
#ifdef TAB_TOGGLING
			AdjustTab(wnd);
#endif
			TurnOnDisplay(wnd);
			RepaintLine(wnd);
			if (c == '\r')
			   SendMessage(wnd, PAINT, 0, 0);
			return TRUE;
		    }
		    break;
	}
    return FALSE;
}

/* ------- Window processing module for EDITBOX class ------ */
int EditorProc(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    switch (msg)    {
		case KEYBOARD:
            if (KeyboardMsg(wnd, p1, p2))
                return TRUE;
            break;
		case SETTEXT:
			return SetTextMsg(wnd, (char *) p1);
        default:
            break;
    }
    return BaseWndProc(EDITOR, wnd, msg, p1, p2);
}

