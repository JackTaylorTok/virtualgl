/* Copyright (C)2004 Landmark Graphics Corporation
 * Copyright (C)2005, 2006 Sun Microsystems, Inc.
 * Copyright (C)2009-2014 D. R. Commander
 *
 * This library is free software and may be redistributed and/or modified under
 * the terms of the wxWindows Library License, Version 3.1 or (at your option)
 * any later version.  The full license is in the LICENSE.txt file included
 * with this distribution.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * wxWindows Library License for more details.
 */

#include "pbdrawable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glxvisual.h"
#include "glext-vgl.h"
#include "tempctx.h"
#include "fakerconfig.h"
#include "vglutil.h"

using namespace vglutil;


extern Display *_localdpy;


#define checkgl(m) if(glerror()) _throw("Could not "m);

// Generic OpenGL error checker (0 = no errors)
static int glerror(void)
{
	int i, ret=0;
	i=glGetError();
	while(i!=GL_NO_ERROR)
	{
		ret=1;
		vglout.print("[VGL] ERROR: OpenGL error 0x%.4x\n", i);
		i=glGetError();
	}
	return ret;
}


Window create_window(Display *dpy, XVisualInfo *vis, int w, int h)
{
	Window win;
	XSetWindowAttributes wattrs;
	Colormap cmap;

	cmap=XCreateColormap(dpy, RootWindow(dpy, vis->screen), vis->visual,
		AllocNone);
	wattrs.background_pixel = 0;
	wattrs.border_pixel = 0;
	wattrs.colormap = cmap;
	wattrs.event_mask = 0;
	win = _XCreateWindow(dpy, RootWindow(dpy, vis->screen), 0, 0, w, h, 1,
		vis->depth, InputOutput, vis->visual,
		CWBackPixel | CWBorderPixel | CWEventMask | CWColormap, &wattrs);
	return win;
}


// Pbuffer constructor

glxdrawable::glxdrawable(int w, int h, GLXFBConfig config)
	: _cleared(false), _stereo(false), _drawable(0), _w(w), _h(h), _depth(0),
	_config(config), _format(0), _pm(0), _win(0), _ispixmap(false)
{
	if(!config || w<1 || h<1) _throw("Invalid argument");

	int pbattribs[]={GLX_PBUFFER_WIDTH, 0, GLX_PBUFFER_HEIGHT, 0,
		GLX_PRESERVED_CONTENTS, True, None};

	pbattribs[1]=w;  pbattribs[3]=h;
	_drawable=glXCreatePbuffer(_localdpy, config, pbattribs);
	if(!_drawable) _throw("Could not create Pbuffer");

	setvisattribs(config);
}


// Pixmap constructor

glxdrawable::glxdrawable(int w, int h, int depth, GLXFBConfig config,
	const int *attribs) : _cleared(false), _stereo(false), _drawable(0), _w(w),
	_h(h), _depth(depth), _config(config), _format(0), _pm(0), _win(0),
	_ispixmap(true)
{
	if(!config || w<1 || h<1 || depth<0) _throw("Invalid argument");

	XVisualInfo *vis=NULL;
	if((vis=_glXGetVisualFromFBConfig(_localdpy, config))==NULL)
		goto bailout;
	_win=create_window(_localdpy, vis, 1, 1);
	if(!_win) goto bailout;
	_pm=XCreatePixmap(_localdpy, _win, w, h, depth>0? depth:vis->depth);
	if(!_pm) goto bailout;
	_drawable=_glXCreatePixmap(_localdpy, config, _pm, attribs);
	if(!_drawable) goto bailout;

	setvisattribs(config);
	return;

	bailout:
	if(vis) XFree(vis);
	_throw("Could not create GLX pixmap");
}


void glxdrawable::setvisattribs(GLXFBConfig config)
{
	if(__vglServerVisualAttrib(config, GLX_STEREO)) _stereo=true;
	int pixelsize=__vglServerVisualAttrib(config, GLX_RED_SIZE)
		+__vglServerVisualAttrib(config, GLX_GREEN_SIZE)
		+__vglServerVisualAttrib(config, GLX_BLUE_SIZE)
		+__vglServerVisualAttrib(config, GLX_ALPHA_SIZE);
	if(pixelsize==32)
	{
		#ifdef GL_BGRA_EXT
		if(littleendian()) _format=GL_BGRA_EXT;
		else
		#endif
		_format=GL_RGBA;
	}
	else
	{
		#ifdef GL_BGR_EXT
		if(littleendian()) _format=GL_BGR_EXT;
		else
		#endif
		_format=GL_RGB;
	}
}


glxdrawable::~glxdrawable(void)
{
	if(_ispixmap)
	{
		if(_drawable)
		{
			_glXDestroyPixmap(_localdpy, _drawable);
			_drawable=0;
		}
		if(_pm) {XFreePixmap(_localdpy, _pm);  _pm=0;}
		if(_win) {_XDestroyWindow(_localdpy, _win);  _win=0;}
	}
	else
	{
		glXDestroyPbuffer(_localdpy, _drawable);
		_drawable=0;
	}
}


XVisualInfo *glxdrawable::visual(void)
{
	return _glXGetVisualFromFBConfig(_localdpy, _config);
}


void glxdrawable::clear(void)
{
	if(_cleared) return;
	_cleared=true;
	GLfloat params[4];
	_glGetFloatv(GL_COLOR_CLEAR_VALUE, params);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(params[0], params[1], params[2], params[3]);
}


void glxdrawable::swap(void)
{
	_glXSwapBuffers(_localdpy, _drawable);
}


// This class encapsulates the relationship between an X11 drawable and the
// 3D off-screen drawable that backs it.

pbdrawable::pbdrawable(Display *dpy, Drawable drawable)
{
	if(!dpy || !drawable) _throw("Invalid argument");
	_dpy=dpy;  _drawable=drawable;
	_pb=NULL;
	_prof_rb.setName("Readback  ");
	_autotestframecount=0;
	_config=0;
	_ctx=0;
	_direct=-1;
}


pbdrawable::~pbdrawable(void)
{
	_mutex.lock(false);
	if(_pb) {delete _pb;  _pb=NULL;}
	if(_ctx) {_glXDestroyContext(_localdpy, _ctx);  _ctx=0;}
	_mutex.unlock(false);
}


int pbdrawable::init(int w, int h, GLXFBConfig config)
{
	static bool alreadyprinted=false;
	if(!config || w<1 || h<1) _throw("Invalid argument");

	CS::SafeLock l(_mutex);
	if(_pb && _pb->width()==w && _pb->height()==h
		&& _FBCID(_pb->config())==_FBCID(config)) return 0;
	if(fconfig.drawable==RRDRAWABLE_PIXMAP)
	{
		if(!alreadyprinted && fconfig.verbose)
		{
			vglout.println("[VGL] Using Pixmaps for rendering");
			alreadyprinted=true;
		}
		_pb=new glxdrawable(w, h, 0, config, NULL);
	}
	else
	{
		if(!alreadyprinted && fconfig.verbose)
		{
			vglout.println("[VGL] Using Pbuffers for rendering");
			alreadyprinted=true;
		}
		_pb=new glxdrawable(w, h, config);
	}
	if(_config && _FBCID(config)!=_FBCID(_config) && _ctx)
	{
		_glXDestroyContext(_localdpy, _ctx);  _ctx=0;
	}
	_config=config;
	return 1;
}


void pbdrawable::setdirect(Bool direct)
{
	if(direct!=True && direct!=False) return;
	if(direct!=_direct && _ctx)
	{
		_glXDestroyContext(_localdpy, _ctx);  _ctx=0;
	}
	_direct=direct;
}


void pbdrawable::clear(void)
{
	CS::SafeLock l(_mutex);
	if(_pb) _pb->clear();
}


// Get the current 3D off-screen drawable

GLXDrawable pbdrawable::getglxdrawable(void)
{
	GLXDrawable retval=0;
	CS::SafeLock l(_mutex);
	retval=_pb->drawable();
	return retval;
}


Display *pbdrawable::get2ddpy(void)
{
	return _dpy;
}


Drawable pbdrawable::getx11drawable(void)
{
	return _drawable;
}


static const char *formatString(int format)
{
	switch(format)
	{
		case GL_RGB:  return "RGB";
		case GL_RGBA:  return "RGBA";
		#ifdef GL_BGR_EXT
		case GL_BGR:  return "BGR";
		#endif
		#ifdef GL_BGRA_EXT
		case GL_BGRA:  return "BGRA";
		#endif
		#ifdef GL_ABGR_EXT
		case GL_ABGR_EXT:  return "ABGR";
		#endif
		case GL_COLOR_INDEX:  return "INDEX";
		case GL_RED:  case GL_GREEN:  case GL_BLUE:
			return "COMPONENT";
		default:  return "????";
	}
}


void pbdrawable::readpixels(GLint x, GLint y, GLint w, GLint pitch, GLint h,
	GLenum format, int ps, GLubyte *bits, GLint buf, bool stereo)
{
	#ifdef GL_VERSION_1_5
	static GLuint pbo=0;
	#endif
	double t0, tRead, tTotal;
	static int numSync=0, numFrames=0, lastFormat=-1;
	static bool usepbo=(fconfig.readback==RRREAD_PBO);
	static bool alreadyprinted=false, alreadywarned=false;
	static const char *ext=NULL;

	// Whenever the readback format changes (perhaps due to switching
	// compression or transports), then reset the PBO synchronicity detector
	int currentFormat=(format==GL_GREEN || format==GL_BLUE)? GL_RED:format;
	if(lastFormat>=0 && lastFormat!=currentFormat)
	{
		usepbo=(fconfig.readback==RRREAD_PBO);
		numSync=numFrames=0;
		alreadyprinted=alreadywarned=false;
	}
	lastFormat=currentFormat;

	GLXDrawable read=_glXGetCurrentDrawable();
	GLXDrawable draw=_glXGetCurrentDrawable();
	if(read==0) read=getglxdrawable();
	if(draw==0) draw=getglxdrawable();

	if(!_ctx)
	{
		if(!isinit())
			_throw("pbdrawable instance has not been fully initialized");
		if((_ctx=_glXCreateNewContext(_localdpy, _config, GLX_RGBA_TYPE, NULL,
			_direct))==0)
			_throw("Could not create OpenGL context for readback");
	}
	tempctx tc(_localdpy, draw, read, _ctx, _config, GLX_RGBA_TYPE);

	glReadBuffer(buf);

	if(pitch%8==0) glPixelStorei(GL_PACK_ALIGNMENT, 8);
	else if(pitch%4==0) glPixelStorei(GL_PACK_ALIGNMENT, 4);
	else if(pitch%2==0) glPixelStorei(GL_PACK_ALIGNMENT, 2);
	else if(pitch%1==0) glPixelStorei(GL_PACK_ALIGNMENT, 1);

	if(usepbo)
	{
		if(!ext)
		{
			ext=(const char *)glGetString(GL_EXTENSIONS);
			if(!ext || !strstr(ext, "GL_ARB_pixel_buffer_object"))
				_throw("GL_ARB_pixel_buffer_object extension not available");
		}
		#ifdef GL_VERSION_1_5
		if(!pbo) glGenBuffers(1, &pbo);
		if(!pbo) _throw("Could not generate pixel buffer object");
		if(!alreadyprinted && fconfig.verbose)
		{
			vglout.println("[VGL] Using pixel buffer objects for readback (%s --> %s)",
				formatString(_pb->format()), formatString(format));
			alreadyprinted=true;
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER_EXT, pbo);
		int size=0;
		glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER_EXT, GL_BUFFER_SIZE, &size);
		if(size!=pitch*h)
			glBufferData(GL_PIXEL_PACK_BUFFER_EXT, pitch*h, NULL, GL_STREAM_READ);
		glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER_EXT, GL_BUFFER_SIZE, &size);
		if(size!=pitch*h)
			_throw("Could not set PBO size");
		#else
		_throw("PBO support not compiled in.  Rebuild VGL on a system that has OpenGL 1.5.");
		#endif
	}
	else
	{
		if(!alreadyprinted && fconfig.verbose)
		{
			vglout.println("[VGL] Using synchronous readback (%s --> %s)",
				formatString(_pb->format()), formatString(format));
			alreadyprinted=true;
		}
	}

	int e=glGetError();
	while(e!=GL_NO_ERROR) e=glGetError();  // Clear previous error
	_prof_rb.startFrame();
	if(usepbo) t0=getTime();
	glReadPixels(x, y, w, h, format, GL_UNSIGNED_BYTE, usepbo? NULL:bits);

	if(usepbo)
	{
		tRead=getTime()-t0;
		#ifdef GL_VERSION_1_5
		unsigned char *pbobits=NULL;
		pbobits=(unsigned char *)glMapBuffer(GL_PIXEL_PACK_BUFFER_EXT,
			GL_READ_ONLY);
		if(!pbobits) _throw("Could not map pixel buffer object");
		memcpy(bits, pbobits, pitch*h);
		if(!glUnmapBuffer(GL_PIXEL_PACK_BUFFER_EXT))
			_throw("Could not unmap pixel buffer object");
		glBindBuffer(GL_PIXEL_PACK_BUFFER_EXT, 0);
		#endif
		tTotal=getTime()-t0;
		numFrames++;
		if(tRead/tTotal>0.5 && numFrames<=10)
		{
			numSync++;
			if(numSync>=10 && !alreadywarned && fconfig.verbose)
			{
				vglout.println("[VGL] NOTICE: PBO readback is not behaving asynchronously.  Disabling PBOs.");
				if(format!=_pb->format())
				{
					vglout.println("[VGL]    This could be due to a mismatch between the readback pixel format");
					vglout.println("[VGL]    (%s) and the Pbuffer pixel format (%s).",
						formatString(format), formatString(_pb->format()));
					if(((_pb->format()==GL_BGRA && format==GL_BGR)
						|| (_pb->format()==GL_RGBA && format==GL_RGB))
						&& fconfig.forcealpha)
						vglout.println("[VGL]    Try setting VGL_FORCEALPHA=0.");
					else if(((_pb->format()==GL_BGR && format==GL_BGRA)
						|| (_pb->format()==GL_RGB && format==GL_RGBA))
						&& !fconfig.forcealpha)
						vglout.println("[VGL]    Try setting VGL_FORCEALPHA=1.");
				}
				alreadywarned=true;
			}
		}
	}

	_prof_rb.endFrame(w*h, 0, stereo? 0.5 : 1);
	checkgl("Read Pixels");

	// If automatic faker testing is enabled, store the FB color in an
	// environment variable so the test program can verify it
	if(fconfig.autotest)
	{
		unsigned char *rowptr, *pixel;  int match=1;
		int color=-1, i, j, k;
		color=-1;
		if(buf!=GL_FRONT_RIGHT && buf!=GL_BACK_RIGHT) _autotestframecount++;
		for(j=0, rowptr=bits; j<h && match; j++, rowptr+=pitch)
			for(i=1, pixel=&rowptr[ps]; i<w && match; i++, pixel+=ps)
				for(k=0; k<ps; k++)
				{
					if(pixel[k]!=rowptr[k]) {match=0;  break;}
				}
		if(match)
		{
			if(format==GL_COLOR_INDEX)
			{
				unsigned char index;
				glReadPixels(0, 0, 1, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, &index);
				color=index;
			}
			else
			{
				unsigned char rgb[3];
				glReadPixels(0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
				color=rgb[0]+(rgb[1]<<8)+(rgb[2]<<16);
			}
		}
		if(buf==GL_FRONT_RIGHT || buf==GL_BACK_RIGHT)
		{
			snprintf(_autotestrclr, 79, "__VGL_AUTOTESTRCLR%x=%d",
				(unsigned int)_drawable, color);
			putenv(_autotestrclr);
		}
		else
		{
			snprintf(_autotestclr, 79, "__VGL_AUTOTESTCLR%x=%d",
				(unsigned int)_drawable, color);
			putenv(_autotestclr);
		}
		snprintf(_autotestframe, 79, "__VGL_AUTOTESTFRAME%x=%d",
			(unsigned int)_drawable, _autotestframecount);
		putenv(_autotestframe);
	}
}


void pbdrawable::copypixels(GLint src_x, GLint src_y, GLint w, GLint h,
	GLint dest_x, GLint dest_y, GLXDrawable draw)
{
	if(!_ctx)
	{
		if(!isinit())
			_throw("pbdrawable instance has not been fully initialized");
		if((_ctx=_glXCreateNewContext(_localdpy, _config, GLX_RGBA_TYPE, NULL,
			_direct))==0)
			_throw("Could not create OpenGL context for readback");
	}
	tempctx tc(_localdpy, draw, getglxdrawable(), _ctx, _config, GLX_RGBA_TYPE);

	glReadBuffer(GL_FRONT);
	_glDrawBuffer(GL_FRONT_AND_BACK);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	int e=glGetError();
	while(e!=GL_NO_ERROR) e=glGetError();  // Clear previous error

	_glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, w, 0, h, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	for(GLint i=0; i<h; i++)
	{
		glRasterPos2i(dest_x, h-dest_y-i-1);
		glCopyPixels(src_x, h-src_y-i-1, w, 1, GL_COLOR);
	}
	checkgl("Copy Pixels");

	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
}
