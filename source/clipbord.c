/* ----------- clipbord.c ------------ */
#include "dflat.h"

char *Clipboard;
unsigned ClipboardLength;

void CopyTextToClipboard(char *text)
{
    ClipboardLength = strlen(text);
    Clipboard = DFrealloc(Clipboard, ClipboardLength);
    memmove(Clipboard, text, ClipboardLength);
}

void CopyToClipboard(WINDOW wnd)
{
    if (TextBlockMarked(wnd))    {
        char *bb = TextBlockBegin(wnd);	/* near pointers */
        char *be = TextBlockEnd(wnd);	/* near pointers */
        if (bb >= be) { /* *** 0.6e extra check *** */
            bb = TextBlockEnd(wnd);	/* sic! */
            be = TextBlockBegin(wnd);	/* sic! */
        }
        ClipboardLength = (unsigned) (be - bb); /* *** unsigned *** */
        Clipboard = DFrealloc(Clipboard, ClipboardLength);
        memmove(Clipboard, bb, ClipboardLength);
    }
}

void ClearClipboard(void)
{
    if (Clipboard != NULL)  {
        free(Clipboard);
        Clipboard = NULL;
    }
}


BOOL PasteText(WINDOW wnd, char *SaveTo, unsigned len)
{
    if (SaveTo != NULL && len > 0)    {
        unsigned plen = strlen(wnd->text) + len;

		if (plen <= wnd->MaxTextLength)	{
        	if (plen+1 > wnd->textlen)    {
            	wnd->text = DFrealloc(wnd->text, plen+3);
            	wnd->textlen = plen+1;
        	}
          	memmove(CurrChar+len, CurrChar, strlen(CurrChar)+1);
           	memmove(CurrChar, SaveTo, len);
           	BuildTextPointers(wnd);
           	wnd->TextChanged = TRUE;
			return TRUE;
		}
    }
	return FALSE;
}

/* NEW 7/2005 - not actually clipboard related but editbox.c is */
/* already tooo long anyway ;-) In-place text section stuff...  */
/* toupper/tolower: see ctype.h  -  do they support COUNTRY...? */

void UpCaseMarked(WINDOW wnd)
{
    if (TextBlockMarked(wnd))    {
        char *bb = TextBlockBegin(wnd);	/* near pointers */
        char *be = TextBlockEnd(wnd);	/* near pointers */
        if (bb >= be) {
            bb = TextBlockEnd(wnd);	/* sic! */
            be = TextBlockBegin(wnd);	/* sic! */
        }
        while (bb < be) {
           bb[0] = toupper(bb[0]);
           bb++;
        }
    }
}

void DownCaseMarked(WINDOW wnd)
{
    if (TextBlockMarked(wnd))    {
        char *bb = TextBlockBegin(wnd);	/* near pointers */
        char *be = TextBlockEnd(wnd);	/* near pointers */
        if (bb >= be) {
            bb = TextBlockEnd(wnd);	/* sic! */
            be = TextBlockBegin(wnd);	/* sic! */
        }
        while (bb < be) {
           bb[0] = tolower(bb[0]);
           bb++;
        }
    }
}

void StatsForMarked(WINDOW wnd, unsigned *bytes, unsigned *words, unsigned *lines)
{
    bytes[0] = words[0] = lines[0] = 0;
    if (TextBlockMarked(wnd))    {
    	int inWord = 0;
        char *bb = TextBlockBegin(wnd);	/* near pointers */
        char *be = TextBlockEnd(wnd);	/* near pointers */
        if (bb >= be) {
            bb = TextBlockEnd(wnd);	/* sic! */
            be = TextBlockBegin(wnd);	/* sic! */
        }
        while (bb < be) {
           char c = bb[0];
           if (c == '\n') lines[0]++;
           if (isspace(c)) {
           	if (inWord) words[0]++;
           	inWord = 0;
           } else {
           	inWord = 1;
           }
           bytes[0]++;
           bb++;
        }
        if (inWord) words[0]++;
    }
}

