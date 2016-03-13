#include "geom.h"

#include "tools.h"
#include "tools.h"

static inline float det2x2 ( float a, float b, float c, float d )
{
	return a * d - b * c;
}
static inline float det3x3 ( float a1,
                             float a2,
                             float a3,
                             float b1,
                             float b2,
                             float b3,
                             float c1,
                             float c2,
                             float c3 )
{
	return a1 * det2x2(b2, b3, c2, c3) - b1 * det2x2(a2, a3, c2, c3) + c1
	        * det2x2(a2, a3, b2, b3);
}

float glmatrixf::determinant () const
{
	float a1 = v[0], a2 = v[1], a3 = v[2], a4 = v[3], b1 = v[4], b2 = v[5], b3 =
	        v[6], b4 = v[7], c1 = v[8], c2 = v[9], c3 = v[10], c4 = v[11], d1 =
	        v[12], d2 = v[13], d3 = v[14], d4 = v[15];

	return a1 * det3x3(b2, b3, b4, c2, c3, c4, d2, d3, d4) - b1
	        * det3x3(a2, a3, a4, c2, c3, c4, d2, d3, d4)
	       + c1 * det3x3(a2, a3, a4, b2, b3, b4, d2, d3, d4)
	       - d1 * det3x3(a2, a3, a4, b2, b3, b4, c2, c3, c4);
}

void glmatrixf::adjoint ( const glmatrixf &m )
{
	float a1 = m.v[0], a2 = m.v[1], a3 = m.v[2], a4 = m.v[3], b1 = m.v[4], b2 =
	        m.v[5], b3 = m.v[6], b4 = m.v[7], c1 = m.v[8], c2 = m.v[9], c3 =
	        m.v[10], c4 = m.v[11], d1 = m.v[12], d2 = m.v[13], d3 = m.v[14],
	        d4 = m.v[15];

	v[0] = det3x3(b2, b3, b4, c2, c3, c4, d2, d3, d4);
	v[1] = -det3x3(a2, a3, a4, c2, c3, c4, d2, d3, d4);
	v[2] = det3x3(a2, a3, a4, b2, b3, b4, d2, d3, d4);
	v[3] = -det3x3(a2, a3, a4, b2, b3, b4, c2, c3, c4);

	v[4] = -det3x3(b1, b3, b4, c1, c3, c4, d1, d3, d4);
	v[5] = det3x3(a1, a3, a4, c1, c3, c4, d1, d3, d4);
	v[6] = -det3x3(a1, a3, a4, b1, b3, b4, d1, d3, d4);
	v[7] = det3x3(a1, a3, a4, b1, b3, b4, c1, c3, c4);

	v[8] = det3x3(b1, b2, b4, c1, c2, c4, d1, d2, d4);
	v[9] = -det3x3(a1, a2, a4, c1, c2, c4, d1, d2, d4);
	v[10] = det3x3(a1, a2, a4, b1, b2, b4, d1, d2, d4);
	v[11] = -det3x3(a1, a2, a4, b1, b2, b4, c1, c2, c4);

	v[12] = -det3x3(b1, b2, b3, c1, c2, c3, d1, d2, d3);
	v[13] = det3x3(a1, a2, a3, c1, c2, c3, d1, d2, d3);
	v[14] = -det3x3(a1, a2, a3, b1, b2, b3, d1, d2, d3);
	v[15] = det3x3(a1, a2, a3, b1, b2, b3, c1, c2, c3);
}

bool glmatrixf::invert ( const glmatrixf &m, float mindet )
{
	float a1 = m.v[0], b1 = m.v[4], c1 = m.v[8], d1 = m.v[12];
	adjoint(m);
	float det = a1 * v[0] + b1 * v[1] + c1 * v[2] + d1 * v[3];   // float det = m.determinant();
	if ( fabs(det) < mindet ) return false;
	float invdet = 1 / det;
	loopi(16)
		v[i] *= invdet;
	return true;
}

