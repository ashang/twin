

#ifdef CONF_HW_TTY_TERMCAP


INLINE void termcap_SetCursorType(uldat type) {
    fprintf(stdOUT, "%s", (type & 0xFFFFFFl) == NOCURSOR ? tc_cursor_off : tc_cursor_on);
}
INLINE void termcap_MoveToXY(udat x, udat y) {
    fputs(tgoto(tc_cursor_goto, x, y), stdOUT);
}


static udat termcap_LookupKey(udat *ShiftFlags, byte *slen, byte *s, byte *retlen, byte **ret) {
    struct linux_keys {
	udat k;
	byte l, *s;
    };
    static struct linux_keys CONST linux_key[] = {
# define IS(k, l, s) { CAT(TW_,k), l, s },
IS(F1,		4, "\033[[A")
IS(F2,		4, "\033[[B")
IS(F3,		4, "\033[[C")
IS(F4,		4, "\033[[D")
IS(F5,		4, "\033[[E")
IS(F6,		5, "\033[17~")
IS(F7,		5, "\033[18~")
IS(F8,		5, "\033[19~")
IS(F9,		5, "\033[20~")
IS(F10,		5, "\033[21~")
IS(F11,		5, "\033[23~")
IS(F12,		5, "\033[24~")

IS(Pause,       3, "\033[P")
IS(Home,        4, "\033[1~")
IS(End,         4, "\033[4~")
IS(Delete,	4, "\033[3~")
IS(Insert,	4, "\033[2~")
IS(Next,	4, "\033[6~")
IS(Prior,	4, "\033[5~")

IS(Left,	3, "\033[D")
IS(Up,		3, "\033[A")
IS(Right,	3, "\033[C")
IS(Down,	3, "\033[B")
#undef IS
    };
    struct linux_keys CONST *lk;
    
    byte **key;
    byte keylen, len = *slen;
   
    *ret = s;
    *ShiftFlags = 0;

    if (len == 0) {
	*retlen = len;
	return TW_Null;
    }
    
    if (len > 1 && *s == '\033') {
	
	if (len == 2 && s[1] >= ' ' && s[1] <= '~') {
	    /* try to handle ALT + <some key> */
	    *slen = *retlen = len;
	    *ShiftFlags = KBD_ALT_FL;
	    return (udat)s[1];
	}
	
	for (key = tc_cap + tc_key_first; key < tc_cap + tc_key_last; key++) {
	    if (*key && **key && (keylen = strlen(*key)) <= len && !memcmp(*key, s, keylen)) {
		lk = linux_key + (key - &tc_cap[tc_key_first]);
	        *slen = keylen;
		*retlen = lk->l;
		*ret = lk->s;
		return lk->k;
	    }
	}

	/*
	 * return stdin_LookupKey(ShiftFlags, slen, s, retlen, ret);
	 */
    }

    *slen = *retlen = 1;
    
    switch (*s) {
      case TW_Tab:
      case TW_Linefeed:
      case TW_Return:
      case TW_Escape:
	return (udat)*s;
      default:
	if (*s < ' ') {
	    /* try to handle CTRL + <some key> */
	    *ShiftFlags = KBD_CTRL_FL;
	    return (udat)*s | 0x40;
	}
	return (udat)*s;
    }
    
    /* NOTREACHED */
    return TW_Null;
}

static char *termcap_extract(CONST char *cap, byte **dest) {
    char buf[20], *d = buf, *s = tgetstr(cap, &d);

    if (!s || !*s) {
	return *dest = CloneStr("");
    }
    /*
     * Remove the padding information. We assume that no terminals
     * need padding nowadays. At least it makes things much easier.
     */
    s += strspn(s, "0123456789*");
    
    for (d = buf; *s; *d++ = *s++) {
	if (*s == '$' && s[1] == '<')
	    if (!(s = strchr(s, '>')) || !*++s)
		break;
    }
    *d = '\0';
    return *dest = CloneStr(buf);
}



static void termcap_cleanup(void) {
    byte **n;
    
    for (n = tc_cap; n < tc_cap + tc_cap_N; n++)
	if (*n)
	    FreeMem(*n);
}

static void fixup_colorbug(void) {
    uldat len = LenStr(tc_attr_off);
    byte *s = AllocMem( len + 9);
    
    if (s) {
	CopyMem(tc_attr_off, s, len);
	CopyMem("\033[37;40m", s + len, 9);
	FreeMem(tc_attr_off);
	tc_attr_off = s;
    }
}

static byte termcap_InitVideo(void) {
    CONST byte *term = tty_TERM;
    CONST char *tc_name[tc_cap_N + 1] = {
        "cl", "cm", "ve", "vi", "md", "mb", "me", "ks", "ke", "bl", "as", "ae",
        "k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "k9", "k;", "F1", "F2",
        "&7", "kh", "@7", "kD", "kI", "kN", "kP", "kl", "ku", "kr", "kd", NULL
    };
    CONST char **n;
    byte **d;
    char tcbuf[4096];		/* by convention, this is enough */

    if (!term) {
	printk("      termcap_InitVideo() failed: unknown terminal type.\n");
	return tfalse;
    }

    switch (tgetent(tcbuf, term)) {
      case 1:
	break;
      case 0:
	printk("      termcap_InitVideo() failed: no entry for `%."STR(TW_SMALLBUFF)"s' in the terminal database.\n"
	       "      Please set your $TERM environment variable correctly.\n", term);
	return tfalse;
      default:
	printk("      termcap_InitVideo() failed: system call error in tgetent(): %."STR(TW_SMALLBUFF)"s\n",
               strerror(errno));
	return tfalse;
    }
    
    tty_is_xterm = !strncmp(term, "xterm", 5);
    
    /* do not use alternate character set mode, i.e. "as" and "ae" ... it causes more problems than it solves */
    tc_name[tc_seq_charset_start] = tc_name[tc_seq_charset_end] = "";
    tc_charset_start = tc_charset_end = NULL;
    
    for (n = tc_name, d = tc_cap; *n; n++, d++) {
	if (**n && !termcap_extract(*n, d)) {
	    printk("      termcap_InitVideo() failed: Out of memory!\n");
	    termcap_cleanup();
	    return tfalse;
	}
    }
    
    if (!*tc_cursor_goto) {
	printk("      termcap_InitVideo() failed: terminal misses `cursor goto' capability\n");
	termcap_cleanup();
	return tfalse;
    }
    
    if (tty_use_utf8 == ttrue+ttrue) {
	/* cannot really autodetect an utf8-capable terminal... use a whitelist */
        uldat termlen = LenStr(term);
	tty_use_utf8 = ((termlen == 5 && (!CmpMem(term, "xterm", 5) || !CmpMem(term, "linux", 5))) ||
                        (termlen >= 6 && !CmpMem(term, "xterm-", 6)) ||
                        (termlen >= 12 && !CmpMem(term, "rxvt-unicode", 12)));
    }
    if (tty_use_utf8 == ttrue) {
        if (!(tc_charset_start = CloneStr("\033%G")) || !(tc_charset_end = CloneStr("\033%@")))
        {
	    printk("      termcap_InitVideo() failed: Out of memory!\n");
	    termcap_cleanup();
	    return tfalse;
        }
    }
    
    wrapglitch = tgetflag("xn");
    if (colorbug)
	fixup_colorbug();
    
    fprintf(stdOUT, "%s%s%s", tc_attr_off, (tc_charset_start ? (CONST char *)tc_charset_start : ""), (tty_is_xterm ? "\033[?1h" : ""));
    
    HW->FlushVideo = termcap_FlushVideo;
    HW->FlushHW = stdout_FlushHW;

    HW->ShowMouse = termcap_ShowMouse;
    HW->HideMouse = termcap_HideMouse;
    HW->UpdateMouseAndCursor = termcap_UpdateMouseAndCursor;
    
    HW->DetectSize  = stdin_DetectSize;
    HW->CheckResize = stdin_CheckResize;
    HW->Resize      = stdin_Resize;
    
    HW->HWSelectionImport  = AlwaysFalse;
    HW->HWSelectionExport  = NoOp;
    HW->HWSelectionRequest = (void *)NoOp;
    HW->HWSelectionNotify  = (void *)NoOp;
    HW->HWSelectionPrivate = 0;

    HW->CanDragArea = termcap_CanDragArea;
    HW->DragArea = termcap_DragArea;
   
    HW->XY[0] = HW->XY[1] = -1;
    HW->TT = -1; /* force updating the cursor */
	
    HW->Beep = termcap_Beep;
    HW->Configure = termcap_Configure;
    HW->ConfigureKeyboard = termcap_ConfigureKeyboard;
    HW->SetPalette = (void *)NoOp;
    HW->ResetPalette = (void *)NoOp;

    HW->QuitVideo = termcap_QuitVideo;
    
    HW->FlagsHW |= FlHWNeedOldVideo;
    HW->FlagsHW &= ~FlHWExpensiveFlushVideo;
    HW->NeedHW = 0;
    HW->merge_Threshold = 0;
    
    LookupKey = termcap_LookupKey;
    
    return ttrue;
}

static void termcap_QuitVideo(void) {
    
    termcap_MoveToXY(0, DisplayHeight-1);
    termcap_SetCursorType(LINECURSOR);
    /* reset colors and charset */
    fprintf(stdOUT, "%s%s", tc_attr_off, tc_charset_end ? tc_charset_end : (byte *)"");
    
    /* restore original alt cursor keys, keypad settings */
    HW->Configure(HW_KBDAPPLIC, ttrue, 0);
    HW->Configure(HW_ALTCURSKEYS, ttrue, 0);
    
    termcap_cleanup();
    
    HW->QuitVideo = NoOp;
}


#define termcap_MogrifyInit() fputs(tc_attr_off, stdOUT); _col = COL(WHITE,BLACK)
#define termcap_MogrifyFinish() do { } while (0)

INLINE byte *termcap_CopyAttr(byte *attr, byte *dest) {
    while ((*dest++ = *attr++))
	;
    return --dest;
}
	
INLINE void termcap_SetColor(hwcol col) {
    static byte colbuf[80];
    byte c, *colp = colbuf;
    
    if ((col & COL(HIGH,HIGH)) != (_col & COL(HIGH,HIGH))) {
	
	if (((_col & COL(0,HIGH)) && !(col & COL(0,HIGH))) ||
	    ((_col & COL(HIGH,0)) && !(col & COL(HIGH,0)))) {
	    
	    /* cannot turn off blinking or standout, reset everything */
	    colp = termcap_CopyAttr(tc_attr_off, colp);
	    _col = COL(WHITE,BLACK);
	}
	if ((col & COL(HIGH,0)) && !(_col & COL(HIGH,0)))
	    colp = termcap_CopyAttr(tc_bold_on, colp);
	if ((col & COL(0,HIGH)) && !(_col & COL(0,HIGH)))
	    colp = termcap_CopyAttr(tc_blink_on, colp);
    }
    
    
    if ((col & COL(WHITE,WHITE)) != (_col & COL(WHITE,WHITE))) {
	*colp++ = '\033'; *colp++ = '[';
        
	if ((col & COL(WHITE,0)) != (_col & COL(WHITE,0))) {
	    *colp++ = '3';
	    c = COLFG(col) & ~HIGH;
	    *colp++ = VGA2ANSI(c) + '0';
	    *colp++ = ';';
	}
        
	if ((col & COL(0,WHITE)) != (_col & COL(0,WHITE))) {
	    *colp++ = '4';
	    c = COLBG(col) & ~HIGH;
	    *colp++ = VGA2ANSI(c) + '0';
	    *colp++ = 'm';
	} else if (colp[-1] == ';') {
            colp[-1] = 'm';
        }
    }
    
    *colp = '\0';
    _col = col;
    
    fputs(colbuf, stdOUT);
}


INLINE void termcap_Mogrify(dat x, dat y, uldat len) {
    uldat delta = x + y * (uldat)DisplayWidth;
    hwattr *V, *oV;
    hwcol col;
    hwfont c, _c;
    byte sending = tfalse;
    
    if (!wrapglitch && delta + len >= (uldat)DisplayWidth * DisplayHeight)
	len = (uldat)DisplayWidth * DisplayHeight - delta - 1;
    
    V = Video + delta;
    oV = OldVideo + delta;
    
    for (; len; V++, oV++, x++, len--) {
	if (!ValidOldVideo || *V != *oV) {
	    if (!sending)
		sending = ttrue, termcap_MoveToXY(x,y);
            
	    col = HWCOL(*V);
	    
	    if (col != _col)
		termcap_SetColor(col);
            
	    c = _c = HWFONT(*V);
            if (c >= 128) {
                if (tty_use_utf8) {
                    /* use utf-8 to output this non-ASCII char */
                    tty_MogrifyUTF8(_c);
                    continue;
                } else if (tty_charset_to_UTF_32[c] != c) {
                    c = tty_UTF_32_to_charset(_c);
                }
            }
            if (c < 32 || c == 127 || c == 128+27) {
                /* can't display it */
                c = Tutf_UTF_32_to_ASCII(_c);
                if (c < 32 || c >= 127)
                    c = 32;
            }
	    putc((char)c, stdOUT);
	} else
	    sending = tfalse;
    }
}

INLINE void termcap_SingleMogrify(dat x, dat y, hwattr V) {
    hwfont c, _c;
    
    if (!wrapglitch && x == DisplayWidth-1 && y == DisplayHeight-1)
        /* wrapglitch is required to write to last screen position without scrolling */
	return;
    
    termcap_MoveToXY(x,y);

    if (HWCOL(V) != _col)
	termcap_SetColor(HWCOL(V));
	
    c = _c = HWFONT(V);
    if (c >= 128) {
        if (tty_use_utf8) {
            /* use utf-8 to output this non-ASCII char */
            tty_MogrifyUTF8(_c);
            return;
        } else if (tty_charset_to_UTF_32[c] != c) {
            c = tty_UTF_32_to_charset(_c);
        }
    }
    if (c < 32 || c == 127 || c == 128+27) {
        /* can't display it */
        c = Tutf_UTF_32_to_ASCII(_c);
        if (c < 32 || c >= 127)
            c = 32;
    }
    putc((char)c, stdOUT);
}

/* HideMouse and ShowMouse depend on Video setup, not on Mouse.
 * so we have vcsa_ and termcap_ versions, not GPM_ ones... */
static void termcap_ShowMouse(void) {
    uldat pos = (HW->Last_x = HW->MouseState.x) + (HW->Last_y = HW->MouseState.y) * (ldat)DisplayWidth;
    hwattr h  = Video[pos], 
	c = HWATTR_COLMASK(~h) ^ HWATTR(COL(HIGH,HIGH),0);
    
    termcap_SingleMogrify(HW->MouseState.x, HW->MouseState.y, c | HWATTR_FONTMASK(h));
    
    /* force updating the cursor */
    HW->XY[0] = HW->XY[1] = -1;
    setFlush();
}

static void termcap_HideMouse(void) {
    uldat pos = HW->Last_x + HW->Last_y * (ldat)DisplayWidth;
    
    termcap_SingleMogrify(HW->Last_x, HW->Last_y, Video[pos]);

    /* force updating the cursor */
    HW->XY[0] = HW->XY[1] = -1;
    setFlush();
}

static void termcap_Beep(void) {
    fputs(tc_audio_bell, stdOUT);
    setFlush();
}

static void termcap_UpdateCursor(void) {
    if (!ValidOldVideo || (CursorType != NOCURSOR && (CursorX != HW->XY[0] || CursorY != HW->XY[1]))) {
	termcap_MoveToXY(HW->XY[0] = CursorX, HW->XY[1] = CursorY);
	setFlush();
    }
    if (!ValidOldVideo || CursorType != HW->TT) {
	termcap_SetCursorType(HW->TT = CursorType);
	setFlush();
    }
}

static void termcap_UpdateMouseAndCursor(void) {
    if ((HW->FlagsHW & FlHWSoftMouse) && (HW->FlagsHW & FlHWChangedMouseFlag)) {
	HW->HideMouse();
	HW->ShowMouse();
	HW->FlagsHW &= ~FlHWChangedMouseFlag;
    }
    
    termcap_UpdateCursor();
}

static void termcap_ConfigureKeyboard(udat resource, byte todefault, udat value) {
    switch (resource) {
    case HW_KBDAPPLIC:
        if (tty_is_xterm) {
            fputs(todefault || !value ? tc_kpad_on : tc_kpad_off, stdOUT);
            /*
             * on xterm, tc_kpad_off has the undesired side-effect
             * of changing the sequences produced by cursor keys,
             * so we must restore the usual sequences
             */
            fputs("\033[?1h", stdOUT);
            setFlush();
        }
	break;
    case HW_ALTCURSKEYS:
	/*
	 fputs(todefault || !value ? "\033[?1l" : "\033[?1h", stdOUT);
	 setFlush();
	 */
	break;
    }
}

static void termcap_Configure(udat resource, byte todefault, udat value) {
    switch (resource) {
    case HW_KBDAPPLIC:
    case HW_ALTCURSKEYS:
	HW->ConfigureKeyboard(resource, todefault, value);
	break;
    case HW_BELLPITCH:
    case HW_BELLDURATION:
	/* unsupported */
	break;
    case HW_MOUSEMOTIONEVENTS:
	HW->ConfigureMouse(resource, todefault, value);
	break;
    default:
	break;
    }
}

static byte termcap_CanDragArea(dat Left, dat Up, dat Rgt, dat Dwn, dat DstLeft, dat DstUp) {
    return Left == 0 && Rgt == HW->X-1 && Dwn == HW->Y-1 && DstUp == 0;
}

static void termcap_DragArea(dat Left, dat Up, dat Rgt, dat Dwn, dat DstLeft, dat DstUp) {
    udat delta = Up - DstUp;
    
    HW->HideMouse();
    HW->FlagsHW |= FlHWChangedMouseFlag;
    
    fprintf(stdOUT, "%s\033[0m%s",			/* hide cursor, reset color */
	    tc_cursor_off, tgoto(tc_cursor_goto, 0, HW->Y-1));/* go to last line */
    
    while (delta--)
	putc('\n', stdOUT);
    
    setFlush();
    
    /* force updating the cursor */
    HW->XY[0] = HW->XY[1] = -1;
    
    /*
     * now the last trick: tty scroll erased the part
     * below DstUp + (Dwn - Up) so we must redraw it.
     */
    NeedRedrawVideo(0, DstUp + (Dwn - Up) + 1, HW->X - 1, HW->Y - 1);
}

static void termcap_FlushVideo(void) {
    dat i, j;
    dat start, end;
    byte FlippedVideo = tfalse, FlippedOldVideo = tfalse;
    hwattr savedOldVideo;
    
    if (!ChangedVideoFlag) {
	HW->UpdateMouseAndCursor();
	return;
    }
    
    /* hide the mouse if needed */
    
    /* first, check the old mouse position */
    if (HW->FlagsHW & FlHWSoftMouse) {
	if (HW->FlagsHW & FlHWChangedMouseFlag) {
	    /* dirty the old mouse position, so that it will be overwritten */
	    
	    /*
	     * with multi-display this is a hack, but since OldVideo gets restored
	     * below *BEFORE* returning from termcap_FlushVideo(), that's ok.
	     */
	    DirtyVideo(HW->Last_x, HW->Last_y, HW->Last_x, HW->Last_y);
	    if (ValidOldVideo) {
		FlippedOldVideo = ttrue;
		savedOldVideo = OldVideo[HW->Last_x + HW->Last_y * (ldat)DisplayWidth];
		OldVideo[HW->Last_x + HW->Last_y * (ldat)DisplayWidth]
		 = ~Video[HW->Last_x + HW->Last_y * (ldat)DisplayWidth];
	    }
	}
	
        i = HW->MouseState.x;
	j = HW->MouseState.y;
	/*
	 * instead of calling ShowMouse(),
	 * we flip the new mouse position in Video[] and dirty it if necessary.
	 */
	if ((HW->FlagsHW & FlHWChangedMouseFlag) || (FlippedVideo = Plain_isDirtyVideo(i, j))) {
	    VideoFlip(i, j);
	    if (!FlippedVideo)
		DirtyVideo(i, j, i, j);
	    HW->FlagsHW &= ~FlHWChangedMouseFlag;
	    FlippedVideo = ttrue;
	} else
	    FlippedVideo = tfalse;
    }
    
    termcap_MogrifyInit();
    if (HW->TT != NOCURSOR)
	termcap_SetCursorType(HW->TT = NOCURSOR);
    for (i=0; i<DisplayHeight*2; i++) {
	start = ChangedVideo[i>>1][i&1][0];
	end   = ChangedVideo[i>>1][i&1][1];
	
	if (start != -1)
	    termcap_Mogrify(start, i>>1, end-start+1);
    }
    
    /* force updating the cursor */
    HW->XY[0] = HW->XY[1] = -1;
    
    setFlush();
    
    /* ... and this redraws the mouse */
    if (HW->FlagsHW & FlHWSoftMouse) {
	if (FlippedOldVideo)
	    OldVideo[HW->Last_x + HW->Last_y * (ldat)DisplayWidth] = savedOldVideo;
	if (FlippedVideo)
	    VideoFlip(HW->Last_x = HW->MouseState.x, HW->Last_y = HW->MouseState.y);
	else if (HW->FlagsHW & FlHWChangedMouseFlag)
	    HW->ShowMouse();
    }
    
    termcap_UpdateCursor();
    
    HW->FlagsHW &= ~FlHWChangedMouseFlag;
}

#endif /* CONF_HW_TTY_TERMCAP */

