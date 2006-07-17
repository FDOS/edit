/* ---------- direct.c --------- */

#include "dflat.h"

#ifndef FA_DIREC
#define FA_DIREC 0x10
#endif

static char path[MAXPATH];
static char drive[MAXDRIVE] = " :";
static char dir[MAXDIR];
static char name[MAXFILE];
static char ext[MAXEXT];

/* ----- Create unambiguous path from file spec, filling in the
     drive and directory if incomplete. Optionally change to
     the new drive and subdirectory ------ */
void CreatePath(char *spath,char *fspec,int InclName,int Change)
{
    int cm = 0;
    unsigned currdrive;
    char currdir[MAXPATH+1];
    char *cp;

    if (!Change)    { /* backup if not to be changed */
        /* ---- save the current drive and subdirectory ---- */
        currdrive = getdisk();
        getcwd(currdir, sizeof currdir);
        memmove(currdir, currdir+2, strlen(currdir+1));
        cp = currdir+strlen(currdir)-1;
        if ((*cp == '\\') && (strlen(dir) > 1)) /* save "\\" - Eric */
            *cp = '\0';
    }
    *drive = *dir = *name = *ext = '\0';

    fnsplit(fspec, drive, dir, name, ext);

    if (!InclName)
        *name = *ext = '\0';
    *drive = toupper(*drive);
    if (*ext)
        cm |= EXTENSION;
    if (InclName && *name)
        cm |= FILENAME;
    if (*dir)
        cm |= DIRECTORY;

    if (*drive)
        cm |= DRIVE;
    if (cm & DRIVE)
        setdisk(*drive - 'A');
    else     {
        *drive = getdisk();
        *drive += 'A';
    }

    if (cm & DIRECTORY)    {
        cp = dir+strlen(dir)-1;
        if ((*cp == '\\') && (strlen(dir) > 1))
            /* save "\\" if it is the only dirspec - Eric */
            *cp = '\0'; /* remove trailing backslash if not needed */
        chdir(dir);
    }
    getcwd(dir, sizeof dir);
    memmove(dir, dir+2, strlen(dir+1));

    if (InclName) { /* unless only drive / directory wanted */
        if (!(cm & FILENAME))
            strcpy(name, "*");
        if (!(cm & EXTENSION) && strchr(fspec, '.') != NULL)
            strcpy(ext, ".*");
    }
    else
        *name = *ext = '\0';

    if (dir[strlen(dir)-1] != '\\')
        strcat(dir, "\\"); /* supply a trailing backslash */

    if (spath != NULL)
    	fnmerge(spath, drive, dir, name, ext);

    if (!Change) { /* restore if not to be changed */
        setdisk(currdrive);
        chdir(currdir);
    }
}

static int dircmp(const void *c1, const void *c2)
{
    return stricmp(*(char **)c1, *(char **)c2);
}

static BOOL BuildList(WINDOW wnd, char *fspec, BOOL dirs)
{
    int ax, i = 0, criterr = 1;
    struct ffblk ff;
    CTLWINDOW *ct = FindCommand(wnd->extension,
							dirs ? ID_DIRECTORY : ID_FILES,LISTBOX);
    WINDOW lwnd;
    char **dirlist = NULL;

    if (ct != NULL)    {
        lwnd = ct->wnd;
        SendMessage(lwnd, CLEARTEXT, 0, 0);

       	while (criterr == 1)    {
           	ax = findfirst(fspec, &ff, dirs ? FA_DIREC: 0);
           	criterr = TestCriticalError();
       	}
       	if (criterr)
           	return FALSE;
        while (ax == 0)    {
			if (!dirs || (ff.ff_attrib & FA_DIREC) && strcmp(ff.ff_name, "."))	{
	            dirlist = DFrealloc(dirlist, sizeof(char *)*(i+1));
    	        dirlist[i] = DFmalloc(strlen(ff.ff_name)+1);
        	    strcpy(dirlist[i++], ff.ff_name);
			}
            ax = findnext(&ff);
        }
        if (dirlist != NULL)    {
            int j;
            /* -- sort file or directory list box data -- */
            qsort(dirlist, i, sizeof(void *), dircmp);
            /* ---- send sorted list to list box ---- */
            for (j = 0; j < i; j++)    {
                SendMessage(lwnd,ADDTEXT,(PARAM)dirlist[j],0);
                free(dirlist[j]);
            }
            free(dirlist);
		}
        SendMessage(lwnd, SHOW_WINDOW, 0, 0);
    }
	return TRUE;
}

BOOL BuildFileList(WINDOW wnd, char *fspec)
{
	return BuildList(wnd, fspec, FALSE);
}

void BuildDirectoryList(WINDOW wnd)
{
	BuildList(wnd, "*.*", TRUE);
}

void BuildDriveList(WINDOW wnd)
{
    CTLWINDOW *ct = FindCommand(wnd->extension, ID_DRIVE,LISTBOX);
    if (ct != NULL)
        {
        union REGS regs;
        char drname[15];
        unsigned int cd, dr;
        WINDOW lwnd = ct->wnd;

        SendMessage(lwnd, CLEARTEXT, 0, 0);
    	cd = getdisk();
    	for (dr = 0; dr < 26; dr++) /* would be better to use LASTDRIVE */
            {
            unsigned ndr;

            setdisk(dr);
            ndr = getdisk(); /* drive really accessible? */
            if (ndr == dr)
                {
                /* ----- test for remapped B drive ----- */
            	if (dr == 1)
                    {
                    regs.x.ax = 0x440e; /* IOCTL func 14 */
                    regs.h.bl = dr+1;
                    int86(DOS, &regs, &regs);
                    if (regs.h.al != 0)
                        continue;

                    }

                sprintf(drname, "[-%c-]", dr+'A');

#ifndef JOEDISK /* Test for network or RAM disk */
/*
                -- Commented out for now...don't really like or need this
                   as of right now -- Joe
 */
                regs.x.ax = 0x4409;     /* IOCTL func 9: check for subst/net */
            	regs.h.bl = dr+1;
            	int86(DOS, &regs, &regs);
                if (!regs.x.cflag)
                    {
                    if (regs.x.dx & 0x1000)
#if 0	/* only for MS ramdisks (0x4800 or 0x0800). Other signs:     */
	/* Ramdisks often have only 1 FAT and 1 "head". Other flags: */
	/* test bit 0x8000 for SUBST. (and: 0x200: "no sector I/O"). */
                    	strcat(drname, " (Net)");
                    else if (regs.x.dx == 0x0800)
                    	strcat(drname, " (RAM)");
#else
			{ /* design by -ea ;-) */
			    drname[0] = '<';
			    drname[4] = '>';
			}; /* "<-X->" for network, "[-X-]" for normal */
			/* CD-ROM counts as network. Add CD-checking? */
#endif
                    }
            	SendMessage(lwnd,ADDTEXT,(PARAM)drname,0);
#endif
        	}

            }

        SendMessage(lwnd, SHOW_WINDOW, 0, 0);
    	setdisk(cd);
	}
}

void BuildPathDisplay(WINDOW wnd)
{
    CTLWINDOW *ct = FindCommand(wnd->extension, ID_PATH,TEXT);
    if (ct != NULL)
        {
        int len;
        WINDOW lwnd = ct->wnd;

        CreatePath(path, "*.*", FALSE, FALSE);
        len = strlen(path);
        if ( (path[len-1] == '\\') && (len > 3) )
            path[len-1] = '\0'; /* strip final backslash IF dirname given */

       	SendMessage(lwnd,SETTEXT,(PARAM)path,0);
        SendMessage(lwnd, PAINT, 0, 0);
	}

}
