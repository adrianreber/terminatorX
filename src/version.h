/*
    terminatorX - realtime audio scratching software
    Copyright (C) 1999-2021  Alexander König

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    File: version.h

    Description: pretty dumb version management header...
*/

#ifndef _H_VERSION
#define _H_VERSION

#ifdef HAVE_CONFIG_H
#include <config.h>
#define VERSIONSTRING PACKAGE " release " VERSION
#else
#define VERSION "4.1.0"
#define VERSIONSTRING "terminatorX release 4.1.0"
#endif

#endif
