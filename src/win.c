#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include <commdlg.h>

#include <process.h>

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "ibm.h"
#include "video.h"
#include "resources.h"
#include "ide.h"
#include "nvr.h"
#include "sound.h"

#include "plat-midi.h"
#include "plat-keyboard.h"

#include "win-ddraw.h"
//#include "win-d3d.h"
//#include "win-opengl.h"

#define TIMER_1SEC 1

int winsizex=640,winsizey=480;
int gfx_present[18];
int wakeups,wokeups;
#undef cs
CRITICAL_SECTION cs;
void waitmain();
void vsyncint();
int vgapresent;

int cursoron = 1;

HANDLE soundthreadh;
HANDLE blitthreadh;
HANDLE mainthreadh;

void soundthread(LPVOID param);
void endmainthread();
void endsoundthread();
void silencesound();
void restoresound();
static HANDLE frameobject;

int infocus=1;

int drawits=0;
void vsyncint()
{
//        if (infocus)
//        {
                        drawits++;
                        wakeups++;
                        SetEvent(frameobject);
//        }
}

int romspresent[26];
int quited=0;

RECT oldclip,pcclip;
int mousecapture=0;
int drawit;
/*  Declare Windows procedure  */
LRESULT CALLBACK WindowProcedure (HWND, UINT, WPARAM, LPARAM);

/*  Make the class name into a global variable  */
char szClassName[ ] = "WindowsApp";

HWND ghwnd;
void updatewindowsize(int x, int y)
{
        RECT r;
        if (vid_resize) return;
        winsizex=x; winsizey=y;
        GetWindowRect(ghwnd,&r);
        MoveWindow(ghwnd,r.left,r.top,
                     x+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),
                     y+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+1,
                     TRUE);
//        startblit();
//        d3d_resize(x, y);
//        endblit();
}

void releasemouse()
{
        if (mousecapture) 
        {
                ClipCursor(&oldclip);
                ShowCursor(TRUE);
                mousecapture = 0;
        }
}

void setrefresh(int rate)
{
        return;
        remove_int(vsyncint);
        drawit=0;
        install_int_ex(vsyncint,BPS_TO_TIMER(rate));
/*        printf("Vsyncint at %i hz\n",rate);
        counter_freq.QuadPart=counter_base.QuadPart/rate;*/
//        DeleteTimerQueueTimer( &hTimer, hTimerQueue, NULL);
//        CreateTimerQueueTimer( &hTimer, hTimerQueue,
//            mainroutine, NULL , 100, 1000/rate, 0);
}

void startblit()
{
        EnterCriticalSection(&cs);
}

void endblit()
{
        LeaveCriticalSection(&cs);
}

int mainthreadon=0;

/*static HANDLE blitobject;
void blitthread(LPVOID param)
{
        while (!quited)
        {
                WaitForSingleObject(blitobject,100);
                if (quited) break;
                doblit();
        }
}

void wakeupblit()
{
        SetEvent(blitobject);
}*/

HWND statushwnd;
int statusopen=0;
extern int updatestatus;
extern int sreadlnum, swritelnum, segareads, segawrites, scycles_lost;

int pause=0;

void mainthread(LPVOID param)
{
        int t = 0;
        int frames = 0;
        DWORD old_time, new_time;
        mainthreadon=1;
//        Sleep(500);
        drawits=0;
        old_time = GetTickCount();
        while (!quited)
        {
//                if (infocus)
//                {
/*                        if (!drawits)
                        {
                                while (!drawits)
                                {
                                        ResetEvent(frameobject);
                                        WaitForSingleObject(frameobject,10);
                                }
                        }*/
                        if (updatestatus)
                        {
                                updatestatus=0;
                                if (statusopen) SendMessage(statushwnd,WM_USER,0,0);
                        }
                        new_time = GetTickCount();
                        drawits += new_time - old_time;
                        old_time = new_time;
                        if (drawits > 0 && !pause)
                        {
//                                printf("Drawits %i\n",drawits);
                                drawits-=10;
                                if (drawits>50) drawits=0;
                                wokeups++;
                                startblit();
                                runpc();
//                                ddraw_draw();
                                endblit();
                                frames++;
                                if (frames >= 200 && nvr_dosave)
                                {
                                        frames = 0;
                                        nvr_dosave = 0;
                                        savenvr();
                                }
//#if 0                                
//#endif
                        }
                        else
                        {
                                Sleep(1);
                        }
//                }
        }
        mainthreadon=0;
}

HMENU menu;

static void initmenu(void)
{
        int c;
        HMENU m;
        char s[32];
        m=GetSubMenu(menu,2); /*Settings*/
        m=GetSubMenu(m,1); /*CD-ROM*/

        /* Loop through each Windows drive letter and test to see if
           it's a CDROM */
        for (c='A';c<='Z';c++)
        {
                sprintf(s,"%c:\\",c);
                if (GetDriveType(s)==DRIVE_CDROM)
                {
                        sprintf(s, "Host CD/DVD Drive (%c:)", c);
                        AppendMenu(m,MF_STRING,IDM_CDROM_REAL+c,s);
                }
        }
}

HINSTANCE hinstance;

void get_executable_name(char *s, int size)
{
        GetModuleFileName(hinstance, s, size);
}

void set_window_title(char *s)
{
        SetWindowText(ghwnd, s);
}

int WINAPI WinMain (HINSTANCE hThisInstance,
                    HINSTANCE hPrevInstance,
                    LPSTR lpszArgument,
                    int nFunsterStil)

{
    HWND hwnd;               /* This is the handle for our window */
    MSG messages;            /* Here messages to the application are saved */
    WNDCLASSEX wincl;        /* Data structure for the windowclass */
    int c, d;

        hinstance=hThisInstance;
    /* The Window structure */
    wincl.hInstance = hThisInstance;
    wincl.lpszClassName = szClassName;
    wincl.lpfnWndProc = WindowProcedure;      /* This function is called by windows */
    wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
    wincl.cbSize = sizeof (WNDCLASSEX);

    /* Use default icon and mouse-pointer */
    wincl.hIcon = LoadIcon (NULL, IDI_APPLICATION);
    wincl.hIconSm = LoadIcon (NULL, IDI_APPLICATION);
    wincl.hCursor = NULL;//LoadCursor (NULL, IDC_ARROW);
    wincl.lpszMenuName = NULL;                 /* No menu */
    wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
    wincl.cbWndExtra = 0;                      /* structure or the window instance */
    /* Use Windows's default color as the background of the window */
    wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

    /* Register the window class, and if it fails quit the program */
    if (!RegisterClassEx (&wincl))
        return 0;

        menu=LoadMenu(hThisInstance,TEXT("MainMenu"));
        initmenu();
        
    /* The class is registered, let's create the program*/
    hwnd = CreateWindowEx (
           0,                   /* Extended possibilites for variation */
           szClassName,         /* Classname */
           "PCem v0.7",         /* Title Text */
           WS_OVERLAPPEDWINDOW&~WS_SIZEBOX, /* default window */
           CW_USEDEFAULT,       /* Windows decides the position */
           CW_USEDEFAULT,       /* where the window ends up on the screen */
           640+(GetSystemMetrics(SM_CXFIXEDFRAME)*2),                 /* The programs width */
           480+(GetSystemMetrics(SM_CYFIXEDFRAME)*2)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+1,                 /* and height in pixels */
           HWND_DESKTOP,        /* The window is a child-window to desktop */
           menu,                /* Menu */
           hThisInstance,       /* Program Instance handler */
           NULL                 /* No Window Creation data */
           );

        /* Make the window visible on the screen */
        ShowWindow (hwnd, nFunsterStil);

//        win_set_window(hwnd);
        
        ghwnd=hwnd;
        
        ddraw_init(hwnd);
        midi_init();
        atexit(midi_close);
//        d3d_init(hwnd);
//        opengl_init(hwnd);
        
        initpc();

        if (vid_resize) SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW|WS_VISIBLE);
        else            SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX)|WS_VISIBLE);

        if (!cdrom_enabled)
           CheckMenuItem(menu, IDM_CDROM_DISABLED, MF_CHECKED);
        else           
           CheckMenuItem(menu, IDM_CDROM_REAL + cdrom_drive, MF_CHECKED);
        CheckMenuItem(menu, IDM_KEY_ALLEGRO + kb_win, MF_CHECKED);
        if (vid_resize) CheckMenuItem(menu, IDM_VID_RESIZE, MF_CHECKED);
        
//        set_display_switch_mode(SWITCH_BACKGROUND);
        
        d=romset;
        for (c=0;c<26;c++)
        {
                romset=c;
                romspresent[c]=loadbios();
                pclog("romset %i - %i\n", c, romspresent[c]);
        }
        
        for (c = 0; c < 26; c++)
        {
                if (romspresent[c])
                   break;
        }
        if (c == 26)
        {
                MessageBox(hwnd,"No ROMs present!\nYou must have at least one romset to use PCem.","PCem fatal error",MB_OK);
                return 0;
        }

        romset=d;
        c=loadbios();

        if (!c)
        {
                if (romset!=-1) MessageBox(hwnd,"Configured romset not available.\nDefaulting to available romset.","PCem error",MB_OK);
                for (c=0;c<26;c++)
                {
                        if (romspresent[c])
                        {
                                romset=c;
                                loadbios();
                                break;
                        }
                }
        }
        
        d = gfxcard;
        for (c = 0; c < 18; c++)
        {
                gfxcard = c;
                gfx_present[c] = mem_load_video_bios();
        }
        gfxcard = d;
        c = mem_load_video_bios();
        if (!c)
        {
                if (romset!=-1) MessageBox(hwnd,"Configured video BIOS not available.\nDefaulting to available romset.","PCem error",MB_OK);
                for (c=0;c<18;c++)
                {
                        if (gfx_present[c])
                        {
                                gfxcard = c;
                                mem_load_video_bios();
                                break;
                        }
                }
        }
        timeBeginPeriod(1);
//        soundobject=CreateEvent(NULL, FALSE, FALSE, NULL);
//        soundthreadh=CreateThread(NULL,0,soundthread,NULL,NULL,NULL);

        
        atexit(releasemouse);
//        atexit(endsoundthread);
        drawit=0;
//        QueryPerformanceFrequency(&counter_base);
///        QueryPerformanceCounter(&counter_posold);
//        counter_posold.QuadPart*=100;

InitializeCriticalSection(&cs);
        frameobject=CreateEvent(NULL, FALSE, FALSE, NULL);
        mainthreadh=(HANDLE)_beginthread(mainthread,0,NULL);
//        atexit(endmainthread);
//        soundthreadh=(HANDLE)_beginthread(soundthread,0,NULL);
//        atexit(endsoundthread);
        SetThreadPriority(mainthreadh, THREAD_PRIORITY_HIGHEST);
        
//        blitobject=CreateEvent(NULL, FALSE, FALSE, NULL);
//        blitthreadh=(HANDLE)_beginthread(blitthread,0,NULL);
        
//        SetThreadPriority(soundthreadh, THREAD_PRIORITY_HIGHEST);

        drawit=0;
        install_int_ex(vsyncint,BPS_TO_TIMER(100));
        
        updatewindowsize(640, 480);
//        focus=1;
//        setrefresh(100);

//        ShowCursor(TRUE);
        
        /* Run the message loop. It will run until GetMessage() returns 0 */
        while (!quited)
        {
/*                if (infocus)
                {
                        if (drawits)
                        {
                                drawits--;
                                if (drawits>10) drawits=0;
                                runpc();
                        }
//;                        else
//                           sleep(0);
                        if ((key[KEY_LCONTROL] || key[KEY_RCONTROL]) && key[KEY_END] && mousecapture)
                        {
                                ClipCursor(&oldclip);
                                mousecapture=0;
                        }
                }*/

                while (GetMessage(&messages,NULL,0,0) && !quited)
                {
                        if (messages.message==WM_QUIT) quited=1;
                        TranslateMessage(&messages);
                        DispatchMessage(&messages);                        
                                if ((key[KEY_LCONTROL] || key[KEY_RCONTROL]) && key[KEY_END] && mousecapture)
                                {
                                        ClipCursor(&oldclip);
                                        ShowCursor(TRUE);
                                        mousecapture=0;
                                }
                }

                quited=1;
//                else
//                sleep(10);
        }
        
        startblit();
//        pclog("Sleep 1000\n");
        Sleep(200);
//        pclog("TerminateThread\n");
        TerminateThread(blitthreadh,0);
        TerminateThread(mainthreadh,0);
//        pclog("Quited? %i\n",quited);
//        pclog("Closepc\n");
        closepc();
//        pclog("dumpregs\n");
        timeEndPeriod(1);
//        endsoundthread();
//        dumpregs();
        if (mousecapture) 
        {
                ClipCursor(&oldclip);
                ShowCursor(TRUE);
        }
//        pclog("Ending! %i %i\n",messages.wParam,quited);
        return messages.wParam;
}

char openfilestring[260];
int getfile(HWND hwnd, char *f, char *fn)
{
        OPENFILENAME ofn;       // common dialog box structure
        BOOL r;
        DWORD err;

        // Initialize OPENFILENAME
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = openfilestring;
        //
        // Set lpstrFile[0] to '\0' so that GetOpenFileName does not
        // use the contents of szFile to initialize itself.
        //
//        ofn.lpstrFile[0] = '\0';
        strcpy(ofn.lpstrFile,fn);
        ofn.nMaxFile = sizeof(openfilestring);
        ofn.lpstrFilter = f;//"All\0*.*\0Text\0*.TXT\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        // Display the Open dialog box.

        pclog("GetOpenFileName - lpstrFile = %s\n", ofn.lpstrFile);
        r = GetOpenFileName(&ofn);
        if (r)
        {
                pclog("GetOpenFileName return true\n");
                return 0;
        }
        pclog("GetOpenFileName return false\n");
        err = CommDlgExtendedError();
        pclog("CommDlgExtendedError return %04X\n", err);
        return 1;
}

int getsfile(HWND hwnd, char *f, char *fn)
{
        OPENFILENAME ofn;       // common dialog box structure
        BOOL r;
        DWORD err;

        // Initialize OPENFILENAME
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFile = openfilestring;
        //
        // Set lpstrFile[0] to '\0' so that GetOpenFileName does not
        // use the contents of szFile to initialize itself.
        //
//        ofn.lpstrFile[0] = '\0';
        strcpy(ofn.lpstrFile,fn);
        ofn.nMaxFile = sizeof(openfilestring);
        ofn.lpstrFilter = f;//"All\0*.*\0Text\0*.TXT\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

        // Display the Open dialog box.

        pclog("GetSaveFileName - lpstrFile = %s\n", ofn.lpstrFile);
        r = GetSaveFileName(&ofn);
        if (r)
        {
                pclog("GetSaveFileName return true\n");
                return 0;
        }
        pclog("GetSaveFileName return false\n");
        err = CommDlgExtendedError();
        pclog("CommDlgExtendedError return %04X\n", err);
        return 1;
}

extern int is486;
int romstolist[26], listtomodel[26], romstomodel[26], modeltolist[26];
int vidtolist[20],listtovid[20];

int mem_list_to_size[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,32,48,64,256};
int mem_size_to_list[]={0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,11,12,13,14,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,16,
                        16,16,16,16,16,16,16,16,16,16,16,16,16,16,16,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,17,18,
                        19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
                        19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
                        19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
                        19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
                        19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,
                        19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19,19, 19};

#include "cpu.h"
#include "model.h"

BOOL CALLBACK configdlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        HWND h;
        int c, d;
        int rom,gfx,mem,fpu;
        int temp_cpu, temp_cpu_m, temp_model;
        int temp_GAMEBLASTER, temp_GUS, temp_sound_card_current;
//        pclog("Dialog msg %i %08X\n",message,message);
        switch (message)
        {
                case WM_INITDIALOG:
                        pause=1;
                h=GetDlgItem(hdlg,IDC_COMBO1);
                for (c=0;c<26;c++) romstolist[c]=0;
                c = d = 0;
                while (models[c].id != -1)
                {
                        pclog("INITDIALOG : %i %i %i\n",c,models[c].id,romspresent[models[c].id]);
                        if (romspresent[models[c].id])
                        {
                                SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)models[c].name);
                                modeltolist[c] = d;
                                listtomodel[d] = c;
                                romstolist[models[c].id] = d;
                                romstomodel[models[c].id] = c;
                                d++;
                        }
                        c++;
                }
                SendMessage(h, CB_SETCURSEL, modeltolist[model], 0);
                
                h=GetDlgItem(hdlg,IDC_COMBO2);
                for (c=0;c<6;c++) vidtolist[c]=0;
                c=4;
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"CGA"); vidtolist[GFX_CGA]=0; listtovid[0]=0;
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"MDA"); vidtolist[GFX_MDA]=1; listtovid[1]=1;
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Hercules"); vidtolist[GFX_HERCULES]=2; listtovid[2]=2;
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"EGA"); vidtolist[GFX_EGA]=3; listtovid[3]=3;
                if (gfx_present[GFX_TVGA])       { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Trident 8900D");           vidtolist[GFX_TVGA]=c;          listtovid[c]=GFX_TVGA;          c++;       }
                if (gfx_present[GFX_ET4000])     { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Tseng ET4000AX");          vidtolist[GFX_ET4000]=c;        listtovid[c]=GFX_ET4000;        c++;       }
                if (gfx_present[GFX_ET4000W32])  { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Diamond Stealth 32");      vidtolist[GFX_ET4000W32]=c;     listtovid[c]=GFX_ET4000W32;     c++;       }
                if (gfx_present[GFX_BAHAMAS64])  { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Paradise Bahamas 64");     vidtolist[GFX_BAHAMAS64]=c;     listtovid[c]=GFX_BAHAMAS64;     c++;       }
                if (gfx_present[GFX_N9_9FX])     { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Number Nine 9FX");         vidtolist[GFX_N9_9FX]=c;        listtovid[c]=GFX_N9_9FX;        c++;       }
                if (gfx_present[GFX_VIRGE])      { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"S3 VIRGE");                vidtolist[GFX_VIRGE]=c;         listtovid[c]=GFX_VIRGE;         c++;       }
                if (gfx_present[GFX_TGUI9440])   { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Trident TGUI9440");        vidtolist[GFX_TGUI9440]=c;      listtovid[c]=GFX_TGUI9440;      c++;       }
                if (gfx_present[GFX_VGA])        { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"VGA");                     vidtolist[GFX_VGA]=c;           listtovid[c]=GFX_VGA;           c++;       }
                if (gfx_present[GFX_VGAEDGE16])  { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"ATI VGA Edge-16");         vidtolist[GFX_VGAEDGE16]=c;     listtovid[c]=GFX_VGAEDGE16;     c++;       }                
                if (gfx_present[GFX_VGACHARGER]) { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"ATI VGA Charger");         vidtolist[GFX_VGACHARGER]=c;    listtovid[c]=GFX_VGACHARGER;    c++;       }                
                if (gfx_present[GFX_OTI067])     { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Oak OTI-067");             vidtolist[GFX_OTI067]=c;        listtovid[c]=GFX_OTI067;        c++;       }
                if (gfx_present[GFX_MACH64GX])   { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"ATI Graphics Pro Turbo");  vidtolist[GFX_MACH64GX]=c;      listtovid[c]=GFX_MACH64GX;      c++;       }
                if (gfx_present[GFX_CL_GD5429])  { SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Cirrus Logic CL-GD5429");  vidtolist[GFX_CL_GD5429]=c;     listtovid[c]=GFX_CL_GD5429;     c++;       }
                SendMessage(h,CB_SETCURSEL,vidtolist[gfxcard],0);
                if (models[model].fixed_gfxcard) EnableWindow(h,FALSE);

                h=GetDlgItem(hdlg,IDC_COMBOCPUM);
                c = 0;
                while (models[romstomodel[romset]].cpu[c].cpus != NULL && c < 3)
                {
                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)models[romstomodel[romset]].cpu[c].name);
                        c++;
                }
                EnableWindow(h,TRUE);
                SendMessage(h, CB_SETCURSEL, cpu_manufacturer, 0);
                if (c == 1) EnableWindow(h, FALSE);
                else        EnableWindow(h, TRUE);

                h=GetDlgItem(hdlg,IDC_COMBO3);
                c = 0;
                while (models[romstomodel[romset]].cpu[cpu_manufacturer].cpus[c].cpu_type != -1)
                {
                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)models[romstomodel[romset]].cpu[cpu_manufacturer].cpus[c].name);
                        c++;
                }
                EnableWindow(h,TRUE);
                SendMessage(h, CB_SETCURSEL, cpu, 0);

                h=GetDlgItem(hdlg,IDC_COMBOSND);
                c = 0;
                while (1)
                {
                        char *s = sound_card_getname(c);

                        if (!s[0])
                                break;
                                
                        SendMessage(h, CB_ADDSTRING, 0, (LPARAM)(LPCSTR)s);
                        c++;
                }
                SendMessage(h, CB_SETCURSEL, sound_card_current, 0);

                h=GetDlgItem(hdlg, IDC_CHECK3);
                SendMessage(h, BM_SETCHECK, GAMEBLASTER, 0);

                h=GetDlgItem(hdlg, IDC_CHECKGUS);
                SendMessage(h, BM_SETCHECK, GUS, 0);
                
                h=GetDlgItem(hdlg, IDC_CHECK2);
                SendMessage(h, BM_SETCHECK, slowega, 0);

                h=GetDlgItem(hdlg, IDC_CHECK4);
                SendMessage(h, BM_SETCHECK, cga_comp, 0);

                h=GetDlgItem(hdlg,IDC_COMBOCHC);
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"A little");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"A bit");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Some");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"A lot");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Infinite");
                SendMessage(h,CB_SETCURSEL,cache,0);

                h=GetDlgItem(hdlg,IDC_COMBOSPD);
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"8-bit");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Slow 16-bit");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Fast 16-bit");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Slow VLB/PCI");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Mid  VLB/PCI");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"Fast VLB/PCI");                
                SendMessage(h,CB_SETCURSEL,video_speed,0);

                h=GetDlgItem(hdlg,IDC_COMBOMEM);
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"1 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"2 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"3 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"4 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"5 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"6 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"7 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"8 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"9 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"10 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"11 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"12 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"13 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"14 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"15 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"16 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"32 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"48 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"64 MB");
                SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)"256 MB");                
                SendMessage(h,CB_SETCURSEL,mem_size_to_list[mem_size-1],0);
                
                pclog("Init cpuspeed %i\n",cpuspeed);

                return TRUE;
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        h=GetDlgItem(hdlg,IDC_COMBO1);
                        temp_model = listtomodel[SendMessage(h,CB_GETCURSEL,0,0)];

                        h=GetDlgItem(hdlg,IDC_COMBO2);
                        gfx=listtovid[SendMessage(h,CB_GETCURSEL,0,0)];

                        h=GetDlgItem(hdlg,IDC_COMBOMEM);
                        mem=mem_list_to_size[SendMessage(h,CB_GETCURSEL,0,0)];

                        h = GetDlgItem(hdlg, IDC_COMBOCPUM);
                        temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);
                        h = GetDlgItem(hdlg, IDC_COMBO3);
                        temp_cpu = SendMessage(h, CB_GETCURSEL, 0, 0);
                        fpu = (models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type >= CPU_i486DX) ? 1 : 0;
                        pclog("newcpu - %i %i %i  %i\n",temp_cpu_m,temp_cpu,fpu,models[temp_model].cpu[temp_cpu_m].cpus[temp_cpu].cpu_type);

                        h = GetDlgItem(hdlg, IDC_CHECK3);
                        temp_GAMEBLASTER = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_CHECKGUS);
                        temp_GUS = SendMessage(h, BM_GETCHECK, 0, 0);

                        h = GetDlgItem(hdlg, IDC_COMBOSND);
                        temp_sound_card_current = SendMessage(h, CB_GETCURSEL, 0, 0);

                        if (temp_model != model || gfx != gfxcard || mem != mem_size || fpu != hasfpu || temp_GAMEBLASTER != GAMEBLASTER || temp_GUS != GUS || temp_sound_card_current != sound_card_current)
                        {
                                if (MessageBox(NULL,"This will reset PCem!\nOkay to continue?","PCem",MB_OKCANCEL)==IDOK)
                                {
                                        model = temp_model;
                                        romset = model_getromset();
                                        gfxcard=gfx;
                                        mem_size=mem;
                                        cpu_manufacturer = temp_cpu_m;
                                        cpu = temp_cpu;
                                        GAMEBLASTER = temp_GAMEBLASTER;
                                        GUS = temp_GUS;
                                        sound_card_current = temp_sound_card_current;
                                        
                                        mem_resize();
                                        loadbios();
                                        mem_load_video_bios();
                                        resetpchard();
                                }
                                else
                                {
                                        EndDialog(hdlg,0);
                                        pause=0;
                                        return TRUE;
                                }
                        }

                        h=GetDlgItem(hdlg,IDC_COMBOSPD);
                        video_speed = SendMessage(h,CB_GETCURSEL,0,0);

                        h=GetDlgItem(hdlg,IDC_CHECK4);
                        cga_comp=SendMessage(h,BM_GETCHECK,0,0);

                        cpu_manufacturer = temp_cpu_m;
                        cpu = temp_cpu;
                        cpu_set();
                        
                        h=GetDlgItem(hdlg,IDC_COMBOCHC);
                        cache=SendMessage(h,CB_GETCURSEL,0,0);
                        mem_updatecache();
                        
                        saveconfig();

                        speedchanged();
//                        if (romset>2) cpuspeed=1;
//                        setpitclock(clocks[AT?1:0][cpuspeed][0]);
//                        if (cpuspeed) setpitclock(8000000.0);
//                        else          setpitclock(4772728.0);

                        case IDCANCEL:
                        EndDialog(hdlg,0);
                        pause=0;
                        return TRUE;
                        case IDC_COMBO1:
                        if (HIWORD(wParam) == CBN_SELCHANGE)
                        {
                                h = GetDlgItem(hdlg,IDC_COMBO1);
                                temp_model = listtomodel[SendMessage(h,CB_GETCURSEL,0,0)];
                                
                                /*Enable/disable gfxcard list*/
                                h = GetDlgItem(hdlg, IDC_COMBO2);
                                if (!models[temp_model].fixed_gfxcard)
                                {
                                        EnableWindow(h, TRUE);
                                        SendMessage(h, CB_SETCURSEL, gfxcard, 0);
                                }
                                else
                                        EnableWindow(h, FALSE);
                                
                                /*Rebuild manufacturer list*/
                                h = GetDlgItem(hdlg, IDC_COMBOCPUM);
                                temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);
                                SendMessage(h, CB_RESETCONTENT, 0, 0);
                                c = 0;
                                while (models[temp_model].cpu[c].cpus != NULL && c < 3)
                                {
                                        SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)models[temp_model].cpu[c].name);
                                        c++;
                                }
                                if (temp_cpu_m >= c) temp_cpu_m = c - 1;
                                SendMessage(h, CB_SETCURSEL, temp_cpu_m, 0);
                                if (c == 1) EnableWindow(h, FALSE);
                                else        EnableWindow(h, TRUE);

                                /*Rebuild CPU list*/
                                h = GetDlgItem(hdlg, IDC_COMBO3);
                                temp_cpu = SendMessage(h, CB_GETCURSEL, 0, 0);
                                SendMessage(h, CB_RESETCONTENT, 0, 0);
                                c = 0;
                                while (models[temp_model].cpu[temp_cpu_m].cpus[c].cpu_type != -1)
                                {
                                        SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)models[temp_model].cpu[temp_cpu_m].cpus[c].name);
                                        c++;
                                }
                                if (temp_cpu >= c) temp_cpu = c - 1;
                                SendMessage(h, CB_SETCURSEL, temp_cpu, 0);
                        }
                        break;
                        case IDC_COMBOCPUM:
                        if (HIWORD(wParam) == CBN_SELCHANGE)
                        {
                                h = GetDlgItem(hdlg, IDC_COMBO1);
                                temp_model = listtomodel[SendMessage(h, CB_GETCURSEL, 0, 0)];
                                h = GetDlgItem(hdlg, IDC_COMBOCPUM);
                                temp_cpu_m = SendMessage(h, CB_GETCURSEL, 0, 0);
                                
                                /*Rebuild CPU list*/
                                h=GetDlgItem(hdlg, IDC_COMBO3);
                                temp_cpu = SendMessage(h, CB_GETCURSEL, 0, 0);
                                SendMessage(h, CB_RESETCONTENT, 0, 0);
                                c = 0;
                                while (models[temp_model].cpu[temp_cpu_m].cpus[c].cpu_type != -1)
                                {
                                        SendMessage(h,CB_ADDSTRING,0,(LPARAM)(LPCSTR)models[temp_model].cpu[temp_cpu_m].cpus[c].name);
                                        c++;
                                }
                                if (temp_cpu >= c) temp_cpu = c - 1;
                                SendMessage(h, CB_SETCURSEL, temp_cpu, 0);
                        }
                        break;
                }
                break;

        }
        return FALSE;
}
static char hd_new_name[512];
static int hd_new_spt, hd_new_hpc, hd_new_cyl;

BOOL CALLBACK hdnewdlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        char s[260];
        HWND h;
        int c;
        PcemHDC hd[2];
        FILE *f;
        uint8_t buf[512];
        switch (message)
        {
                case WM_INITDIALOG:
                h = GetDlgItem(hdlg, IDC_EDIT1);
                sprintf(s, "%i", 63);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
                h = GetDlgItem(hdlg, IDC_EDIT2);
                sprintf(s, "%i", 16);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
                h = GetDlgItem(hdlg, IDC_EDIT3);
                sprintf(s, "%i", 511);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);

                h = GetDlgItem(hdlg, IDC_EDITC);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)"");
                
                h = GetDlgItem(hdlg, IDC_TEXT1);
                sprintf(s, "Size : %imb", (((511*16*63)*512)/1024)/1024);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);

                return TRUE;
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        h = GetDlgItem(hdlg, IDC_EDITC);
                        SendMessage(h, WM_GETTEXT, 511, (LPARAM)hd_new_name);
                        if (!hd_new_name[0])
                        {
                                MessageBox(ghwnd,"Please enter a valid filename","PCem error",MB_OK);
                                return TRUE;
                        }
                        h = GetDlgItem(hdlg, IDC_EDIT1);
                        SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
                        sscanf(s, "%i", &hd_new_spt);
                        h = GetDlgItem(hdlg, IDC_EDIT2);
                        SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
                        sscanf(s, "%i", &hd_new_hpc);
                        h = GetDlgItem(hdlg, IDC_EDIT3);
                        SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
                        sscanf(s, "%i", &hd_new_cyl);
                        
                        if (hd_new_spt > 63)
                        {
                                MessageBox(ghwnd,"Drive has too many sectors (maximum is 63)","PCem error",MB_OK);
                                return TRUE;
                        }
                        if (hd_new_hpc > 128)
                        {
                                MessageBox(ghwnd,"Drive has too many heads (maximum is 128)","PCem error",MB_OK);
                                return TRUE;
                        }
                        if (hd_new_cyl > 16383)
                        {
                                MessageBox(ghwnd,"Drive has too many cylinders (maximum is 16383)","PCem error",MB_OK);
                                return TRUE;
                        }
                        
                        f = fopen64(hd_new_name, "wb");
                        if (!f)
                        {
                                MessageBox(ghwnd,"Can't open file for write","PCem error",MB_OK);
                                return TRUE;
                        }
                        memset(buf, 0, 512);
                        for (c = 0; c < (hd_new_cyl * hd_new_hpc * hd_new_spt); c++)
                            fwrite(buf, 512, 1, f);
                        fclose(f);
                        
                        MessageBox(ghwnd,"Remember to partition and format the new drive","PCem",MB_OK);
                        
                        EndDialog(hdlg,1);
                        return TRUE;
                        case IDCANCEL:
                        EndDialog(hdlg,0);
                        return TRUE;

                        case IDC_CFILE:
                        if (!getsfile(hdlg, "Hard disc image (*.IMG)\0*.IMG\0All files (*.*)\0*.*\0", ""))
                        {
                                h = GetDlgItem(hdlg, IDC_EDITC);
                                SendMessage(h, WM_SETTEXT, 0, (LPARAM)openfilestring);
                        }
                        return TRUE;
                        
                        case IDC_EDIT1: case IDC_EDIT2: case IDC_EDIT3:
                        h=GetDlgItem(hdlg,IDC_EDIT1);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].spt);
                        h=GetDlgItem(hdlg,IDC_EDIT2);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].hpc);
                        h=GetDlgItem(hdlg,IDC_EDIT3);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].tracks);

                        h=GetDlgItem(hdlg,IDC_TEXT1);
                        sprintf(s,"Size : %imb",(((((uint64_t)hd[0].tracks*(uint64_t)hd[0].hpc)*(uint64_t)hd[0].spt)*512)/1024)/1024);
                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                        return TRUE;
                }
                break;

        }
        return FALSE;
}

BOOL CALLBACK hdsizedlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        char s[260];
        HWND h;
        PcemHDC hd[2];
        switch (message)
        {
                case WM_INITDIALOG:
                h = GetDlgItem(hdlg, IDC_EDIT1);
                sprintf(s, "%i", hd_new_spt);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
                h = GetDlgItem(hdlg, IDC_EDIT2);
                sprintf(s, "%i", hd_new_hpc);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
                h = GetDlgItem(hdlg, IDC_EDIT3);
                sprintf(s, "%i", hd_new_cyl);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);

                h = GetDlgItem(hdlg, IDC_TEXT1);
                sprintf(s, "Size : %imb", ((((uint64_t)hd_new_spt*(uint64_t)hd_new_hpc*(uint64_t)hd_new_cyl)*512)/1024)/1024);
                SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);

                return TRUE;
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        h = GetDlgItem(hdlg, IDC_EDIT1);
                        SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
                        sscanf(s, "%i", &hd_new_spt);
                        h = GetDlgItem(hdlg, IDC_EDIT2);
                        SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
                        sscanf(s, "%i", &hd_new_hpc);
                        h = GetDlgItem(hdlg, IDC_EDIT3);
                        SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
                        sscanf(s, "%i", &hd_new_cyl);
                        
                        if (hd_new_spt > 63)
                        {
                                MessageBox(ghwnd,"Drive has too many sectors (maximum is 63)","PCem error",MB_OK);
                                return TRUE;
                        }
                        if (hd_new_hpc > 128)
                        {
                                MessageBox(ghwnd,"Drive has too many heads (maximum is 128)","PCem error",MB_OK);
                                return TRUE;
                        }
                        if (hd_new_cyl > 16383)
                        {
                                MessageBox(ghwnd,"Drive has too many cylinders (maximum is 16383)","PCem error",MB_OK);
                                return TRUE;
                        }
                        
                        EndDialog(hdlg,1);
                        return TRUE;
                        case IDCANCEL:
                        EndDialog(hdlg,0);
                        return TRUE;

                        case IDC_EDIT1: case IDC_EDIT2: case IDC_EDIT3:
                        h=GetDlgItem(hdlg,IDC_EDIT1);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].spt);
                        h=GetDlgItem(hdlg,IDC_EDIT2);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].hpc);
                        h=GetDlgItem(hdlg,IDC_EDIT3);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].tracks);

                        h=GetDlgItem(hdlg,IDC_TEXT1);
                        sprintf(s,"Size : %imb",(((((uint64_t)hd[0].tracks*(uint64_t)hd[0].hpc)*(uint64_t)hd[0].spt)*512)/1024)/1024);
                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                        return TRUE;
                }
                break;

        }
        return FALSE;
}

static int hd_changed = 0;
BOOL CALLBACK hdconfdlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        char s[260];
        HWND h;
        PcemHDC hd[2];
        FILE *f;
        off64_t sz;
        switch (message)
        {
                case WM_INITDIALOG:
                pause=1;
                hd[0]=hdc[0];
                hd[1]=hdc[1];
                hd_changed = 0;
                
                h=GetDlgItem(hdlg,IDC_EDIT1);
                sprintf(s,"%i",hdc[0].spt);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                h=GetDlgItem(hdlg,IDC_EDIT2);
                sprintf(s,"%i",hdc[0].hpc);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                h=GetDlgItem(hdlg,IDC_EDIT3);
                sprintf(s,"%i",hdc[0].tracks);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                h=GetDlgItem(hdlg,IDC_EDITC);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)ide_fn[0]);

                h=GetDlgItem(hdlg,IDC_EDIT4);
                sprintf(s,"%i",hdc[1].spt);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                h=GetDlgItem(hdlg,IDC_EDIT5);
                sprintf(s,"%i",hdc[1].hpc);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                h=GetDlgItem(hdlg,IDC_EDIT6);
                sprintf(s,"%i",hdc[1].tracks);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                h=GetDlgItem(hdlg,IDC_EDITD);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)ide_fn[1]);
                
                h=GetDlgItem(hdlg,IDC_TEXT1);
                sprintf(s,"Size : %imb",(((((uint64_t)hd[0].tracks*(uint64_t)hd[0].hpc)*(uint64_t)hd[0].spt)*512)/1024)/1024);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);

                h=GetDlgItem(hdlg,IDC_TEXT2);
                sprintf(s,"Size : %imb",(((((uint64_t)hd[1].tracks*(uint64_t)hd[1].hpc)*(uint64_t)hd[1].spt)*512)/1024)/1024);
                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                return TRUE;
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        if (hd_changed)
                        {                     
                                if (MessageBox(NULL,"This will reset PCem!\nOkay to continue?","PCem",MB_OKCANCEL)==IDOK)
                                {
                                        h=GetDlgItem(hdlg,IDC_EDIT1);
                                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                                        sscanf(s,"%i",&hd[0].spt);
                                        h=GetDlgItem(hdlg,IDC_EDIT2);
                                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                                        sscanf(s,"%i",&hd[0].hpc);
                                        h=GetDlgItem(hdlg,IDC_EDIT3);
                                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                                        sscanf(s,"%i",&hd[0].tracks);
                                        h=GetDlgItem(hdlg,IDC_EDITC);
                                        SendMessage(h,WM_GETTEXT,511,(LPARAM)ide_fn[0]);

                                        h=GetDlgItem(hdlg,IDC_EDIT4);
                                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                                        sscanf(s,"%i",&hd[1].spt);
                                        h=GetDlgItem(hdlg,IDC_EDIT5);
                                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                                        sscanf(s,"%i",&hd[1].hpc);
                                        h=GetDlgItem(hdlg,IDC_EDIT6);
                                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                                        sscanf(s,"%i",&hd[1].tracks);
                                        h=GetDlgItem(hdlg,IDC_EDITD);
                                        SendMessage(h,WM_GETTEXT,511,(LPARAM)ide_fn[1]);
                                        
                                        hdc[0] = hd[0];
                                        hdc[1] = hd[1];

                                        saveconfig();
                                                                                
                                        resetpchard();
                                }                                
                        }
                        case IDCANCEL:
                        EndDialog(hdlg,0);
                        pause=0;
                        return TRUE;

                        case IDC_CNEW:
                        if (DialogBox(hinstance,TEXT("HdNewDlg"),hdlg,hdnewdlgproc) == 1)
                        {
                                h=GetDlgItem(hdlg,IDC_EDIT1);
                                sprintf(s,"%i",hd_new_spt);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                h=GetDlgItem(hdlg,IDC_EDIT2);
                                sprintf(s,"%i",hd_new_hpc);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                h=GetDlgItem(hdlg,IDC_EDIT3);
                                sprintf(s,"%i",hd_new_cyl);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                h=GetDlgItem(hdlg,IDC_EDITC);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)hd_new_name);

                                h=GetDlgItem(hdlg,IDC_TEXT1);
                                sprintf(s,"Size : %imb",(((((uint64_t)hd_new_cyl*(uint64_t)hd_new_hpc)*(uint64_t)hd_new_spt)*512)/1024)/1024);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);

                                hd_changed = 1;
                        }                              
                        return TRUE;
                        
                        case IDC_CFILE:
                        if (!getfile(hdlg, "Hard disc image (*.IMG)\0*.IMG\0All files (*.*)\0*.*\0", ""))
                        {
                                f = fopen64(openfilestring, "rb");
                                if (!f)
                                {
                                        MessageBox(ghwnd,"Can't open file for read","PCem error",MB_OK);
                                        return TRUE;
                                }
                                fseeko64(f, -1, SEEK_END);
                                sz = ftello64(f) + 1;
                                fclose(f);
                                hd_new_spt = 63;
                                hd_new_hpc = 16;
                                hd_new_cyl = ((sz / 512) / 16) / 63;
                                
                                if (DialogBox(hinstance,TEXT("HdSizeDlg"),hdlg,hdsizedlgproc) == 1)
                                {
                                        h=GetDlgItem(hdlg,IDC_EDIT1);
                                        sprintf(s,"%i",hd_new_spt);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                        h=GetDlgItem(hdlg,IDC_EDIT2);
                                        sprintf(s,"%i",hd_new_hpc);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                        h=GetDlgItem(hdlg,IDC_EDIT3);
                                        sprintf(s,"%i",hd_new_cyl);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                        h=GetDlgItem(hdlg,IDC_EDITC);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)openfilestring);

                                        h=GetDlgItem(hdlg,IDC_TEXT1);
                                        sprintf(s,"Size : %imb",(((((uint64_t)hd_new_cyl*(uint64_t)hd_new_hpc)*(uint64_t)hd_new_spt)*512)/1024)/1024);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
        
                                        hd_changed = 1;
                                }
                        }
                        return TRUE;
                                
                        case IDC_DNEW:
                        if (DialogBox(hinstance,TEXT("HdNewDlg"),hdlg,hdnewdlgproc) == 1)
                        {
                                h=GetDlgItem(hdlg,IDC_EDIT4);
                                sprintf(s,"%i",hd_new_spt);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                h=GetDlgItem(hdlg,IDC_EDIT5);
                                sprintf(s,"%i",hd_new_hpc);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                h=GetDlgItem(hdlg,IDC_EDIT6);
                                sprintf(s,"%i",hd_new_cyl);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                h=GetDlgItem(hdlg,IDC_EDITD);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)hd_new_name);

                                h=GetDlgItem(hdlg,IDC_TEXT2);
                                sprintf(s,"Size : %imb",(((((uint64_t)hd_new_cyl*(uint64_t)hd_new_hpc)*(uint64_t)hd_new_spt)*512)/1024)/1024);
                                SendMessage(h,WM_SETTEXT,0,(LPARAM)s);

                                hd_changed = 1;
                        }                              
                        return TRUE;
                        
                        case IDC_DFILE:
                        if (!getfile(hdlg, "Hard disc image (*.IMG)\0*.IMG\0All files (*.*)\0*.*\0", ""))
                        {
                                f = fopen64(openfilestring, "rb");
                                if (!f)
                                {
                                        MessageBox(ghwnd,"Can't open file for read","PCem error",MB_OK);
                                        return TRUE;
                                }
                                fseeko64(f, -1, SEEK_END);
                                sz = ftello64(f) + 1;
                                fclose(f);
                                hd_new_spt = 63;
                                hd_new_hpc = 16;
                                hd_new_cyl = ((sz / 512) / 16) / 63;
                                
                                if (DialogBox(hinstance,TEXT("HdSizeDlg"),hdlg,hdsizedlgproc) == 1)
                                {
                                        h=GetDlgItem(hdlg,IDC_EDIT4);
                                        sprintf(s,"%i",hd_new_spt);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                        h=GetDlgItem(hdlg,IDC_EDIT5);
                                        sprintf(s,"%i",hd_new_hpc);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                        h=GetDlgItem(hdlg,IDC_EDIT6);
                                        sprintf(s,"%i",hd_new_cyl);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                                        h=GetDlgItem(hdlg,IDC_EDITD);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)openfilestring);

                                        h=GetDlgItem(hdlg,IDC_TEXT2);
                                        sprintf(s,"Size : %imb",(((((uint64_t)hd_new_cyl*(uint64_t)hd_new_hpc)*(uint64_t)hd_new_spt)*512)/1024)/1024);
                                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
        
                                        hd_changed = 1;
                                }
                        }
                        return TRUE;

                        case IDC_EDIT1: case IDC_EDIT2: case IDC_EDIT3:
                        h=GetDlgItem(hdlg,IDC_EDIT1);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].spt);
                        h=GetDlgItem(hdlg,IDC_EDIT2);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].hpc);
                        h=GetDlgItem(hdlg,IDC_EDIT3);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[0].tracks);

                        h=GetDlgItem(hdlg,IDC_TEXT1);
                        sprintf(s,"Size : %imb",((((hd[0].tracks*hd[0].hpc)*hd[0].spt)*512)/1024)/1024);
                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                        return TRUE;

                        case IDC_EDIT4: case IDC_EDIT5: case IDC_EDIT6:
                        h=GetDlgItem(hdlg,IDC_EDIT4);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[1].spt);
                        h=GetDlgItem(hdlg,IDC_EDIT5);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[1].hpc);
                        h=GetDlgItem(hdlg,IDC_EDIT6);
                        SendMessage(h,WM_GETTEXT,255,(LPARAM)s);
                        sscanf(s,"%i",&hd[1].tracks);

                        h=GetDlgItem(hdlg,IDC_TEXT2);
                        sprintf(s,"Size : %imb",((((hd[1].tracks*hd[1].hpc)*hd[1].spt)*512)/1024)/1024);
                        SendMessage(h,WM_SETTEXT,0,(LPARAM)s);
                        return TRUE;
                }
                break;

        }
        return FALSE;
}

BOOL CALLBACK statusdlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        int egasp;
        char s[256];
        switch (message)
        {
                case WM_INITDIALOG:
                statusopen=1;
                case WM_USER:
                sprintf(s,"CPU speed : %f MIPS",mips);
                SendDlgItemMessage(hdlg,IDC_STEXT1,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"FPU speed : %f MFLOPS",flops);
                SendDlgItemMessage(hdlg,IDC_STEXT2,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"Cache misses (read) : %i/sec",sreadlnum);
                SendDlgItemMessage(hdlg,IDC_STEXT3,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"Cache misses (write) : %i/sec",swritelnum);
                SendDlgItemMessage(hdlg,IDC_STEXT4,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"Video throughput (read) : %i bytes/sec",segareads);
                SendDlgItemMessage(hdlg,IDC_STEXT5,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"Video throughput (write) : %i bytes/sec",segawrites);
                SendDlgItemMessage(hdlg,IDC_STEXT6,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                egasp=(slowega)?egacycles:egacycles2;
                sprintf(s,"Effective clockspeed : %iHz",clockrate-(sreadlnum*memwaitstate)-(swritelnum*memwaitstate)- scycles_lost);
//                pclog("%i : %i %i %i %i : %i %i\n",clockrate,sreadlnum*memwaitstate,swritelnum*memwaitstate,segareads*egasp,segawrites*egasp,segawrites,egasp);
                SendDlgItemMessage(hdlg,IDC_STEXT7,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"Timer 0 frequency : %fHz",pit_timer0_freq());
                SendDlgItemMessage(hdlg,IDC_STEXT8,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                if (chain4) sprintf(s,"VGA chained (possibly mode 13h)");
                else        sprintf(s,"VGA unchained (possibly mode-X)");
                SendDlgItemMessage(hdlg,IDC_STEXT9,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
/*                if (!video_bpp) sprintf(s,"VGA in text mode");
                else            */sprintf(s,"VGA colour depth : %i bpp", video_bpp);
                SendDlgItemMessage(hdlg,IDC_STEXT10,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"VGA resolution : %i x %i", video_res_x, video_res_y);
                SendDlgItemMessage(hdlg,IDC_STEXT11,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"SB frequency : %i Hz",0);
                SendDlgItemMessage(hdlg,IDC_STEXT12,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                sprintf(s,"Video refresh rate : %i Hz", emu_fps);
                SendDlgItemMessage(hdlg,IDC_STEXT13,WM_SETTEXT,(WPARAM)NULL,(LPARAM)s);
                return TRUE;
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        case IDCANCEL:
                        statusopen=0;
                        EndDialog(hdlg,0);
                        return TRUE;
                }
                break;
        }
        return FALSE;
}


HHOOK hKeyboardHook;

LRESULT CALLBACK LowLevelKeyboardProc( int nCode, WPARAM wParam, LPARAM lParam )
{
    if (nCode < 0 || nCode != HC_ACTION || !mousecapture) return CallNextHookEx( hKeyboardHook, nCode, wParam, lParam); 
	
	KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

    if(p->vkCode==VK_TAB && p->flags & LLKHF_ALTDOWN) return 1; //disable alt-tab
    if(p->vkCode==VK_SPACE && p->flags & LLKHF_ALTDOWN) return 1; //disable alt-tab    
	if((p->vkCode == VK_LWIN) || (p->vkCode == VK_RWIN)) return 1;//disable windows keys
	if (p->vkCode == VK_ESCAPE && p->flags & LLKHF_ALTDOWN) return 1;//disable alt-escape
	BOOL bControlKeyDown = GetAsyncKeyState (VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1);//checks ctrl key pressed
	if (p->vkCode == VK_ESCAPE && bControlKeyDown) return 1; //disable ctrl-escape
	
	return CallNextHookEx( hKeyboardHook, nCode, wParam, lParam );
}

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
        HMENU hmenu;
        RECT rect;
//        pclog("Message %i %08X\n",message,message);
        switch (message)
        {
                case WM_CREATE:
                SetTimer(hwnd, TIMER_1SEC, 1000, NULL);
                hKeyboardHook = SetWindowsHookEx( WH_KEYBOARD_LL,  LowLevelKeyboardProc, GetModuleHandle(NULL), 0 );
                break;
                
                case WM_COMMAND:
//                        pclog("WM_COMMAND %i\n",LOWORD(wParam));
                hmenu=GetMenu(hwnd);
                switch (LOWORD(wParam))
                {
                        case IDM_FILE_RESET:
                        pause=1;
                        Sleep(100);
                        resetpc();
                        pause=0;
                        drawit=1;
                        break;
                        case IDM_FILE_HRESET:
                        pause=1;
                        Sleep(100);
                        resetpchard();
                        pause=0;
                        drawit=1;
                        break;
                        case IDM_FILE_EXIT:
                        PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
                        break;
                        case IDM_DISC_A:
                        if (!getfile(hwnd,"Disc image (*.IMG;*.IMA)\0*.IMG;*.IMA\0All files (*.*)\0*.*\0",discfns[0]))
                        {
                                savedisc(0);
                                loaddisc(0,openfilestring);
                                saveconfig();
                        }
                        break;
                        case IDM_DISC_B:
                        if (!getfile(hwnd,"Disc image (*.IMG;*.IMA)\0*.IMG;*.IMA\0All files (*.*)\0*.*\0",discfns[1]))
                        {
                                savedisc(1);
                                loaddisc(1,openfilestring);
                                saveconfig();
                        }
                        break;
                        case IDM_EJECT_A:
                        savedisc(0);
                        ejectdisc(0);
                        saveconfig();
                        break;
                        case IDM_EJECT_B:
                        savedisc(1);
                        ejectdisc(1);
                        saveconfig();
                        break;
                        case IDM_HDCONF:
                        DialogBox(hinstance,TEXT("HdConfDlg"),hwnd,hdconfdlgproc);
                        break;
                        case IDM_CONFIG:
                        DialogBox(hinstance,TEXT("ConfigureDlg"),hwnd,configdlgproc);
                        break;
                        case IDM_STATUS:
                        statushwnd=CreateDialog(hinstance,TEXT("StatusDlg"),hwnd,statusdlgproc);
                        ShowWindow(statushwnd,SW_SHOW);
                        break;
                        
                        case IDM_VID_RESIZE:
                        vid_resize=!vid_resize;
                        CheckMenuItem(hmenu, IDM_VID_RESIZE, (vid_resize)?MF_CHECKED:MF_UNCHECKED);
                        if (vid_resize) SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW|WS_VISIBLE);
                        else            SetWindowLong(hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW&~WS_SIZEBOX&~WS_THICKFRAME&~WS_MAXIMIZEBOX)|WS_VISIBLE);
                        GetWindowRect(hwnd,&rect);
                        SetWindowPos(hwnd, 0, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_FRAMECHANGED);
                        saveconfig();
                        break;
                        case IDM_KEY_ALLEGRO: case IDM_KEY_WINDOWS:
                        CheckMenuItem(hmenu,LOWORD(wParam),MF_CHECKED);
                        CheckMenuItem(hmenu,LOWORD(wParam)^1,MF_UNCHECKED);
//                        if (kb_win && LOWORD(wParam)==IDM_KEY_ALLEGRO) install_keyboard();
//                        if (!kb_win && LOWORD(wParam)==IDM_KEY_WINDOWS) remove_keyboard();
                        kb_win=LOWORD(wParam)-IDM_KEY_ALLEGRO;
                        break;
                        
                        case IDM_CDROM_DISABLED:
                        if (cdrom_enabled)
                        {
                                if (MessageBox(NULL,"This will reset PCem!\nOkay to continue?","PCem",MB_OKCANCEL) != IDOK)
                                   break;
                        }   
                        CheckMenuItem(hmenu, IDM_CDROM_REAL + cdrom_drive, MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_CDROM_DISABLED,           MF_CHECKED);
                        CheckMenuItem(hmenu, IDM_CDROM_EMPTY,              MF_UNCHECKED);
                        if (cdrom_enabled)
                        {
                                cdrom_enabled = 0;                                             
                                saveconfig();
                                resetpchard();
                        }
                        break;
                        
                        case IDM_CDROM_EMPTY:
                        if (!cdrom_enabled)
                        {
                                if (MessageBox(NULL,"This will reset PCem!\nOkay to continue?","PCem",MB_OKCANCEL) != IDOK)
                                   break;
                        }                        
                        atapi->exit();
                        ioctl_open(0);
                        CheckMenuItem(hmenu, IDM_CDROM_REAL + cdrom_drive, MF_UNCHECKED);
                        CheckMenuItem(hmenu, IDM_CDROM_DISABLED,           MF_UNCHECKED);
                        cdrom_drive=0;
                        CheckMenuItem(hmenu, IDM_CDROM_EMPTY, MF_CHECKED);
                        saveconfig();
                        if (!cdrom_enabled)
                        {
                                cdrom_enabled = 1;
                                saveconfig();
                                resetpchard();
                        }
                        break;
                        default:
                        if (LOWORD(wParam)>=IDM_CDROM_REAL && LOWORD(wParam)<(IDM_CDROM_REAL+100))
                        {
                                if (!cdrom_enabled)
                                {
                                        if (MessageBox(NULL,"This will reset PCem!\nOkay to continue?","PCem",MB_OKCANCEL) != IDOK)
                                           break;
                                }         
                                atapi->exit();
                                ioctl_open(LOWORD(wParam)-IDM_CDROM_REAL);
                                CheckMenuItem(hmenu, IDM_CDROM_REAL + cdrom_drive, MF_UNCHECKED);
                                CheckMenuItem(hmenu, IDM_CDROM_DISABLED,           MF_UNCHECKED);
                                cdrom_drive = LOWORD(wParam) - IDM_CDROM_REAL;
                                CheckMenuItem(hmenu, IDM_CDROM_REAL + cdrom_drive, MF_CHECKED);
                                saveconfig();
                                if (!cdrom_enabled)
                                {
                                        cdrom_enabled = 1;
                                        saveconfig();
                                        resetpchard();
                                }
                        }
                        break;
                }
                return 0;
                
                case WM_SETFOCUS:
                infocus=1;
                drawit=0;
 //               QueryPerformanceCounter(&counter_posold);
//                ResetEvent(frameobject);
//                restoresound();
//                pclog("Set focus!\n");
                break;
                case WM_KILLFOCUS:
                infocus=0;
                if (mousecapture)
                {
                        ClipCursor(&oldclip);
                        ShowCursor(TRUE);
                        mousecapture=0;
                }
//                silencesound();
//                pclog("Lost focus!\n");
                break;

                case WM_LBUTTONUP:
                if (!mousecapture)
                {
                        GetClipCursor(&oldclip);
                        GetWindowRect(hwnd,&pcclip);
                        pcclip.left+=GetSystemMetrics(SM_CXFIXEDFRAME)+10;
                        pcclip.right-=GetSystemMetrics(SM_CXFIXEDFRAME)+10;
                        pcclip.top+=GetSystemMetrics(SM_CXFIXEDFRAME)+GetSystemMetrics(SM_CYMENUSIZE)+GetSystemMetrics(SM_CYCAPTION)+10;
                        pcclip.bottom-=GetSystemMetrics(SM_CXFIXEDFRAME)+10;
                        ClipCursor(&pcclip);
                        mousecapture=1;
                        ShowCursor(FALSE);
                }
                break;

                case WM_ENTERMENULOOP:
//                if (key[KEY_ALT] || key[KEY_ALTGR]) return 0;
                break;

                case WM_SIZE:
                winsizex=lParam&0xFFFF;
                winsizey=lParam>>16;
//                startblit();
//                d3d_resize(winsizex, winsizey);
//                endblit();
                break;

                case WM_TIMER:
                if (wParam == TIMER_1SEC)
                   onesec();
                break;

                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP:
//                if (mousecapture)
                   return 0;
//                return DefWindowProc (hwnd, message, wParam, lParam);
                
                case WM_DESTROY:
                UnhookWindowsHookEx( hKeyboardHook );
                KillTimer(hwnd, TIMER_1SEC);
                PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
                break;
                default:
//                        pclog("Def %08X %i\n",message,message);
                return DefWindowProc (hwnd, message, wParam, lParam);
        }
        return 0;
}