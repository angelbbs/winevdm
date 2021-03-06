/*
 * 16-bit dialog functions
 *
 * Copyright 1993, 1994, 1996, 2003 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "config.h"
#include "wine/port.h"

#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "wownt32.h"
#include "wine/winuser16.h"
#include "user_private.h"
#include "wine/debug.h"
#include <Uxtheme.h>

WINE_DEFAULT_DEBUG_CHANNEL(dialog);

/* Dialog control information */
typedef struct
{
    DWORD      style;
    INT16      x;
    INT16      y;
    INT16      cx;
    INT16      cy;
    UINT       id;
    LPCSTR     className;
    LPCSTR     windowName;
    LPCVOID    data;
} DLG_CONTROL_INFO;

  /* Dialog template */
typedef struct
{
    DWORD      style;
    UINT16     nbItems;
    INT16      x;
    INT16      y;
    INT16      cx;
    INT16      cy;
    LPCSTR     menuName;
    LPCSTR     className;
    LPCSTR     caption;
    INT16      pointSize;
    LPCSTR     faceName;
} DLG_TEMPLATE;

#define DIALOG_CLASS_ATOM MAKEINTATOM(32770)

/***********************************************************************
 *           DIALOG_EnableOwner
 *
 * Helper function for modal dialogs to enable again the
 * owner of the dialog box.
 */
static void DIALOG_EnableOwner( HWND hOwner )
{
    /* Owner must be a top-level window */
    if (hOwner)
        hOwner = GetAncestor( hOwner, GA_ROOT );
    if (!hOwner) return;
    EnableWindow( hOwner, TRUE );
}


/***********************************************************************
 *           DIALOG_DisableOwner
 *
 * Helper function for modal dialogs to disable the
 * owner of the dialog box. Returns TRUE if owner was enabled.
 */
static BOOL DIALOG_DisableOwner( HWND hOwner )
{
    /* Owner must be a top-level window */
    if (hOwner)
        hOwner = GetAncestor( hOwner, GA_ROOT );
    if (!hOwner) return FALSE;
    if (IsWindowEnabled( hOwner ))
    {
        EnableWindow( hOwner, FALSE );
        return TRUE;
    }
    else
        return FALSE;
}


/***********************************************************************
 *           DIALOG_GetControl16
 *
 * Return the class and text of the control pointed to by ptr,
 * fill the header structure and return a pointer to the next control.
 */
static LPCSTR DIALOG_GetControl16( LPCSTR p, DLG_CONTROL_INFO *info )
{
    static char buffer[10];
    int int_id;

    info->x       = GET_WORD(p);  p += sizeof(WORD);
    info->y       = GET_WORD(p);  p += sizeof(WORD);
    info->cx      = GET_WORD(p);  p += sizeof(WORD);
    info->cy      = GET_WORD(p);  p += sizeof(WORD);
    info->id      = GET_WORD(p);  p += sizeof(WORD);
    info->style   = GET_DWORD(p); p += sizeof(DWORD);

    if (*p & 0x80)
    {
        switch((BYTE)*p)
        {
            case 0x80: strcpy( buffer, "BUTTON" ); break;
            case 0x81: strcpy( buffer, "EDIT" ); break;
            case 0x82: strcpy( buffer, "STATIC" ); break;
            case 0x83: strcpy( buffer, "LISTBOX" ); break;
            case 0x84: strcpy( buffer, "SCROLLBAR" ); break;
            case 0x85: strcpy( buffer, "COMBOBOX" ); break;
            default:   buffer[0] = '\0'; break;
        }
        info->className = buffer;
        p++;
    }
    else
    {
        info->className = p;
        p += strlen(p) + 1;
    }

    int_id = ((BYTE)*p == 0xff);
    if (int_id)
    {
        /* Integer id, not documented (?). Only works for SS_ICON controls */
        info->windowName = MAKEINTRESOURCEA(GET_WORD(p+1));
        p += 3;
    }
    else
    {
        info->windowName = p;
        p += strlen(p) + 1;
    }

    if (*p) info->data = p + 1;
    else info->data = NULL;

    p += *p + 1;

    TRACE("   %s %s %d, %d, %d, %d, %d, %08x, %p\n",
          debugstr_a(info->className),  debugstr_a(info->windowName),
          info->id, info->x, info->y, info->cx, info->cy,
          info->style, info->data );

    return p;
}


/***********************************************************************
 *           DIALOG_CreateControls16
 *
 * Create the control windows for a dialog.
 */
static BOOL DIALOG_CreateControls16( HWND hwnd, LPCSTR template,
                                     const DLG_TEMPLATE *dlgTemplate, HINSTANCE16 hInst )
{
    DIALOGINFO *dlgInfo = wow_handlers32.get_dialog_info( hwnd, TRUE );
    DLG_CONTROL_INFO info;
    HWND hwndCtrl, hwndDefButton = 0;
    INT items = dlgTemplate->nbItems;

    TRACE(" BEGIN\n" );
    while (items--)
    {
        HINSTANCE16 instance = hInst;
        SEGPTR segptr;

        template = DIALOG_GetControl16( template, &info );
        info.className = info.className;
        segptr = MapLS( info.data );
        hwndCtrl = WIN_Handle32( CreateWindowEx16( WS_EX_NOPARENTNOTIFY,
                                                   info.className, info.windowName,
                                                   info.style | WS_CHILD,
                                                   MulDiv(info.x, dlgInfo->xBaseUnit, 4),
                                                   MulDiv(info.y, dlgInfo->yBaseUnit, 8),
                                                   MulDiv(info.cx, dlgInfo->xBaseUnit, 4),
                                                   MulDiv(info.cy, dlgInfo->yBaseUnit, 8),
                                                   HWND_16(hwnd), (HMENU16)info.id,
                                                   instance, (LPVOID)segptr ));
        UnMapLS( segptr );

        if (!hwndCtrl)
        {
            if (dlgTemplate->style & DS_NOFAILCREATE) continue;
            return FALSE;
        }

            /* Send initialisation messages to the control */
        if (dlgInfo->hUserFont) SendMessageA( hwndCtrl, WM_SETFONT,
                                             (WPARAM)dlgInfo->hUserFont, 0 );
        if (SendMessageA(hwndCtrl, WM_GETDLGCODE, 0, 0) & DLGC_DEFPUSHBUTTON)
        {
              /* If there's already a default push-button, set it back */
              /* to normal and use this one instead. */
            if (hwndDefButton)
                SendMessageA( hwndDefButton, BM_SETSTYLE,
                                BS_PUSHBUTTON,FALSE );
            hwndDefButton = hwndCtrl;
            dlgInfo->idResult = GetWindowLongPtrA( hwndCtrl, GWLP_ID );
        }
    }
    TRACE(" END\n" );
    return TRUE;
}


/***********************************************************************
 *           DIALOG_ParseTemplate16
 *
 * Fill a DLG_TEMPLATE structure from the dialog template, and return
 * a pointer to the first control.
 */
static LPCSTR DIALOG_ParseTemplate16( LPCSTR p, DLG_TEMPLATE * result )
{
    result->style   = GET_DWORD(p); p += sizeof(DWORD);
    result->nbItems = (unsigned char) *p++;
    result->x       = GET_WORD(p);  p += sizeof(WORD);
    result->y       = GET_WORD(p);  p += sizeof(WORD);
    result->cx      = GET_WORD(p);  p += sizeof(WORD);
    result->cy      = GET_WORD(p);  p += sizeof(WORD);

    TRACE("DIALOG %d, %d, %d, %d\n", result->x, result->y, result->cx, result->cy );
    TRACE(" STYLE %08x\n", result->style );

    /* Get the menu name */

    switch( (BYTE)*p )
    {
    case 0:
        result->menuName = 0;
        p++;
        break;
    case 0xff:
        result->menuName = MAKEINTRESOURCEA(GET_WORD( p + 1 ));
        p += 3;
        TRACE(" MENU %04x\n", LOWORD(result->menuName) );
        break;
    default:
        result->menuName = p;
        TRACE(" MENU '%s'\n", p );
        p += strlen(p) + 1;
        break;
    }

    /* Get the class name */

    if (*p)
    {
        result->className = p;
        TRACE(" CLASS '%s'\n", result->className );
    }
    else result->className = (LPCSTR)DIALOG_CLASS_ATOM;
    p += strlen(p) + 1;

    /* Get the window caption */

    result->caption = p;
    p += strlen(p) + 1;
    TRACE(" CAPTION '%s'\n", result->caption );

    /* Get the font name */

    result->pointSize = 0;
    result->faceName = NULL;

    if (result->style & DS_SETFONT)
    {
        result->pointSize = GET_WORD(p);
        p += sizeof(WORD);
        result->faceName = p;
        p += strlen(p) + 1;
        TRACE(" FONT %d,'%s'\n", result->pointSize, result->faceName );
    }
    return p;
}
LRESULT get_message_callback(HWND16 hwnd, UINT16 msg, WPARAM16 wp, LPARAM lp,
	LRESULT *result, void *arg);
LRESULT CALLBACK DlgProcCall16(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HWND16 hWnd16 = HWND_16(hDlg);
	WNDPROC16 wndproc16 = GetWndProc16(hWnd16);
	switch (Msg)
	{
	case WM_INITDIALOG:
		{
			CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
			LPARAM *params = cs->lpCreateParams;
			HMENU hMenu;
			if (cs->hMenu)
			{
				BOOL ret = SetMenu(hDlg, HMENU_32(cs->hMenu));
			}
			ATOM classatom = GetClassLongA(hDlg, GCW_ATOM);//WNDPROC16
			HWND16 hWnd16 = HWND_16(hDlg);
			if (classatom)
			{
				if (params[1])
				{
					SetDlgProc16(hWnd16, params[1]);
				}
				else
				{
                    SetDlgProc16(hWnd16, WNDCLASS16Info[classatom].wndproc);
				}
			}
			cs->lpCreateParams = params[0];
			free(cs);
		}
		break;
	}
	if (wndproc16)
	{
		MSG msg;
		msg.hwnd = hDlg;
		msg.message = Msg;
		msg.wParam = wParam;
		msg.lParam = lParam;
		MSG16 msg16;
		LRESULT unused;
		WINPROC_CallProc32ATo16(get_message_callback, msg.hwnd, msg.message, msg.wParam, msg.lParam,
			&unused, &msg16);
		switch (msg.message)
		{
		default:
		case WM_PAINT:
		case WM_COMMAND:
			return DispatchMessage16(&msg16),1;
		}
	}
	return 0;//DefWindowProcA(hDlg, Msg, wParam, lParam);
}
LRESULT call_window_proc16(HWND16 hwnd, UINT16 msg, WPARAM16 wParam, LPARAM lParam,
    LRESULT *result, void *arg);
LRESULT call_dialog_proc16(HWND16 hwnd, UINT16 msg, WPARAM16 wParam, LPARAM lParam,
    LRESULT *result, void *arg)
{
    SetWindowLong16(hwnd, DWL_MSGRESULT, 0xdeadbeef);
    call_window_proc16(hwnd, msg, wParam, lParam, result, arg);
    LRESULT r = *result;
    if (GetWindowLong16(hwnd, DWL_MSGRESULT) == 0xdeadbeef)
    {
        return r;
    }
    else
    {
        *result = GetWindowLong16(hwnd, DWL_MSGRESULT);
    }
    return r;
}
INT_PTR CALLBACK DlgProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	HWND16 hWnd16 = HWND_16(hDlg);
	WNDPROC16 wndproc16 = GetDlgProc16(hWnd16);
    LRESULT ret = 0;
	if (wndproc16)
	{
		MSG msg;
		msg.hwnd = hDlg;
		msg.message = Msg;
		msg.wParam = wParam;
		msg.lParam = lParam;
		MSG16 msg16;
        switch (msg.message)
        {
        case WM_INITDIALOG:
            break;
        default:
        {
            INT_PTR result = WINPROC_CallProc32ATo16(call_dialog_proc16, msg.hwnd, msg.message, msg.wParam, msg.lParam,
                &ret, wndproc16);
            SetWindowLongA(hDlg, DWL_MSGRESULT, ret);
            return result;
        }
        }
	}
	switch (Msg) {
	case WM_INITDIALOG:
	{
		CREATESTRUCTA* cs = (CREATESTRUCTA*)lParam;
		LPARAM *params = cs->lpCreateParams;
		HMENU hMenu;
		if (cs->hMenu)
		{
			BOOL ret = SetMenu(hDlg, HMENU_32(cs->hMenu));
		}
		ATOM classatom = GetClassLongA(hDlg, GCW_ATOM);//WNDPROC16
		HWND16 hWnd16 = HWND_16(hDlg);
		if (classatom)
		{
			if (params[1])
			{
				SetDlgProc16(hWnd16, params[1]);
			}
			else
			{
				SetDlgProc16(hWnd16, WNDCLASS16Info[classatom].wndproc);
			}
		}
		cs->lpCreateParams = params[0];
		wndproc16 = GetDlgProc16(hWnd16);
		if (wndproc16)
		{
			LRESULT unused;
			call_window_proc16(hWnd16, WM_INITDIALOG, HWND_16(wParam), cs->lpCreateParams, &unused, wndproc16);
		}
		free(cs);
        ret = 1;
	}
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDCANCEL:
			EndDialog(hDlg, IDCANCEL);
			return TRUE;
		case IDOK:
			EndDialog(hDlg, IDOK);
			return TRUE;
		}
		return FALSE;
	case WM_CLOSE:
		EndDialog(hDlg, IDCANCEL);
		return TRUE;
	}

	return ret;
}
/* internal API for COMMDLG hooks */
LRESULT WINAPI DIALOG_CallDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, WNDPROC proc)
{
    LRESULT ret;
    BOOL16 result = WINPROC_CallProc32ATo16(call_dialog_proc16, hwnd, msg, wParam, lParam,
        &ret, proc);
    SetWindowLongA(hwnd, DWL_MSGRESULT, ret);
    return result;
}

LRESULT CALLBACK DefWndProca(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam);
void paddingDWORD(DWORD **d)
{
	*d = (BYTE*)(((DWORD)*d + 3) & ~((DWORD)3));
}
void copy_widestr(LPCSTR name, LPWSTR *templatew)
{
	int len = 0;
	if (strlen(name))
	{
		len = MultiByteToWideChar(CP_ACP, NULL, name, -1, *templatew, strlen(name) * 4)
			* 2;
	}
	if (len)
	{
		*templatew = (WORD*)((BYTE*)*templatew + len);
	}
	else
	{
		**templatew = 0;
		(*templatew)++;
	}
}
#define DCLASS "_____DIALOGCLASS_____"
static BOOL init_dialog_class(HINSTANCE hInst)
{/*
	static BOOL init = 0;
	static ATOM catom;
	if (init) return catom;*/
	WNDCLASSEXA wc;
	if (GetClassInfoExA(hInst, DCLASS, &wc))
	{
		return 1;
	}
	// Get the info for this class.
	// #32770 is the default class name for dialogs boxes.
	GetClassInfoExA(hInst, "#32770", &wc);
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = DefWndProca;
	wc.lpszClassName = DCLASS;
	if (RegisterClassExA(&wc))
		return TRUE;//init = TRUE;
	return 0;
}
/***********************************************************************
*           DIALOG_CreateControls16
*
* Create the control windows for a dialog.
*/
static BOOL DIALOG_CreateControls16Ex(HWND hwnd, LPCSTR template,
	const DLG_TEMPLATE *dlgTemplate, HINSTANCE16 hInst, DLGITEMTEMPLATE *dlgItemTemplate32)
{
	DLG_CONTROL_INFO info;
	HWND hwndCtrl, hwndDefButton = 0;
	INT items = dlgTemplate->nbItems;
	dlgItemTemplate32 = (BYTE*) (((DWORD)dlgItemTemplate32 + 3) & ~((DWORD)3));
	WORD *dlgItemTemplatew;
	TRACE(" BEGIN\n");
	while (items--)
	{
		HINSTANCE16 instance = hInst;
		SEGPTR segptr;

		paddingDWORD(&dlgItemTemplate32);
		template = DIALOG_GetControl16(template, &info);
		//segptr = MapLS(info.data);
		dlgItemTemplate32->style = info.style | WS_CHILD | WS_VISIBLE;
		dlgItemTemplate32->dwExtendedStyle = 0;// WS_EX_NOPARENTNOTIFY;
		dlgItemTemplate32->x = info.x;
		dlgItemTemplate32->y = info.y;
		dlgItemTemplate32->cx = info.cx;
		dlgItemTemplate32->cy = info.cy;
		dlgItemTemplate32->id = info.id;
		dlgItemTemplatew = (WORD*)(dlgItemTemplate32 + 1);
        info.className = win32classname(hInst, info.className);
		copy_widestr(info.className, &dlgItemTemplatew);
        if (!HIWORD(info.windowName))
        {
            char buffer[512];
            if (((dlgItemTemplate32->style & 0xF) == SS_ICON || (dlgItemTemplate32->style & 0xF) == SS_BITMAP) && stricmp(info.className, "STATIC") == 0)
            {
                sprintf(buffer, "#%d", (int)info.windowName);
                copy_widestr(buffer, &dlgItemTemplatew);
                //LoadImage16
            }
            else
            {
                if (LoadString16(hInst, LOWORD(info.windowName), buffer, sizeof(buffer)))
                    info.windowName = buffer;
                else
                    info.windowName = "dialog error: invalid window name";
                copy_widestr(info.windowName, &dlgItemTemplatew);
            }
        }
        else
        {
            copy_widestr(info.windowName, &dlgItemTemplatew);
        }
		if (info.data)
		{
			*dlgItemTemplatew++ = sizeof(WORD) + sizeof(info.data);
			*((LPCVOID*)dlgItemTemplatew) = info.data;
			dlgItemTemplatew += 2;
		}
		else
		{
			*dlgItemTemplatew++ = 0;
		}
		dlgItemTemplate32 = dlgItemTemplatew;
		
	}
	TRACE(" END\n");
	return TRUE;
}
static BOOL DIALOG_DumpControls32(HWND hwnd, LPCSTR template,
	const DLG_TEMPLATE *dlgTemplate, HINSTANCE16 hInst, DLGITEMTEMPLATE *dlgItemTemplate32)
{
	DLG_CONTROL_INFO info;
	HWND hwndCtrl, hwndDefButton = 0;
	INT items = dlgTemplate->nbItems;
	dlgItemTemplate32 = (BYTE*)(((DWORD)dlgItemTemplate32 + 3) & ~((DWORD)3));
	WORD *dlgItemTemplatew;
	TRACE(" BEGIN\n");
	while (items--)
	{
		char buf[256];
		template = DIALOG_GetControl16(template, &info);
		GetDlgItemTextA(hwnd, info.id, buf, 256);
		char	pszError[512];
		//最後のエラー番号取得
		DWORD dwError = GetLastError();
		//文字列変換
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, dwError, NULL, pszError, sizeof(pszError), NULL);
		DPRINTF("id 0x%x=%s, err=%s\n", info.id, buf, pszError);
	}
	TRACE(" END\n");
	return TRUE;
}

#include <stdio.h>

/* internal API for COMMDLG hooks */
DLGTEMPLATE *WINAPI dialog_template16_to_template32(HINSTANCE16 hInst, LPCVOID dlgTemplate, DWORD *size)
{
    HMENU16 hMenu = 0;
    DLG_TEMPLATE template;
    /* Parse dialog template */
    dlgTemplate = DIALOG_ParseTemplate16(dlgTemplate, &template);
    /* Load menu */
    if (template.menuName) hMenu = LoadMenu16(hInst, template.menuName);
    //FIXME:memory
    DLGTEMPLATE *template32 = malloc(1024 + template.nbItems * 512);
    *size = 1024 + template.nbItems * 512;
    template32->style = template.style;
    template32->dwExtendedStyle = 0;
    template32->cdit = template.nbItems;
    template32->x = template.x;
    template32->y = template.y;
    template32->cx = template.cx;
    template32->cy = template.cy;
    WORD *templatew = (WORD*)(template32 + 1);
    //Menu
    *templatew++ = 0;
    int len;
    HINSTANCE hInst32 = HINSTANCE_32(hInst);
    init_dialog_class(hInst32);
    BOOL hasclass = TRUE;
    if (template.className == DIALOG_CLASS_ATOM)
    {
        hasclass = FALSE;
        //Don't set __DIALOGCLASS__.
        *templatew++ = 0;
    }
    else
    {
        //WNDclass
        template.className = win32classname(hInst, template.className);
        len = MultiByteToWideChar(CP_ACP, NULL, template.className, -1, (LPWSTR)templatew, (1 + strlen(template.className)) * 4)
            * 2;
        if (len)
        {
            templatew = (WORD*)((BYTE*)templatew + len);
        }
        else
        {
            *templatew++ = 0;
        }
    }
    //dialog title
    len = MultiByteToWideChar(CP_ACP, NULL, template.caption, -1, (LPWSTR)templatew, (1 + strlen(template.caption)) * 4)
        * 2;
    if (len)
    {
        templatew = (WORD*)((BYTE*)templatew + len);
    }
    else
    {
        *templatew++ = 0;
    }
    if (template.style & DS_SETFONT)
    {
        *templatew++ = template.pointSize;
        len = MultiByteToWideChar(CP_ACP, NULL, template.faceName, -1, (LPWSTR)templatew, (1 + strlen(template.faceName)) * 4)
            * 2;
        if (len)
        {
            templatew = (WORD*)((BYTE*)templatew + len);
        }
        else
        {
            *templatew++ = 0;
        }
    }
    DIALOG_CreateControls16Ex(NULL, dlgTemplate, &template, hInst, templatew);
    WNDCLASSEXA wc, wc2 = { 0 };
    // Get the info for this class.
    // #32770 is the default class name for dialogs boxes.
    GetClassInfoExA(NULL, "#32770", &wc);
    GetClassInfoExA(hInst32, template.className, &wc2);
    if (!wc2.lpszClassName)
        GetClassInfoExA(GetModuleHandle(NULL), template.className, &wc2);
    return template32;
}
/***********************************************************************
*           DIALOG_CreateIndirect16
*
* Creates a dialog box window
*
* modal = TRUE if we are called from a modal dialog box.
* (it's more compatible to do it here, as under Windows the owner
* is never disabled if the dialog fails because of an invalid template)
*/
static HWND DIALOG_CreateIndirect16(HINSTANCE16 hInst, LPCVOID dlgTemplate,
	HWND owner, DLGPROC16 dlgProc, LPARAM param,
	BOOL modal)
{
	//DoDebugDialog(NULL, NULL);
	HWND hwnd;
	RECT rect;
	POINT pos;
	SIZE size;
	DLG_TEMPLATE template;
	DIALOGINFO * dlgInfo;
	BOOL ownerEnabled = TRUE;
	DWORD exStyle = 0;
	DWORD units = GetDialogBaseUnits();
	HMENU16 hMenu = 0;
	HFONT hUserFont = 0;
	UINT flags = 0;
	UINT xBaseUnit = LOWORD(units);
	UINT yBaseUnit = HIWORD(units);

	/* Parse dialog template */

	dlgTemplate = DIALOG_ParseTemplate16(dlgTemplate, &template);

	DLG_TEMPLATE *templatedbg = &template;
	/* Load menu */

	if (template.menuName) hMenu = LoadMenu16(hInst, template.menuName);
	//FIXME:memory
	DLGTEMPLATE *template32 = malloc(1024 + template.nbItems * 512);//zatsu
	template32->style = template.style;
	template32->dwExtendedStyle = 0;
	template32->cdit = template.nbItems;
	template32->x = template.x;
	template32->y = template.y;
	template32->cx = template.cx;
	template32->cy = template.cy;
	WORD *templatew = (WORD*)(template32 + 1);
	//Menu
	*templatew++ = 0;
	int len;
	HINSTANCE hInst32 = HINSTANCE_32(hInst);
	init_dialog_class(hInst32);
	BOOL hasclass = TRUE;
	if (template.className == DIALOG_CLASS_ATOM)
	{
		hasclass = FALSE;
		TRACE("\n");
		template.className = DCLASS;//"#32770";
		//*templatew++ = 0;
		//WNDclass
		len = MultiByteToWideChar(CP_ACP, NULL, template.className, -1, (LPWSTR)templatew, (1 + strlen(template.className)) * 4)
			* 2;
		if (len)
		{
			templatew = (WORD*)((BYTE*)templatew + len);
		}
		else
		{
			*templatew++ = 0;
		}
	}
	else
	{
		//WNDclass
        template.className = win32classname(hInst, template.className);
		len = MultiByteToWideChar(CP_ACP, NULL, template.className, -1, (LPWSTR)templatew, (1 + strlen(template.className)) * 4)
			* 2;
		if (len)
		{
			templatew = (WORD*)((BYTE*)templatew + len);
		}
		else
		{
			*templatew++ = 0;
		}
	}
	//dialog title
	len = MultiByteToWideChar(CP_ACP, NULL, template.caption, -1, (LPWSTR)templatew, (1 + strlen(template.caption)) * 4)
		* 2;
	if (len)
	{
		templatew = (WORD*)((BYTE*)templatew + len);
	}
	else
	{
		*templatew++ = 0;
	}
	if (template.style & DS_SETFONT)
	{
		*templatew++ = template.pointSize;
		len = MultiByteToWideChar(CP_ACP, NULL, template.faceName, -1, (LPWSTR)templatew, (1 + strlen(template.faceName)) * 4)
			* 2;
		if (len)
		{
			templatew = (WORD*)((BYTE*)templatew + len);
		}
		else
		{
			*templatew++ = 0;
		}
	}
	DIALOG_CreateControls16Ex(NULL, dlgTemplate, &template, hInst, templatew);
	WNDCLASSEXA wc, wc2 = { 0 };
	// Get the info for this class.
	// #32770 is the default class name for dialogs boxes.
	GetClassInfoExA(NULL, "#32770", &wc);
	GetClassInfoExA(hInst32, template.className, &wc2);
	if (!wc2.lpszClassName)
		GetClassInfoExA(GetModuleHandle(NULL), template.className, &wc2);
	//template32->cdit = 1;
	void *paramd = malloc(sizeof(param) * 2 + sizeof(CREATESTRUCTA));
	CREATESTRUCTA *paramcs = (CREATESTRUCTA*)paramd;
	paramcs->hMenu = LoadMenu16(hInst, wc2.lpszMenuName);
	paramd = (LPBYTE)paramd + sizeof(CREATESTRUCTA);
	paramcs->lpCreateParams = paramd;
	*(LPARAM*)paramd = param;
	if (dlgProc)
	{
		*((LPARAM*)paramd + 1) = dlgProc;
	}
	else
	{
		*((LPARAM*)paramd + 1) = 0;
	}
	paramd = (LPBYTE)paramd - sizeof(CREATESTRUCTA);
    DWORD count;
    ReleaseThunkLock(&count);
	if (modal)
	{
		INT_PTR ret = DialogBoxIndirectParamA(
			hInst32,
			(DLGTEMPLATE*)template32,
			owner,
			hasclass ? DlgProc : DlgProc, paramd);
        RestoreThunkLock(count);
		return ret;
	}
	else
	hwnd = CreateDialogIndirectParamA(
		hInst32,
		(DLGTEMPLATE*)template32,
		owner,
		hasclass ? DlgProc : DlgProc, paramd);
    RestoreThunkLock(count);
	//SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS);
	//SetWindowTheme(hwnd, "", "");
    //SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
	//DIALOG_DumpControls32(hwnd, dlgTemplate, &template, hInst, templatew);
	free(template32);
	//if (wc2.lpszMenuName) hMenu = LoadMenu16(hInst, wc2.lpszMenuName);
	//BOOL ret = SetMenu(hwnd, HMENU_32(hMenu));
	ATOM classatom = GetClassLongA(hwnd, GCW_ATOM);//WNDPROC16
	HWND16 hWnd16 = HWND_16(hwnd);
	if (classatom)
	{
		if (dlgProc)
		{
            SetDlgProc16(hWnd16, dlgProc);
		}
		else
		{
            //why??
			//SetWndProc16(hWnd16, WNDCLASS16Info[classatom].wndproc);
		}
	}
	return hwnd;
}
/***********************************************************************
 *           DIALOG_CreateIndirect16
 *
 * Creates a dialog box window
 *
 * modal = TRUE if we are called from a modal dialog box.
 * (it's more compatible to do it here, as under Windows the owner
 * is never disabled if the dialog fails because of an invalid template)
 */
static HWND DIALOG_CreateIndirect16Old( HINSTANCE16 hInst, LPCVOID dlgTemplate,
                                     HWND owner, DLGPROC16 dlgProc, LPARAM param,
                                     BOOL modal )
{
    HWND hwnd;
    RECT rect;
    POINT pos;
    SIZE size;
    DLG_TEMPLATE template;
    DIALOGINFO * dlgInfo;
    BOOL ownerEnabled = TRUE;
    DWORD exStyle = 0;
    DWORD units = GetDialogBaseUnits();
    HMENU16 hMenu = 0;
    HFONT hUserFont = 0;
    UINT flags = 0;
    UINT xBaseUnit = LOWORD(units);
    UINT yBaseUnit = HIWORD(units);

      /* Parse dialog template */

    dlgTemplate = DIALOG_ParseTemplate16( dlgTemplate, &template );

      /* Load menu */

    if (template.menuName) hMenu = LoadMenu16( hInst, template.menuName );

      /* Create custom font if needed */

    if (template.style & DS_SETFONT)
    {
          /* We convert the size to pixels and then make it -ve.  This works
           * for both +ve and -ve template.pointSize */
        HDC dc;
        int pixels;
        dc = GetDC(0);
        pixels = MulDiv(template.pointSize, GetDeviceCaps(dc , LOGPIXELSY), 72);
        hUserFont = CreateFontA( -pixels, 0, 0, 0, FW_DONTCARE,
                                 FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                 PROOF_QUALITY, FF_DONTCARE, template.faceName );
        if (hUserFont)
        {
            SIZE charSize;
            HFONT hOldFont = SelectObject( dc, hUserFont );
            charSize.cx = GdiGetCharDimensions( dc, NULL, &charSize.cy );
            if (charSize.cx)
            {
                xBaseUnit = charSize.cx;
                yBaseUnit = charSize.cy;
            }
            SelectObject( dc, hOldFont );
        }
        ReleaseDC(0, dc);
        TRACE("units = %d,%d\n", xBaseUnit, yBaseUnit );
    }

    /* Create dialog main window */

    rect.left = rect.top = 0;
    rect.right = MulDiv(template.cx, xBaseUnit, 4);
    rect.bottom =  MulDiv(template.cy, yBaseUnit, 8);
    if (template.style & DS_MODALFRAME) exStyle |= WS_EX_DLGMODALFRAME;
    AdjustWindowRectEx( &rect, template.style, (hMenu != 0), exStyle );
    pos.x = rect.left;
    pos.y = rect.top;
    size.cx = rect.right - rect.left;
    size.cy = rect.bottom - rect.top;

    if (template.x == CW_USEDEFAULT16)
    {
        pos.x = pos.y = CW_USEDEFAULT16;
    }
    else
    {
        HMONITOR monitor = 0;
        MONITORINFO mon_info;

        mon_info.cbSize = sizeof(mon_info);
        if (template.style & DS_CENTER)
        {
            monitor = MonitorFromWindow( owner ? owner : GetActiveWindow(), MONITOR_DEFAULTTOPRIMARY );
            GetMonitorInfoW( monitor, &mon_info );
            pos.x = (mon_info.rcWork.left + mon_info.rcWork.right - size.cx) / 2;
            pos.y = (mon_info.rcWork.top + mon_info.rcWork.bottom - size.cy) / 2;
        }
        else if (template.style & DS_CENTERMOUSE)
        {
            GetCursorPos( &pos );
            monitor = MonitorFromPoint( pos, MONITOR_DEFAULTTOPRIMARY );
            GetMonitorInfoW( monitor, &mon_info );
        }
        else
        {
            pos.x += MulDiv(template.x, xBaseUnit, 4);
            pos.y += MulDiv(template.y, yBaseUnit, 8);
            if (!(template.style & (WS_CHILD|DS_ABSALIGN))) ClientToScreen( owner, &pos );
        }
        if ( !(template.style & WS_CHILD) )
        {
            INT dX, dY;

            /* try to fit it into the desktop */

            if (!monitor)
            {
                SetRect( &rect, pos.x, pos.y, pos.x + size.cx, pos.y + size.cy );
                monitor = MonitorFromRect( &rect, MONITOR_DEFAULTTOPRIMARY );
                GetMonitorInfoW( monitor, &mon_info );
            }
            if ((dX = pos.x + size.cx + GetSystemMetrics(SM_CXDLGFRAME) - mon_info.rcWork.right) > 0)
                pos.x -= dX;
            if ((dY = pos.y + size.cy + GetSystemMetrics(SM_CYDLGFRAME) - mon_info.rcWork.bottom) > 0)
                pos.y -= dY;
            if( pos.x < mon_info.rcWork.left ) pos.x = mon_info.rcWork.left;
            if( pos.y < mon_info.rcWork.top ) pos.y = mon_info.rcWork.top;
        }
    }

    if (modal)
    {
        ownerEnabled = DIALOG_DisableOwner( owner );
        if (ownerEnabled) flags |= DF_OWNERENABLED;
    }

	DLG_TEMPLATE *templatedbg = &template;
    hwnd = WIN_Handle32( CreateWindowEx16(exStyle, template.className,
                                          template.caption, template.style & ~WS_VISIBLE,
                                          pos.x, pos.y, size.cx, size.cy,
										  HWND_16(owner), hMenu, hInst, NULL));
	HWND16 hWnd16 = HWND_16(hwnd);
	ATOM classatom = GetClassLongA(hwnd, GCW_ATOM);//WNDPROC16
	if (classatom)
	{
		dlgProc = WNDCLASS16Info[classatom].wndproc;
	}
    if (!hwnd)
    {
        if (hUserFont) DeleteObject( hUserFont );
        if (hMenu) DestroyMenu16( hMenu );
        if (modal && (flags & DF_OWNERENABLED)) DIALOG_EnableOwner(owner);
        return 0;
    }
    dlgInfo = wow_handlers32.get_dialog_info( hwnd, TRUE );
    dlgInfo->hwndFocus   = 0;
    dlgInfo->hUserFont   = hUserFont;
    dlgInfo->hMenu       = HMENU_32( hMenu );
    dlgInfo->xBaseUnit   = xBaseUnit;
    dlgInfo->yBaseUnit   = yBaseUnit;
    dlgInfo->idResult    = 0;
    dlgInfo->flags       = flags;

    SetWindowLong16( HWND_16(hwnd), DWLP_DLGPROC, (LONG)dlgProc );

    if (hUserFont)
        SendMessageA( hwnd, WM_SETFONT, (WPARAM)hUserFont, 0 );

    /* Create controls */

    if (DIALOG_CreateControls16( hwnd, dlgTemplate, &template, hInst ))
    {
        HWND hwndPreInitFocus;

        /* Send initialisation messages and set focus */

        dlgInfo->hwndFocus = GetNextDlgTabItem( hwnd, 0, FALSE );

        hwndPreInitFocus = GetFocus();
        if (SendMessageA( hwnd, WM_INITDIALOG, (WPARAM)dlgInfo->hwndFocus, param ))
        {
            /* check where the focus is again,
	     * some controls status might have changed in WM_INITDIALOG */
            dlgInfo->hwndFocus = GetNextDlgTabItem( hwnd, 0, FALSE);
            if( dlgInfo->hwndFocus )
                SetFocus( dlgInfo->hwndFocus );
        }
        else
        {
            /* If the dlgproc has returned FALSE (indicating handling of keyboard focus)
               but the focus has not changed, set the focus where we expect it. */
            if ((GetFocus() == hwndPreInitFocus) &&
                (GetWindowLongW( hwnd, GWL_STYLE ) & WS_VISIBLE))
            {
                dlgInfo->hwndFocus = GetNextDlgTabItem( hwnd, 0, FALSE);
                if( dlgInfo->hwndFocus )
                    SetFocus( dlgInfo->hwndFocus );
            }
        }

        if (template.style & WS_VISIBLE && !(GetWindowLongW( hwnd, GWL_STYLE ) & WS_VISIBLE))
        {
            ShowWindow( hwnd, SW_SHOWNORMAL );  /* SW_SHOW doesn't always work */
        }
        return hwnd;
    }
    if( IsWindow(hwnd) ) DestroyWindow( hwnd );
    if (modal && ownerEnabled) DIALOG_EnableOwner(owner);
    return 0;
}


/***********************************************************************
 *		DialogBox (USER.87)
 */
INT16 WINAPI DialogBox16( HINSTANCE16 hInst, LPCSTR dlgTemplate,
                          HWND16 owner, DLGPROC16 dlgProc )
{
    return DialogBoxParam16( hInst, dlgTemplate, owner, dlgProc, 0 );
}


/**************************************************************************
 *              EndDialog   (USER.88)
 */
BOOL16 WINAPI EndDialog16( HWND16 hwnd, INT16 retval )
{
    return EndDialog( WIN_Handle32(hwnd), retval );
}


/***********************************************************************
 *		CreateDialog (USER.89)
 */
HWND16 WINAPI CreateDialog16( HINSTANCE16 hInst, LPCSTR dlgTemplate,
                              HWND16 owner, DLGPROC16 dlgProc )
{
    return CreateDialogParam16( hInst, dlgTemplate, owner, dlgProc, 0 );
}


/**************************************************************************
 *              GetDlgItem   (USER.91)
 */
HWND16 WINAPI GetDlgItem16( HWND16 hwndDlg, INT16 id )
{
    return HWND_16( GetDlgItem( WIN_Handle32(hwndDlg), (UINT16) id ));
}


/**************************************************************************
 *              SetDlgItemText   (USER.92)
 */
void WINAPI SetDlgItemText16( HWND16 hwnd, INT16 id, SEGPTR lpString )
{
	const char *txt = MapSL(lpString);
	SetDlgItemTextA(HWND_32(hwnd), id, txt);
    SendDlgItemMessage16( hwnd, id, WM_SETTEXT, 0, lpString );
}


/**************************************************************************
 *              GetDlgItemText   (USER.93)
 */
INT16 WINAPI GetDlgItemText16( HWND16 hwnd, INT16 id, SEGPTR str, UINT16 len )
{
	return GetDlgItemTextA(HWND_32(hwnd), id, MapSL(str), len);
    return SendDlgItemMessage16( hwnd, id, WM_GETTEXT, len, str );
}


/**************************************************************************
 *              SetDlgItemInt   (USER.94)
 */
void WINAPI SetDlgItemInt16( HWND16 hwnd, INT16 id, UINT16 value, BOOL16 fSigned )
{
    SetDlgItemInt( WIN_Handle32(hwnd), (UINT)(UINT16)id,
             (UINT)(fSigned ? (INT16) value : value), fSigned );
}


/**************************************************************************
 *              GetDlgItemInt   (USER.95)
 */
UINT16 WINAPI GetDlgItemInt16( HWND16 hwnd, INT16 id, BOOL16 *translated, BOOL16 fSigned )
{
    UINT result;
    BOOL ok;

    if (translated) *translated = FALSE;
    result = GetDlgItemInt( WIN_Handle32(hwnd), (UINT)(UINT16)id, &ok, fSigned );
    if (!ok) return 0;
    if (fSigned)
    {
        if (((INT)result < -32767) || ((INT)result > 32767)) return 0;
    }
    else
    {
        if (result > 65535) return 0;
    }
    if (translated) *translated = TRUE;
    return (UINT16)result;
}


/**************************************************************************
 *              CheckRadioButton   (USER.96)
 */
BOOL16 WINAPI CheckRadioButton16( HWND16 hwndDlg, UINT16 firstID,
                                  UINT16 lastID, UINT16 checkID )
{
    return CheckRadioButton( WIN_Handle32(hwndDlg), firstID, lastID, checkID );
}


/**************************************************************************
 *              CheckDlgButton   (USER.97)
 */
BOOL16 WINAPI CheckDlgButton16( HWND16 hwnd, INT16 id, UINT16 check )
{
    SendDlgItemMessage16( hwnd, id, BM_SETCHECK16, check, 0 );
    return TRUE;
}


/**************************************************************************
 *              IsDlgButtonChecked   (USER.98)
 */
UINT16 WINAPI IsDlgButtonChecked16( HWND16 hwnd, UINT16 id )
{
    return (UINT16)SendDlgItemMessage16( hwnd, id, BM_GETCHECK16, 0, 0 );
}


/**************************************************************************
 *              DlgDirSelect   (USER.99)
 */
BOOL16 WINAPI DlgDirSelect16( HWND16 hwnd, LPSTR str, INT16 id )
{
    return DlgDirSelectEx16( hwnd, str, 128, id );
}


/**************************************************************************
 *              DlgDirList   (USER.100)
 */
INT16 WINAPI DlgDirList16( HWND16 hDlg, LPSTR spec, INT16 idLBox,
                           INT16 idStatic, UINT16 attrib )
{
    /* according to Win16 docs, DDL_DRIVES should make DDL_EXCLUSIVE
     * be set automatically (this is different in Win32, and
     * DIALOG_DlgDirList sends Win32 messages to the control,
     * so do it here) */
    if (attrib & DDL_DRIVES) attrib |= DDL_EXCLUSIVE;
    return DlgDirListA( WIN_Handle32(hDlg), spec, idLBox, idStatic, attrib );
}


/**************************************************************************
 *              SendDlgItemMessage   (USER.101)
 */
LRESULT WINAPI SendDlgItemMessage16( HWND16 hwnd, INT16 id, UINT16 msg,
                                     WPARAM16 wParam, LPARAM lParam )
{
    HWND16 hwndCtrl = GetDlgItem16( hwnd, id );
    if (hwndCtrl) return SendMessage16( hwndCtrl, msg, wParam, lParam );
    else return 0;
}


/**************************************************************************
 *              MapDialogRect   (USER.103)
 */
void WINAPI MapDialogRect16( HWND16 hwnd, LPRECT16 rect )
{
	RECT rect32;
	rect32.left = rect->left;
	rect32.right = rect->right;
	rect32.top = rect->top;
	rect32.bottom = rect->bottom;
    MapDialogRect( WIN_Handle32(hwnd), &rect32 );
    rect->left   = rect32.left;
    rect->right  = rect32.right;
    rect->top    = rect32.top;
    rect->bottom = rect32.bottom;
}


/**************************************************************************
 *              DlgDirSelectComboBox   (USER.194)
 */
BOOL16 WINAPI DlgDirSelectComboBox16( HWND16 hwnd, LPSTR str, INT16 id )
{
    return DlgDirSelectComboBoxEx16( hwnd, str, 128, id );
}


/**************************************************************************
 *              DlgDirListComboBox   (USER.195)
 */
INT16 WINAPI DlgDirListComboBox16( HWND16 hDlg, LPSTR spec, INT16 idCBox,
                                   INT16 idStatic, UINT16 attrib )
{
    return DlgDirListComboBoxA( WIN_Handle32(hDlg), spec, idCBox, idStatic, attrib );
}


/***********************************************************************
 *		DialogBoxIndirect (USER.218)
 */
INT16 WINAPI DialogBoxIndirect16( HINSTANCE16 hInst, HANDLE16 dlgTemplate,
                                  HWND16 owner, DLGPROC16 dlgProc )
{
    return DialogBoxIndirectParam16( hInst, dlgTemplate, owner, dlgProc, 0 );
}


/***********************************************************************
 *		CreateDialogIndirect (USER.219)
 */
HWND16 WINAPI CreateDialogIndirect16( HINSTANCE16 hInst, LPCVOID dlgTemplate,
                                      HWND16 owner, DLGPROC16 dlgProc )
{
    return CreateDialogIndirectParam16( hInst, dlgTemplate, owner, dlgProc, 0);
}


/**************************************************************************
 *              GetNextDlgGroupItem   (USER.227)
 */
HWND16 WINAPI GetNextDlgGroupItem16( HWND16 hwndDlg, HWND16 hwndCtrl,
                                     BOOL16 fPrevious )
{
    return HWND_16( GetNextDlgGroupItem( WIN_Handle32(hwndDlg), WIN_Handle32(hwndCtrl), fPrevious ));
}


/**************************************************************************
 *              GetNextDlgTabItem   (USER.228)
 */
HWND16 WINAPI GetNextDlgTabItem16( HWND16 hwndDlg, HWND16 hwndCtrl,
                                   BOOL16 fPrevious )
{
    return HWND_16( GetNextDlgTabItem( WIN_Handle32(hwndDlg), WIN_Handle32(hwndCtrl), fPrevious ));
}


/***********************************************************************
 *		DialogBoxParam (USER.239)
 */
INT16 WINAPI DialogBoxParam16( HINSTANCE16 hInst, LPCSTR template,
                               HWND16 owner16, DLGPROC16 dlgProc, LPARAM param )
{
    HWND hwnd = 0;
    HRSRC16 hRsrc;
    HGLOBAL16 hmem;
    LPCVOID data;
    int ret = -1;

    if (!(hRsrc = FindResource16( hInst, template, (LPSTR)RT_DIALOG ))) return 0;
    if (!(hmem = LoadResource16( hInst, hRsrc ))) return 0;
    if ((data = LockResource16( hmem )))
    {
        HWND owner = WIN_Handle32(owner16);
        hwnd = DIALOG_CreateIndirect16( hInst, data, owner, dlgProc, param, TRUE );
        GlobalUnlock16( hmem );
        return hwnd;
    }
    FreeResource16( hmem );
    return ret;
}


/***********************************************************************
 *		DialogBoxIndirectParam (USER.240)
 */
INT16 WINAPI DialogBoxIndirectParam16( HINSTANCE16 hInst, HANDLE16 dlgTemplate,
                                       HWND16 owner16, DLGPROC16 dlgProc, LPARAM param )
{
    HWND hwnd, owner = WIN_Handle32( owner16 );
    LPCVOID ptr;

    if (!(ptr = GlobalLock16( dlgTemplate ))) return -1;
    hwnd = DIALOG_CreateIndirect16( hInst, ptr, owner, dlgProc, param, TRUE );
    GlobalUnlock16( dlgTemplate );
    return hwnd;
}


/***********************************************************************
 *		CreateDialogParam (USER.241)
 */
HWND16 WINAPI CreateDialogParam16( HINSTANCE16 hInst, LPCSTR dlgTemplate,
                                   HWND16 owner, DLGPROC16 dlgProc, LPARAM param )
{
    HWND16 hwnd = 0;
    HRSRC16 hRsrc;
    HGLOBAL16 hmem;
    LPCVOID data;

    TRACE("%04x,%s,%04x,%p,%ld\n",
          hInst, debugstr_a(dlgTemplate), owner, dlgProc, param );

    if (!(hRsrc = FindResource16( hInst, dlgTemplate, (LPSTR)RT_DIALOG ))) return 0;
    if (!(hmem = LoadResource16( hInst, hRsrc ))) return 0;
    if (!(data = LockResource16( hmem ))) hwnd = 0;
    else hwnd = CreateDialogIndirectParam16( hInst, data, owner, dlgProc, param );
    FreeResource16( hmem );
    return hwnd;
}


/***********************************************************************
 *		CreateDialogIndirectParam (USER.242)
 */
HWND16 WINAPI CreateDialogIndirectParam16( HINSTANCE16 hInst, LPCVOID dlgTemplate,
                                           HWND16 owner, DLGPROC16 dlgProc, LPARAM param )
{
    if (!dlgTemplate) return 0;
    return HWND_16( DIALOG_CreateIndirect16( hInst, dlgTemplate, WIN_Handle32(owner),
                                             dlgProc, param, FALSE ));
}


/**************************************************************************
 *              DlgDirSelectEx   (USER.422)
 */
BOOL16 WINAPI DlgDirSelectEx16( HWND16 hwnd, LPSTR str, INT16 len, INT16 id )
{
    return DlgDirSelectExA( WIN_Handle32(hwnd), str, len, id );
}


/**************************************************************************
 *              DlgDirSelectComboBoxEx   (USER.423)
 */
BOOL16 WINAPI DlgDirSelectComboBoxEx16( HWND16 hwnd, LPSTR str, INT16 len,
                                        INT16 id )
{
    return DlgDirSelectComboBoxExA( WIN_Handle32(hwnd), str, len, id );
}

LONG WINAPI GetDialogBaseUnits16(VOID)
{
    return GetDialogBaseUnits();
}
