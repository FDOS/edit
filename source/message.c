/* --------- message.c ---------- */

#include "dflat.h"

#define BLINKING_CLOCK 0	/* set to 1 to make the clock blink */
		/* blinking is off by default since 0.7c */
#define USECBRKHNLDR 1 /* removing this line disables ctrl-break processing */
/* #define OLDCRITERR 1 */
		/* ^-- remove to try out new harderr() crit error handler */
/* other defines: HOOKTIMER (use obsolete int 8 handler) and of course */
/* HOOKKEYB (use int 9 handler to allow ctrl/alt/shift - ins/del/bs)   */

static int px = -1, py = -1;
static int pmx = -1, pmy = -1;
static int mx, my;
static int handshaking = 0;
static volatile BOOL CriticalError;
BOOL AllocTesting = FALSE;
jmp_buf AllocError;
BOOL AltDown = FALSE;

/* ---------- event queue ---------- */
static struct events    {
    MESSAGE event;
    int mx;
    int my;
} EventQueue[MAXMESSAGES];

/* ---------- message queue --------- */
static struct msgs {
    WINDOW wnd;
    MESSAGE msg;
    PARAM p1;
    PARAM p2;
} MsgQueue[MAXMESSAGES];

static int EventQueueOnCtr;
static int EventQueueOffCtr;
static int EventQueueCtr;

static int MsgQueueOnCtr;
static int MsgQueueOffCtr;
static int MsgQueueCtr;

static int lagdelay = FIRSTDELAY;

#ifdef HOOKTIMER
static void (interrupt far *oldtimer)(void);
#endif

#ifdef HOOKKEYB
static void (interrupt far *oldkeyboard)(void);
#endif

#ifdef HOOKKEYB
static int keyportvalue;	/* for watching for key release */
#endif

WINDOW CaptureMouse;
WINDOW CaptureKeyboard;
static BOOL NoChildCaptureMouse;
static BOOL NoChildCaptureKeyboard;

#ifdef HOOKTIMER /* if IRQ used, timer variables are really used */
static int doubletimer = -1;
static int delaytimer  = -1;
static int clocktimer  = -1;
#else 		/* else, variables only contain an array index! */

static int doubletimer = 0;
static int delaytimer  = 1;
static int clocktimer  = 2;
static char timerused[3] = {0, 0, 0}; /* 0 means off, 2 timed out */
			/* 1 counting */
static long unsigned int timerend[3] = {0, 0, 0};
static long unsigned int timerstart[3] = {0, 0, 0};
volatile long unsigned int *biostimer = MK_FP(0x40,0x6c);
			/* just using BIOS DATA "timer tick count" */

#endif

char time_string[] = "           "; /* max. length "12:34:56pm "; */

static WINDOW Cwnd;

#ifdef HOOKKEYB
static void interrupt far newkeyboard(void)
{
	keyportvalue = inp(KEYBOARDPORT);
	oldkeyboard();
}
#endif

/* ------- timer interrupt service routine ------- */
#ifdef HOOKTIMER /* now replaced by non-IRQ version normally -ea */
static void interrupt far newtimer(void)
{
    if (timer_running(doubletimer))
        countdown(doubletimer);
    if (timer_running(delaytimer))
        countdown(delaytimer);
    if (timer_running(clocktimer))
        countdown(clocktimer);
    oldtimer();
}
#else

/* More complex countdown handling by Eric Auer */
/* Allows us to work without hooking intr. 0x08 */

int timed_out(int timer)		/* was: countdown 0? */
{
    if ((timer > 2) || (timer < 0))
        return -1;			/* invalid -> always elapsed */
    if (timerused[timer] == 0)		/* not active at all? */
        return 0;
    if (timerused[timer] == 2)		/* timeout already known? */
        return 1;
    if (  (biostimer[0] < timerstart[timer])  /* wrapped? */
       || (biostimer[0] >= timerend[timer]) ) /* elapsed? */
    {
    	/* could me more exact here - the "elapsed if wrapped"  */
    	/* logics gives early timeout at midnight. On the other */
    	/* hand, we do not know the ticks-per-day BIOS limit... */
        timerused[timer] = 2;		/* countdown elapsed */
        return 1;
    }
    return 0;				/* still waiting */
}

int timer_running(int timer)		/* was: countdown > 0? */
{
    if ((timer > 2) || (timer < 0))
        return 0;			/* invalid -> never running */
    if (timerused[timer] == 1)		/* running? */
    {
        return (1 - timed_out(timer));	/* if not elapsed, running */
    }
    else return 0;			/* certainly not running */
}

int timer_disabled(int timer)		/* was: countdown -1? */
{
    if ((timer > 2) || (timer < 0))
        return 1;			/* invalid -> always disabled */
    return (timerused[timer] == 0);
}

void disable_timer(int timer)		/* was: countdown = -1 */
{
    if ((timer > 2) || (timer < 0))
        return;
    timerused[timer] = 0;
}

void set_timer(int timer, int secs)
{
    if ((timer > 2) || (timer < 0))
        return;
    timerstart[timer] = biostimer[0];
    timerend[timer] = timerstart[timer] + (secs*182UL/10) + 1;
    timerused[timer] = 1;	/* mark as running */
}

void set_timer_ticks(int timer, int ticks)
{
    if ((timer > 2) || (timer < 0))
        return;
    timerstart[timer] = biostimer[0];
    timerend[timer] = timerstart[timer] + ticks;
    timerused[timer] = 1;	/* mark as running */
}

#endif

#ifdef OLDCRITERR
static char ermsg[] = "Error accessing drive x:";
#else
static char ermsg[] = "Error accessing drive";
#endif

/* -------- test for critical errors --------- */
int TestCriticalError(void)
{
    int rtn = 0;
    if (CriticalError)    {
    	beep();
        rtn = 1;
        CriticalError = FALSE;
        if (TestErrorMessage(ermsg) == FALSE)
            rtn = 2;
    }
    return rtn;
}

/* ------ critical error interrupt service routine ------ */
#ifdef OLDCRITERR  /* new style would be using harderr() */

/* on call, AH = type / flags, AL = drive number if AH bit 7 clear */
/* flags: AH bit 5 "ignore allowed", 4 "retry allowed", 3 "fail.." */
/* if you attempt blocked reactions, DOS selects next allowed one. */
/* AH bit 2..1: 0=DOS, 1=FAT, 2=ROOT 3=DATA. AH bit 0: write.      */
/* not used here: BP:SI device header, DI low byte error code if   */
/* AH bit 7 set. Also has special stack contents...                */
/* returns action code in AL: 0 ignore 1 retry 2 abort (3 fail)    */

static void interrupt far newcrit(IREGS ir);

static void interrupt far newcrit(IREGS ir)
{
    if (!(ir.ax & 0x8000))     { /* if any drive affected... */
        ermsg[sizeof(ermsg) - 2] =
           (ir.ax & 0xff) + 'A'; /* ... patch drive letter into message */
        CriticalError = TRUE;    /* ... only then we have a crit. error */
    }
    ir.ax = 0;
}

#else

int crit_error(void);

/* ----- critical error handler ----- */
/* HINT: Whoever tests CriticalError should display errors as */
/* (errno < sys_nerr) ? sys_errlist[errno] : "Unknown error"  */
int crit_error(void)
{
    CriticalError = TRUE;
    hardretn(-1);		/* return an error!    */
    return 2;			/* is this correct???  */

    /* some possibilities: hardresume(1) is retry,     */
    /* hardresume(2) is abort. Or return to the caller */
    /* with a fake function result: hardretn(result)   */
}

#endif

void at_exit(void);
static void StopMsg(void);

/* ----- extra safety handler - avoid dangling int.vect. ----- */
void at_exit(void)
{
#ifdef HOOKKEYB
  if (oldkeyboard != NULL) {
      printf("int " KEYBOARDVECT " restored\n");
          /* "Extra StopMsg() - dangling handlers found!\n" */
      StopMsg();
   }
#endif
#ifdef HOOKTIMER
  if (oldtimer != NULL) {
      printf("int " TIMER " restored\n");
           /* "Extra StopMsg() - dangling handlers found!\n" */
      StopMsg();
   }
#endif
}

static void StopMsg(void)
{
#ifdef HOOKTIMER
    if (oldtimer != NULL)    {
        setvect(TIMER, oldtimer);
        oldtimer = NULL;
    }
#endif
#ifdef HOOKKEYB
    if (oldkeyboard != NULL)    {
        setvect(KEYBOARDVECT, oldkeyboard);
        oldkeyboard = NULL;
    }
#endif
	ClearClipboard();
	ClearDialogBoxes();
	restorecursor();
	unhidecursor();
    hide_mousecursor();
}

#ifdef USECBRKHNLDR
/* ------ control break handler --------- */
#define ABORT		0
#define CONTINUE	1	/* any non-zero # will continue */
int c_break(void)
{
    /*    PostMessage(NULL, STOP, 0, 0);  */ /*  StopMsg();  */
#if 0 /* *** does not work! inside this handler we cannot fully use YesNoBox *** */
      /* *** ... CLOSE_WINDOW asks for confirm if unsaved changes anyway ... *** */
    if (YesNoBox("Control-Break pressed, close current (inFocus) window?"))
#endif
#if 0
    /* if (peekb(0x40, 0x71) & 0x80) ... alas already cleared at that point */
    /* ... intended use:  ... only if really Ctrl-Break, not on Ctrl-C ...  */
        PostMessage(inFocus, CLOSE_WINDOW, 0, 0);
#endif
    return CONTINUE;	/* JUST IGNORE CTRL BREAK ... */
}
#endif /* USECBRKHNLDR */

/* ------------ initialize the message system --------- */
BOOL init_messages(void)
{
	AllocTesting = TRUE;
	if (setjmp(AllocError) != 0)	{
		StopMsg();
		return FALSE;
	}
    resetmouse();
	set_mousetravel(0, SCREENWIDTH-1, 0, SCREENHEIGHT-1);
	savecursor();
	hidecursor();
    px = py = -1;
    pmx = pmy = -1;
    mx = my = 0;
    CaptureMouse = CaptureKeyboard = NULL;
    NoChildCaptureMouse = FALSE;
    NoChildCaptureKeyboard = FALSE;
    MsgQueueOnCtr = MsgQueueOffCtr = MsgQueueCtr = 0;
    EventQueueOnCtr = EventQueueOffCtr = EventQueueCtr = 0;

#ifdef HOOKTIMER
    if (oldtimer == NULL)    {
        oldtimer = getvect(TIMER);
        setvect(TIMER, newtimer);
    }
#endif
#ifdef HOOKKEYB
    if (oldkeyboard == NULL)    {
        oldkeyboard = getvect(KEYBOARDVECT);
        setvect(KEYBOARDVECT, newkeyboard);
    }
#endif

#ifdef OLDCRITERR		/* old style: hook intr 0x24 manually    */
    setvect(CRIT, newcrit);	/* (vector save/restore not needed here) */
#else
    harderr(crit_error);	/* set critical error handler (dos.h)  */
    				/* handler uses hardretn / hardresume  */
#endif

#ifdef USECBRKHNLDR
    ctrlbrk(c_break);		/* set ctrl break handler (dos.h) */
    				/* handler returns 0 to abort program */
    setcbrk(0);			/* 1 = all / 0 = only con calls check */
#endif

    atexit(at_exit); /* do not allow exit without restoring vectors! */

    PostMessage(NULL,START,0,0);
    lagdelay = FIRSTDELAY;
	return TRUE;
}

/* ----- post an event and parameters to event queue ---- */
static void PostEvent(MESSAGE event, int p1, int p2)
{
    if (EventQueueCtr != MAXMESSAGES)    {
        EventQueue[EventQueueOnCtr].event = event;
        EventQueue[EventQueueOnCtr].mx = p1;
        EventQueue[EventQueueOnCtr].my = p2;
        if (++EventQueueOnCtr == MAXMESSAGES)
            EventQueueOnCtr = 0;
        EventQueueCtr++;
    }
}

/* ------ collect mouse, clock, and keyboard events ----- */
static void near collect_events(void)
{
    static int ShiftKeys = 0;
	int sk;
    struct tm *now;
#if BLINKING_CLOCK
    static BOOL flipflop = FALSE;
#endif
    int hr;

    /* -------- test for a clock event (one/second) ------- */
    if (timed_out(clocktimer))    {
        struct COUNTRY thiscountry; /* country support new 0.7a */
        time_t t = time(NULL);	/* the current time */
        char timesep = ':';	/* default 12:12:12am separator */
        int ampmflag = 1;
        if ( country(0 /* current */, &thiscountry) ) { /* dos.h (int 21.38) */
          timesep = thiscountry.co_tmsep[0];
          ampmflag = (thiscountry.co_time == 0); /* 0 ampm 1 24h clock */
          /* co_date: 0 USA mm dd yy 1 EU dd mm yy 2 JP yy mm dd */
        }
#if BLINKING_CLOCK
        /* ------- blink the ':'s at one-second intervals ----- */
        flipflop = (!flipflop);
        if (flipflop) timesep = (timesep == ':') ? '.' : ' ';
            /* new in EDIT 0.7b: toggle ':' / '.' (or [other] / ' ') */
            /* which looks less 'nervous' than ':' / space toggling. */
#endif
        /* ----- get the current time ----- */
        now = localtime(&t);
        hr = now->tm_hour;
        if (ampmflag && (hr > 12)) hr -= 12;
        if (ampmflag && (hr == 0)) hr  = 12;
        if (ampmflag) {
          sprintf(time_string, "%2d%c%02d%c%02d%s", hr, timesep, now->tm_min,
            timesep, now->tm_sec,
            ((now->tm_hour > 11) ? "pm " : "am ") );
        } else
          sprintf(time_string, "%2d%c%02d%c%02d", hr, timesep, now->tm_min,
            timesep, now->tm_sec);
            /* min / sec use 02 digits, only 0.6 had 2 digits ' ' padded hours */
        /* -------- reset the timer -------- */
        set_timer(clocktimer, 1);
        /* -------- post the clock event -------- */
        PostEvent(CLOCKTICK, FP_SEG(time_string), FP_OFF(time_string));
    }

    /* --------- keyboard events ---------- */
    if ((sk = getshift()) != ShiftKeys)    {
        ShiftKeys = sk;
        /* ---- the shift status changed ---- */
        PostEvent(SHIFT_CHANGED, sk, 0);
    	if (sk & ALTKEY)
			AltDown = TRUE;
    }

    /* ----- build keyboard events for key combinations that BIOS -- *
     * -- does not report (only possible with HOOKKEYB!) ----------- *
     * -- UPDATE EDIT 0.7: BIOS functions int 16.1x DO support... -- *
     * -- ... so console.c now has direct BIOS support for those. -- */
#ifdef HOOKKEYB
    if (sk & ALTKEY)	{
        if (keyportvalue == 14)    {
			AltDown = FALSE;
			waitforkeyboard();
            PostEvent(KEYBOARD, ALT_BS, sk); /* ALT backspace */
        }
        if (keyportvalue == 83)    {
			AltDown = FALSE;
			waitforkeyboard();
            PostEvent(KEYBOARD, ALT_DEL, sk); /* ALT delete */
        }
	} /* ALTKEY */
    if (sk & CTRLKEY)	{
		AltDown = FALSE;
        if (keyportvalue == 82)    {
			waitforkeyboard();
            PostEvent(KEYBOARD, CTRL_INS, sk); /* CTRL insert */
        }
	} /* CTRLKEY */
#endif
    /* ----------- test for keystroke ------- */
    if (keyhit())    {
#ifdef HOOKKEYB
        static int cvt[] = {CTRL_V,END,DN,PGDN,BS,'5',
                        FWD,HOME,UP,PGUP};
#endif
        int c = getkey(); /* ASCII, or (FKEY | scancode) */

		AltDown = FALSE;
#ifdef HOOKKEYB
        /* -------- convert numeric pad keys ------- */
        if (sk & (LEFTSHIFT | RIGHTSHIFT))    {
            if (c >= '0' && c <= '9')
                c = cvt[c-'0'];
            else if (c == '.' || c == DEL)
                c = CTRL_X;
            else if (c == INS)
                c = CTRL_V;
        }
#if 0
        if (c != '\r' && (c < ' ' || c > 127))
	    clearBIOSbuffer(); /* nah, why so picky? */
#endif
#endif
        /* ------ post the keyboard event ------ */
        PostEvent(KEYBOARD, c, sk);
    }

    /* ------------ test for mouse events --------- */
    if (button_releases())    {
        /* ------- the button was released -------- */
		AltDown = FALSE;
        set_timer_ticks(doubletimer, DOUBLETICKS); /* *** */
        PostEvent(BUTTON_RELEASED, mx, my);
        disable_timer(delaytimer);
    }
    get_mouseposition(&mx, &my);
    if (mx != px || my != py)  {
        px = mx;
        py = my;
        PostEvent(MOUSE_MOVED, mx, my);
    }
    if (rightbutton())	{
		AltDown = FALSE;
        PostEvent(RIGHT_BUTTON, mx, my);
	}
    if (leftbutton())    {
		AltDown = FALSE;
        if (mx == pmx && my == pmy)    {
            /* ---- same position as last left button ---- */
            if (timer_running(doubletimer))    {
                /* -- second click before double timeout -- */
                disable_timer(doubletimer);
                PostEvent(DOUBLE_CLICK, mx, my);
            }
            else if (!timer_running(delaytimer))    {
                /* ---- button held down a while ---- */
                set_timer_ticks(delaytimer, lagdelay); /* *** */
                lagdelay = DELAYTICKS;
                /* ---- post a typematic-like button ---- */
                PostEvent(LEFT_BUTTON, mx, my);
            }
        }
        else    {
            /* --------- new button press ------- */
            disable_timer(doubletimer);
            set_timer_ticks(delaytimer, FIRSTDELAY); /* *** */
            lagdelay = DELAYTICKS;
            PostEvent(LEFT_BUTTON, mx, my);
            pmx = mx;
            pmy = my;
        }
    }
    else
        lagdelay = FIRSTDELAY;
}

/* ----- post a message and parameters to msg queue ---- */
void PostMessage(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    if (MsgQueueCtr != MAXMESSAGES)    {
        MsgQueue[MsgQueueOnCtr].wnd = wnd;
        MsgQueue[MsgQueueOnCtr].msg = msg;
        MsgQueue[MsgQueueOnCtr].p1 = p1;
        MsgQueue[MsgQueueOnCtr].p2 = p2;
        if (++MsgQueueOnCtr == MAXMESSAGES)
            MsgQueueOnCtr = 0;
        MsgQueueCtr++;
    }
}

/* --------- send a message to a window ----------- */
int SendMessage(WINDOW wnd, MESSAGE msg, PARAM p1, PARAM p2)
{
    int rtn = TRUE, x, y;

#ifdef INCLUDE_LOGGING
	LogMessages(wnd, msg, p1, p2);
#endif
    if (wnd != NULL)
        switch (msg)    {
            case PAINT:
            case BORDER:
                /* ------- don't send these messages unless the
                    window is visible -------- */
                if (isVisible(wnd))
	                rtn = (*wnd->wndproc)(wnd, msg, p1, p2);
                break;
            case RIGHT_BUTTON:
            case LEFT_BUTTON:
            case DOUBLE_CLICK:
            case BUTTON_RELEASED:
                /* --- don't send these messages unless the
                    window is visible or has captured the mouse -- */
                if (isVisible(wnd) || wnd == CaptureMouse)
	                rtn = (*wnd->wndproc)(wnd, msg, p1, p2);
                break;
            case KEYBOARD:
            case SHIFT_CHANGED:
                /* ------- don't send these messages unless the
                    window is visible or has captured the keyboard -- */
                if (!(isVisible(wnd) || wnd == CaptureKeyboard))
	                break;
            default:
                rtn = (*wnd->wndproc)(wnd, msg, p1, p2);
                break;
        }
    /* ----- window processor returned true or the message was sent
        to no window at all (NULL) ----- */
    if (rtn != FALSE)    {
        /* --------- process messages that a window sends to the
            system itself ---------- */
        switch (msg)    {
            case STOP:
				StopMsg();
                break;
            /* ------- clock messages --------- */
            case CAPTURE_CLOCK:
				if (Cwnd == NULL)
	                set_timer(clocktimer, 0);
				wnd->PrevClock = Cwnd;
                Cwnd = wnd;
                break;
            case RELEASE_CLOCK:
                Cwnd = wnd->PrevClock;
				if (Cwnd == NULL)
	                disable_timer(clocktimer);
                break;
            /* -------- keyboard messages ------- */
            case KEYBOARD_CURSOR:
                if (wnd == NULL)
                    cursor((int)p1, (int)p2);
                else if (wnd == inFocus)
                    cursor(GetClientLeft(wnd)+(int)p1,
                                GetClientTop(wnd)+(int)p2);
                break;
            case CAPTURE_KEYBOARD:
                if (p2)
                    ((WINDOW)p2)->PrevKeyboard=CaptureKeyboard;
                else
                    wnd->PrevKeyboard = CaptureKeyboard;
                CaptureKeyboard = wnd;
                NoChildCaptureKeyboard = (int)p1;
                break;
            case RELEASE_KEYBOARD:
				if (wnd != NULL)	{
					if (CaptureKeyboard == wnd || (int)p1)
	                	CaptureKeyboard = wnd->PrevKeyboard;
					else	{
						WINDOW twnd = CaptureKeyboard;
						while (twnd != NULL)	{
							if (twnd->PrevKeyboard == wnd)	{
								twnd->PrevKeyboard = wnd->PrevKeyboard;
								break;
							}
							twnd = twnd->PrevKeyboard;
						}
						if (twnd == NULL)
							CaptureKeyboard = NULL;
					}
                	wnd->PrevKeyboard = NULL;
				}
				else
					CaptureKeyboard = NULL;
                NoChildCaptureKeyboard = FALSE;
                break;
            case CURRENT_KEYBOARD_CURSOR:
                curr_cursor(&x, &y);
                *(int*)p1 = x;
                *(int*)p2 = y;
                break;
            case SAVE_CURSOR:
                savecursor();
                break;
            case RESTORE_CURSOR:
                restorecursor();
                break;
            case HIDE_CURSOR:
                normalcursor();
                hidecursor();
                break;
            case SHOW_CURSOR:
                if (p1)
                    set_cursor_type(0x0607);
                else
                    set_cursor_type(0x0106);

                unhidecursor();
                break;
			case WAITKEYBOARD:
				waitforkeyboard();
				break;
            /* -------- mouse messages -------- */
			case RESET_MOUSE:
				resetmouse();
				set_mousetravel(0, SCREENWIDTH-1, 0, SCREENHEIGHT-1);
				break;
            case MOUSE_INSTALLED:
                rtn = mouse_installed();
                break;
			case MOUSE_TRAVEL:	{
				RECT rc;
				if (!p1)	{
        			rc.lf = rc.tp = 0;
        			rc.rt = SCREENWIDTH-1;
        			rc.bt = SCREENHEIGHT-1;
				}
				else 
					rc = *(RECT *)p1;
				set_mousetravel(rc.lf, rc.rt, rc.tp, rc.bt);
				break;
			}
            case SHOW_MOUSE:
                show_mousecursor();
                break;
            case HIDE_MOUSE:
                hide_mousecursor();
                break;
            case MOUSE_CURSOR:
                set_mouseposition((int)p1, (int)p2);
                break;
            case CURRENT_MOUSE_CURSOR:
                get_mouseposition((int*)p1,(int*)p2);
                break;
            case WAITMOUSE:
                waitformouse();
                break;
            case TESTMOUSE:
                rtn = mousebuttons();
                break;
            case CAPTURE_MOUSE:
                if (p2)
                    ((WINDOW)p2)->PrevMouse = CaptureMouse;
                else
                    wnd->PrevMouse = CaptureMouse;
                CaptureMouse = wnd;
                NoChildCaptureMouse = (int)p1;
                break;
            case RELEASE_MOUSE:
				if (wnd != NULL)	{
					if (CaptureMouse == wnd || (int)p1)
	                	CaptureMouse = wnd->PrevMouse;
					else	{
						WINDOW twnd = CaptureMouse;
						while (twnd != NULL)	{
							if (twnd->PrevMouse == wnd)	{
								twnd->PrevMouse = wnd->PrevMouse;
								break;
							}
							twnd = twnd->PrevMouse;
						}
						if (twnd == NULL)
							CaptureMouse = NULL;
					}
                	wnd->PrevMouse = NULL;
				}
				else
					CaptureMouse = NULL;
                NoChildCaptureMouse = FALSE;
                break;
            default:
                break;
        }
    }
    return rtn;
}

static RECT VisibleRect(WINDOW wnd)
{
	RECT rc = WindowRect(wnd);
	if (!TestAttribute(wnd, NOCLIP))	{
		WINDOW pwnd = GetParent(wnd);
		RECT prc;
		prc = ClientRect(pwnd);
		while (pwnd != NULL)	{
			if (TestAttribute(pwnd, NOCLIP))
				break;
			rc = subRectangle(rc, prc);
			if (!ValidRect(rc))
				break;
			if ((pwnd = GetParent(pwnd)) != NULL)
				prc = ClientRect(pwnd);
		}
	}
	return rc;
}

/* ----- find window that mouse coordinates are in --- */
static WINDOW inWindow(WINDOW wnd, int x, int y)
{
	WINDOW Hit = NULL;
	while (wnd != NULL)	{
		if (isVisible(wnd))	{
			WINDOW wnd1;
			RECT rc = VisibleRect(wnd);
			if (InsideRect(x, y, rc))
				Hit = wnd;
			if ((wnd1 = inWindow(LastWindow(wnd), x, y)) != NULL)
				Hit = wnd1;
			if (Hit != NULL)
				break;
		}
		wnd = PrevWindow(wnd);
	}
	return Hit;
}

static WINDOW MouseWindow(int x, int y)
{
    /* ------ get the window in which a
                    mouse event occurred ------ */
    WINDOW Mwnd = inWindow(ApplicationWindow, x, y);
    /* ---- process mouse captures ----- */
    if (CaptureMouse != NULL)	{
        if (NoChildCaptureMouse ||
				Mwnd == NULL 	||
					!isAncestor(Mwnd, CaptureMouse))
            Mwnd = CaptureMouse;
	}
	return Mwnd;
}

void handshake(void)
{
	handshaking++;
	dispatch_message();
	--handshaking;
}

/* ---- dispatch messages to the message proc function ---- */
BOOL dispatch_message(void)
{
    WINDOW Mwnd, Kwnd;
    /* -------- collect mouse and keyboard events ------- */
    collect_events();

    /* only message.c can fill the event queue, but all components */
    /* can fill the message queue. Events come from user or clock. */
    if ( (EventQueueCtr == 0) && (MsgQueueCtr == 0) &&
        (handshaking == 0) ) {	/* BORED - new 0.7c */
        union REGS r;
#if 0				/* int 2f is often quite crowded */
        r.x.ax = 0x1680;	/* release multitasker timeslice */
        int86(0x2f, &r, &r);	/* multiplexer call */
#else
        r.h.ah = 0x84;		/* "network" idle call */
        int86(0x2a, &r, &r);	/* network interfaces */
#endif
    }

    /* --------- dequeue and process events -------- */
    while (EventQueueCtr > 0)  {
        struct events ev;
			
		ev = EventQueue[EventQueueOffCtr];
        if (++EventQueueOffCtr == MAXMESSAGES)
            EventQueueOffCtr = 0;
        --EventQueueCtr;

        /* ------ get the window in which a
                        keyboard event occurred ------ */
        Kwnd = inFocus;

        /* ---- process keyboard captures ----- */
        if (CaptureKeyboard != NULL)
            if (Kwnd == NULL ||
                    NoChildCaptureKeyboard ||
						!isAncestor(Kwnd, CaptureKeyboard))
                Kwnd = CaptureKeyboard;

        /* -------- send mouse and keyboard messages to the
            window that should get them -------- */
        switch (ev.event)    {
            case SHIFT_CHANGED:
            case KEYBOARD:
				if (!handshaking)
	                SendMessage(Kwnd, ev.event, ev.mx, ev.my);
                break;
            case LEFT_BUTTON:
				if (!handshaking)	{
		        	Mwnd = MouseWindow(ev.mx, ev.my);
                	if (!CaptureMouse ||
                        	(!NoChildCaptureMouse &&
								isAncestor(Mwnd, CaptureMouse)))
                    	if (Mwnd != inFocus)
                        	SendMessage(Mwnd, SETFOCUS, TRUE, 0);
                	SendMessage(Mwnd, LEFT_BUTTON, ev.mx, ev.my);
				}
                break;
            case BUTTON_RELEASED:
            case DOUBLE_CLICK:
            case RIGHT_BUTTON:
				if (handshaking)
					break;
            case MOUSE_MOVED:
		        Mwnd = MouseWindow(ev.mx, ev.my);
                SendMessage(Mwnd, ev.event, ev.mx, ev.my);
                break;
            case CLOCKTICK:
                SendMessage(Cwnd, ev.event,
                    (PARAM) MK_FP(ev.mx, ev.my), 0);
				break;
            default:
                break;
        }
    }
    /* ------ dequeue and process messages ----- */
    while (MsgQueueCtr > 0)  {
        struct msgs mq;

		mq = MsgQueue[MsgQueueOffCtr];
        if (++MsgQueueOffCtr == MAXMESSAGES)
            MsgQueueOffCtr = 0;
        --MsgQueueCtr;
        SendMessage(mq.wnd, mq.msg, mq.p1, mq.p2);
        if (mq.msg == ENDDIALOG)
			return FALSE;
        if (mq.msg == STOP)	{
		    PostMessage(NULL, STOP, 0, 0);
			return FALSE;
		}
    }
    return TRUE;
}


