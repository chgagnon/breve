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

#ifndef _SIMULATION_H
#define _SIMULATION_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

// M_PI not defined on some platforms?  bastards.

#ifndef M_PI
#define M_PI 3.14159265
#endif

// do not change the order of the includes--they depend on eachother

#include "util.h"

#include "simulationTypedefs.h"

#include "movie.h"
#include "image.h"
#include "integrate.h"
#include "gldraw.h"
#include "netsim.h"
#include "shape.h"
#include "patch.h"
#include "world.h"
#include "link.h"
#include "lightdetector.h"
#include "vclip.h"
#include "vclipData.h"
#include "camera.h"
#include "multibody.h"
#include "springs.h"
#include "worldObject.h"
#include "joint.h"
#include "volInt.h"
#include "terrain.h"

#endif /* _SIMULATION_H */