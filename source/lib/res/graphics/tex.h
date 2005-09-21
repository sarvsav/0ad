// texture loaders and support functions
//
// Copyright (c) 2003-2005 Jan Wassenberg
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; either version 2 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// Contact info:
//   Jan.Wassenberg@stud.uni-karlsruhe.de
//   http://www.stud.uni-karlsruhe.de/~urkt/

#ifndef __TEX_H__
#define __TEX_H__

#include "../handle.h"



// flags & TEX_DXT stores the DXT number (1, 3, or 5). we need a special
// value for DXT1a to obviate passing around an extra TEX_ALPHA flag.
// value is arbitrary; do not rely on its relative ordering!
const int DXT1A = 11;

enum TexFlags
{
	TEX_DXT   = 0x7,	// mask; value = {1,3,5}

	TEX_ALPHA = 0x10,
	TEX_GREY  = 0x20,

	TEX_BGR   = 0x08,

	// orientation - never returned by ogl_tex_load, since it automatically
	// flips to match global orientation. these are passed to tex_write
	// to indicate the image orientation, or to tex_set_global_orientation.
	TEX_BOTTOM_UP = 0x40,
	TEX_TOP_DOWN  = 0x80,
	TEX_ORIENTATION = TEX_BOTTOM_UP|TEX_TOP_DOWN,	// mask

	// mipmaps - if this flag is set, the image contains data for all
	// mipmap levels down to 1x1, stored contiguously.
	TEX_MIPMAPS = 0x100
};

// minimize size - stored in ogl tex resource control block
struct Tex
{
	Handle hm;			// H_Mem handle to loaded file
	size_t ofs;			// offset to image data in file
	uint w : 16;
	uint h : 16;
	uint bpp : 16;

	// describes the image format; indicates deviations from the
	// plain image format (e.g. BGR layout, upside-down).
	//
	// we don't want to just load as-is and set flags, thereby dumping the
	// burden of conversion on apps. instead, we convert as much as is
	// convenient and necessary at load-time. however, we do not go
	// overboard and support all<->all conversions (too complex) or
	// encourage transforms at runtime.
	// in release builds, prefer formats optimized for their intended use
	// that don't require any preprocessing (minimize load time!)
	uint flags : 16;
};


// supports BMP, TGA, JPG, JP2, PNG, DDS.
extern int tex_load(const char* fn, Tex* t);
extern int tex_free(Tex* t);

extern int tex_wrap(uint w, uint h, uint bpp, uint flags, void* img, Tex* t);

extern u8* tex_get_data(const Tex* t);
extern size_t tex_img_size(const Tex* t);
extern size_t tex_hdr_size(const char* fn);

extern int tex_transform(Tex* t, uint transforms);

extern int tex_write(const char* fn, uint w, uint h, uint bpp, uint flags, void* img);

// rationale: some libraries can flip images for free when loading, so set a
// global orientation rather than only flipping at upload time.
// param: either TEX_BOTTOM_UP or TEX_TOP_DOWN
extern void tex_set_global_orientation(int orientation);


// internal use only:
extern int tex_validate(uint line, const Tex* t);

#endif	// __TEX_H__
