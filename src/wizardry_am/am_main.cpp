/*
 *  Copyright (C) 2014 KoriTama
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#ifdef WIN32
#include <signal.h>
#include <process.h>
#endif

#include "cross.h"
#include "SDL.h"

#include "dosbox.h"
#include "video.h"
#include "mouse.h"
#include "pic.h"
#include "timer.h"
#include "setup.h"
#include "support.h"
#include "debug.h"
#include "mapper.h"
#include "vga.h"
#include "keyboard.h"
#include "cpu.h"
#include "cross.h"
#include "control.h"
#include "paging.h"

#define MAPPERFILE "mapper-" VERSION ".map"
//#define DISABLE_JOYSTICK

#if C_OPENGL
#include "SDL_opengl.h"

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#ifdef __WIN32__
#define NVIDIA_PixelDataRange 1

#ifndef WGL_NV_allocate_memory
#define WGL_NV_allocate_memory 1
typedef void * (APIENTRY * PFNWGLALLOCATEMEMORYNVPROC) (int size, float readfreq, float writefreq, float priority);
typedef void (APIENTRY * PFNWGLFREEMEMORYNVPROC) (void *pointer);
#endif

extern PFNWGLALLOCATEMEMORYNVPROC db_glAllocateMemoryNV;
extern PFNWGLFREEMEMORYNVPROC db_glFreeMemoryNV;

#else

#endif

#if defined(NVIDIA_PixelDataRange)

#ifndef GL_NV_pixel_data_range
#define GL_NV_pixel_data_range 1
#define GL_WRITE_PIXEL_DATA_RANGE_NV      0x8878
typedef void (APIENTRYP PFNGLPIXELDATARANGENVPROC) (GLenum target, GLsizei length, GLvoid *pointer);
typedef void (APIENTRYP PFNGLFLUSHPIXELDATARANGENVPROC) (GLenum target);
#endif

extern PFNGLPIXELDATARANGENVPROC glPixelDataRangeNV;

#endif

#endif //C_OPENGL

#if !(ENVIRON_INCLUDED)
extern char** environ;
#endif

#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <SDL_syswm.h>
#include <windows.h>
#if (HAVE_DDRAW_H)
#include <ddraw.h>
struct private_hwdata {
	LPDIRECTDRAWSURFACE3 dd_surface;
	LPDIRECTDRAWSURFACE3 dd_writebuf;
};
#endif
#include <commctrl.h>
#include <commdlg.h>
#define STDOUT_FILE	TEXT("stdout.txt")
#define STDERR_FILE	TEXT("stderr.txt")
#define DEFAULT_CONFIG_FILE "/dosbox.conf"
#elif defined(MACOSX)
#define DEFAULT_CONFIG_FILE "/Library/Preferences/DOSBox Preferences"
#else /*linux freebsd*/
#define DEFAULT_CONFIG_FILE "/.dosboxrc"
#endif

#if C_SET_PRIORITY
#include <sys/resource.h>
#define PRIO_TOTAL (PRIO_MAX-PRIO_MIN)
#endif

#ifdef OS2
#define INCL_DOS
#define INCL_WIN
#include <os2.h>
#endif

SDL_Window *AutoMapWindow = NULL;
Uint32 AutoMapWindowID = 0;
SDL_GLContext AutoMapContext;

enum AutoMapGame { AMG_None = 0, AMG_Wizardry6, AMG_Wizardry7 } ;
AutoMapGame amGame = AMG_None;

const char *amGameName[3] = {
	"undetected",
	"Wizardry 6",
	"Wizardry 7",
};

void DrawRectGL(int x, int y, int w, int h, int colorIndex)
{
  glPushMatrix();  //Make sure our transformations don't affect any other transformations in other code
    glTranslatef(x, y, 0);  //Translate rectangle to its assigned x and y position
    //Put other transformations here
    glBegin(GL_QUADS);   //We want to draw a quad, i.e. shape with four sides

	switch (colorIndex){
	case -1: glColor3f(0.0, 	0.0, 	0.0);break;//black
	case 0: glColor3f(1.0, 	0.0, 	0.0);break;// red	   (no feature)
	case 1: glColor3f(0.0, 	1.0, 	0.0);break;// green    (stairs up)
	case 2: glColor3f(1.0,	1.0, 	0.0);break;// yellow   (stairs down)
	case 3: glColor3f(0.0, 	0.0,	1.0);break;// blue     (sconce)
	case 4: glColor3f(1.0, 	0.0, 	1.0);break;// Magenta(purple)   (fountain)
	case 5: glColor3f(0.0,	1.0, 	1.0);break;
	case 6: glColor3f(0.25, 	0.25, 	0.25);break;
	case 7: glColor3f(0.75, 	0.75, 	0.75);break;
	case 8: glColor3f(0.60, 	0.40, 	0.12);break;
	case 9: glColor3f(0.98, 	0.625, 	0.12);break;// Pumpkin orange (niche/niche with red box/)
	case 10: glColor3f(0.98, 	.04, 	0.7);break;
	case 11: glColor3f(0.60, 	0.40, 	0.70); break;
	default: glColor3f(1.0, 	1.0, 	1.0);
	};
      glVertex2f(0, 0);            //Draw the four corners of the rectangle
      glVertex2f(0, h);
      glVertex2f(w, h);
      glVertex2f(w, 0);       
    glEnd();
  glPopMatrix();
}

void DrawTexturedRectGL(int x, int y, int w, int h, float u0, float v0, float u1, float v1, GLuint tex, bool dark = false)
{
	glBindTexture(GL_TEXTURE_2D, tex);
	glEnable(GL_TEXTURE_2D);
  glPushMatrix();  //Make sure our transformations don't affect any other transformations in other code
    glTranslatef(x, y, 0);  //Translate rectangle to its assigned x and y position
    //Put other transformations here
    glBegin(GL_QUADS);   //We want to draw a quad, i.e. shape with four sides
	  if (dark)
		glColor3f(0.5f,0.5f, 0.5f);
	  else
		glColor3f(1, 1, 1);
	  glTexCoord2f(u0, v1);
      glVertex2f(0, 0);            //Draw the four corners of the rectangle
	  glTexCoord2f(u0, v0);
      glVertex2f(0, h);
	  glTexCoord2f(u1, v0);
      glVertex2f(w, h);
	  glTexCoord2f(u1, v1);
      glVertex2f(w, 0);       
    glEnd();
  glPopMatrix();
  glDisable(GL_TEXTURE_2D);
}


GLuint texName;
TOOLINFOW g_toolItem;
HWND hAMWindow;
HWND g_hwndTrackingTT;

HWND CreateTrackingToolTip(HWND hDlg, LPWSTR pText)
{
	hAMWindow = hDlg;
    // Create a tooltip.
    HWND hwndTT = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, 
                                 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | WS_CHILD | WS_VISIBLE | SS_NOTIFY, 
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 
                                 hDlg, NULL, GetModuleHandle(NULL),NULL);

    if (!hwndTT)
    {
      return NULL;
    }

	SetWindowPos(hwndTT, HWND_TOPMOST, 0, 0, 0, 0, 
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Set up the tool information. In this case, the "tool" is the entire parent window.
    memset((void *)&g_toolItem.cbSize,0,sizeof(g_toolItem));
    g_toolItem.cbSize   = TTTOOLINFOW_V2_SIZE; //sizeof(TOOLINFO); - bad without manifest
    g_toolItem.uFlags   = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    g_toolItem.hwnd     = hDlg;
    g_toolItem.hinst    = GetModuleHandle(NULL);
    g_toolItem.lpszText = pText;
    g_toolItem.uId      = (UINT_PTR)hDlg;
	g_toolItem.lParam   = 0;
	g_toolItem.lpReserved = NULL;
    GetClientRect (hDlg, &g_toolItem.rect);

    // Associate the tooltip with the tool window.
    
    SendMessageW(hwndTT, TTM_ADDTOOLW, 0, (LPARAM) (LPTOOLINFOW) &g_toolItem);	
    SendMessageW(hwndTT, TTM_SETMAXTIPWIDTH, 0, 400);//Allow MULTILINE secial symbols like \n\r doesn't work without it
    return hwndTT;
}

TOOLINFOW g_toolItemM;
HWND hMWindow;
HWND g_hwndTrackingTTM;

HWND CreateTrackingToolTipM(HWND hDlg, LPWSTR pText)
{
	hMWindow = hDlg;
    // Create a tooltip.
    HWND hwndTT = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, 
                                 WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP | WS_CHILD | WS_VISIBLE | SS_NOTIFY, 
                                 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 
                                 hDlg, NULL, GetModuleHandle(NULL),NULL);

    if (!hwndTT)
    {
      return NULL;
    }

	SetWindowPos(hwndTT, HWND_TOPMOST, 0, 0, 0, 0, 
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Set up the tool information. In this case, the "tool" is the entire parent window.
	memset((void *)&g_toolItemM.cbSize,0,sizeof(g_toolItemM));
    g_toolItemM.cbSize   = TTTOOLINFOW_V2_SIZE; //sizeof(TOOLINFO); - bad without manifest
    g_toolItemM.uFlags   = TTF_IDISHWND | TTF_TRACK | TTF_ABSOLUTE;
    g_toolItemM.hwnd     = hDlg;
    g_toolItemM.hinst    = GetModuleHandle(NULL);
    g_toolItemM.lpszText = pText;
    g_toolItemM.uId      = (UINT_PTR)hDlg;
    
    GetClientRect (hDlg, &g_toolItemM.rect);

    // Associate the tooltip with the tool window.
    
    SendMessageW(hwndTT, TTM_ADDTOOLW, 0, (LPARAM) (LPTOOLINFOW) &g_toolItemM);	
    SendMessageW(hwndTT, TTM_SETMAXTIPWIDTH, 0, 300);//Allow MULTILINE secial symbols like \n\r doesn't work without it
    return hwndTT;
}

void AutoMapOnMainWindowCreate(HWND hMW){
	hMWindow = hMW;
   InitCommonControls();
   g_hwndTrackingTTM = CreateTrackingToolTipM(hMW, L"test tooltip");
}

void AutoMapOnMainWindowDestroy(){

}

extern unsigned long tiles32[];
extern int tiles32_count;
extern bool amw6_show_tooltips;
bool am_show_automap;
int am_height = 512;
int am_width = 512;
extern bool amw6_hide_in_dark_zones;

void AutoMapInit(Section *section){
	Section_prop * sec = static_cast<Section_prop *>(section);

	amw6_show_tooltips = sec->Get_bool("show_tooltips");
	am_show_automap = sec->Get_bool("enable");
	amw6_hide_in_dark_zones = sec->Get_bool("hide_in_dark_zones");
	am_height = sec->Get_int("height");
	am_width = sec->Get_int("width");
}

void AutoMapCreate(){
	if (!am_show_automap) return;

	SDL_GL_MakeCurrent(AutoMapWindow, AutoMapContext);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

   glGenTextures(1, &texName);
   glBindTexture(GL_TEXTURE_2D, texName);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, 
                   GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 
                   GL_NEAREST);

   for (int i = 0; i < tiles32_count; i++){
	   if (tiles32[i]!=0)
		   tiles32[i] |= 0xFF000000;
   }

   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 
                16, 0, GL_RGBA, GL_UNSIGNED_BYTE, 
                tiles32);

   InitCommonControls();
   SDL_SysWMinfo amwInfo;
   SDL_VERSION(&amwInfo.version);
   SDL_GetWindowWMInfo(AutoMapWindow, &amwInfo);
   g_hwndTrackingTT = CreateTrackingToolTip(amwInfo.info.win.window, L"test tooltip");

}

void W6_Update(int XSize, int YSize);
void SetAutomapWindowTitle(const char *title){
	SDL_SetWindowTitle(AutoMapWindow,title);
}

void AutoMapUpdate(){
	if (!am_show_automap) return;
	int XSize,YSize;
	SDL_GetWindowSize(AutoMapWindow, &XSize, &YSize);
	SDL_GL_MakeCurrent(AutoMapWindow, AutoMapContext);
	if (amGame == AMG_Wizardry6){
		W6_Update(XSize,YSize);
	} else {
		char title[100];
		sprintf(title,"%s - Automap Mod by KoriTama", amGameName[(int)amGame]);
		SetAutomapWindowTitle(title);
	}
	SDL_GL_SwapWindow(AutoMapWindow);
}

BOOL g_TrackingMouse = FALSE;
BOOL g_TrackingMouseM = FALSE;
int dragStartX;
int dragStartY;
bool dragStart = false;
int cursorState = 1;

void OnAutoMapWindowEvent(SDL_Event &event){
	switch (event.type) {
		case SDL_WINDOWEVENT:
			switch (event.window.event) {
				case SDL_WINDOWEVENT_EXPOSED:
					AutoMapUpdate();
					break;
				case SDL_WINDOWEVENT_ENTER:
					cursorState = SDL_ShowCursor(SDL_QUERY);
					SDL_ShowCursor(SDL_ENABLE);
					break;
				case SDL_WINDOWEVENT_LEAVE:
					SDL_ShowCursor(cursorState);
					SendMessage(g_hwndTrackingTT, TTM_TRACKACTIVATE, (WPARAM)FALSE, (LPARAM)&g_toolItem);
					g_TrackingMouse = FALSE;
					dragStart = false;
					break;
				case SDL_WINDOWEVENT_CLOSE:
					SDL_Quit();
					raise(SIGTERM);
					break;
			};
			break;
	};

}
BOOL show_tip_backup = -1;

void OnMainWindowEvent(SDL_Event &event){
	switch (event.type) {
		case SDL_WINDOWEVENT:
			switch (event.window.event) {
				case SDL_WINDOWEVENT_EXPOSED:
					break;
				case SDL_WINDOWEVENT_ENTER:
					break;
				case SDL_WINDOWEVENT_LEAVE:
					show_tip_backup = FALSE;
					SendMessage(g_hwndTrackingTTM, TTM_TRACKACTIVATE, (WPARAM)FALSE, (LPARAM)&g_toolItemM);
					g_TrackingMouseM = FALSE;
					break;
				case SDL_WINDOWEVENT_CLOSE:
					SDL_Quit();
					raise(SIGTERM);
					break;
			};
			break;
	};
}

void TooltipForMainWindow_Show(bool show){
	BOOL SHOW = (show) ? TRUE : FALSE;
	if (show_tip_backup!=SHOW){
		show_tip_backup = SHOW;
		SendMessageW(g_hwndTrackingTTM, TTM_TRACKACTIVATE, (WPARAM)(SHOW), (LPARAM)&g_toolItemM);
	}
}

void TooltipForMainWindow_SetText(wchar_t *text){
	g_toolItemM.lpszText = text;
	SendMessageW(g_hwndTrackingTTM, TTM_SETTOOLINFOW, 0, (LPARAM)&g_toolItemM);
}

void W6_OnMouseMotionInMainWindow(RECT rc, int newX, int newY);

void OnAutomapMouseMotionInMainWindow(SDL_MouseMotionEvent * motion){
    int newX, newY;
    if (!g_TrackingMouseM)   // The mouse has just entered the window.
    {                       // Request notification when the mouse leaves.
    
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
        tme.hwndTrack       = hMWindow;
        tme.dwFlags         = TME_LEAVE;
        
        TrackMouseEvent(&tme);

        // Activate the tooltip.
    //    SendMessage(g_hwndTrackingTTM, TTM_TRACKACTIVATE, (WPARAM)TRUE, (LPARAM)&g_toolItemM);
	//	SendMessage(g_hwndTrackingTT, TTM_ACTIVATE, true, NULL);
        
        g_TrackingMouseM = TRUE;
    }

    newX = motion->x;
    newY = motion->y;

	RECT rc;
	GetClientRect(hMWindow,&rc);

	if (amGame == AMG_Wizardry6) W6_OnMouseMotionInMainWindow(rc,newX,newY);

        // Position the tooltip. The coordinates are adjusted so that the tooltip does not overlap the mouse pointer.
        
    POINT pt = { newX, newY }; 
    ClientToScreen(hMWindow, &pt);
    SendMessageW(g_hwndTrackingTTM, TTM_TRACKPOSITION, 0, (LPARAM)MAKELONG(pt.x + 20, pt.y - 30));
}


void TooltipForAutomapWindow_Show(bool show){
	BOOL SHOW = (show) ? TRUE : FALSE;
	SendMessageW(g_hwndTrackingTT, TTM_TRACKACTIVATE, (WPARAM)(SHOW), (LPARAM)&g_toolItem);
}

void TooltipForAutomapWindow_SetText(wchar_t *text){
	g_toolItem.lpszText = text;
	SendMessageW(g_hwndTrackingTT, TTM_SETTOOLINFOW, 0, (LPARAM)&g_toolItem);
}

void W6_OnAutomapDrag(int deltaX, int deltaY);
void W6_OnMouseMotionInAutomapWindow(int newX, int newY, bool alt);

void OnAutomapMouseMotion(SDL_MouseMotionEvent * motion) {
	if (!am_show_automap) return;
	static int oldX, oldY;
    int newX, newY;
	const Uint8 *keys = SDL_GetKeyboardState(NULL);
	bool alt = (keys[SDL_SCANCODE_LALT]) || (keys[SDL_SCANCODE_RALT]);
//	SDL_SetWindowTitle(AutoMapWindow,"move");
    if (!g_TrackingMouse)   // The mouse has just entered the window.
    {                       // Request notification when the mouse leaves.
    
        TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT) };
        tme.hwndTrack       = hAMWindow;
        tme.dwFlags         = TME_LEAVE;
        
        TrackMouseEvent(&tme);

        // Activate the tooltip.
//        SendMessage(g_hwndTrackingTT, TTM_TRACKACTIVATE, (WPARAM)TRUE, (LPARAM)&g_toolItem);
//		SendMessage(g_hwndTrackingTT, TTM_ACTIVATE, (WPARAM)TRUE, NULL);
        
        g_TrackingMouse = TRUE;
    }

    newX = motion->x;
    newY = motion->y;

    // Make sure the mouse has actually moved. The presence of the tooltip 
    // causes Windows to send the message continuously.
    
	if (!alt)
    if ((newX != oldX) || (newY != oldY))
    {
		if (dragStart){
			if (amGame == AMG_Wizardry6) W6_OnAutomapDrag(oldX - newX, oldY - newY);
		}

		oldX = newX;
        oldY = newY;
    }

	if (amGame == AMG_Wizardry6) W6_OnMouseMotionInAutomapWindow(newX, newY, alt);

	// Position the tooltip. The coordinates are adjusted so that the tooltip does not overlap the mouse pointer.
        
	POINT pt = { newX, newY }; 
	ClientToScreen(hAMWindow, &pt);
	SendMessageW(g_hwndTrackingTT, TTM_TRACKPOSITION, 0, (LPARAM)MAKELONG(pt.x + 20, pt.y - 30));
}

#include "InputBox.h"
#include <fstream>
#include <string>
#include <sstream>
void W6_OnMouseButtonInAutomapWindow(SDL_MouseButtonEvent * button, bool alt, bool ctrl);

bool InputBox(const wchar_t *title, const wchar_t *hint, wchar_t *buf, int bufSize){
	CInputBox	myinp(GetModuleHandle(NULL));
	return (myinp.ShowInputBox(hAMWindow,title,hint,buf,bufSize)==IDOK);
}

bool ChooseColorDialog(Bit32u &color, Bit32u *palette){
	CHOOSECOLOR cc;
	cc.lStructSize = sizeof(CHOOSECOLOR);
	cc.hwndOwner = hAMWindow;
	cc.hInstance = (HWND)GetModuleHandle(NULL);
	cc.lpCustColors = (COLORREF*)palette;
	cc.Flags = CC_ANYCOLOR | CC_FULLOPEN | CC_RGBINIT; 
	cc.rgbResult = color;
	if (ChooseColor(&cc)==TRUE){
		color = cc.rgbResult;
		return true;
	}
	return false;
}

void OnAutomapMouseButton(SDL_MouseButtonEvent * button){
	if (!am_show_automap) return;

	const Uint8 *keys = SDL_GetKeyboardState(NULL);
	bool alt = (keys[SDL_SCANCODE_LALT]) || (keys[SDL_SCANCODE_RALT]);
	bool ctrl = (keys[SDL_SCANCODE_LCTRL]) || (keys[SDL_SCANCODE_RCTRL]);
	if ((!alt) && (!ctrl))
	if ((button->state==SDL_PRESSED) && (button->button==SDL_BUTTON_LEFT)){
		dragStartX = button->x;
		dragStartY = button->y;
		dragStart = true;
	}
	if (button->state!=SDL_RELEASED) return;
	if ((button->button==SDL_BUTTON_LEFT) && (dragStart)){
		dragStart = false;
		if ((dragStartX != button->x) || (dragStartY != button->y)) return;
	}
		
	if (amGame == AMG_Wizardry6) W6_OnMouseButtonInAutomapWindow(button, alt, ctrl);
}

void W6_NewGame();
void W6_Load();
void W6_Save();
extern Bit32u amw6_dataseg_addr;

void AutoMapDetectGame(char * name, Bit16u loadseg, Bit16u headersize){
	AutoMapGame amGameOld = amGame;
	amGame = AMG_None;
	if (strcmpi(name,"WROOT.EXE")==0) amGame = AMG_Wizardry6;

	PhysPt loadaddress=PhysMake(loadseg,0);

	if (amGame == AMG_Wizardry6){
		char wmaze[6];
		for (int i = 0; i < 6; i++){
			Bit8u c;
			mem_readb_checked(loadaddress+0x10580+i-headersize, &c);
			wmaze[i] = c;
		}
		if (strncmp(wmaze,"WMAZE",6)!=0) amGame = AMG_None;//check data seg

		amw6_dataseg_addr = loadaddress+0x10580-headersize - 0x600;
	}
}

void AutoMapOnCreateDOSFile(const char * fileName){
	if (!am_show_automap) return;
	int l = strlen(fileName);
	if (l<1) return;
	int i = -1;
	for (i = l - 1; i >= 0; i--){
		if ((fileName[i]=='\\') || (fileName[i]=='/'))
			break;
	}
	if (amGame == AMG_Wizardry6){
		if (stricmp(&fileName[i+1],"SAVEGAME.DBS")!=0) return;
		W6_Save();
		//save game -> save map
	}
}


void AutoMapOnOpenDOSFile(const char * fileName){
	if (!am_show_automap) return;
	int l = strlen(fileName);
	if (l<1) return;
	int i = -1;
	for (i = l - 1; i >= 0; i--){
		if ((fileName[i]=='\\') || (fileName[i]=='/'))
			break;
	}
	if (amGame == AMG_Wizardry6){
		if (stricmp(&fileName[i+1],"NEWGAME.DBS")==0){
			W6_NewGame();
			return;
		}
		if (stricmp(&fileName[i+1],"SAVEGAME.DBS")!=0) return;
		W6_Load();
	}
}