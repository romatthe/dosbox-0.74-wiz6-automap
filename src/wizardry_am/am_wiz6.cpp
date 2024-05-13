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

bool amw6_show_tooltips = true;
bool amw6_hide_in_dark_zones = true;
extern int am_height;
extern int am_width;
#define SCREENH am_height
#define SCREENW am_width

//each w6 map have 12 quadrants
#define W6_QUADRANT_COUNT 12
#define W6_SQUARE_SIZE 22 
#define W6_LEVEL_COUNT 16

Bit32u amw6_dataseg_addr;
Bit8u amw6_visdata[W6_LEVEL_COUNT][W6_QUADRANT_COUNT][8][8];
Bit16u amw6_level = 0;
Bit16u amw6_jLevel = 0xFFFF;
Bit8u amw6_jX = 0;
Bit8u amw6_jY = 0;
/* wizardry 6 cache */
Bit8u amw6_cache_qsx[W6_LEVEL_COUNT][W6_QUADRANT_COUNT];
Bit8u amw6_cache_qsy[W6_LEVEL_COUNT][W6_QUADRANT_COUNT];
Bit8u amw6_cache_hwalls[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64*2/8];
Bit8u amw6_cache_vwalls[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64*2/8];
Bit8u amw6_cache_features[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64*4/8];
Bit8u amw6_cache_features_dirs[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64*2/8];
Bit8u amw6_cache_floor[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64/8];
Bit8u amw6_cache_roof[W6_LEVEL_COUNT][W6_QUADRANT_COUNT*64/8];

Bit32u wiz6_palette[16];

struct AMW6Note {
	Bit16u quadrant;
	Bit16u qX;
	Bit16u qY;
	Bit32u color;
	std::wstring str;
};

std::vector<AMW6Note> amw6_notes[W6_LEVEL_COUNT];

const char *amw6MapNames[W6_LEVEL_COUNT] = {
	"Castle Entrance and Towers",
	"Castle Upper Level, Tower Stairs and Bell Tower",
	"Castle Basement and Hazard Area",
	"Mountain Area and Amazulu Burial Chamber",
	"Amazulu Pyramid",
	"Mines Area",
	"Mountain Alpes and Castle Lower Level",
	"Pyramid Basement",
	"River Styx",
	"Hall of the Dead and Tomb of the Damned",
	"Swamp Area",
	"Temple of Ramm",
	"Forest Area",
	"Lower Level of the Temple of Ramm",
	"Map 14",
	"Map 15",
};

/////////////////////////////////////////////////
// Tiles
/////////////////////////////////////////////////

extern GLuint texName;

void DrawTexturedRectGL(int x, int y, int w, int h, float u0, float v0, float u1, float v1, GLuint tex, bool dark = false);

void W6_DrawWaterTile(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,0,0,7.0f/256.0f,7.0f/16.0f,texName,dark);
}

void W6_DrawDarkTile(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,16.0f/256.0f,0,23.0f/256.0f,7.0f/16.0f,texName,dark);
}

void W6_DrawLightTile(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,32.0f/256.0f,0,39.0f/256.0f,7.0f/16.0f,texName,dark);
}

void W6_DrawHPassage(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,48.0f/256.0f,0,57.0f/256.0f,3.0f/16.0f,texName,dark);
}

void W6_DrawVPassage(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,71.0f/256.0f,0,74.0f/256.0f,9.0f/16.0f,texName,dark);
}

void W6_DrawHWall(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,80.0f/256.0f,0,89.0f/256.0f,3.0f/16.0f,texName,dark);
}

void W6_DrawVWall(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,103.0f/256.0f,0,106.0f/256.0f,9.0f/16.0f,texName,dark);
}

void W6_DrawHDoor(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,112.0f/256.0f,0,121.0f/256.0f,3.0f/16.0f,texName,dark);
}

void W6_DrawVDoor(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,135.0f/256.0f,0,138.0f/256.0f,9.0f/16.0f,texName,dark);
}

void W6_DrawHPortcullis(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,60.0f/256.0f,0,69.0f/256.0f,3.0f/16.0f,texName,dark);
}

void W6_DrawVPortcullis(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,42.0f/256.0f,0,45.0f/256.0f,9.0f/16.0f,texName,dark);
}

void W6_DrawStairsUp(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,142.0f/256.0f,13.0f/16.0f,155.0f/256.0f,0,texName,dark);
}

void W6_DrawFountainUp(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,189.0f/256.0f,15.0f/16.0f,205.0f/256.0f,0,texName,dark);
}

void W6_DrawFountainDown(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,189.0f/256.0f,0,105.0f/256.0f,15.0f/16.0f,texName,dark);
}

void W6_DrawStairsDown(int x, int y, int w, int h, bool dark = false){
	DrawTexturedRectGL(x,y,w,h,142.0f/256.0f,0,155.0f/256.0f,13.0f/16.0f,texName,dark);
}

void W6_DrawCursorUp(int x, int y, int w, int h){
	DrawTexturedRectGL(x,y,w,h,174.0f/256.0f,14.0f/16.0f,185.0f/256.0f,0,texName,false);
}

void W6_DrawCursorDown(int x, int y, int w, int h){
	DrawTexturedRectGL(x,y,w,h,174.0f/256.0f,0,185.0f/256.0f,14.0f/16.0f,texName,false);
}


void W6_DrawCursorRight(int x, int y, int w, int h){
	DrawTexturedRectGL(x,y,w,h,157.0f/256.0f,2.0f/16.0f,171.0f/256.0f,13.0f/16.0f,texName,false);
}

void W6_DrawCursorLeft(int x, int y, int w, int h){
	DrawTexturedRectGL(x,y,w,h,171.0f/256.0f,2.0f/16.0f,157.0f/256.0f,13.0f/16.0f,texName,false);
}


//////////////////////////////////////////////////////////////////////////////////////////////
///  Save / Load
//////////////////////////////////////////////////////////////////////////////////////////////

void W6_NewGame(){
	memset(wiz6_palette, 0, sizeof(wiz6_palette));
	memset(amw6_visdata, 0, sizeof(amw6_visdata));
	memset(amw6_cache_qsx, 0, sizeof(amw6_cache_qsx));
	memset(amw6_cache_qsy, 0, sizeof(amw6_cache_qsy));
	memset(amw6_cache_hwalls, 0, sizeof(amw6_cache_hwalls));
	memset(amw6_cache_vwalls, 0, sizeof(amw6_cache_vwalls));
	memset(amw6_cache_features, 0, sizeof(amw6_cache_features));
	memset(amw6_cache_features_dirs, 0, sizeof(amw6_cache_features_dirs));
	memset(amw6_cache_floor, 0, sizeof(amw6_cache_floor));
	memset(amw6_cache_roof, 0, sizeof(amw6_cache_roof));
	memset(amw6_visdata, 0, sizeof(amw6_visdata));
	for (int i = 0; i < W6_LEVEL_COUNT; i++){
		amw6_notes[i].clear();
	}
}

void W6_Load(){
	memset(wiz6_palette, 0, sizeof(wiz6_palette));
	if (DOS_FileExists("MAP.PAL")){
		Bit16u handle = 0;
		if (DOS_OpenFile("MAP.PAL",OPEN_READ,&handle)){
			Bit16u toread = sizeof(wiz6_palette);
			DOS_ReadFile(handle,(Bit8u*)&wiz6_palette[0],&toread);

			DOS_CloseFile(handle);
		}
	}

	memset(amw6_visdata, 0, sizeof(amw6_visdata));
	memset(amw6_cache_qsx, 0, sizeof(amw6_cache_qsx));
	memset(amw6_cache_qsy, 0, sizeof(amw6_cache_qsy));
	memset(amw6_cache_hwalls, 0, sizeof(amw6_cache_hwalls));
	memset(amw6_cache_vwalls, 0, sizeof(amw6_cache_vwalls));
	memset(amw6_cache_features, 0, sizeof(amw6_cache_features));
	memset(amw6_cache_features_dirs, 0, sizeof(amw6_cache_features_dirs));
	memset(amw6_cache_floor, 0, sizeof(amw6_cache_floor));
	memset(amw6_cache_roof, 0, sizeof(amw6_cache_roof));
	if (DOS_FileExists("MAP.CAC")){
		Bit16u handle = 0;
		if (DOS_OpenFile("MAP.CAC",OPEN_READ,&handle)){
			Bit16u toread = sizeof(amw6_cache_qsx);
			DOS_ReadFile(handle,&amw6_cache_qsx[0][0],&toread);

			toread = sizeof(amw6_cache_qsy);
			DOS_ReadFile(handle,&amw6_cache_qsy[0][0],&toread);

			toread = sizeof(amw6_cache_hwalls);
			DOS_ReadFile(handle,&amw6_cache_hwalls[0][0],&toread);

			toread = sizeof(amw6_cache_vwalls);
			DOS_ReadFile(handle,&amw6_cache_vwalls[0][0],&toread);

			toread = sizeof(amw6_cache_features);
			DOS_ReadFile(handle,&amw6_cache_features[0][0],&toread);

			toread = sizeof(amw6_cache_features_dirs);
			DOS_ReadFile(handle,&amw6_cache_features_dirs[0][0],&toread);

			toread = sizeof(amw6_cache_floor);
			DOS_ReadFile(handle,&amw6_cache_floor[0][0],&toread);

			toread = sizeof(amw6_cache_roof);
			DOS_ReadFile(handle,&amw6_cache_roof[0][0],&toread);

			DOS_CloseFile(handle);
		}
	}

	memset(amw6_visdata, 0, sizeof(amw6_visdata));
	if (DOS_FileExists("MAP.VIS")){
		Bit16u handle = 0;
		if (DOS_OpenFile("MAP.VIS",OPEN_READ,&handle)){
			Bit16u toread = sizeof(amw6_visdata);
			DOS_ReadFile(handle,&amw6_visdata[0][0][0][0],&toread);
			DOS_CloseFile(handle);
		}
	}

	for (int i = 0; i < W6_LEVEL_COUNT; i++){
		amw6_notes[i].clear();
	}
	if (DOS_FileExists("MAP.NTS")){
		Bit16u handle = 0;
		if (DOS_OpenFile("MAP.NTS",OPEN_READ,&handle)){
			Bit16u toread = 4;
			Bit32u dwbuf = 0;
			for (int i = 0; i < W6_LEVEL_COUNT; i++){
				toread = 4;
				Bit32u ncount = 0;
				DOS_ReadFile(handle,(Bit8u*)&ncount,&toread);
				for (int j = 0; j < ncount; j++){
					AMW6Note note;
					toread = 2;
					DOS_ReadFile(handle,(Bit8u*)&note.quadrant,&toread);
					toread = 2;
					DOS_ReadFile(handle,(Bit8u*)&note.qX,&toread);
					toread = 2;
					DOS_ReadFile(handle,(Bit8u*)&note.qY,&toread);
					toread = 4;
					DOS_ReadFile(handle,(Bit8u*)&note.color,&toread);
					toread = 4;
					dwbuf = 0;
					DOS_ReadFile(handle,(Bit8u*)&dwbuf,&toread);
					toread = (dwbuf + 1) * 2;
					note.str.resize(dwbuf);
					DOS_ReadFile(handle,(Bit8u*)&note.str[0],&toread);

					amw6_notes[i].push_back(note);
				}
			}
			DOS_CloseFile(handle);
		}
	}
}

void W6_Save(){	
	//backup
	if (DOS_FileExists("MAP.PAL")){
		if (DOS_FileExists("MAPPAL.BAK")){
			DOS_UnlinkFile("MAPPAL.BAK");
		}
		DOS_Rename("MAP.PAL","MAPPAL.BAK");
	}
	Bit16u handle = 0;
	if (DOS_CreateFile("MAP.PAL",0,&handle)){
		Bit16u towrite = sizeof(wiz6_palette);
		DOS_WriteFile(handle,(Bit8u*)&wiz6_palette[0],&towrite);
		DOS_FlushFile(handle);
		DOS_CloseFile(handle);
	}
	//backup
	if (DOS_FileExists("MAP.VIS")){
		if (DOS_FileExists("MAPVIS.BAK")){
			DOS_UnlinkFile("MAPVIS.BAK");
		}
		DOS_Rename("MAP.VIS","MAPVIS.BAK");
	}
	handle = 0;
	if (DOS_CreateFile("MAP.VIS",0,&handle)){
		Bit16u towrite = sizeof(amw6_visdata);
		DOS_WriteFile(handle,&amw6_visdata[0][0][0][0],&towrite);
		DOS_FlushFile(handle);
		DOS_CloseFile(handle);
	}
	//backup
	if (DOS_FileExists("MAP.NTS")){
		if (DOS_FileExists("MAPNTS.BAK")){
			DOS_UnlinkFile("MAPNTS.BAK");
		}
		DOS_Rename("MAP.NTS","MAPNTS.BAK");
	}
	handle = 0;
	if (DOS_CreateFile("MAP.NTS",0,&handle)){
		Bit16u towrite = 4;
		Bit32u dwbuf = 0;
		for (int i = 0; i < W6_LEVEL_COUNT; i++){
			towrite = 4;
			dwbuf = amw6_notes[i].size();
			DOS_WriteFile(handle,(Bit8u*)&dwbuf,&towrite);
			for (int j = 0; j < amw6_notes[i].size(); j++){
				towrite = 2;
				DOS_WriteFile(handle,(Bit8u*)&amw6_notes[i][j].quadrant,&towrite);
				towrite = 2;
				DOS_WriteFile(handle,(Bit8u*)&amw6_notes[i][j].qX,&towrite);
				towrite = 2;
				DOS_WriteFile(handle,(Bit8u*)&amw6_notes[i][j].qY,&towrite);
				towrite = 4;
				DOS_WriteFile(handle,(Bit8u*)&amw6_notes[i][j].color,&towrite);
				towrite = 4;
				dwbuf = amw6_notes[i][j].str.length();
				DOS_WriteFile(handle,(Bit8u*)&dwbuf,&towrite);
				towrite = (dwbuf + 1) * 2;
				DOS_WriteFile(handle,(Bit8u*)&amw6_notes[i][j].str[0],&towrite);
			}
		}
		DOS_FlushFile(handle);
		DOS_CloseFile(handle);
	}
	//backup
	if (DOS_FileExists("MAP.CAC")){
		if (DOS_FileExists("MAPCAC.BAK")){
			DOS_UnlinkFile("MAPCAC.BAK");
		}
		DOS_Rename("MAP.CAC","MAPCAC.BAK");
	}
	handle = 0;
	if (DOS_CreateFile("MAP.CAC",0,&handle)){
		Bit16u towrite = sizeof(amw6_cache_qsx);
		DOS_WriteFile(handle,&amw6_cache_qsx[0][0],&towrite);

		towrite = sizeof(amw6_cache_qsy);
		DOS_WriteFile(handle,&amw6_cache_qsy[0][0],&towrite);

		towrite = sizeof(amw6_cache_hwalls);
		DOS_WriteFile(handle,&amw6_cache_hwalls[0][0],&towrite);

		towrite = sizeof(amw6_cache_vwalls);
		DOS_WriteFile(handle,&amw6_cache_vwalls[0][0],&towrite);

		towrite = sizeof(amw6_cache_features);
		DOS_WriteFile(handle,&amw6_cache_features[0][0],&towrite);

		towrite = sizeof(amw6_cache_features_dirs);
		DOS_WriteFile(handle,&amw6_cache_features_dirs[0][0],&towrite);

		towrite = sizeof(amw6_cache_floor);
		DOS_WriteFile(handle,&amw6_cache_floor[0][0],&towrite);

		towrite = sizeof(amw6_cache_roof);
		DOS_WriteFile(handle,&amw6_cache_roof[0][0],&towrite);

		DOS_FlushFile(handle);
		DOS_CloseFile(handle);
	}
}



////////////////////////////////////////////////////////////////////////////////////////////////////
////////        DIRECT                                                      
////////////////////////////////////////////////////////////////////////////////////////////////////

Bit16u W6_GetBitArrayElement(Bit16u arrayOfs, Bit16u index, Bit16u bitsPerElement){
	Bit16u ret;
	Bit16u elemOfs = index * bitsPerElement;
	Bit16u elemShift = elemOfs % 8;
	Bit16u elemMask = 0xFFFF;
	elemMask >>= 16 - bitsPerElement;
	elemOfs /= 8;
	mem_readw_checked(amw6_dataseg_addr + arrayOfs + elemOfs, &ret);
	ret >>= elemShift;
	ret &= elemMask;
	return ret;
}

bool W6_TestBitArray(Bit16u arrayOfs, Bit16u index){
	Bit16u ret;
	Bit16u elemOfs = index;
	Bit16u elemShift = elemOfs % 8;
	Bit16u elemMask = 1;
	elemMask <<= elemShift;
	elemOfs /= 8;
	mem_readw_checked(amw6_dataseg_addr + arrayOfs + elemOfs, &ret);
	return ((ret & elemMask)!=0);
}

bool W6_PointInQuadrant(Bit16u x, Bit16u y, Bit16u quadrant){
	Bit16u mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

	Bit8u qsx,qsy;
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &qsx);
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &qsy);
	if ((x>=qsx) && (y>=qsy) && (x<=qsx+7) && (y<=qsy+7))
		return true;
	return false;
}

bool W6_PointAtLeftSideOfQuadrant(Bit16u x, Bit16u y, Bit16u quadrant){
	Bit16u mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

	Bit8u qsx,qsy;
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &qsx);
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &qsy);
	if ((x==qsx) && (y>=qsy) && (y<=qsy+7))
		return true;
	return false;
}

bool W6_PointAtBottomSideOfQuadrant(Bit16u x, Bit16u y, Bit16u quadrant){
	Bit16u mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

	Bit8u qsx,qsy;
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &qsx);
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &qsy);
	if ((x>=qsx) && (x<=qsx+7) && (y==qsy+7))
		return true;
	return false;
}

bool W6_PointInAnyQuadrant(Bit16u x, Bit16u y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6_PointInQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6_PointAtLeftSideOfAnyQuadrant(Bit16u x, Bit16u y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6_PointAtLeftSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6_PointAtBottomSideOfAnyQuadrant(Bit16u x, Bit16u y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6_PointAtBottomSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6_AbsToQuadrant(int x, int y, Bit16u &quadrant, Bit16u &qx, Bit16u &qy){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6_PointInQuadrant(x, y, q)){
			Bit16u mapdataOfs;
			mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

			Bit8u qsx,qsy;
			mem_readb_checked(amw6_dataseg_addr + mapdataOfs + q + 0x1E0, &qsx);
			mem_readb_checked(amw6_dataseg_addr + mapdataOfs + q + 0x1EC, &qsy);


			quadrant = q;
			qx = x - qsx;
			qy = y - qsy;

			return true;
		}
	}
	return false;
}

bool W6_TopIsVisible(Bit16u x, Bit16u y){
	Bit16u tq,tqx,tqy;
	if (!W6_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;

	Bit16u mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);
	mapdataOfs += 0x60;//horizontal walls map
	Bit16u q = W6_GetBitArrayElement(mapdataOfs,tq*64 + tqy*8 + tqx, 2);
	return (q<2);
}

bool W6_BottomIsVisible(Bit16u x, Bit16u y){
	return W6_TopIsVisible(x,y-1);
}

bool W6_RightIsVisible(Bit16u x, Bit16u y){
	Bit16u tq,tqx,tqy;
	if (!W6_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;

	Bit16u mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);
	mapdataOfs += 0x120;//vertical walls map
	Bit16u q = W6_GetBitArrayElement(mapdataOfs,tq*64 + tqy*8 + tqx, 2);
	return (q<2);
}

bool W6_LeftIsVisible(Bit16u x, Bit16u y){
	return W6_RightIsVisible(x-1,y);
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////        CACHE                                                      
////////////////////////////////////////////////////////////////////////////////////////////////////

Bit16u W6C_GetBitArrayElement(Bit8u *pArray, Bit16u index, Bit16u bitsPerElement){
	Bit16u ret;
	Bit16u elemOfs = index * bitsPerElement;
	Bit16u elemShift = elemOfs % 8;
	Bit16u elemMask = 0xFFFF;
	elemMask >>= 16 - bitsPerElement;
	elemOfs /= 8;
	ret = *((Bit16u*)(pArray + elemOfs));
	ret >>= elemShift;
	ret &= elemMask;
	return ret;
}

bool W6C_TestBitArray(Bit8u *pArray, Bit16u index){
	Bit16u ret;
	Bit16u elemOfs = index;
	Bit16u elemShift = elemOfs % 8;
	Bit16u elemMask = 1;
	elemMask <<= elemShift;
	elemOfs /= 8;
	ret = *((Bit16u*)(pArray + elemOfs));
	return ((ret & elemMask)!=0);
}

bool W6C_PointInQuadrant(Bit16u x, Bit16u y, Bit16u quadrant){
	Bit8u qsx,qsy;
	qsx = amw6_cache_qsx[amw6_level][quadrant];
	qsy = amw6_cache_qsy[amw6_level][quadrant];
	if ((x>=qsx) && (y>=qsy) && (x<=qsx+7) && (y<=qsy+7))
		return true;
	return false;
}

bool W6C_PointAtLeftSideOfQuadrant(Bit16u x, Bit16u y, Bit16u quadrant){
	Bit8u qsx,qsy;
	qsx = amw6_cache_qsx[amw6_level][quadrant];
	qsy = amw6_cache_qsy[amw6_level][quadrant];
	if ((x==qsx) && (y>=qsy) && (y<=qsy+7))
		return true;
	return false;
}

bool W6C_PointAtBottomSideOfQuadrant(Bit16u x, Bit16u y, Bit16u quadrant){
	Bit8u qsx,qsy;
	qsx = amw6_cache_qsx[amw6_level][quadrant];
	qsy = amw6_cache_qsy[amw6_level][quadrant];
	if ((x>=qsx) && (x<=qsx+7) && (y==qsy+7))
		return true;
	return false;
}

bool W6C_PointInAnyQuadrant(Bit16u x, Bit16u y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6C_PointInQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6C_PointAtLeftSideOfAnyQuadrant(Bit16u x, Bit16u y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6C_PointAtLeftSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6C_PointAtBottomSideOfAnyQuadrant(Bit16u x, Bit16u y){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6C_PointAtBottomSideOfQuadrant(x, y, q))
			return true;
	}
	return false;
}

bool W6C_AbsToQuadrant(int x, int y, Bit16u &quadrant, Bit16u &qx, Bit16u &qy){
	for (int q = 0; q < W6_QUADRANT_COUNT; q++){
		if (W6C_PointInQuadrant(x, y, q)){
			Bit8u qsx,qsy;
			qsx = amw6_cache_qsx[amw6_level][q];
			qsy = amw6_cache_qsy[amw6_level][q];


			quadrant = q;
			qx = x - qsx;
			qy = y - qsy;

			return true;
		}
	}
	return false;
}

bool W6C_TopIsVisible(Bit16u x, Bit16u y){
	Bit16u tq,tqx,tqy;
	if (!W6C_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;

	Bit16u q = W6C_GetBitArrayElement(amw6_cache_hwalls[amw6_level],tq*64 + tqy*8 + tqx, 2);
	return (q<2);
}

bool W6C_BottomIsVisible(Bit16u x, Bit16u y){
	return W6C_TopIsVisible(x,y-1);
}

bool W6C_RightIsVisible(Bit16u x, Bit16u y){
	Bit16u tq,tqx,tqy;
	if (!W6C_AbsToQuadrant(x,y,tq,tqx,tqy)) return false;


	Bit16u q = W6C_GetBitArrayElement(amw6_cache_vwalls[amw6_level],tq*64 + tqy*8 + tqx, 2);
	return (q<2);
}

bool W6C_LeftIsVisible(Bit16u x, Bit16u y){
	return W6C_RightIsVisible(x-1,y);
}

bool W6C_IsDarkZone(Bit16u quadrant, Bit16u qx, Bit16u qy){
	Bit16u f = W6C_GetBitArrayElement(&amw6_cache_features[amw6_level][0],quadrant*64 + qy*8 + qx,4);
	Bit16u q = W6C_GetBitArrayElement(amw6_cache_floor[amw6_level],quadrant*64 + qy*8 + qx,1);
	bool isDarkZone =  (!((q==0) && (f!=14 /*Pit*/))) && ((amw6_level==12) || (amw6_level==5));
	return isDarkZone && amw6_hide_in_dark_zones;
}

int W6_FindNote(Bit16u quadrant, Bit16u qx, Bit16u qy){
	for (int i = 0; i < amw6_notes[amw6_level].size(); i++){
		if ((amw6_notes[amw6_level][i].quadrant==quadrant) && 
			(amw6_notes[amw6_level][i].qX==qx) &&
			(amw6_notes[amw6_level][i].qY==qy))
			return i;
	}
	return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////
/// Rendering
/////////////////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////
//	amw6_dataseg_addr + 0x4F9C = Current Quadrant
//	amw6_dataseg_addr + 0x4FA0 = Current Quadrant X
//	amw6_dataseg_addr + 0x4F9E = Current Quadrant Y
//	amw6_dataseg_addr + 0x363C = Current Level Index
//  amw6_dataseg_addr + 0x4EE8 = quadIndex0 * W6_QUADRANT_COUNT + quadIndex1
//////////////////////////////////////////////////////
//  amw6_dataseg_addr + 0x4FA8    =	QMAP DATA OFFSET
//  qmapdataOfs + 0x0 =   ?? 16 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x120 = ?? 16 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x240 = ?? 16 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]    //Bit Packed: quadrantIndex, qX, qY
//  qmapdataOfs + 0x360 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x3F0 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x480 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x510 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x5A0 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x630 = ?? 8 bit array[W6_QUADRANT_COUNT][W6_QUADRANT_COUNT]
//  qmapdataOfs + 0x6C0 = ?? 8 bit array[W6_QUADRANT_COUNT]
//////////////////////////////////////////////////////
//  amw6_dataseg_addr + 0x4FAA    = MAP DATA OFFSET
//  mapdataOfs + 0x60  = horizontal walls map
//  mapdataOfs + 0x120 = vertical walls map
//  mapdataOfs + 0x1E0 = quadrant start X
//  mapdataOfs + 0x1EC = quadrant start Y
//  mapdataOfs + 0x1F8 = "feature" map
//  mapdataOfs + 0x378 = "feature direction" map
//  mapdataOfs + 0x43A = floor map
//  mapdataOfs + 0x49A = roof map
//  mapdataOfs + 0x4FA = ?? 3 bit array
//  mapdataOfs + 0x512 = ?? 3 bit array
//  mapdataOfs + 0x52A = ?? 4 bit array[W6_QUADRANT_COUNT]
//  mapdataOfs + 0x536 = ?? 8 bit array[W6_QUADRANT_COUNT]
//////////////////////////////////////////////////////

void W6_DrawQuadrantFirstPass(int x, int y, Bit16u quadrant){
	Bit8u cqsx,cqsy;
	cqsx = amw6_cache_qsx[amw6_level][quadrant];
	cqsy = amw6_cache_qsy[amw6_level][quadrant];

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			Bit16u f = W6C_GetBitArrayElement(&amw6_cache_features[amw6_level][0],quadrant*64 + i*8 + j,4);
			Bit16u q = W6C_GetBitArrayElement(amw6_cache_floor[amw6_level],quadrant*64 + i*8 + j,1);
			Bit16u qx =  W6_SQUARE_SIZE*j;
			Bit16u qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			if ((q==0) && (f!=14 /*Pit*/))
				W6_DrawDarkTile(x + qx+1,y + qy+1,W6_SQUARE_SIZE-1,W6_SQUARE_SIZE-1,(amw6_visdata[amw6_level][quadrant][j][i]!=1));
		}
	}
	//water map = floor map & roof map
	if ((amw6_level==8) || (amw6_level==10) || (amw6_level==12))
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			Bit16u r = W6C_GetBitArrayElement(amw6_cache_roof[amw6_level],quadrant*64 + i*8 + j,1);
			Bit16u q = W6C_GetBitArrayElement(amw6_cache_floor[amw6_level],quadrant*64 + i*8 + j,1);
			Bit16u qx =  W6_SQUARE_SIZE*j;
			Bit16u qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			if ((q!=0) && ((r!=0) || (amw6_level==8)))
				W6_DrawWaterTile(x + qx+1,y + qy+1,W6_SQUARE_SIZE-1,W6_SQUARE_SIZE-1,(amw6_visdata[amw6_level][quadrant][j][i]!=1));
		}
	}
}

void W6_DrawQuadrantSecondPass(int x, int y, Bit16u quadrant){
	Bit8u cqsx,cqsy;
	cqsx = amw6_cache_qsx[amw6_level][quadrant];
	cqsy = amw6_cache_qsy[amw6_level][quadrant];

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			Bit16u tq,tqx,tqy;
			bool v = false;
			bool d = (amw6_visdata[amw6_level][quadrant][j][i]!=1);
			if (W6C_AbsToQuadrant(cqsx + j + 1,cqsy + i,tq,tqx,tqy)){
				v = (amw6_visdata[amw6_level][tq][tqx][tqy]!=0);
				d = ((amw6_visdata[amw6_level][tq][tqx][tqy]!=1) && d);
			}
			if ((!v) && amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			Bit16u q = W6C_GetBitArrayElement(amw6_cache_vwalls[amw6_level],quadrant*64 + i*8 + j,2);
			//q:
			//   0 - empty (no wall)
			//   1 - passage 
			//   2 - solid wall
			//   3 - horizontal wood door
			Bit16u qx =  W6_SQUARE_SIZE + W6_SQUARE_SIZE*j;
			Bit16u qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			switch (q){
			case 0://skip
				break;
			case 1:	W6_DrawVPassage(x + qx,y + qy,3,W6_SQUARE_SIZE+2,d);
				break;
			case 2:
				{
					if ((W6C_GetBitArrayElement(amw6_cache_features[amw6_level],quadrant*64 + i*8 + j,4)==7) && (W6C_GetBitArrayElement(amw6_cache_features_dirs[amw6_level],quadrant*64 + i*8 + j,2)==1))
						W6_DrawVPortcullis(x + qx, y + qy,3,W6_SQUARE_SIZE+2,d);
					else
						W6_DrawVWall(x + qx, y + qy,3,W6_SQUARE_SIZE+2,d);
				}
				break;
			case 3: W6_DrawVDoor(x + qx, y + qy,3,W6_SQUARE_SIZE+2,d);
				break;
			};
		}
	}

	for (int i = 0; i < 8; i++){
		if (amw6_visdata[amw6_level][quadrant][0][i]!=0)
		if (!W6C_PointAtLeftSideOfAnyQuadrant(cqsx-8,cqsy+i)){
			Bit16u qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			W6_DrawVWall(x, y + qy,3,W6_SQUARE_SIZE+2,(amw6_visdata[amw6_level][quadrant][0][i]!=1));
		}

		if (amw6_visdata[amw6_level][quadrant][i][0]!=0)
		if (!W6C_PointInAnyQuadrant(cqsx+i,cqsy-1)){
			Bit16u qx = W6_SQUARE_SIZE*i;
			W6_DrawHWall(x + qx, y + W6_SQUARE_SIZE*8,W6_SQUARE_SIZE+2,3,(amw6_visdata[amw6_level][quadrant][i][0]!=1));
		}
	}

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			Bit16u tq,tqx,tqy;
			bool v = false;
			bool d = (amw6_visdata[amw6_level][quadrant][j][i]!=1);
			if (W6C_AbsToQuadrant(cqsx + j,cqsy + i + 1,tq,tqx,tqy)){
				v = (amw6_visdata[amw6_level][tq][tqx][tqy]!=0);
				d = ((amw6_visdata[amw6_level][tq][tqx][tqy]!=1) && d);
			}
			if ((!v) && (amw6_visdata[amw6_level][quadrant][j][i]==0)) continue;
			Bit16u q = W6C_GetBitArrayElement(amw6_cache_hwalls[amw6_level],quadrant*64 + i*8 + j,2);
			//q:
			//   0 - empty (no wall)
			//   1 - passage
			//   2 - solid wall
			//   3 - horizontal wood door
			Bit16u qx =  W6_SQUARE_SIZE*j;
			Bit16u qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			switch (q){
			case 0://skip
				break;
			case 1:	W6_DrawHPassage(x + qx,y + qy,W6_SQUARE_SIZE+2,3,d);
				break;
			case 2: 
				{
					if ((W6C_GetBitArrayElement(amw6_cache_features[amw6_level],quadrant*64 + i*8 + j,4)==7) && (W6C_GetBitArrayElement(amw6_cache_features_dirs[amw6_level],quadrant*64 + i*8 + j,2)==0))
						W6_DrawHPortcullis(x + qx, y + qy,W6_SQUARE_SIZE+2,3,d);
					else
						W6_DrawHWall(x + qx, y + qy,W6_SQUARE_SIZE+2,3,d);
				}
				break;
			case 3: W6_DrawHDoor(x + qx, y + qy,W6_SQUARE_SIZE+2,3,d);
				break;
			};
		}
	}

}

void W6_DrawQuadrantThirdPass(int x, int y, Bit16u quadrant){
	//stairs
	static const int direction[4][2] = {
		{0,-W6_SQUARE_SIZE},
		{W6_SQUARE_SIZE,0},
		{0,W6_SQUARE_SIZE},
		{-W6_SQUARE_SIZE,0},
	};
	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			Bit16u q = W6C_GetBitArrayElement(amw6_cache_features[amw6_level],quadrant*64 + i*8 + j,4);
			Bit16u d = W6C_GetBitArrayElement(amw6_cache_features_dirs[amw6_level],quadrant*64 + i*8 + j,2);
			Bit16u qx =  W6_SQUARE_SIZE*j;
			Bit16u qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			switch (q){
			case 1: W6_DrawStairsUp(x + qx+3 + direction[d][0], y + qy+3 + direction[d][1],W6_SQUARE_SIZE-3,W6_SQUARE_SIZE-3,(amw6_visdata[amw6_level][quadrant][j][i]!=1)); break;
			case 2: W6_DrawStairsDown(x + qx+3 + direction[d][0], y + qy+3 + direction[d][1],W6_SQUARE_SIZE-3,W6_SQUARE_SIZE-3,(amw6_visdata[amw6_level][quadrant][j][i]!=1)); break;
			};
		}
	}

	for (int i = 0; i < 8; i++){
		for (int j = 0; j < 8; j++){
			if (amw6_visdata[amw6_level][quadrant][j][i]==0) continue;
			Bit16u q = W6C_GetBitArrayElement(&amw6_cache_features[amw6_level][0],quadrant*64 + i*8 + j,4);
			Bit16u d = W6C_GetBitArrayElement(&amw6_cache_features_dirs[amw6_level][0],quadrant*64 + i*8 + j,2);
			Bit16u qx =  W6_SQUARE_SIZE*j;
			Bit16u qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*i;
			switch (q){
			//Fountain
			case 4: W6_DrawFountainUp(x + qx+3, y + qy+3,W6_SQUARE_SIZE-5,W6_SQUARE_SIZE-5,(amw6_visdata[amw6_level][quadrant][j][i]!=1)); break;
			//Pit Down
			//case 14: DrawRectGL(x + qx, y + qy,W6_SQUARE_SIZE,W6_SQUARE_SIZE,0); break;
			};
		}
	}
}

void W6_DrawQuadrantFourthPass(int x, int y, Bit16u quadrant, Bit16u qX, Bit16u qY, bool showMark){
	
	for (int i = 0; i < amw6_notes[amw6_level].size(); i++){
		if (amw6_notes[amw6_level][i].quadrant==quadrant){
			Bit16u qx = x + W6_SQUARE_SIZE*amw6_notes[amw6_level][i].qX;
			Bit16u qy = y + (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*amw6_notes[amw6_level][i].qY;
			float w = W6_SQUARE_SIZE-6;
			float h = W6_SQUARE_SIZE-7;

			glPushMatrix();  //Make sure our transformations don't affect any other transformations in other code
			glTranslatef(qx+5, qy+5, 0);  //Translate rectangle to its assigned x and y position
			//Put other transformations here
			glLineWidth(3.0f);
			glBegin(GL_LINES);  
				Bit32u cl = amw6_notes[amw6_level][i].color;
				glColor3f((cl & 0xFF)/255.0f, 	((cl >> 8) & 0xFF)/255.0f, 	((cl >> 16) & 0xFF)/255.0f);
				glVertex2f(0, 0);           
				glVertex2f(0, h);

				glVertex2f(0, h);
				glVertex2f(w, h);

				glVertex2f(w, h);
				glVertex2f(w, 0);       

				glVertex2f(w, 0);
				glVertex2f(0, 0);
			glEnd();
			glPopMatrix();
		}
	}

	Bit16u amw6level = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);

	if ((showMark) && (amw6level==amw6_level) && (!W6C_IsDarkZone(quadrant,qX,qY))){
		Bit16u mdir = 0;
		mem_readw_checked(amw6_dataseg_addr + 0x4F9A, &mdir);

		glEnable(GL_BLEND); 
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		Bit16u qx = W6_SQUARE_SIZE*qX;
		Bit16u qy = (W6_SQUARE_SIZE*7) - W6_SQUARE_SIZE*qY;
		Bit16u tsize = W6_SQUARE_SIZE;
		switch (mdir){
			case 0: W6_DrawCursorUp(x+qx,y+qy,tsize,tsize); break;
			case 1: W6_DrawCursorRight(x+qx,y+qy,tsize,tsize); break;
			case 3: W6_DrawCursorLeft(x+qx,y+qy,tsize,tsize); break;
			default:
				W6_DrawCursorDown(x+qx,y+qy,tsize,tsize);
		};
		glDisable(GL_BLEND); 
//		DrawRectGL(x+qx+(W6_SQUARE_SIZE/4),y+qy+(W6_SQUARE_SIZE/4),tsize,tsize,-1);
	}
}

int map_draw_x = 0;
int map_draw_y = 0;
int map_scroll_x = 0;
int map_scroll_y = 0;
bool amW6inGame = false;

void SetAutomapWindowTitle(const char *title);
void AutoMapUpdate();
void W6_Update(int XSize, int YSize){
	Bit16u quadrant = 0;
	Bit16u qx = 0;
	Bit16u qy = 0;
	Bit16u amw6level = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9C, &quadrant);
	mem_readw_checked(amw6_dataseg_addr + 0x4FA0, &qx);
	mem_readw_checked(amw6_dataseg_addr + 0x4F9E, &qy);
	mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
	//----------------Detect in-game cond---------------------
	Bit16u W6GameState = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x363A, &W6GameState);
	amW6inGame = (W6GameState == 5) || ((W6GameState>=10) && (W6GameState<=14)) || (W6GameState==19) || (W6GameState==20) || (W6GameState==22) || (W6GameState==24);
	if (amW6inGame){
		if (amw6level>=16){
			amW6inGame = false;
		} 
	}
	//-----------------------------------------------------------
	Bit16u mdir = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9A, &mdir);
	if (amW6inGame){
		static Bit16u oquadrant = 0;
		static Bit16u oqx = 0;
		static Bit16u oqy = 0;
		static Bit16u oamw6level = 0;
		static Bit16u omdir = 0;
		if ((oquadrant != quadrant) ||
			(oqx != qx) ||
			(oqy != qy) ||
			(oamw6level != amw6level) ||
			(omdir != mdir)){
				amw6_level = amw6level;
				//Reset scroll
				map_scroll_x = 0;
				map_scroll_y = 0;
				//update cache
				Bit16u mapdataOfs;
				mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x1E0, &amw6_cache_qsx[amw6level][0], sizeof(amw6_cache_qsx)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x1EC, &amw6_cache_qsy[amw6level][0], sizeof(amw6_cache_qsy)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x60, &amw6_cache_hwalls[amw6level][0], sizeof(amw6_cache_hwalls)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x120, &amw6_cache_vwalls[amw6level][0], sizeof(amw6_cache_vwalls)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x1F8, &amw6_cache_features[amw6level][0], sizeof(amw6_cache_features)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x378, &amw6_cache_features_dirs[amw6level][0], sizeof(amw6_cache_features_dirs)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x43A, &amw6_cache_floor[amw6level][0], sizeof(amw6_cache_floor)/W6_LEVEL_COUNT);
				MEM_BlockRead(amw6_dataseg_addr + mapdataOfs + 0x49A, &amw6_cache_roof[amw6level][0], sizeof(amw6_cache_roof)/W6_LEVEL_COUNT);
		}

		oquadrant = quadrant;
		oqx = qx;
		oqy = qy;
		oamw6level = amw6level;
		omdir = mdir;
	}
	if (amW6inGame)
	if (amw6level==amw6_level) 
	if ((quadrant<W6_QUADRANT_COUNT) && (qx<8) && (qy<8)){
		

		if (!W6C_IsDarkZone(quadrant,qx,qy))
			amw6_visdata[amw6_level][quadrant][qx][qy] = 1;

		Bit16u mapdataOfs;
		mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

		Bit8u qsx,qsy;
		mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &qsx);
		mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &qsy);

		Bit16u hvq,hvqx,hvqy;
		if ((W6_AbsToQuadrant(qsx+qx,  qsy+qy-1,hvq,hvqx,hvqy)) && (amw6_visdata[amw6_level][hvq][hvqx][hvqy]==0)){
			if (W6_BottomIsVisible(qsx+qx,  qsy+qy))
				if (!W6C_IsDarkZone(hvq,hvqx,hvqy))
					amw6_visdata[amw6_level][hvq][hvqx][hvqy] = 2;
		}
		if ((W6_AbsToQuadrant(qsx+qx-1,qsy+qy,hvq,hvqx,hvqy)) && (amw6_visdata[amw6_level][hvq][hvqx][hvqy]==0)){
			if (W6_LeftIsVisible(qsx+qx,  qsy+qy))
				if (!W6C_IsDarkZone(hvq,hvqx,hvqy))
					amw6_visdata[amw6_level][hvq][hvqx][hvqy] = 2;
		}
		if ((W6_AbsToQuadrant(qsx+qx+1,qsy+qy,hvq,hvqx,hvqy)) && (amw6_visdata[amw6_level][hvq][hvqx][hvqy]==0)){
			if (W6_RightIsVisible(qsx+qx,  qsy+qy))
				if (!W6C_IsDarkZone(hvq,hvqx,hvqy))
					amw6_visdata[amw6_level][hvq][hvqx][hvqy] = 2;
		}
		if ((W6_AbsToQuadrant(qsx+qx,  qsy+qy+1,hvq,hvqx,hvqy)) && (amw6_visdata[amw6_level][hvq][hvqx][hvqy]==0)){
			if (W6_TopIsVisible(qsx+qx,  qsy+qy))
				if (!W6C_IsDarkZone(hvq,hvqx,hvqy))
					amw6_visdata[amw6_level][hvq][hvqx][hvqy] = 2;	
		}
	}

	char title[200]={0};
	if (amW6inGame){
		sprintf(title,"Wizardry 6 - Map:[%d]%s - Automap Mod by KoriTama", amw6_level, amw6MapNames[amw6_level]);
	} else {
		sprintf(title,"Wizardry 6 - Automap Mod by KoriTama");
	}
	SetAutomapWindowTitle(title);
					

					glMatrixMode (GL_PROJECTION);
					glLoadIdentity();
					glOrtho(0, XSize, YSize, 0, 0, 1);
					glMatrixMode(GL_MODELVIEW);
					glLoadIdentity();
					glClearColor (0.0, 0.0, 0.0, 1.0);
					glShadeModel (GL_FLAT);
					glDisable (GL_DEPTH_TEST);
					glDisable (GL_LIGHTING);
					glDisable(GL_CULL_FACE);
					glClear(GL_COLOR_BUFFER_BIT);
			//		glEnable(GL_BLEND); 
			//		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

					//DrawRectGL(5,5,25,25);


					
//					DrawTexturedRectGL(5,50,256,16,0,0,1,1,texName);
				
	Bit16u mapdataOfs;
	mem_readw_checked(amw6_dataseg_addr + 0x4FAA, &mapdataOfs);

	Bit8u tlcqx,tlcqy;
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1E0, &tlcqx);
	mem_readb_checked(amw6_dataseg_addr + mapdataOfs + quadrant + 0x1EC, &tlcqy);

	map_draw_x = -(((tlcqx+qx)*W6_SQUARE_SIZE) - ((SCREENW/2)-(W6_SQUARE_SIZE/2)));
	map_draw_y = -(((255+8)*W6_SQUARE_SIZE - (tlcqy+qy)*W6_SQUARE_SIZE) - ((SCREENH/2)-(W6_SQUARE_SIZE/2)));
	
	if (amW6inGame){
		if (!W6C_IsDarkZone(quadrant,qx,qy)){
			for (int q = 0; q < W6_QUADRANT_COUNT; q++){
				Bit8u tlqx,tlqy;
				tlqx = amw6_cache_qsx[amw6_level][q];
				tlqy = amw6_cache_qsy[amw6_level][q];
				W6_DrawQuadrantFirstPass(map_draw_x + map_scroll_x + tlqx*W6_SQUARE_SIZE, map_draw_y + map_scroll_y+ 255*W6_SQUARE_SIZE - tlqy*W6_SQUARE_SIZE, q);
			}

			for (int q = 0; q < W6_QUADRANT_COUNT; q++){
				Bit8u tlqx,tlqy;
				tlqx = amw6_cache_qsx[amw6_level][q];
				tlqy = amw6_cache_qsy[amw6_level][q];
				W6_DrawQuadrantSecondPass(map_draw_x + map_scroll_x + tlqx*W6_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W6_SQUARE_SIZE - tlqy*W6_SQUARE_SIZE, q);
			}

			for (int q = 0; q < W6_QUADRANT_COUNT; q++){
				Bit8u tlqx,tlqy;
				tlqx = amw6_cache_qsx[amw6_level][q];
				tlqy = amw6_cache_qsy[amw6_level][q];
				W6_DrawQuadrantThirdPass(map_draw_x + map_scroll_x + tlqx*W6_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W6_SQUARE_SIZE - tlqy*W6_SQUARE_SIZE, q);
			}

			for (int q = 0; q < W6_QUADRANT_COUNT; q++){
				Bit8u tlqx,tlqy;
				tlqx = amw6_cache_qsx[amw6_level][q];
				tlqy = amw6_cache_qsy[amw6_level][q];
				W6_DrawQuadrantFourthPass(map_draw_x + map_scroll_x + tlqx*W6_SQUARE_SIZE, map_draw_y + map_scroll_y + 255*W6_SQUARE_SIZE - tlqy*W6_SQUARE_SIZE, q, qx, qy, (quadrant==q));
			}

			Bit16u amw6level = 0;
			mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
			if (amw6_jLevel==amw6_level){
				Bit16u qx = map_draw_x + map_scroll_x + amw6_jX*W6_SQUARE_SIZE;
				Bit16u qy = map_draw_y + map_scroll_y+ (255+7)*W6_SQUARE_SIZE - amw6_jY*W6_SQUARE_SIZE;
				float w = W6_SQUARE_SIZE-3;
				float h = W6_SQUARE_SIZE-3;

				glPushMatrix();  //Make sure our transformations don't affect any other transformations in other code
				glTranslatef(qx+3, qy+3, 0);  //Translate rectangle to its assigned x and y position
				//Put other transformations here
				glLineWidth(2.0f);
				glBegin(GL_LINES);  
					glColor3f(0.98, 	0.625, 	0.12);
					glVertex2f(0, 0);           
					glVertex2f(0, h);

					glVertex2f(0, h);
					glVertex2f(w, h);

					glVertex2f(w, h);
					glVertex2f(w, 0);       

					glVertex2f(w, 0);
					glVertex2f(0, 0);
				glEnd();
				glPopMatrix();
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// User Input
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool MousePositionToQuadrantPosition(int mx, int my, Bit16u &quadrant, Bit16u &qx, Bit16u &qy){
	int selAbsX = ((-map_draw_x-map_scroll_x) + mx) / W6_SQUARE_SIZE;
	int selAbsY = ((-map_draw_y-map_scroll_y) + my) / W6_SQUARE_SIZE;
	selAbsY -= 7;
	selAbsY = 255 - selAbsY; 
	if (selAbsY < 0)
		selAbsY = 0;

	return W6C_AbsToQuadrant(selAbsX,selAbsY,quadrant,qx,qy);
}

void TooltipForMainWindow_Show(bool show);
void TooltipForMainWindow_SetText(wchar_t *text);

void W6_OnMouseMotionInMainWindow(RECT rc, int newX, int newY){
	if (!amw6_show_tooltips){
		TooltipForMainWindow_Show(false);
		return;
	}
	Bit16u W6GameState = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x363A, &W6GameState);
	Bit16u TE = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x503E, &TE);

	Bit16u amw6level = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
	//----------------Detect in-game cond---------------------
	amW6inGame = (W6GameState == 5) || ((W6GameState>=10) && (W6GameState<=14)) || (W6GameState==19) || (W6GameState==20) || (W6GameState==22) || (W6GameState==24);
	if (amW6inGame){
		if (amw6level>=16){
			amW6inGame = false;
		} 
	}
	
	if (!amW6inGame || (W6GameState==5 && TE!=0 && TE!=1)){
		TooltipForMainWindow_Show(false);
		return;
	}
#define PY (78.0f / 420.0f)
#define PWIDTH (145.0f / 640.0f)
#define PHEIGHT (67.0f / 420.0f)
	int psx = rc.right - (int)(rc.right * PWIDTH);
	int psy = (int)(rc.bottom * PY);
	int pw = (int)(rc.right * PWIDTH);
	int ph = (int)(rc.bottom * PHEIGHT);

	static wchar_t *nums[6] = {
		L"1",
		L"2",
		L"3",
		L"4",
		L"5",
		L"6",
	};
	static char *magicCats[6] = {
		"Fire",
		"Water",
		"Air",
		"Earth",
		"Mental",
		"Magic",
	};
	static char *neffects[10] = {
		"Silenced",
		"Frightened",
		"Falls asleep",
		"Paralyzed",
		"Blinded",
		"Poisoned",
		"Diseased",
		"Veggified!",
		"Nauseous",
		"Cursed",
	};
	int num = -1;
	int nEnemy = -1;
	if ((newY>psy) && (newY<psy+ph*3)){
		num = ((newY-psy) / ph);
		if (num>2) num = 2;
		if (newX<pw){
			num *= 2;
		} else
		if (newX>psx){
			num = num * 2 + 1;
		} else
			num = -1;
	}

	Bit16u chaCount = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x43CE, &chaCount);

	static wchar_t char_tip[1024*10];
	char_tip[0] = 0;
	if ((num>=0) && (num<chaCount)){
		//num++;
		char name[8];
		Bit16u cofs = 0x43E8 + num*0x1B0;
		MEM_BlockRead(amw6_dataseg_addr + cofs, &name[0], 8);
		Bit16u chp,mhp;
		mem_readw_checked(amw6_dataseg_addr + cofs + 0x18,&chp);//Current HP
		mem_readw_checked(amw6_dataseg_addr + cofs + 0x1A,&mhp);//Maximum HP
		name[7] = 0;
		swprintf(char_tip,L"Name: %s\r\nHP: %d/%d",&name[0],chp,mhp);
		bool fc = true;
		for (int i = 0; i < 6; i++){
			Bit16u cmp,mmp;
			mem_readw_checked(amw6_dataseg_addr + cofs + 0x28 + i*4,&cmp);//Current MP
			mem_readw_checked(amw6_dataseg_addr + cofs + 0x28 + i*4 + 2,&mmp);//Maximum MP
			if (mmp>0){
				if (fc){
					swprintf(&char_tip[wcslen(char_tip)],L"\r\nMana points:");
					fc = false;
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s %d/%d",&magicCats[i][0],cmp,mmp);
			}
		}

		fc = true;
		for (int i = 0; i < 10; i++){
			Bit8u d;
			mem_readb_checked(amw6_dataseg_addr + cofs + 0x122 + i,&d);
			if (d>0){
				if (fc){
					swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
					fc = false;
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s(%d rounds)",&neffects[i][0],d);
			}
		}

		Bit16u SubScreenState = 0;
		mem_readw_checked(amw6_dataseg_addr + 0x3598, &SubScreenState);

		if ((W6GameState>=11) && (W6GameState<=14)) {

		Bit8u d;
		Bit16u pGroup;
		mem_readw_checked(amw6_dataseg_addr + 0x43A8, &pGroup);
		mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*num + 0x29,&d);
		if ((d & 0x80)!=0){
			if (fc){
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
				fc = false;
			}
			swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Slowed");
		}

		mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*num + 0x28,&d);
		if ((d & 0x80)!=0){
			if (fc){
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
				fc = false;
			}
			swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Weakened");
		}

		//Positive effects
		fc = true;
		mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*num + 0x29,&d);
		if (((d & 0x80)==0) && (d!=0)){
			if (fc){
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nPositive effects:");
				fc = false;
			}
			swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Hasted");
		}

		}
	} else {
#define EWIDTH (320.0f / 640.0f)
#define EY (316.0f / 420.0f)
#define EHEIGHT ((420.0f-305.0f-22.0f) / 420.0f)
		int esx = rc.right - (int)(rc.right * EWIDTH);
		int esy = (int)(rc.bottom * EY);
		int ew = (int)(rc.right * EWIDTH);
		int eh = (int)(rc.bottom * EHEIGHT);

		Bit16u SpellScreenState = 0;
		mem_readw_checked(amw6_dataseg_addr + 0x5016, &SpellScreenState);

		Bit16u SubScreenState = 0;
		mem_readw_checked(amw6_dataseg_addr + 0x3598, &SubScreenState);

		if ((W6GameState==12) && (SubScreenState!=5398/*use*/) && (SubScreenState!=4865/*spell*/) && (SubScreenState!=5391/*spell*/))     ///(SpellScreenState!=0) && (SpellScreenState!=1))
		if ((newX>esx) && (newX<esx+ew))
		if ((newY>esy) && (newY<esy+eh)){
			nEnemy = ((newY-esy) / (eh/5));
			if (nEnemy>4) nEnemy = 4;
			Bit16u gtAddr = 0x43B8 + nEnemy*2;//Template
			Bit16u pTemplate = 0;
			Bit16u gAddr = 0x43AA + nEnemy*2;
			Bit16u pGroup = 0;
			Bit8u eCount = 0;
			mem_readw_checked(amw6_dataseg_addr + gtAddr, &pTemplate);
			mem_readw_checked(amw6_dataseg_addr + gAddr, &pGroup);
			mem_readb_checked(amw6_dataseg_addr + pGroup + 0x19D,&eCount);

			Bit8u dn = 0;
			mem_readb_checked(amw6_dataseg_addr + pGroup + 0x19C, &dn);//0x19C: 0=known monster; 1=unknown monster     ???

			/* Groub Member Array [gAddr]
			       0x0       word        Level
				   0x2       word        Current HP
				   0x4       word        Maximum HP

				   0xE                              ?? Silenced ??

				   0x19D     byte		 Count of members
				   0x19E     byte        ?? Count of ready to fight members ??
			*/

			if (eCount>0){
				int nSubEnemy = (newX-esx) / (ew / eCount);
				Bit16u curHP = 0, maxHP = 0, curStamina = 0, maxStamina = 0;
				char ename[16];
				MEM_BlockRead(amw6_dataseg_addr + pTemplate + 32*dn, &ename[0], 16);
				mem_readw_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x2,&curHP);
				mem_readw_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x4,&maxHP);
				mem_readw_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x6,&curStamina);
				mem_readw_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x8,&maxStamina);

				char_tip[0] = 0;
				if (eCount>1){
					swprintf(&char_tip[wcslen(char_tip)],L"%s[%d/%d]",&ename[0],nSubEnemy+1,eCount);
				} else {
					swprintf(&char_tip[wcslen(char_tip)],L"%s",&ename[0]);
				}
				swprintf(&char_tip[wcslen(char_tip)],L"\r\nHP: %d/%d",curHP,maxHP);

				bool fc = true;
				for (int i = 0; i < 10; i++){
					Bit8u d;
					mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0xE + i,&d);
					if (d>0){
						if (fc){
							swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
							fc = false;
						}
						swprintf(&char_tip[wcslen(char_tip)],L"\r\n  %s(%d rounds)",&neffects[i][0],d);
					}
				}
				Bit8u d;
				mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x29,&d);
				if ((d & 0x80)!=0){
					if (fc){
						swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
						fc = false;
					}
					swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Slowed");
				}

				mem_readb_checked(amw6_dataseg_addr + pGroup + 0x2C*nSubEnemy + 0x28,&d);
				if ((d & 0x80)!=0){
					if (fc){
						swprintf(&char_tip[wcslen(char_tip)],L"\r\nNegative effects:");
						fc = false;
					}
					swprintf(&char_tip[wcslen(char_tip)],L"\r\n  Weakened");
				}
			} else
				nEnemy = -1;
		}
	}

	static wchar_t char_tip_backup[1024*10];
	bool tipUpdated = (wcscmp(&char_tip[0],&char_tip_backup[0])!=0);
	if (tipUpdated){
		wcscpy(&char_tip_backup[0],&char_tip[0]);
	}
	if ((((num>=0) && (num<chaCount)) || (nEnemy>=0)) && (tipUpdated))
		TooltipForMainWindow_SetText(&char_tip_backup[0]);
	TooltipForMainWindow_Show(((num>=0) && (num<chaCount)) || (nEnemy>=0));
}

void W6_OnAutomapDrag(int deltaX, int deltaY){
	if (!amW6inGame) return;

	Bit16u quadrant = 0;
	Bit16u qx = 0;
	Bit16u qy = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9C, &quadrant);
	mem_readw_checked(amw6_dataseg_addr + 0x4FA0, &qx);
	mem_readw_checked(amw6_dataseg_addr + 0x4F9E, &qy);

	if (W6C_IsDarkZone(quadrant,qx,qy)){
		return;
	}

	map_scroll_x -= deltaX;
	map_scroll_y -= deltaY;
	if (map_scroll_x<W6_SQUARE_SIZE*(-256))
		map_scroll_x = W6_SQUARE_SIZE*(-256);
	if (map_scroll_x>W6_SQUARE_SIZE*256)
		map_scroll_x = W6_SQUARE_SIZE*256;
	if (map_scroll_y<W6_SQUARE_SIZE*(-256))
		map_scroll_y = W6_SQUARE_SIZE*(-256);
	if (map_scroll_y>W6_SQUARE_SIZE*256)
		map_scroll_y = W6_SQUARE_SIZE*256;
	AutoMapUpdate();
}

void TooltipForAutomapWindow_Show(bool show);
void TooltipForAutomapWindow_SetText(wchar_t *text);

void W6_OnMouseMotionInAutomapWindow(int newX, int newY, bool alt){
	if (!amW6inGame){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	Bit16u quadrant = 0;
	Bit16u qx = 0;
	Bit16u qy = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9C, &quadrant);
	mem_readw_checked(amw6_dataseg_addr + 0x4FA0, &qx);
	mem_readw_checked(amw6_dataseg_addr + 0x4F9E, &qy);

	if (W6C_IsDarkZone(quadrant,qx,qy)){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	if (!alt){
		Bit16u squadrant = 0;
		Bit16u sqx = 0;
		Bit16u sqy = 0;
		bool qexists = MousePositionToQuadrantPosition(newX,newY,squadrant,sqx,sqy);
		bool selc = (qexists && (squadrant==quadrant) && (sqx==qx) && (sqy==qy));
		int n = (qexists) ? W6_FindNote(squadrant,sqx,sqy) : -1;
        // Update the text.
        wchar_t *text = (selc) ? (wchar_t*)L"Current Position" : (wchar_t*)L"Invisible";// coords;
		if (n>=0)
			text = &amw6_notes[amw6_level][n].str[0];
		TooltipForAutomapWindow_SetText(text);
		TooltipForAutomapWindow_Show(selc || (n>=0));
	}

	if (alt){
		Bit16u squadrant = 0;
		Bit16u sqx = 0;
		Bit16u sqy = 0;
		if (MousePositionToQuadrantPosition(newX,newY,squadrant,sqx,sqy)){
			static wchar_t buf[100];
			swprintf(buf,L"Quadrant: %d qX: %d qY: %d",squadrant,sqx,sqy);
			TooltipForAutomapWindow_SetText(&buf[0]);
			TooltipForAutomapWindow_Show(true);
		} else
			TooltipForAutomapWindow_Show(false);
	}
}

bool InputBox(const wchar_t *title, const wchar_t *hint, wchar_t *buf, int bufSize);
bool ChooseColorDialog(Bit32u &color, Bit32u *palette);

void W6_OnMouseButtonInAutomapWindow(SDL_MouseButtonEvent * button, bool alt, bool ctrl){
	if (!amW6inGame) return;

	Bit16u quadrant = 0;
	Bit16u qx = 0;
	Bit16u qy = 0;
	mem_readw_checked(amw6_dataseg_addr + 0x4F9C, &quadrant);
	mem_readw_checked(amw6_dataseg_addr + 0x4FA0, &qx);
	mem_readw_checked(amw6_dataseg_addr + 0x4F9E, &qy);

	if (W6C_IsDarkZone(quadrant,qx,qy)){
		TooltipForAutomapWindow_Show(false);
		return;
	}

	if (button->button==SDL_BUTTON_MIDDLE){
		Bit16u amw6level = 0; 
		mem_readw_checked(amw6_dataseg_addr + 0x363C, &amw6level);
		amw6_level = amw6level;
		map_scroll_x = 0;
		map_scroll_y = 0;
		AutoMapUpdate();
	}
	Bit16u squadrant = 0;
	Bit16u sqx = 0;
	Bit16u sqy = 0;
	if ((alt) && (!ctrl))
	if (button->clicks!=0)
	if (MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		if ((button->button==SDL_BUTTON_LEFT)){
			char buf[100];
			sprintf(buf,"{%d:%d:%d:%d}",amw6_level,squadrant,sqx,sqy);
			SDL_SetClipboardText(buf);
		}
	}

	if ((!alt) && (ctrl))
	if (button->clicks!=0)
	if (MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		if ((button->button==SDL_BUTTON_LEFT)){
			int n = W6_FindNote(squadrant,sqx,sqy);
			if (n>=0){
				const wchar_t *str = amw6_notes[amw6_level][n].str.c_str();
				int start = -1;
				int end = -1;
				for (int i = 0; i < wcslen(str); i++)
					if (str[i]==L'{'){
						start = i;
						break;
					}

				for (int i = 0; i < wcslen(str); i++)
					if (str[i]==L'}'){
						end = i;
						break;
					}

				if ((start>=0) && (end>start)){
					wchar_t buf[100];
					buf[end-start+1]=0;
					wcsncpy(&buf[0], &str[start], end-start+1);
					int jLevel = 0;
					int jQuadrant = 0;
					int jqX = 0;
					int jqY = 0;
					if (swscanf(&buf[0],L"{%d:%d:%d:%d}",&jLevel,&jQuadrant,&jqX,&jqY)==4){
						if ((jLevel>=0) && (jLevel<W6_LEVEL_COUNT) && (jQuadrant>=0) && (jQuadrant<W6_QUADRANT_COUNT) && (jqX>=0) && (jqX<8) && (jqY>=0) && (jqY<8)){
							amw6_level = jLevel;

							Bit8u tlcqx,tlcqy;
							tlcqx = amw6_cache_qsx[amw6_level][jQuadrant];
							tlcqy = amw6_cache_qsy[amw6_level][jQuadrant];
							amw6_jLevel = amw6_level;
							amw6_jX = tlcqx + jqX;
							amw6_jY = tlcqy + jqY;

							int JDX = ( -(((tlcqx+jqX)*W6_SQUARE_SIZE) - ((SCREENW/2)-(W6_SQUARE_SIZE/2))) );
							int JDY = ( -(((255+8)*W6_SQUARE_SIZE - (tlcqy+jqY)*W6_SQUARE_SIZE) - ((SCREENH/2)-(W6_SQUARE_SIZE/2))) );
							map_scroll_x = JDX - map_draw_x;
							map_scroll_y = JDY - map_draw_y;

							AutoMapUpdate();
						}
					}
				}
			}
		}
	}

	if ((!alt) && (!ctrl))
	if (button->clicks!=0)
	if (MousePositionToQuadrantPosition(button->x, button->y, squadrant, sqx, sqy)){
		int n = W6_FindNote(squadrant,sqx,sqy);
		if ((button->button==SDL_BUTTON_LEFT)){
				TooltipForAutomapWindow_Show(false);

				std::wstring ibuf;
				ibuf.resize(1024);
				ibuf[0] = 0;
				if (n>=0){
					wcscpy(&ibuf[0],amw6_notes[amw6_level][n].str.c_str());
				}
				if (InputBox(L"Enter comment",L"if you leave it blank, it will remove marker from map",&ibuf[0],1023)){
					if (n>=0){
						if (wcslen(&ibuf[0])>0)
							amw6_notes[amw6_level][n].str = std::wstring(&ibuf[0]);
						else {
							amw6_notes[amw6_level].erase(amw6_notes[amw6_level].begin() + n);
						}
					} else 
					if (wcslen(&ibuf[0])>0)
					{
						AMW6Note note;
						note.quadrant = squadrant;
						note.qX = sqx;
						note.qY = sqy;
						note.str = &ibuf[0];
						note.color = 0xFF0000FF;
						amw6_notes[amw6_level].push_back(note);
					}
					AutoMapUpdate();
				}
		} else
		if ((button->button==SDL_BUTTON_RIGHT) && (n>=0)){
				TooltipForAutomapWindow_Show(false);

				if (ChooseColorDialog(amw6_notes[amw6_level][n].color,wiz6_palette)){
					AutoMapUpdate();
				}
		}
	}
}