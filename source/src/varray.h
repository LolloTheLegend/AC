#ifndef VARRAY_H
#define VARRAY_H

#include "tools.h"

namespace varray
{
    enum
    {
        ATTRIB_VERTEX    = 1<<0,
        ATTRIB_COLOR     = 1<<1,
        ATTRIB_NORMAL    = 1<<2,
        ATTRIB_TEXCOORD0 = 1<<3,
        ATTRIB_TEXCOORD1 = 1<<4,
        MAXATTRIBS       = 5
    };

    extern vector<uchar> data;

    extern void enable();
    extern void begin(GLenum mode);
    extern void defattrib(int type, int size, GLenum format);

    template<class T>
    static inline void attrib(T x)
    {
        T *buf = (T *)data.pad(sizeof(T));
        buf[0] = x;
    }

    template<class T>
    static inline void attrib(T x, T y)
    {
        T *buf = (T *)data.pad(2*sizeof(T));
        buf[0] = x;
        buf[1] = y;
    }

    template<class T>
    static inline void attrib(T x, T y, T z)
    {
        T *buf = (T *)data.pad(3*sizeof(T));
        buf[0] = x;
        buf[1] = y;
        buf[2] = z;
    }

    template<class T>
    static inline void attrib(T x, T y, T z, T w)
    {
        T *buf = (T *)data.pad(4*sizeof(T));
        buf[0] = x;
        buf[1] = y;
        buf[2] = z;
        buf[3] = w;
    }

    template<size_t N, class T>
    static inline void attribv(const T *v)
    {
        data.put((const uchar *)v, N*sizeof(T));
    }

    extern int end();
    extern void disable();

}

#endif	// VARRAY_H

