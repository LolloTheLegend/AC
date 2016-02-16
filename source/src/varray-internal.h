/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   varray-internal.h
 * Author: edav
 *
 * Created on February 16, 2016, 2:01 PM
 */

#ifndef VARRAY_INTERNAL_H
#define VARRAY_INTERNAL_H

#include "tools.h"
#include <GL/gl.h>

namespace varray
{
    struct attribinfo
    {
        int type, size, formatsize, offset;
        GLenum format;

        attribinfo() : type(0), size(0), formatsize(0), offset(0), format(GL_FALSE) {}

        bool operator==(const attribinfo &a) const
        {
            return type == a.type && size == a.size && format == a.format && offset == a.offset;
        }
        bool operator!=(const attribinfo &a) const
        {
            return type != a.type || size != a.size || format != a.format || offset != a.offset;
        }
    };

    vector<uchar> data;
    static attribinfo attribs[MAXATTRIBS], lastattribs[MAXATTRIBS];
    static int enabled = 0, numattribs = 0, attribmask = 0, numlastattribs = 0, lastattribmask = 0, vertexsize = 0, lastvertexsize = 0;
    static GLenum primtype = GL_TRIANGLES;
    static uchar *lastbuf = NULL;
    static bool changedattribs = false;

    void enable()
    {
        enabled = 0;
        numlastattribs = lastattribmask = lastvertexsize = 0;
        lastbuf = NULL;
    }

    void begin(GLenum mode)
    {
        primtype = mode;
    }

    void defattrib(int type, int size, GLenum format)
    {
        if(type == ATTRIB_VERTEX)
        {
            numattribs = attribmask = 0;
            vertexsize = 0;
        }
        changedattribs = true;
        attribmask |= type;
        attribinfo &a = attribs[numattribs++];
        a.type = type;
        a.size = size;
        a.format = format;
        switch(format)
        {
            case GL_UNSIGNED_BYTE:  a.formatsize = 1; break;
            case GL_BYTE:           a.formatsize = 1; break;
            case GL_UNSIGNED_SHORT: a.formatsize = 2; break;
            case GL_SHORT:          a.formatsize = 2; break;
            case GL_UNSIGNED_INT:   a.formatsize = 4; break;
            case GL_INT:            a.formatsize = 4; break;
            case GL_FLOAT:          a.formatsize = 4; break;
            case GL_DOUBLE:         a.formatsize = 8; break;
            default:                a.formatsize = 0; break;
        }
        a.formatsize *= size;
        a.offset = vertexsize;
        vertexsize += a.formatsize;
    }

    static inline void setattrib(const attribinfo &a, uchar *buf)
    {
        switch(a.type)
        {
            case ATTRIB_VERTEX:
                if(!(enabled&a.type)) glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(a.size, a.format, vertexsize, buf);
                break;
            case ATTRIB_COLOR:
                if(!(enabled&a.type)) glEnableClientState(GL_COLOR_ARRAY);
                glColorPointer(a.size, a.format, vertexsize, buf);
                break;
            case ATTRIB_NORMAL:
                if(!(enabled&a.type)) glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(a.format, vertexsize, buf);
                break;
            case ATTRIB_TEXCOORD0:
                if(!(enabled&a.type)) glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(a.size, a.format, vertexsize, buf);
                break;
            case ATTRIB_TEXCOORD1:
                glClientActiveTexture_(GL_TEXTURE1_ARB);
                if(!(enabled&a.type)) glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(a.size, a.format, vertexsize, buf);
                glClientActiveTexture_(GL_TEXTURE0_ARB);
                break;
        }
        enabled |= a.type;
    }

    static inline void unsetattrib(const attribinfo &a)
    {
        switch(a.type)
        {
            case ATTRIB_VERTEX:
                glDisableClientState(GL_VERTEX_ARRAY);
                break;
            case ATTRIB_COLOR:
                glDisableClientState(GL_COLOR_ARRAY);
                break;
            case ATTRIB_NORMAL:
                glDisableClientState(GL_NORMAL_ARRAY);
                break;
            case ATTRIB_TEXCOORD0:
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                break;
            case ATTRIB_TEXCOORD1:
                glClientActiveTexture_(GL_TEXTURE1_ARB);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                glClientActiveTexture_(GL_TEXTURE0_ARB);
                break;
        }
        enabled &= ~a.type;
    }

    int end()
    {
        if(data.empty()) return 0;
        uchar *buf = data.getbuf();
        bool forceattribs = numattribs != numlastattribs || vertexsize != lastvertexsize || buf != lastbuf;
        if(forceattribs || changedattribs)
        {
            int diffmask = enabled & lastattribmask & ~attribmask;
            if(diffmask) loopi(numlastattribs)
            {
                const attribinfo &a = lastattribs[i];
                if(diffmask & a.type) unsetattrib(a);
            }
            uchar *src = buf;
            loopi(numattribs)
            {
                const attribinfo &a = attribs[i];
                if(forceattribs || a != lastattribs[i])
                {
                    setattrib(a, src);
                    lastattribs[i] = a;
                }
                src += a.formatsize;
            }
            lastbuf = buf;
            numlastattribs = numattribs;
            lastattribmask = attribmask;
            lastvertexsize = vertexsize;
            changedattribs = false;
        }
        int numvertexes = data.length()/vertexsize;
        glDrawArrays(primtype, 0, numvertexes);
        data.setsize(0);
        return numvertexes;
    }

    void disable()
    {
        if(!enabled) return;
        if(enabled&ATTRIB_VERTEX) glDisableClientState(GL_VERTEX_ARRAY);
        if(enabled&ATTRIB_COLOR) glDisableClientState(GL_COLOR_ARRAY);
        if(enabled&ATTRIB_NORMAL) glDisableClientState(GL_NORMAL_ARRAY);
        if(enabled&ATTRIB_TEXCOORD0) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        if(enabled&ATTRIB_TEXCOORD1)
        {
            glClientActiveTexture_(GL_TEXTURE1_ARB);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glClientActiveTexture_(GL_TEXTURE0_ARB);
        }
        enabled = 0;
    }
}

#endif /* VARRAY_INTERNAL_H */

