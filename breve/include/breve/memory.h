/*****************************************************************************
 *                                                                           *
 * The breve Simulation Environment                                          *
 * Copyright (C) 2000, 2001, 2002, 2003 Jonathan Klein                       *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License as published by      *
 * the Free Software Foundation; either version 2 of the License, or         *
 * (at your option) any later version.                                       *
 *                                                                           *
 * This program is distributed in the hope that it will be useful,           *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU General Public License for more details.                              *
 *                                                                           *
 * You should have received a copy of the GNU General Public License         *
 * along with this program; if not, write to the Free Software               *
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA *
 *****************************************************************************/

#ifndef _MEMORY_H
#define _MEMORY_H

#include <stdlib.h>
#include <string.h>

#include "config.h"

#define slMalloc(n) calloc(1,n)
#define slRealloc(p,n) realloc(p,n)
#define slFree(p) free(p)

#if HAVE_STRDUP
#define slStrdup(s) strdup(s)
#else
DLLEXPORT char *slStrdup(const char *);
#endif

inline char *slStrdupAndFree(char *);

#endif
