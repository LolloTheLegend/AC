#include "cube.h"

VARP(animationinterpolationtime, 0, 150, 1000);

model *loadingmodel = NULL;

const int dbgts = 0;
VAR(tsswap, 0, 1, 1);

struct tristrip
{
    struct drawcall
    {
        GLenum type;
        GLuint start, minvert, maxvert;
        GLsizei count;

        drawcall() {}
        drawcall(GLenum type, GLuint start, GLsizei count = 0) : type(type), start(start), minvert(~0U), maxvert(0), count(count) {}
    };

    enum
    {
        // must be larger than all other triangle/vert indices
        UNUSED  = 0xFFFE,
        REMOVED = 0xFFFF
    };

    struct triangle
    {
        ushort v[3], n[3];

        bool link(ushort neighbor, ushort old = UNUSED)
        {
            loopi(3)
            {
                if(n[i]==neighbor) return true;
                else if(n[i]==old) { n[i] = neighbor; return true; }
            }
            if(dbgts && old==UNUSED) conoutf("excessive links");
            return false;
        }

        void unlink(ushort neighbor, ushort unused = UNUSED)
        {
            loopi(3) if(n[i]==neighbor) n[i] = unused;
        }

        int numlinks() const { int num = 0; loopi(3) if(n[i]<UNUSED) num++; return num; }

        bool hasvert(ushort idx) const { loopi(3) if(v[i]==idx) return true; return false; }
        ushort diffvert(ushort v1, ushort v2) { loopi(3) if(v[i]!=v1 && v[i]!=v2) return v[i]; return UNUSED; }

        bool haslink(ushort neighbor) const { loopi(3) if(n[i]==neighbor) return true; return false; }
    };

    vector<triangle> triangles;
    vector<ushort> connectivity[4];
    vector<uchar> nodes;

    template<class T>
    void addtriangles(const T *tris, int numtris)
    {
        if(dbgts) conoutf("addtriangles: tris = %d, inds = %d", numtris, numtris*3);
        loopi(numtris)
        {
            triangle &tri = triangles.add();
            loopj(3)
            {
                tri.v[j] = tris[i].vert[j];
                tri.n[j] = UNUSED;
            }
            if(tri.v[0]==tri.v[1] || tri.v[1]==tri.v[2] || tri.v[2]==tri.v[0])
            {
                if(dbgts) conoutf("degenerate triangle");
                triangles.drop();
            }
        }
    }

    struct edge { ushort from, to; };

    void findconnectivity()
    {
        hashtable<edge, ushort> edges;
        nodes.setsize(0);
        loopv(triangles)
        {
            triangle &tri = triangles[i];
            loopj(3)
            {
                edge e = { tri.v[j==2 ? 0 : j+1], tri.v[j] };
                edges.access(e, i);

                while(nodes.length()<=tri.v[j]) nodes.add(0);
                nodes[tri.v[j]]++;
            }
        }
        loopv(triangles)
        {
            triangle &tri = triangles[i];
            loopj(3)
            {
                edge e = { tri.v[j], tri.v[j==2 ? 0 : j+1] };
                ushort *owner = edges.access(e);
                if(!owner) continue;
                else if(!tri.link(*owner))
                {
                    if(dbgts) conoutf("failed linkage 1: %d -> %d", *owner, i);
                }
                else if(!triangles[*owner].link(i))
                {
                    if(dbgts) conoutf("failed linkage 2: %d -> %d", *owner, i);
                    tri.unlink(*owner);
                }
            }
        }
        loopi(4) connectivity[i].setsize(0);
        loopv(triangles) connectivity[triangles[i].numlinks()].add(i);
        if(dbgts) conoutf("no connections: %d", connectivity[0].length());
    }

    void removeconnectivity(ushort i)
    {
        triangle &tri = triangles[i];
        loopj(3) if(tri.n[j]<UNUSED)
        {
            triangle &neighbor = triangles[tri.n[j]];
            int conn = neighbor.numlinks();
            bool linked = false;
            loopk(3) if(neighbor.n[k]==i) { linked = true; neighbor.n[k] = REMOVED; break; }
            if(linked)
            {
                connectivity[conn].replacewithlast(tri.n[j]);
                connectivity[conn-1].add(tri.n[j]);
            }
        }
        removenodes(i);
    }

    bool remaining() const
    {
        loopi(4) if(!connectivity[i].empty()) return true;
        return false;
    }

    ushort leastconnected()
    {
        loopi(4) if(!connectivity[i].empty())
        {
            ushort least = connectivity[i].pop();
            removeconnectivity(least);
            return least;
        }
        return UNUSED;
    }

    int findedge(const triangle &from, const triangle &to, ushort v = UNUSED)
    {
        loopi(3) loopj(3)
        {
            if(from.v[i]==to.v[j] && from.v[i]!=v) return i;
        }
        return -1;
    }

    void removenodes(int i)
    {
        loopj(3) nodes[triangles[i].v[j]]--;
    }

    ushort nexttriangle(triangle &tri, bool &nextswap, ushort v1 = UNUSED, ushort v2 = UNUSED)
    {
        ushort next = UNUSED;
        int nextscore = 777;
        nextswap = false;
        loopi(3) if(tri.n[i]<UNUSED)
        {
            triangle &nexttri = triangles[tri.n[i]];
            int score = nexttri.numlinks();
            bool swap = false;
            if(v1!=UNUSED)
            {
                if(!nexttri.hasvert(v1))
                {
                    swap = true;
                    score += nodes[v2] > nodes[v1] ? 1 : -1;
                }
                else if(nexttri.hasvert(v2)) continue;
                else score += nodes[v1] > nodes[v2] ? 1 : -1;
                if(!tsswap && swap) continue;
                score += swap ? 1 : -1;
            }
            if(score < nextscore) { next = tri.n[i]; nextswap = swap; nextscore = score; }
        }
        if(next!=UNUSED)
        {
            tri.unlink(next, REMOVED);
            connectivity[triangles[next].numlinks()].replacewithlast(next);
            removeconnectivity(next);
        }
        return next;
    }

    void buildstrip(vector<ushort> &strip, bool reverse = false, bool prims = false)
    {
        ushort prev = leastconnected();
        if(prev==UNUSED) return;
        triangle &first = triangles[prev];
        bool doswap;
        ushort cur = nexttriangle(first, doswap);
        if(cur==UNUSED)
        {
            loopi(3) strip.add(first.v[!prims && reverse && i>=1 ? 3-i : i]);
            return;
        }
        int from = findedge(first, triangles[cur]),
            to = findedge(first, triangles[cur], first.v[from]);
        if(from+1!=to) swap(from, to);
        strip.add(first.v[(to+1)%3]);
        if(reverse) swap(from, to);
        strip.add(first.v[from]);
        strip.add(first.v[to]);

        ushort v1 = first.v[to], v2 = first.v[from];
        while(cur!=UNUSED)
        {
            prev = cur;
            cur = nexttriangle(triangles[prev], doswap, v1, v2);
            if(doswap) strip.add(v2);
            ushort v = triangles[prev].diffvert(v1, v2);
            strip.add(v);
            if(!doswap) v2 = v1;
            v1 = v;
        }

    }

    void buildstrips(vector<ushort> &strips, vector<drawcall> &draws, bool prims = true, bool degen = false)
    {
        vector<ushort> singles;
        findconnectivity();
        int numtris = 0, numstrips = 0;
        while(remaining())
        {
            vector<ushort> strip;
            bool reverse = degen && !strips.empty() && (strips.length()&1);
            buildstrip(strip, reverse, prims);
            numstrips++;
            numtris += strip.length()-2;
            if(strip.length()==3 && prims)
            {
                loopv(strip) singles.add(strip[i]);
                continue;
            }

            if(!strips.empty() && degen) { strips.dup(); strips.add(strip[0]); }
            else draws.add(drawcall(GL_TRIANGLE_STRIP, strips.length()));
            drawcall &d = draws.last();
            loopv(strip)
            {
                ushort index = strip[i];
                strips.add(index);
                d.minvert = min(d.minvert, (GLuint)index);
                d.maxvert = max(d.maxvert, (GLuint)index);
            }
            d.count = strips.length() - d.start;
        }
        if(prims && !singles.empty())
        {
            drawcall &d = draws.add(drawcall(GL_TRIANGLES, strips.length()));
            loopv(singles)
            {
                ushort index = singles[i];
                strips.add(index);
                d.minvert = min(d.minvert, (GLuint)index);
                d.maxvert = max(d.maxvert, (GLuint)index);
            }
            d.count = strips.length() - d.start;
        }
        if(dbgts) conoutf("strips = %d, tris = %d, inds = %d, merges = %d", numstrips, numtris, numtris + numstrips*2, (degen ? 2 : 1)*(numstrips-1));
    }

};

static inline uint hthash(const tristrip::edge &x) { return x.from^x.to; }
static inline bool htcmp(const tristrip::edge &x, const tristrip::edge &y) { return x.from==y.from && x.to==y.to; }

template<class T> struct modelcacheentry
{
    typedef modelcacheentry<T> entry;

    T *prev, *next, *nextalloc;
    size_t size;
    bool locked;

    modelcacheentry(T *prev = NULL, T *next = NULL) : prev(prev), next(next), nextalloc(NULL), size(0), locked(false) {}

    bool empty() const
    {
        return prev==next;
    }

    void linkbefore(T *pos)
    {
        next = pos;
        prev = pos->entry::prev;
        prev->entry::next = (T *)this;
        next->entry::prev = (T *)this;
    }

    void linkafter(T *pos)
    {
        next = pos->entry::next;
        prev = pos;
        prev->entry::next = (T *)this;
        next->entry::prev = (T *)this;
    }

    void unlink()
    {
        prev->modelcacheentry<T>::next = next;
        next->modelcacheentry<T>::prev = prev;
        prev = next = (T *)this;
    }

    void *getdata()
    {
        return (T *)this + 1;
    }
};

template<class T> struct modelcachelist : modelcacheentry<T>
{
    typedef modelcacheentry<T> entry;

    modelcachelist() : entry((T *)this, (T *)this) {}

    T *start() { return entry::next; }
    T *end() { return (T *)this; }

    void addfirst(entry *e)
    {
        e->linkafter((T *)this);
    }

    void addlast(entry *e)
    {
        e->linkbefore((T *)this);
    }

    void removefirst()
    {
        if(!entry::empty()) entry::next->unlink();
    }

    void removelast()
    {
        if(!entry::empty()) entry::prev->unlink();
    }
};

struct modelcache
{
    struct entry : modelcacheentry<entry> {};

    uchar *buf;
    size_t size;
    entry *curalloc;

    modelcache(size_t size = 0) : buf(size ? new uchar[size] : NULL), size(size), curalloc(NULL) {}
    ~modelcache() { DELETEA(buf); }

    void resize(size_t nsize)
    {
        if(curalloc)
        {
            for(curalloc = (entry *)buf; curalloc; curalloc = curalloc->nextalloc) curalloc->unlink();
        }
        DELETEA(buf);
        buf = nsize ? new uchar[nsize] : 0;
        size = nsize;
    }

    template<class T>
    T *allocate(size_t reqsize)
    {
        reqsize += sizeof(T);
        if(reqsize > size) return NULL;

        if(!curalloc)
        {
            curalloc = (entry *)buf;
            curalloc->size = reqsize;
            curalloc->locked = false;
            curalloc->nextalloc = NULL;
            return (T *)curalloc;
        }

        if(curalloc) for(bool failed = false;;)
        {
            uchar *nextfree = curalloc ? (uchar *)curalloc + curalloc->size : (uchar *)buf;
            entry *nextused = curalloc ? curalloc->nextalloc : (entry *)buf;
            for(;;)
            {
                if(!nextused)
                {
                    if(size_t(&buf[size] - nextfree) >= reqsize) goto success;
                    break;
                }
                else if(size_t((uchar *)nextused - nextfree) >= reqsize) goto success;
                else if(nextused->locked) break;
                nextused->unlink();
                nextused = nextused->nextalloc;
            }
            if(curalloc) curalloc->nextalloc = nextused;
            else if(failed) { curalloc = nextused; break; }
            else failed = true;
            curalloc = nextused;
            continue;

        success:
            entry *result = (entry *)nextfree;
            result->size = reqsize;
            result->locked = false;
            result->nextalloc = nextused;
            if(curalloc) curalloc->nextalloc = result;
            curalloc = result;
            return (T *)curalloc;
        }

        curalloc = (entry *)buf;
        return NULL;
    }

    template<class T>
    void release(modelcacheentry<T> *e)
    {
        e->unlink();
    }
};


static VARP(dynshadowsize, 4, 5, 8);
static VARP(aadynshadow, 0, 2, 3);
static VARP(saveshadows, 0, 1, 1);

static VARP(dynshadowquad, 0, 0, 1);

static VAR(shadowyaw, 0, 45, 360);
static vec shadowdir(0, 0, -1), shadowpos(0, 0, 0);

const int dbgstenc = 0;
const int dbgvlight = 0;

VARP(mdldlist, 0, 1, 1);

vec modelpos;
float modelyaw, modelpitch;

struct vertmodel : model
{
    struct anpos
    {
        int fr1, fr2;
        float t;

        void setframes(const animstate &as)
        {
            int time = lastmillis-as.basetime;
            fr1 = (int)(time/as.speed); // round to full frames
            t = (time-fr1*as.speed)/as.speed; // progress of the frame, value from 0.0f to 1.0f
            ASSERT(t >= 0.0f);
            if(as.anim&ANIM_LOOP)
            {
                fr1 = fr1%as.range+as.frame;
                fr2 = fr1+1;
                if(fr2>=as.frame+as.range) fr2 = as.frame;
            }
            else
            {
                fr1 = min(fr1, as.range-1)+as.frame;
                fr2 = min(fr1+1, as.frame+as.range-1);
            }
            if(as.anim&ANIM_REVERSE)
            {
                fr1 = (as.frame+as.range-1)-(fr1-as.frame);
                fr2 = (as.frame+as.range-1)-(fr2-as.frame);
            }
        }

        bool operator==(const anpos &a) const { return fr1==a.fr1 && fr2==a.fr2 && (fr1==fr2 || t==a.t); }
        bool operator!=(const anpos &a) const { return fr1!=a.fr1 || fr2!=a.fr2 || (fr1!=fr2 && t!=a.t); }
    };

    struct bb
    {
        vec low, high;

        void addlow(const vec &v)
        {
            low.x = min(low.x, v.x);
            low.y = min(low.y, v.y);
            low.z = min(low.z, v.z);
        }

        void addhigh(const vec &v)
        {
            high.x = max(high.x, v.x);
            high.y = max(high.y, v.y);
            high.z = max(high.z, v.z);
        }

        void add(const vec &v)
        {
            addlow(v);
            addhigh(v);
        }

        void add(const bb &b)
        {
            addlow(b.low);
            addhigh(b.high);
        }
    };

    struct tcvert { float u, v; };
    struct lightvert { uchar r, g, b, a; };
    struct tri { ushort vert[3]; ushort neighbor[3]; };

    struct part;

    typedef tristrip::drawcall drawcall;

    struct dyncacheentry : modelcacheentry<dyncacheentry>
    {
        anpos cur, prev;
        float t;

        vec *verts() { return (vec *)getdata(); }
        int numverts() { return int((size - sizeof(dyncacheentry)) / sizeof(vec)); }
    };

    struct shadowcacheentry : modelcacheentry<shadowcacheentry>
    {
        anpos cur, prev;
        float t;
        vec dir;

        ushort *idxs() { return (ushort *)getdata(); }
        int numidxs() { return int((size - sizeof(shadowcacheentry)) / sizeof(ushort)); }
    };

    struct lightcacheentry : modelcacheentry<lightcacheentry>
    {
        anpos cur, prev;
        float t;
        int lastcalclight;
        vec pos;
        float yaw, pitch;

        lightvert *verts() { return (lightvert *)getdata(); }
        int numverts() { return int((size - sizeof(lightcacheentry)) / sizeof(lightvert)); }
    };

    struct mesh
    {
        char *name;
        part *owner;
        vec *verts;
        tcvert *tcverts;
        bb *bbs;
        ushort *shareverts;
        tri *tris;
        int numverts, numtris;

        Texture *skin;
        int tex;

        modelcachelist<dyncacheentry> dyncache;
        modelcachelist<shadowcacheentry> shadowcache;
        modelcachelist<lightcacheentry> lightcache;

        ushort *dynidx;
        int dynlen;
        drawcall *dyndraws;
        int numdyndraws;
        GLuint statlist;
        int statlen;

        mesh() : name(0), owner(0), verts(0), tcverts(0), bbs(0), shareverts(0), tris(0), skin(notexture), tex(0), dynidx(0), dyndraws(0), statlist(0)
        {
        }

        ~mesh()
        {
            DELETEA(name);
            DELETEA(verts);
            DELETEA(tcverts);
            DELETEA(bbs);
            DELETEA(shareverts);
            DELETEA(tris);
            if(statlist) glDeleteLists(statlist, 1);
            DELETEA(dynidx);
            DELETEA(dyndraws);
        }

        void genstrips()
        {
            tristrip ts;
            ts.addtriangles(tris, numtris);
            vector<ushort> idxs;
            vector<drawcall> draws;
            ts.buildstrips(idxs, draws);
            dynidx = new ushort[idxs.length()];
            memcpy(dynidx, idxs.getbuf(), idxs.length()*sizeof(ushort));
            dynlen = idxs.length();
            dyndraws = new drawcall[draws.length()];
            memcpy(dyndraws, draws.getbuf(), draws.length()*sizeof(drawcall));
            numdyndraws = draws.length();
        }

#if defined(__GNUC__) && defined(__OPTIMIZE__) && !defined(__clang__) && !defined(__ICL) && __GNUC__ >= 4 && __GNUC_MINOR__ > 4
__attribute__((optimize(2)))
#endif
        dyncacheentry *gendynverts(animstate &as, anpos &cur, anpos *prev, float ai_t)
        {
            dyncacheentry *d = dyncache.start();
            int cachelen = 0;
            for(; d != dyncache.end(); d = d->next, cachelen++)
            {
                if(d->cur != cur) continue;
                if(prev)
                {
                    if(d->prev == *prev && d->t == ai_t) return d;
                }
                else if(d->prev.fr1 < 0) return d;
            }

            d = dynalloc.allocate<dyncacheentry>((numverts + 1)*sizeof(vec));
            if(!d) return NULL;

            if(cachelen >= owner->model->cachelimit) dyncache.removelast();
            dyncache.addfirst(d);
            vec *buf = d->verts(),
                *vert1 = &verts[cur.fr1 * numverts],
                *vert2 = &verts[cur.fr2 * numverts],
                *pvert1 = NULL, *pvert2 = NULL;
            d->cur = cur;
            d->t = ai_t;
            if(prev)
            {
                d->prev = *prev;
                pvert1 = &verts[prev->fr1 * numverts];
                pvert2 = &verts[prev->fr2 * numverts];
            }
            else d->prev.fr1 = -1;

            #define iploop(body) \
                loopi(numverts) \
                { \
                    vec &v = buf[i]; \
                    body; \
                }
            #define ip(p1, p2, t) (p1+t*(p2-p1))
            #define ip_v(p, c, t) ip(p##1[i].c, p##2[i].c, t)
            #define ip_v_ai(c) ip(ip_v(pvert, c, prev->t), ip_v(vert, c, cur.t), ai_t)
            if(prev) iploop(v = vec(ip_v_ai(x), ip_v_ai(y), ip_v_ai(z)))
            else iploop(v = vec(ip_v(vert, x, cur.t), ip_v(vert, y, cur.t), ip_v(vert, z, cur.t)))
            #undef ip
            #undef ip_v
            #undef ip_v_ai

            if(d->verts() == lastvertexarray) lastvertexarray = (void *)-1;

            return d;
        }

        void weldverts()
        {
            hashtable<vec, int> idxs;
            shareverts = new ushort[numverts];
            loopi(numverts) shareverts[i] = (ushort)idxs.access(verts[i], i);
            for(int i = 1; i < owner->numframes; i++)
            {
                const vec *frame = &verts[i*numverts];
                loopj(numverts)
                {
                    if(frame[j] != frame[shareverts[j]]) shareverts[j] = (ushort)j;
                }
            }
        }

        void findneighbors()
        {
            hashtable<uint, uint> edges;
            loopi(numtris)
            {
                const tri &t = tris[i];
                loopj(3)
                {
                    uint e1 = shareverts[t.vert[j]], e2 = shareverts[t.vert[(j+1)%3]], shift = 0;
                    if(e1 > e2) { swap(e1, e2); shift = 16; }
                    uint &edge = edges.access(e1 | (e2<<16), ~0U);
                    if(((edge>>shift)&0xFFFF) != 0xFFFF) edge = 0;
                    else
                    {
                        edge &= 0xFFFF<<(16-shift);
                        edge |= i<<shift;
                    }
                }
            }
            loopi(numtris)
            {
                tri &t = tris[i];
                loopj(3)
                {
                    uint e1 = shareverts[t.vert[j]], e2 = shareverts[t.vert[(j+1)%3]], shift = 0;
                    if(e1 > e2) { swap(e1, e2); shift = 16; }
                    uint edge = edges[e1 | (e2<<16)];
                    if(!edge || int((edge>>shift)&0xFFFF)!=i) t.neighbor[j] = 0xFFFF;
                    else t.neighbor[j] = (edge>>(16-shift))&0xFFFF;
                }
            }
        }


        void cleanup()
        {
            if(statlist)
            {
                glDeleteLists(1, statlist);
                statlist = 0;
            }
        }

#if defined(__GNUC__) && defined(__OPTIMIZE__) && !defined(__clang__) && !defined(__ICL) && __GNUC__ >= 4 && __GNUC_MINOR__ > 4
__attribute__((optimize(2)))
#endif
        shadowcacheentry *genshadowvolume(animstate &as, anpos &cur, anpos *prev, float ai_t, vec *buf)
        {
            if(!shareverts) return NULL;

            shadowcacheentry *d = shadowcache.start();
            int cachelen = 0;
            for(; d != shadowcache.end(); d = d->next, cachelen++)
            {
                if(d->dir != shadowpos || d->cur != cur) continue;
                if(prev)
                {
                    if(d->prev == *prev && d->t == ai_t) return d;
                }
                else if(d->prev.fr1 < 0) return d;
            }

            d = (owner->numframes > 1 || as.anim&ANIM_DYNALLOC ? dynalloc : statalloc).allocate<shadowcacheentry>(9*numtris*sizeof(ushort));
            if(!d) return NULL;

            if(cachelen >= owner->model->cachelimit) shadowcache.removelast();
            shadowcache.addfirst(d);
            d->dir = shadowpos;
            d->cur = cur;
            d->t = ai_t;
            if(prev) d->prev = *prev;
            else d->prev.fr1 = -1;

            static vector<uchar> side;
            side.setsize(0);

            loopi(numtris)
            {
                const tri &t = tris[i];
                const vec &a = buf[t.vert[0]], &b = buf[t.vert[1]], &c = buf[t.vert[2]];
                side.add(vec().cross(vec(b).sub(a), vec(c).sub(a)).dot(shadowdir) <= 0 ? 1 : 0);
            }

            ushort *idx = d->idxs();
            loopi(numtris) if(side[i])
            {
                const tri &t = tris[i];
                loopj(3)
                {
                    ushort n = t.neighbor[j];
                    if(n==0xFFFF || !side[n])
                    {
                        ushort e1 = shareverts[t.vert[j]], e2 = shareverts[t.vert[(j+1)%3]];
                        *idx++ = e2;
                        *idx++ = e1;
                        *idx++ = numverts;
                    }
                }
            }

            if(dbgstenc >= (owner->numframes > 1 || as.anim&ANIM_DYNALLOC ? 2 : 1)) conoutf("%s: %d tris", owner->filename, (idx - d->idxs())/3);

            d->size = (uchar *)idx - (uchar *)d;
            return d;
        }

#if defined(__GNUC__) && defined(__OPTIMIZE__) && !defined(__clang__) && !defined(__ICL) && __GNUC__ >= 4 && __GNUC_MINOR__ > 4
__attribute__((optimize(2)))
#endif
        lightcacheentry *lightvertexes(animstate &as, anpos &cur, anpos *prev, float ai_t, vec *buf)
        {
            if(dbgvlight) return NULL;

            lightcacheentry *d = lightcache.start();
            int cachelen = 0;
            for(; d != lightcache.end(); d = d->next, cachelen++)
            {
                if(d->lastcalclight != lastcalclight || d->pos != modelpos || d->yaw != modelyaw || d->pitch != modelpitch || d->cur != cur) continue;
                if(prev)
                {
                    if(d->prev == *prev && d->t == ai_t) return d;
                }
                else if(d->prev.fr1 < 0) return d;
            }

            bb curbb;
            getcurbb(curbb, as, cur, prev, ai_t);
            float dist = max(curbb.low.magnitude(), curbb.high.magnitude());
            if(OUTBORDRAD(modelpos.x, modelpos.y, dist)) return NULL;

            d = (owner->numframes > 1 || as.anim&ANIM_DYNALLOC ? dynalloc : statalloc).allocate<lightcacheentry>(numverts*sizeof(lightvert));
            if(!d) return NULL;

            if(cachelen >= owner->model->cachelimit) lightcache.removelast();
            lightcache.addfirst(d);
            d->lastcalclight = lastcalclight;
            d->pos = modelpos;
            d->yaw = modelyaw;
            d->pitch = modelpitch;
            d->cur = cur;
            d->t = ai_t;
            if(prev) d->prev = *prev;
            else d->prev.fr1 = -1;

            const glmatrixf &m = matrixstack[matrixpos];
            lightvert *v = d->verts();
            loopi(numverts)
            {
                int x = (int)m.transformx(*buf), y = (int)m.transformy(*buf);
                const sqr *s = S(x, y);
                v->r = s->r;
                v->g = s->g;
                v->b = s->b;
                v->a = 255;
                v++;
                buf++;
            }
            if(d->verts() == lastcolorarray) lastcolorarray = (void *)-1;
            return d;
        }

        void getcurbb(bb &b, animstate &as, anpos &cur, anpos *prev, float ai_t)
        {
            b = bbs[cur.fr1];
            b.add(bbs[cur.fr2]);

            if(prev)
            {
                b.add(bbs[prev->fr1]);
                b.add(bbs[prev->fr2]);
            }
        }

        void render(animstate &as, anpos &cur, anpos *prev, float ai_t)
        {
            if(!(as.anim&ANIM_NOSKIN))
            {
                GLuint id = tex < 0 ? -tex : skin->id;
                if(tex > 0) id = lookuptexture(tex)->id;
                if(id != lasttex)
                {
                    glBindTexture(GL_TEXTURE_2D, id);
                    lasttex = id;
                }
                if(enablealphablend) { glDisable(GL_BLEND); enablealphablend = false; }
                if(skin->bpp == 32 && owner->model->alphatest > 0)
                {
                    if(!enablealphatest) { glEnable(GL_ALPHA_TEST); enablealphatest = true; }
                    if(lastalphatest != owner->model->alphatest)
                    {
                        glAlphaFunc(GL_GREATER, owner->model->alphatest);
                        lastalphatest = owner->model->alphatest;
                    }
                }
                else if(enablealphatest) { glDisable(GL_ALPHA_TEST); enablealphatest = false; }
                if(!enabledepthmask) { glDepthMask(GL_TRUE); enabledepthmask = true; }
            }

            if(enableoffset)
            {
                disablepolygonoffset(GL_POLYGON_OFFSET_FILL);
                enableoffset = false;
            }

            bool isstat = as.frame==0 && as.range==1;
            if(isstat && statlist && !stenciling)
            {
                glCallList(statlist);
                xtraverts += statlen;
                return;
            }
            else if(stenciling==1)
            {
                bb curbb;
                getcurbb(curbb, as, cur, prev, ai_t);
                glmatrixf mat;
                mat.mul(mvpmatrix, matrixstack[matrixpos]);
                if(!addshadowbox(curbb.low, curbb.high, shadowpos, mat)) return;
            }

            vec *buf = verts;
            dyncacheentry *d = NULL;
            if(!isstat)
            {
                d = gendynverts(as, cur, prev, ai_t);
                if(!d) return;
                buf = d->verts();
            }
            if(lastvertexarray != buf)
            {
                if(!lastvertexarray) glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(3, GL_FLOAT, sizeof(vec), buf);
                lastvertexarray = buf;
            }
            lightvert *vlight = NULL;
            if(as.anim&ANIM_NOSKIN && (!isstat || stenciling))
            {
                if(lasttexcoordarray)
                {
                    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                    lasttexcoordarray = NULL;
                }
            }
            else
            {
                if(lasttexcoordarray != tcverts)
                {
                    if(!lasttexcoordarray) glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                    glTexCoordPointer(2, GL_FLOAT, sizeof(tcvert), tcverts);
                    lasttexcoordarray = tcverts;
                }
                if(owner->model->vertexlight)
                {
                    if(d) d->locked = true;
                    lightcacheentry *l = lightvertexes(as, cur, isstat ? NULL : prev, ai_t, buf);
                    if(d) d->locked = false;
                    if(l) vlight = l->verts();
                }
            }
            if(lastcolorarray != vlight)
            {
                if(vlight)
                {
                    if(!lastcolorarray) glEnableClientState(GL_COLOR_ARRAY);
                    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(lightvert), vlight);
                }
                else glDisableClientState(GL_COLOR_ARRAY);
                lastcolorarray = vlight;
            }
            if(stenciling)
            {
                if(d) d->locked = true;
                shadowcacheentry *s = genshadowvolume(as, cur, isstat ? NULL : prev, ai_t, buf);
                if(d) d->locked = false;
                if(!s) return;
                buf[numverts] = s->dir;
                glDrawElements(GL_TRIANGLES, s->numidxs(), GL_UNSIGNED_SHORT, s->idxs());
                xtraverts += s->numidxs();
                return;
            }

            bool builddlist = isstat && !owner->model->vertexlight && mdldlist;
            if(builddlist) glNewList(statlist = glGenLists(1), GL_COMPILE);
            loopi(numdyndraws)
            {
                const drawcall &d = dyndraws[i];
                if(hasDRE && !builddlist) glDrawRangeElements_(d.type, d.minvert, d.maxvert, d.count, GL_UNSIGNED_SHORT, &dynidx[d.start]);
                else glDrawElements(d.type, d.count, GL_UNSIGNED_SHORT, &dynidx[d.start]);
            }
            if(builddlist)
            {
                glEndList();
                glCallList(statlist);
                statlen = dynlen;
            }
            xtraverts += dynlen;
        }

        int findvert(int axis, int dir)
        {
            if(axis<0 || axis>2) return -1;
            int vert = -1;
            float bestval = -1e16f;
            loopi(numverts)
            {
                float val = verts[i][axis]*dir;
                if(val > bestval)
                {
                    vert = i;
                    bestval = val;
                }
            }
            return vert;
        }

        float calcradius()
        {
            float rad = 0;
            loopi(numverts) rad = max(rad, verts[i].magnitudexy());
            return rad;
        }

        void calcneighbors()
        {
            if(!shareverts)
            {
                weldverts();
                findneighbors();
            }
        }

        void calcbbs()
        {
            if(bbs) return;
            bbs = new bb[owner->numframes];
            loopi(owner->numframes)
            {
                bb &b = bbs[i];
                b.low = vec(1e16f, 1e16f, 1e16f);
                b.high = vec(-1e16f, -1e16f, -1e16f);
                const vec *frame = &verts[numverts*i];
                loopj(numverts) b.add(frame[j]);
            }
        }
    };

    struct animinfo
    {
        int frame, range;
        float speed;
    };

    struct particleemitter
    {
        int type, args[2], seed, lastemit;

        particleemitter() : type(-1), seed(-1), lastemit(-1)
        {
            memset(args, 0, sizeof(args));
        }
    };

    struct tag
    {
        char *name;
        vec pos;
        float transform[3][3];

        tag() : name(NULL) {}
        ~tag() { DELETEA(name); }

        void identity()
        {
            transform[0][0] = 1;
            transform[0][1] = 0;
            transform[0][2] = 0;

            transform[1][0] = 0;
            transform[1][1] = 1;
            transform[1][2] = 0;

            transform[2][0] = 0;
            transform[2][1] = 0;
            transform[2][2] = 1;
        }
    };

    struct linkedpart
    {
        part *p;
        particleemitter *emitter;
        vec *pos;

        linkedpart() : p(NULL), emitter(NULL), pos(NULL) {}
        ~linkedpart() { DELETEP(emitter); }
    };

    struct part
    {
        char *filename;
        vertmodel *model;
        int index, numframes;
        vector<mesh *> meshes;
        vector<animinfo> *anims;
        linkedpart *links;
        tag *tags;
        int numtags;
        GLuint *shadows;
        float shadowrad;

        part() : filename(NULL), anims(NULL), links(NULL), tags(NULL), numtags(0), shadows(NULL), shadowrad(0) {}
        virtual ~part()
        {
            DELETEA(filename);
            meshes.deletecontents();
            DELETEA(anims);
            DELETEA(links);
            DELETEA(tags);
            if(shadows) glDeleteTextures(numframes, shadows);
            DELETEA(shadows);
        }

        void cleanup()
        {
            loopv(meshes) meshes[i]->cleanup();
            if(shadows)
            {
                glDeleteTextures(numframes, shadows);
                DELETEA(shadows);
            }
        }

        bool link(part *link, const char *tag, vec *pos = NULL)
        {
            loopi(numtags) if(!strcmp(tags[i].name, tag))
            {
                links[i].p = link;
                links[i].pos = pos;
                return true;
            }
            return false;
        }

        bool gentag(const char *name, int *verts, int numverts, mesh *m = NULL)
        {
            if(!m)
            {
                if(meshes.empty()) return false;
                m = meshes[0];
            }
            if(numverts < 1) return false;
            loopi(numverts) if(verts[i] < 0 || verts[i] > m->numverts) return false;

            tag *ntags = new tag[(numtags + 1)*numframes];
            ntags[numtags].name = newstring(name);
            loopi(numframes)
            {
                memcpy(&ntags[(numtags + 1)*i], &tags[numtags*i], numtags*sizeof(tag));

                tag *t = &ntags[(numtags + 1)*i + numtags];
                t->pos = m->verts[m->numverts*i + verts[0]];
                if(numverts > 1)
                {
                    for(int j = 1; j < numverts; j++) t->pos.add(m->verts[m->numverts*i + verts[j]]);
                    t->pos.div(numverts);
                }
                t->identity();
            }
            loopi(numtags) tags[i].name = NULL;

            DELETEA(tags);
            tags = ntags;
            numtags++;

            linkedpart *nlinks = new linkedpart[numtags];
            loopi(numtags-1) swap(links[i].emitter, nlinks[i].emitter);
            DELETEA(links);
            links = nlinks;
            return true;
        }

        bool addemitter(const char *tag, int type, int arg1 = 0, int arg2 = 0)
        {
            loopi(numtags) if(!strcmp(tags[i].name, tag))
            {
                if(!links[i].emitter) links[i].emitter = new particleemitter;
                particleemitter &p = *links[i].emitter;
                p.type = type;
                p.args[0] = arg1;
                p.args[1] = arg2;
                return true;
            }
            return false;
        }

        void scaleverts(const float scale, const vec &translate)
        {
           loopv(meshes)
           {
               mesh &m = *meshes[i];
               loopj(numframes*m.numverts)
               {
                   vec &v = m.verts[j];
                   if(!index) v.add(translate);
                   v.mul(scale);
               }
           }
           loopi(numframes*numtags)
           {
               vec &v = tags[i].pos;
               if(!index) v.add(translate);
               v.mul(scale);
           }
        }

        void genstrips()
        {
            loopv(meshes) meshes[i]->genstrips();
        }

        virtual void getdefaultanim(animstate &as, int anim, int varseed)
        {
            as.frame = 0;
            as.range = 1;
        }

        void gentagmatrix(anpos &cur, anpos *prev, float ai_t, int i, GLfloat *matrix)
        {
            tag *tag1 = &tags[cur.fr1*numtags+i];
            tag *tag2 = &tags[cur.fr2*numtags+i];
            #define ip(p1, p2, t) (p1+t*(p2-p1))
            #define ip_ai_tag(c) ip( ip( tag1p->c, tag2p->c, prev->t), ip( tag1->c, tag2->c, cur.t), ai_t)
            if(prev)
            {
                tag *tag1p = &tags[prev->fr1 * numtags + i];
                tag *tag2p = &tags[prev->fr2 * numtags + i];
                loopj(3) matrix[j] = ip_ai_tag(transform[0][j]); // transform
                loopj(3) matrix[4 + j] = ip_ai_tag(transform[1][j]);
                loopj(3) matrix[8 + j] = ip_ai_tag(transform[2][j]);
                loopj(3) matrix[12 + j] = ip_ai_tag(pos[j]); // position
            }
            else
            {
                loopj(3) matrix[j] = ip(tag1->transform[0][j], tag2->transform[0][j], cur.t); // transform
                loopj(3) matrix[4 + j] = ip(tag1->transform[1][j], tag2->transform[1][j], cur.t);
                loopj(3) matrix[8 + j] = ip(tag1->transform[2][j], tag2->transform[2][j], cur.t);
                loopj(3) matrix[12 + j] = ip(tag1->pos[j], tag2->pos[j], cur.t); // position
            }
            #undef ip_ai_tag
            #undef ip
            matrix[3] = matrix[7] = matrix[11] = 0.0f;
            matrix[15] = 1.0f;
        }

        bool calcanimstate(int anim, int varseed, float speed, int basetime, dynent *d, animstate &as)
        {
            as.anim = anim;
            as.speed = speed<=0 ? 100.0f : speed;
            as.basetime = basetime;
            if((anim&ANIM_INDEX)==ANIM_ALL)
            {
                as.frame = 0;
                as.range = numframes;
            }
            else if(anims)
            {
                vector<animinfo> &ais = anims[anim&ANIM_INDEX];
                if(ais.length())
                {
                    animinfo &ai = ais[uint(varseed)%ais.length()];
                    as.frame = ai.frame;
                    as.range = ai.range;
                    if(ai.speed>0) as.speed = 1000.0f/ai.speed;
                }
                else getdefaultanim(as, anim&ANIM_INDEX, varseed);
            }
            else getdefaultanim(as, anim&ANIM_INDEX, varseed);
            if(anim&(ANIM_START|ANIM_END))
            {
                if(anim&ANIM_END) as.frame += as.range-1;
                as.range = 1;
            }

            if(as.frame+as.range>numframes)
            {
                if(as.frame>=numframes) return false;
                as.range = numframes-as.frame;
            }

            if(d && index<2)
            {
                if(d->lastmodel[index]!=this || d->lastanimswitchtime[index]==-1 || lastmillis-d->lastrendered>animationinterpolationtime)
                {
                    d->current[index] = as;
                    d->lastanimswitchtime[index] = lastmillis-animationinterpolationtime*2;
                }
                else if(d->current[index] != as)
                {
                    if(lastmillis-d->lastanimswitchtime[index]>animationinterpolationtime/2) d->prev[index] = d->current[index];
                    d->current[index] = as;
                    d->lastanimswitchtime[index] = lastmillis;
                }
                d->lastmodel[index] = this;
            }
            return true;
        }

        void render(int anim, int varseed, float speed, int basetime, dynent *d)
        {
            if(meshes.empty()) return;
            animstate as;
            if(!calcanimstate(anim, varseed, speed, basetime, d, as)) return;

            if(!meshes[0]->dynidx) genstrips();

            anpos prev, cur;
            cur.setframes(d && index<2 ? d->current[index] : as);

            float ai_t = 0;
            bool doai = !(anim&ANIM_NOINTERP) && d && index<2 && lastmillis-d->lastanimswitchtime[index]<animationinterpolationtime && d->prev[index].range>0;
            if(doai)
            {
                prev.setframes(d->prev[index]);
                ai_t = (lastmillis-d->lastanimswitchtime[index])/(float)animationinterpolationtime;
            }

            glPushMatrix();
            glMultMatrixf(matrixstack[matrixpos].v);
            loopv(meshes) meshes[i]->render(as, cur, doai ? &prev : NULL, ai_t);
            glPopMatrix();

            loopi(numtags)
            {
                linkedpart &link = links[i];
                if(!(link.p || link.pos || (anim&ANIM_PARTICLE && link.emitter))) continue;

                // render the linked models - interpolate rotation and position of the 'link-tags'
                glmatrixf linkmat;
                gentagmatrix(cur, doai ? &prev : NULL, ai_t, i, linkmat.v);

                matrixpos++;
                matrixstack[matrixpos].mul(matrixstack[matrixpos-1], linkmat);

                if(link.pos) *link.pos = matrixstack[matrixpos].gettranslation();

                if(link.p)
                {
                    vec oldshadowdir, oldshadowpos;

                    if(stenciling)
                    {
                        oldshadowdir = shadowdir;
                        oldshadowpos = shadowpos;
                        linkmat.invertnormal(shadowdir);
                        linkmat.invertvertex(shadowpos);
                    }

                    link.p->render(anim, varseed, speed, basetime, d);

                    if(stenciling)
                    {
                        shadowdir = oldshadowdir;
                        shadowpos = oldshadowpos;
                    }
                }

                if(anim&ANIM_PARTICLE && link.emitter)
                {
                    particleemitter &p = *link.emitter;

                    if(p.lastemit!=basetime)
                    {
                        p.seed = rnd(0x1000000);
                        p.lastemit = basetime;
                    }

                    particle_emit(p.type, p.args, basetime, p.seed, matrixstack[matrixpos].gettranslation());
                }

                matrixpos--;
            }
        }

        void setanim(int num, int frame, int range, float speed)
        {
            if(frame<0 || frame>=numframes || range<=0 || frame+range>numframes)
            {
                conoutf("invalid frame %d, range %d in model %s", frame, range, model->loadname);
                return;
            }
            if(!anims) anims = new vector<animinfo>[NUMANIMS];
            animinfo &ai = anims[num].add();
            ai.frame = frame;
            ai.range = range;
            ai.speed = speed;
        }

        virtual void begingenshadow()
        {
        }

        virtual void endgenshadow()
        {
        }

        void blurshadow(const uchar *in, uchar *out, uint size)
        {
            static const uint filter3x3[9] =
            {
                1, 2, 1,
                2, 4, 2,
                1, 2, 1
            };
            static const uint filter3x3sum = 16;
            const uchar *src = in, *prev = in - size, *next = in + size;
            uchar *dst = out;

            #define FILTER(c0, c1, c2, c3, c4, c5, c6, c7, c8) \
            { \
                uint c = *src, \
                     val = (filter3x3[0]*(c0) + filter3x3[1]*(c1) + filter3x3[2]*(c2) + \
                            filter3x3[3]*(c3) + filter3x3[4]*(c4) + filter3x3[5]*(c5) + \
                            filter3x3[6]*(c6) + filter3x3[7]*(c7) + filter3x3[8]*(c8)); \
                *dst++ = val/filter3x3sum; \
                src++; \
                prev++; \
                next++; \
            }

            FILTER(c, c, c, c, c, src[1], c, next[0], next[1]);
            for(uint x = 1; x < size-1; x++) FILTER(c, c, c, src[-1], c, src[1], next[-1], next[0], next[1]);
            FILTER(c, c, c, src[-1], c, c, next[-1], next[0], c);

            for(uint y = 1; y < size-1; y++)
            {
                FILTER(c, prev[0], prev[1], c, c, src[1], c, next[0], next[1]);
                for(uint x = 1; x < size-1; x++) FILTER(prev[-1], prev[0], prev[1], src[-1], c, src[1], next[-1], next[0], next[1]);
                FILTER(prev[-1], prev[0], c, src[-1], c, c, next[-1], next[0], c);
            }

            FILTER(c, prev[0], prev[1], c, c, src[1], c, c, c);
            for(uint x = 1; x < size-1; x++) FILTER(prev[-1], prev[0], prev[1], src[-1], c, src[1], c, c, c);
            FILTER(prev[-1], prev[0], c, src[-1], c, c, c, c, c);

            #undef FILTER
        }

        void genshadow(int aasize, int frame, stream *f)
        {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            model->startrender();
            render(ANIM_ALL|ANIM_NOINTERP|ANIM_NOSKIN, 0, 1, lastmillis-frame, NULL);
            model->endrender();

            uchar *pixels = new uchar[2*aasize*aasize];
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, aasize, aasize, GL_RED, GL_UNSIGNED_BYTE, pixels);
#if 0
            SDL_Surface *img = SDL_CreateRGBSurface(SDL_SWSURFACE, aasize, aasize, 24, 0x0000FF, 0x00FF00, 0xFF0000, 0);
            loopi(aasize*aasize) memset((uchar *)img->pixels + 3*i, pixels[i], 3);
            defformatstring(imgname)("%s_%d.bmp", model->loadname, frame);
            for(char *s; (s = strchr(imgname, '/'));) *s = '_';
            SDL_SaveBMP(img, imgname);
            SDL_FreeSurface(img);
#endif
            if(aasize > 1<<dynshadowsize)
                scaletexture(pixels, aasize, aasize, 1, pixels, 1<<dynshadowsize, 1<<dynshadowsize);

            int texsize = min(aasize, 1<<dynshadowsize);
            blurshadow(pixels, &pixels[texsize*texsize], texsize);
            if(f) f->write(&pixels[texsize*texsize], texsize*texsize);
            createtexture(shadows[frame], texsize, texsize, &pixels[texsize*texsize], 3, true, false, GL_ALPHA);

            delete[] pixels;
        }

        struct shadowheader
        {
            ushort size, frames;
            float height, rad;
        };

        void genshadows(float height, float rad)
        {
            if(shadows) return;

            char *filename = shadowfile();
            if(filename && loadshadows(filename)) return;

            shadowrad = rad;
            shadows = new GLuint[numframes];
            glGenTextures(numframes, shadows);

            extern SDL_Surface *screen;
            int aasize = 1<<(dynshadowsize + aadynshadow);
            while(aasize > screen->w || aasize > screen->h) aasize /= 2;

            stream *f = filename ? opengzfile(filename, "wb") : NULL;
            if(f)
            {
                shadowheader hdr;
                hdr.size = min(aasize, 1<<dynshadowsize);
                hdr.frames = numframes;
                hdr.height = height;
                hdr.rad = rad;
                f->putlil(hdr.size);
                f->putlil(hdr.frames);
                f->putlil(hdr.height);
                f->putlil(hdr.rad);
            }

            glViewport(0, 0, aasize, aasize);
            glClearColor(0, 0, 0, 1);
            glDisable(GL_FOG);
            glColor3f(1, 1, 1);

            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(-rad, rad, -rad, rad, 0.15f, height);

            glMatrixMode(GL_MODELVIEW);
            glLoadIdentity();
            glScalef(1, -1, 1);

            glTranslatef(0, 0, -height);
            begingenshadow();
            loopi(numframes) genshadow(aasize, i, f);
            endgenshadow();

            glEnable(GL_FOG);
            glViewport(0, 0, screen->w, screen->h);

            if(f) delete f;
        }

        bool loadshadows(const char *filename)
        {
            stream *f = opengzfile(filename, "rb");
            if(!f) return false;
            shadowheader hdr;
            if(f->read(&hdr, sizeof(shadowheader))!=sizeof(shadowheader)) { delete f; return false; }
            lilswap(&hdr.size, 1);
            lilswap(&hdr.frames, 1);
            if(hdr.size!=(1<<dynshadowsize) || hdr.frames!=numframes) { delete f; return false; }
            lilswap(&hdr.height, 1);
            lilswap(&hdr.rad, 1);

            uchar *buf = new uchar[hdr.size*hdr.size*hdr.frames];
            if(f->read(buf, hdr.size*hdr.size*hdr.frames)!=hdr.size*hdr.size*hdr.frames) { delete f; delete[] buf; return false; }

            shadowrad = hdr.rad;
            shadows = new GLuint[hdr.frames];
            glGenTextures(hdr.frames, shadows);

            loopi(hdr.frames) createtexture(shadows[i], hdr.size, hdr.size, &buf[i*hdr.size*hdr.size], 3, true, false, GL_ALPHA);

            delete[] buf;
            delete f;

            return true;
        }

        void rendershadow(int anim, int varseed, float speed, int basetime, const vec &o, float yaw)
        {
            if(!shadows) return;
            animstate as;
            if(!calcanimstate(anim, varseed, speed, basetime, NULL, as)) return;
            anpos cur;
            cur.setframes(as);

            GLuint id = shadows[cur.fr1];
            if(id!=lasttex)
            {
                glBindTexture(GL_TEXTURE_2D, id);
                lasttex = id;
            }

            if(enabledepthmask) { glDepthMask(GL_FALSE); enabledepthmask = false; }
            if(enablealphatest) { glDisable(GL_ALPHA_TEST); enablealphatest = false; }
            if(!enablealphablend)
            {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                enablealphablend = true;
            }

            yaw *= RAD;
            float c = cos(yaw), s = sin(yaw);
            float x1 = -shadowrad, x2 = shadowrad;
            float y1 = -shadowrad, y2 = shadowrad;

            if(dynshadowquad)
            {
                glBegin(GL_QUADS);
                glTexCoord2f(0, 1); glVertex3f(x1*c - y1*s + o.x, y1*c + x1*s + o.y, o.z);
                glTexCoord2f(1, 1); glVertex3f(x2*c - y1*s + o.x, y1*c + x2*s + o.y, o.z);
                glTexCoord2f(1, 0); glVertex3f(x2*c - y2*s + o.x, y2*c + x2*s + o.y, o.z);
                glTexCoord2f(0, 0); glVertex3f(x1*c - y2*s + o.x, y2*c + x1*s + o.y, o.z);
                glEnd();
                xtraverts += 4;
                return;
            }

            if(!enableoffset)
            {
                enablepolygonoffset(GL_POLYGON_OFFSET_FILL);
                enableoffset = true;
            }
            if(lastvertexarray)
            {
                glDisableClientState(GL_VERTEX_ARRAY);
                lastvertexarray = NULL;
            }
            if(lasttexcoordarray)
            {
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                lasttexcoordarray = NULL;
            }

            vec texgenS, texgenT;
            texgenS.x = c / (2*shadowrad);
            texgenS.y = s / (2*shadowrad);
            texgenS.z = -(x1*c - y2*s + o.x)*texgenS.x - (y2*c + x1*s + o.y)*texgenS.y;

            texgenT.x = s / (2*shadowrad);
            texgenT.y = -c / (2*shadowrad);
            texgenT.z = -(x1*c - y2*s + o.x)*texgenT.x - (y2*c + x1*s + o.y)*texgenT.y;

            ::rendershadow(int(floor(o.x-shadowrad)), int(floor(o.y-shadowrad)), int(ceil(o.x+shadowrad)), int(ceil(o.y+shadowrad)), texgenS, texgenT);
        }

        char *shadowfile()
        {
            if(!saveshadows || !filename) return NULL;

            static string s;
            char *dir = strrchr(filename, PATHDIV);
            if(!dir) s[0] = '\0';
            else copystring(s, filename, dir-filename+2);
            concatstring(s, "shadows.dat");
            return s;
        }

        float calcradius()
        {
            float rad = 0;
            loopv(meshes) rad = max(rad, meshes[i]->calcradius());
            return rad;
        }

        void calcneighbors()
        {
            loopv(meshes) meshes[i]->calcneighbors();
        }

        void calcbbs()
        {
            loopv(meshes) meshes[i]->calcbbs();
        }
    };

    bool loaded;
    char *loadname;
    vector<part *> parts;

    vertmodel(const char *name) : loaded(false)
    {
        loadname = newstring(name);
    }

    ~vertmodel()
    {
        delete[] loadname;
        parts.deletecontents();
    }

    char *name() { return loadname; }

    void cleanup()
    {
        loopv(parts) parts[i]->cleanup();
    }

    bool link(part *link, const char *tag, vec *pos = NULL)
    {
        loopv(parts) if(parts[i]->link(link, tag, pos)) return true;
        return false;
    }

    void setskin(int tex = 0)
    {
        //if(parts.length()!=1 || parts[0]->meshes.length()!=1) return;
        if(parts.length() < 1 || parts[0]->meshes.length() < 1) return;
        mesh &m = *parts[0]->meshes[0];
        m.tex = tex;
    }

    void genshadows(float height, float rad)
    {
        if(parts.length()>1) return;
        parts[0]->genshadows(height, rad);
    }

    bool hasshadows()
    {
        return parts.length()==1 && parts[0]->shadows;
    }

    float calcradius()
    {
        return parts.empty() ? 0.0f : parts[0]->calcradius();
    }

    void calcneighbors()
    {
        loopv(parts) parts[i]->calcneighbors();
    }

    void calcbbs()
    {
        loopv(parts) parts[i]->calcbbs();
    }

    static bool enablealphablend, enablealphatest, enabledepthmask, enableoffset;
    static GLuint lasttex;
    static float lastalphatest;
    static void *lastvertexarray, *lasttexcoordarray, *lastcolorarray;
    static glmatrixf matrixstack[32];
    static int matrixpos;

    void startrender()
    {
        enablealphablend = enablealphatest = enableoffset = false;
        enabledepthmask = true;
        lasttex = 0;
        lastalphatest = -1;
        lastvertexarray = lasttexcoordarray = lastcolorarray = NULL;
    }

    void endrender()
    {
        if(enablealphablend) glDisable(GL_BLEND);
        if(enablealphatest) glDisable(GL_ALPHA_TEST);
        if(!enabledepthmask) glDepthMask(GL_TRUE);
        if(lastvertexarray) glDisableClientState(GL_VERTEX_ARRAY);
        if(lasttexcoordarray) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        if(lastcolorarray) glDisableClientState(GL_COLOR_ARRAY);
        if(enableoffset) disablepolygonoffset(GL_POLYGON_OFFSET_FILL);
    }

    static modelcache dynalloc, statalloc;
};

bool vertmodel::enablealphablend = false, vertmodel::enablealphatest = false, vertmodel::enabledepthmask = true, vertmodel::enableoffset = false;
GLuint vertmodel::lasttex = 0;
float vertmodel::lastalphatest = -1;
void *vertmodel::lastvertexarray = NULL, *vertmodel::lasttexcoordarray = NULL, *vertmodel::lastcolorarray = NULL;
glmatrixf vertmodel::matrixstack[32];
int vertmodel::matrixpos = 0;

VARF(mdldyncache, 1, 2, 32, vertmodel::dynalloc.resize(mdldyncache<<20));
VARF(mdlstatcache, 1, 1, 32, vertmodel::statalloc.resize(mdlstatcache<<20));

modelcache vertmodel::dynalloc(mdldyncache<<20), vertmodel::statalloc(mdlstatcache<<20);



struct md2;

md2 *loadingmd2 = 0;

struct md2 : vertmodel
{
    struct md2_header
    {
        int magic;
        int version;
        int skinwidth, skinheight;
        int framesize;
        int numskins, numvertices, numtexcoords;
        int numtriangles, numglcommands, numframes;
        int offsetskins, offsettexcoords, offsettriangles;
        int offsetframes, offsetglcommands, offsetend;
    };

    struct md2_vertex
    {
        uchar vertex[3], normalindex;
    };

    struct md2_frame
    {
        float      scale[3];
        float      translate[3];
        char       name[16];
    };

    md2(const char *name) : vertmodel(name) {}

    int type() { return MDL_MD2; }

    struct md2part : part
    {
        void gentcverts(int *glcommands, vector<tcvert> &tcverts, vector<ushort> &vindexes, vector<tri> &tris)
        {
            hashtable<ivec, int> tchash;
            vector<ushort> idxs;
            for(int *command = glcommands; (*command)!=0;)
            {
                int numvertex = *command++;
                bool isfan = numvertex<0;
                if(isfan) numvertex = -numvertex;
                idxs.setsize(0);
                loopi(numvertex)
                {
                    union { int i; float f; } u, v;
                    u.i = *command++;
                    v.i = *command++;
                    int vindex = *command++;
                    ivec tckey(u.i, v.i, vindex);
                    int *idx = tchash.access(tckey);
                    if(!idx)
                    {
                        idx = &tchash[tckey];
                        *idx = tcverts.length();
                        tcvert &tc = tcverts.add();
                        tc.u = u.f;
                        tc.v = v.f;
                        vindexes.add((ushort)vindex);
                    }
                    idxs.add(*idx);
                }
                loopi(numvertex-2)
                {
                    tri &t = tris.add();
                    if(isfan)
                    {
                        t.vert[0] = idxs[0];
                        t.vert[1] = idxs[i+1];
                        t.vert[2] = idxs[i+2];
                    }
                    else loopk(3) t.vert[k] = idxs[i&1 && k ? i+(1-(k-1))+1 : i+k];
                }
            }
        }

        bool load(char *path)
        {
            if(filename) return true;

            stream *file = openfile(path, "rb");
            if(!file) return false;

            md2_header header;
            file->read(&header, sizeof(md2_header));
            lilswap((int *)&header, sizeof(md2_header)/sizeof(int));

            if(header.magic!=844121161 || header.version!=8)
            {
                delete file;
                return false;
            }

            numframes = header.numframes;

            mesh &m = *new mesh;
            meshes.add(&m);
            m.owner = this;

            int *glcommands = new int[header.numglcommands];
            file->seek(header.offsetglcommands, SEEK_SET);
            int numglcommands = (int)file->read(glcommands, sizeof(int)*header.numglcommands)/sizeof(int);
            lilswap(glcommands, numglcommands);
            if(numglcommands < header.numglcommands) memset(&glcommands[numglcommands], 0, (header.numglcommands-numglcommands)*sizeof(int));

            vector<tcvert> tcgen;
            vector<ushort> vgen;
            vector<tri> trigen;
            gentcverts(glcommands, tcgen, vgen, trigen);
            delete[] glcommands;

            m.numverts = tcgen.length();
            m.tcverts = new tcvert[m.numverts];
            memcpy(m.tcverts, tcgen.getbuf(), m.numverts*sizeof(tcvert));
            m.numtris = trigen.length();
            m.tris = new tri[m.numtris];
            memcpy(m.tris, trigen.getbuf(), m.numtris*sizeof(tri));

            m.verts = new vec[m.numverts*numframes+1];

            md2_vertex *tmpverts = new md2_vertex[header.numvertices];
            int frame_offset = header.offsetframes;
            vec *curvert = m.verts;
            loopi(header.numframes)
            {
                md2_frame frame;
                file->seek(frame_offset, SEEK_SET);
                file->read(&frame, sizeof(md2_frame));
                lilswap((float *)&frame, 6);

                file->read(tmpverts, sizeof(md2_vertex)*header.numvertices);
                loopj(m.numverts)
                {
                    const md2_vertex &v = tmpverts[vgen[j]];
                    *curvert++ = vec(v.vertex[0]*frame.scale[0]+frame.translate[0],
                                   -(v.vertex[1]*frame.scale[1]+frame.translate[1]),
                                     v.vertex[2]*frame.scale[2]+frame.translate[2]);
                }
                frame_offset += header.framesize;
            }
            delete[] tmpverts;

            delete file;

            filename = newstring(path);
            return true;
        }

        void getdefaultanim(animstate &as, int anim, int varseed)
        {
            //                      0   1   2   3   4   5   6   7   8   9  10  11  12   13  14  15  16  17 18  19  20   21  21  23  24
            //                      I   R   A   P   P   P   J   L   F   S   T   W   P   CI  CW  CA  CP  CD  D   D   D   LD  LD  LD   F
            static int frame[] =  { 0,  40, 46, 54, 58, 62, 66, 69, 72, 84, 95, 112,123,135,154,160,169,173,178,184,190,183,189,197, 0 };
            static int range[] =  { 40, 6,  8,  4,  4,  4,  3,  3,  12, 11, 17, 11, 12, 19, 6,  9,  4,  5,  6,  6,  8,  1,  1,  1,   7 };
            static int animfr[] = { 0,  1,  2,  3,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 21, 24 };

            if((size_t)anim >= sizeof(animfr)/sizeof(animfr[0]))
            {
                as.frame = 0;
                as.range = 1;
                return;
            }
            int n = animfr[anim];
            if(anim==ANIM_PAIN || anim==ANIM_DEATH || anim==ANIM_LYING_DEAD) n += uint(varseed)%3;
            as.frame = frame[n];
            as.range = range[n];
        }

        void begingenshadow()
        {
            matrixpos = 0;
            matrixstack[0].identity();
            matrixstack[0].rotate_around_z(180*RAD);
        }
    };

    void render(int anim, int varseed, float speed, int basetime, const vec &o, float yaw, float pitch, dynent *d, modelattach *a, float scale)
    {
        if(!loaded) return;

        if(a) for(int i = 0; a[i].tag; i++)
        {
            if(a[i].pos) link(NULL, a[i].tag, a[i].pos);
        }

        if(!cullface) glDisable(GL_CULL_FACE);
        else if(anim&ANIM_MIRROR) glCullFace(GL_BACK);

        if(stenciling && !parts[0]->index)
        {
            shadowdir = vec(0, 1/SQRT2, -1/SQRT2);
            shadowdir.rotate_around_z((float)(-shadowyaw-yaw-180.0f)*RAD);
            shadowdir.rotate_around_y(-pitch*RAD);
            (shadowpos = shadowdir).mul(shadowdist);
        }

        modelpos = o;
        modelyaw = yaw;
        modelpitch = pitch;

        matrixpos = 0;
        matrixstack[0].identity();
        matrixstack[0].translate(o);
        matrixstack[0].rotate_around_z((yaw+180)*RAD);
        matrixstack[0].rotate_around_y(-pitch*RAD);
        if(anim&ANIM_MIRROR || scale!=1) matrixstack[0].scale(scale, anim&ANIM_MIRROR ? -scale : scale, scale);
        parts[0]->render(anim, varseed, speed, basetime, d);

        if(!cullface) glEnable(GL_CULL_FACE);
        else if(anim&ANIM_MIRROR) glCullFace(GL_FRONT);

        if(a) for(int i = 0; a[i].tag; i++)
        {
            if(a[i].pos) link(NULL, a[i].tag, NULL);

            vertmodel *m = (vertmodel *)a[i].m;
            if(!m) continue;
            m->parts[0]->index = parts.length()+i;
            m->setskin();
            m->render(anim, varseed, speed, basetime, o, yaw, pitch, d, NULL, scale);
        }

        if(d) d->lastrendered = lastmillis;
    }

    void rendershadow(int anim, int varseed, float speed, int basetime, const vec &o, float yaw, modelattach *a)
    {
        parts[0]->rendershadow(anim, varseed, speed, basetime, o, yaw);
        if(a) for(int i = 0; a[i].tag; i++)
        {
            vertmodel *m = (vertmodel *)a[i].m;
            if(!m) continue;
            part *p = m->parts[0];
            p->rendershadow(anim, varseed, speed, basetime, o, yaw);
        }
    }

    bool load()
    {
        if(loaded) return true;
        md2part &mdl = *new md2part;
        parts.add(&mdl);
        mdl.model = this;
        mdl.index = 0;
        const char *pname = parentdir(loadname);
        defformatstring(name1)("packages/models/%s/tris.md2", loadname);
        if(!mdl.load(path(name1)))
        {
            formatstring(name1)("packages/models/%s/tris.md2", pname);    // try md2 in parent folder (vert sharing)
            if(!mdl.load(path(name1))) return false;
        }
        Texture *skin;
        loadskin(loadname, pname, skin);
        loopv(mdl.meshes) mdl.meshes[i]->skin  = skin;
        if(skin==notexture) conoutf(_("could not load model skin for %s"), name1);
        loadingmd2 = this;
        defformatstring(name2)("packages/models/%s/md2.cfg", loadname);
        per_idents = false;
        neverpersist = true;
        if(!execfile(name2))
        {
            formatstring(name2)("packages/models/%s/md2.cfg", pname);
            execfile(name2);
        }
        neverpersist = false;
        per_idents = true;
        loadingmd2 = 0;
        loopv(parts) parts[i]->scaleverts(scale/16.0f, vec(translate.x, -translate.y, translate.z));
        radius = calcradius();
        if(shadowdist) calcneighbors();
        calcbbs();
        return loaded = true;
    }
};

void md2anim(char *anim, int *frame, int *range, float *speed)
{
    if(!loadingmd2 || loadingmd2->parts.empty()) { conoutf("not loading an md2"); return; }
    int num = findanim(anim);
    if(num<0) { conoutf("could not find animation %s", anim); return; }
    loadingmd2->parts.last()->setanim(num, *frame, *range, *speed);
}

void md2tag(char *name, char *vert1, char *vert2, char *vert3, char *vert4)
{
    if(!loadingmd2 || loadingmd2->parts.empty()) { conoutf("not loading an md2"); return; }
    md2::part &mdl = *loadingmd2->parts.last();
    int indexes[4] = { -1, -1, -1, -1 }, numverts = 0;
    loopi(4)
    {
        char *vert = NULL;
        switch(i)
        {
            case 0: vert = vert1; break;
            case 1: vert = vert2; break;
            case 2: vert = vert3; break;
            case 3: vert = vert4; break;
        }
        if(!vert[0]) break;
        if(isdigit(vert[0])) indexes[i] = ATOI(vert);
        else
        {
            int axis = 0, dir = 1;
            for(char *s = vert; *s; s++) switch(*s)
            {
                case '+': dir = 1; break;
                case '-': dir = -1; break;
                case 'x':
                case 'X': axis = 0; break;
                case 'y':
                case 'Y': axis = 1; break;
                case 'z':
                case 'Z': axis = 2; break;
            }
            if(!mdl.meshes.empty()) indexes[i] = mdl.meshes[0]->findvert(axis, dir);
        }
        if(indexes[i] < 0) { conoutf("could not find vertex %s", vert); return; }
        numverts = i + 1;
    }
    if(!mdl.gentag(name, indexes, numverts)) { conoutf("could not generate tag %s", name); return; }
}

void md2emit(char *tag, int *type, int *arg1, int *arg2)
{
    if(!loadingmd2 || loadingmd2->parts.empty()) { conoutf("not loading an md2"); return; };
    md2::part &mdl = *loadingmd2->parts.last();
    if(!mdl.addemitter(tag, *type, *arg1, *arg2)) { conoutf("could not find tag %s", tag); return; }
}

COMMAND(md2anim, "siif");
COMMAND(md2tag, "sssss");
COMMAND(md2emit, "siii");

struct md3;

md3 *loadingmd3 = NULL;

string md3dir;

struct md3tag
{
    char name[64];
    vec pos;
    float rotation[3][3];
};

struct md3vertex
{
    short vertex[3];
    short normal;
};

struct md3triangle
{
    int vertexindices[3];
};

struct md3header
{
    char id[4];
    int version;
    char name[64];
    int flags;
    int numframes, numtags, nummeshes, numskins;
    int ofs_frames, ofs_tags, ofs_meshes, ofs_eof; // offsets
};

struct md3meshheader
{
    char id[4];
    char name[64];
    int flags;
    int numframes, numshaders, numvertices, numtriangles;
    int ofs_triangles, ofs_shaders, ofs_uv, ofs_vertices, meshsize; // offsets
};

struct md3 : vertmodel
{
    md3(const char *name) : vertmodel(name) {}

    int type() { return MDL_MD3; }

    struct md3part : part
    {
        bool load(char *path)
        {
            if(filename) return true;

            stream *f = openfile(path, "rb");
            if(!f) return false;
            md3header header;
            f->read(&header, sizeof(md3header));
            lilswap(&header.version, 1);
            lilswap(&header.flags, 9);
            if(strncmp(header.id, "IDP3", 4) != 0 || header.version != 15) // header check
            {
                delete f;
                conoutf("md3: corrupted header");
                return false;
            }

            numframes = header.numframes;
            numtags = header.numtags;
            if(numtags)
            {
                tags = new tag[numframes*numtags];
                f->seek(header.ofs_tags, SEEK_SET);
                md3tag tag;

                loopi(header.numframes*header.numtags)
                {
                    f->read(&tag, sizeof(md3tag));
                    lilswap((float *)&tag.pos, 12);
                    if(tag.name[0] && i<header.numtags) tags[i].name = newstring(tag.name);
                    tags[i].pos = vec(tag.pos.y, tag.pos.x, tag.pos.z);
                    memcpy(tags[i].transform, tag.rotation, sizeof(tag.rotation));
                    // undo the x/y swap
                    loopj(3) swap(tags[i].transform[0][j], tags[i].transform[1][j]);
                    // then restore it
                    loopj(3) swap(tags[i].transform[j][0], tags[i].transform[j][1]);
                }
                links = new linkedpart[numtags];
            }

            int mesh_offset = header.ofs_meshes;
            loopi(header.nummeshes)
            {
                md3meshheader mheader;
                f->seek(mesh_offset, SEEK_SET);
                f->read(&mheader, sizeof(md3meshheader));
                lilswap(&mheader.flags, 10);

                if(mheader.numtriangles <= 0)
                {
                    mesh_offset += mheader.meshsize;
                    continue;
                }

                mesh &m = *meshes.add(new mesh);
                m.owner = this;
                m.name = newstring(mheader.name);

                m.numtris = mheader.numtriangles;
                m.tris = new tri[m.numtris];
                f->seek(mesh_offset + mheader.ofs_triangles, SEEK_SET);
                loopj(mheader.numtriangles)
                {
                    md3triangle tri;
                    f->read(&tri, sizeof(md3triangle)); // read the triangles
                    lilswap((int *)&tri, 3);
                    loopk(3) m.tris[j].vert[k] = (ushort)tri.vertexindices[k];
                }

                m.numverts = mheader.numvertices;
                m.tcverts = new tcvert[m.numverts];
                f->seek(mesh_offset + mheader.ofs_uv , SEEK_SET);
                f->read(m.tcverts, 2*sizeof(float)*m.numverts); // read the UV data
                lilswap(&m.tcverts[0].u, 2*m.numverts);

                m.verts = new vec[numframes*m.numverts + 1];
                f->seek(mesh_offset + mheader.ofs_vertices, SEEK_SET);
                loopj(numframes*mheader.numvertices)
                {
                    md3vertex v;
                    f->read(&v, sizeof(md3vertex)); // read the vertices
                    lilswap((short *)&v, 4);

                    m.verts[j].x = v.vertex[1]/64.0f;
                    m.verts[j].y = v.vertex[0]/64.0f;
                    m.verts[j].z = v.vertex[2]/64.0f;
                }

                mesh_offset += mheader.meshsize;
            }
            delete f;

            filename = newstring(path);
            return true;
        }

        void begingenshadow()
        {
            matrixpos = 0;
            matrixstack[0].identity();
            matrixstack[0].rotate_around_z(180*RAD);
        }
    };

    void render(int anim, int varseed, float speed, int basetime, const vec &o, float yaw, float pitch, dynent *d, modelattach *a, float scale)
    {
        if(!loaded) return;

        if(a) for(int i = 0; a[i].tag; i++)
        {
            vertmodel *m = (vertmodel *)a[i].m;
            if(!m)
            {
                if(a[i].pos) link(NULL, a[i].tag);
                continue;
            }
            part *p = m->parts[0];
            if(link(p, a[i].tag, a[i].pos)) p->index = parts.length()+i;
        }

        if(!cullface) glDisable(GL_CULL_FACE);
        else if(anim&ANIM_MIRROR) glCullFace(GL_BACK);

        if(stenciling)
        {
            shadowdir = vec(0, 1/SQRT2, -1/SQRT2);
            shadowdir.rotate_around_z((-shadowyaw-yaw-180.0f)*RAD);
            shadowdir.rotate_around_y(-pitch*RAD);
            (shadowpos = shadowdir).mul(shadowdist);
        }

        modelpos = o;
        modelyaw = yaw;
        modelpitch = pitch;

        matrixpos = 0;
        matrixstack[0].identity();
        matrixstack[0].translate(o);
        matrixstack[0].rotate_around_z((yaw+180)*RAD);
        matrixstack[0].rotate_around_y(-pitch*RAD);
        if(anim&ANIM_MIRROR || scale!=1) matrixstack[0].scale(scale, anim&ANIM_MIRROR ? -scale : scale, scale);
        parts[0]->render(anim, varseed, speed, basetime, d);

        if(!cullface) glEnable(GL_CULL_FACE);
        else if(anim&ANIM_MIRROR) glCullFace(GL_FRONT);

        if(a) for(int i = 0; a[i].tag; i++) link(NULL, a[i].tag);

        if(d) d->lastrendered = lastmillis;
    }

    void rendershadow(int anim, int varseed, float speed, int basetime, const vec &o, float yaw, modelattach *a)
    {
        if(parts.length()>1) return;
        parts[0]->rendershadow(anim, varseed, speed, basetime, o, yaw);
    }

    bool load()
    {
        if(loaded) return true;
        formatstring(md3dir)("packages/models/%s", loadname);

        const char *pname = parentdir(loadname);
        defformatstring(cfgname)("packages/models/%s/md3.cfg", loadname);

        loadingmd3 = this;
        per_idents = false;
        neverpersist = true;
        if(execfile(cfgname) && parts.length()) // configured md3, will call the md3* commands below
        {
            neverpersist = false;
            per_idents = true;
            loadingmd3 = NULL;
            if(parts.empty()) return false;
            loopv(parts) if(!parts[i]->filename) return false;
        }
        else // md3 without configuration, try default tris and skin
        {
            per_idents = false;
            loadingmd3 = NULL;
            md3part &mdl = *new md3part;
            parts.add(&mdl);
            mdl.model = this;
            mdl.index = 0;
            defformatstring(name1)("packages/models/%s/tris.md3", loadname);
            if(!mdl.load(path(name1)))
            {
                formatstring(name1)("packages/models/%s/tris.md3", pname);    // try md3 in parent folder (vert sharing)
                if(!mdl.load(path(name1))) return false;
            };
            Texture *skin;
            loadskin(loadname, pname, skin);
            loopv(mdl.meshes) mdl.meshes[i]->skin  = skin;
            if(skin==notexture) conoutf("could not load model skin for %s", name1);
        }
        loopv(parts) parts[i]->scaleverts(scale/16.0f, vec(translate.x, -translate.y, translate.z));
        radius = calcradius();
        if(shadowdist) calcneighbors();
        calcbbs();
        return loaded = true;
    }
};

void md3load(char *model)
{
    if(!loadingmd3) { conoutf("not loading an md3"); return; };
    defformatstring(filename)("%s/%s", md3dir, model);
    md3::md3part &mdl = *new md3::md3part;
    loadingmd3->parts.add(&mdl);
    mdl.model = loadingmd3;
    mdl.index = loadingmd3->parts.length()-1;
    if(!mdl.load(path(filename))) conoutf("could not load %s", filename); // ignore failure
}

void md3skin(char *objname, char *skin)
{
    if(!objname || !skin) return;
    if(!loadingmd3 || loadingmd3->parts.empty()) { conoutf("not loading an md3"); return; };
    md3::part &mdl = *loadingmd3->parts.last();
    loopv(mdl.meshes)
    {
        md3::mesh &m = *mdl.meshes[i];
        if(!strcmp(objname, "*") || !strcmp(m.name, objname))
        {
            defformatstring(spath)("%s/%s", md3dir, skin);
            m.skin = textureload(spath);
        }
    }
}

void md3anim(char *anim, int *startframe, int *range, float *speed)
{
    if(!loadingmd3 || loadingmd3->parts.empty()) { conoutf("not loading an md3"); return; };
    int num = findanim(anim);
    if(num<0) { conoutf("could not find animation %s", anim); return; };
    loadingmd3->parts.last()->setanim(num, *startframe, *range, *speed);
}

void md3link(int *parent, int *child, char *tagname)
{
    if(!loadingmd3) { conoutf("not loading an md3"); return; };
    if(!loadingmd3->parts.inrange(*parent) || !loadingmd3->parts.inrange(*child)) { conoutf("no models loaded to link"); return; }
    if(!loadingmd3->parts[*parent]->link(loadingmd3->parts[*child], tagname)) conoutf("could not link model %s", loadingmd3->loadname);
}

void md3emit(char *tag, int *type, int *arg1, int *arg2)
{
    if(!loadingmd3 || loadingmd3->parts.empty()) { conoutf("not loading an md3"); return; };
    md3::part &mdl = *loadingmd3->parts.last();
    if(!mdl.addemitter(tag, *type, *arg1, *arg2)) { conoutf("could not find tag %s", tag); return; }
}

COMMAND(md3load, "s");
COMMAND(md3skin, "ss");
COMMAND(md3anim, "siif");
COMMAND(md3link, "iis");
COMMAND(md3emit, "siii");

#define checkmdl if(!loadingmodel) { conoutf("not loading a model"); return; }

void mdlcullface(int *cullface)
{
    checkmdl;
    loadingmodel->cullface = *cullface!=0;
}

COMMAND(mdlcullface, "i");

void mdlvertexlight(int *vertexlight)
{
    checkmdl;
    loadingmodel->vertexlight = *vertexlight!=0;
}

COMMAND(mdlvertexlight, "i");

void mdltranslucent(int *translucency)
{
    checkmdl;
    loadingmodel->translucency = *translucency/100.0f;
}

COMMAND(mdltranslucent, "i");

void mdlalphatest(int *alphatest)
{
    checkmdl;
    loadingmodel->alphatest = *alphatest/100.0f;
}

COMMAND(mdlalphatest, "i");

void mdlalphablend(int *alphablend) //ALX Alpha channel models
{
    checkmdl;
    loadingmodel->alphablend = *alphablend!=0;
}
COMMAND(mdlalphablend, "i");

void mdlscale(int *percent)
{
    checkmdl;
    float scale = 0.3f;
    if(*percent>0) scale = *percent/100.0f;
    else if(*percent<0) scale = 0.0f;
    loadingmodel->scale = scale;
}

COMMAND(mdlscale, "i");

void mdltrans(float *x, float *y, float *z)
{
    checkmdl;
    loadingmodel->translate = vec(*x, *y, *z);
}

COMMAND(mdltrans, "fff");

void mdlshadowdist(int *dist)
{
    checkmdl;
    loadingmodel->shadowdist = *dist;
}

COMMAND(mdlshadowdist, "i");

void mdlcachelimit(int *limit)
{
    checkmdl;
    loadingmodel->cachelimit = *limit;
}

COMMAND(mdlcachelimit, "i");

vector<mapmodelinfo> mapmodels;

void mapmodel(int *rad, int *h, int *zoff, char *snap, char *name)
{
    mapmodelinfo &mmi = mapmodels.add();
    mmi.rad = *rad;
    mmi.h = *h;
    mmi.zoff = *zoff;
    mmi.m = NULL;
    formatstring(mmi.name)("mapmodels/%s", name);
}

void mapmodelreset()
{
    if(execcontext==IEXC_MAPCFG) mapmodels.shrink(0);
}

mapmodelinfo &getmminfo(int i) { return mapmodels.inrange(i) ? mapmodels[i] : *(mapmodelinfo *)0; }

COMMAND(mapmodel, "iiiss");
COMMAND(mapmodelreset, "");

hashtable<const char *, model *> mdllookup;
hashtable<const char *, char> mdlnotfound;

model *loadmodel(const char *name, int i, bool trydl)     // load model by name (optional) or from mapmodels[i]
{
    if(!name)                                   // name == NULL -> get index i from mapmodels[]
    {
        if(!mapmodels.inrange(i)) return NULL;
        mapmodelinfo &mmi = mapmodels[i];
        if(mmi.m) return mmi.m;                 // mapmodels[i] was already loaded
        name = mmi.name;
    }
    if(mdlnotfound.access(name)) return NULL;   // already tried to find that earlier -> not available
    model **mm = mdllookup.access(name);
    model *m;
    if(mm) m = *mm;                             // a model of that name is already loaded
    else
    {
        pushscontext(IEXC_MDLCFG);
        m = new md2(name);                      // try md2
        loadingmodel = m;
        if(!m->load())                          // md2 didn't load
        {
            delete m;
            m = new md3(name);                  // try md3
            loadingmodel = m;
            if(!m->load())                      // md3 didn't load -> we don't have that model
            {
                delete m;
                m = loadingmodel = NULL;
                if(trydl)
                {
                    defformatstring(dl)("packages/models/%s", name);
                    requirepackage(PCK_MAPMODEL, dl);
                }
                else
                {
                    conoutf("\f3failed to load model %s", name);
                    mdlnotfound.access(newstring(name), 0);  // do not search for this name again
                }
            }
        }
        popscontext();
        if(loadingmodel && m) mdllookup.access(m->name(), m);
        loadingmodel = NULL;
    }
    if(mapmodels.inrange(i) && !mapmodels[i].m) mapmodels[i].m = m;
    return m;
}

void cleanupmodels()
{
    enumerate(mdllookup, model *, m, m->cleanup());
}

VARP(dynshadow, 0, 40, 100);
VARP(dynshadowdecay, 0, 1000, 3000);

struct batchedmodel
{
    vec o;
    int anim, varseed, tex;
    float yaw, pitch, speed;
    int basetime;
    playerent *d;
    int attached;
    float scale;
};
struct modelbatch
{
    model *m;
    vector<batchedmodel> batched;
};
static vector<modelbatch *> batches;
static vector<modelattach> modelattached;
static int numbatches = -1;

void startmodelbatches()
{
    numbatches = 0;
    modelattached.setsize(0);
}

batchedmodel &addbatchedmodel(model *m)
{
    modelbatch *b = NULL;
    if(m->batch>=0 && m->batch<numbatches && batches[m->batch]->m==m) b = batches[m->batch];
    else
    {
        if(numbatches<batches.length())
        {
            b = batches[numbatches];
            b->batched.setsize(0);
        }
        else b = batches.add(new modelbatch);
        b->m = m;
        m->batch = numbatches++;
    }
    return b->batched.add();
}

void renderbatchedmodel(model *m, batchedmodel &b)
{
    modelattach *a = NULL;
    if(b.attached>=0) a = &modelattached[b.attached];

    if(stenciling)
    {
        m->render(b.anim|ANIM_NOSKIN, b.varseed, b.speed, b.basetime, b.o, b.yaw, b.pitch, b.d, a, b.scale);
        return;
    }

    int x = (int)b.o.x, y = (int)b.o.y;
    if(!OUTBORD(x, y))
    {
        sqr *s = S(x, y);
        glColor3ub(s->r, s->g, s->b);
    }
    else glColor3f(1, 1, 1);

    m->setskin(b.tex);

    if(b.anim&ANIM_TRANSLUCENT)
    {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        m->render(b.anim|ANIM_NOSKIN, b.varseed, b.speed, b.basetime, b.o, b.yaw, b.pitch, b.d, a, b.scale);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GLfloat color[4];
        glGetFloatv(GL_CURRENT_COLOR, color);
        glColor4f(color[0], color[1], color[2], m->translucency);
    }

    m->render(b.anim, b.varseed, b.speed, b.basetime, b.o, b.yaw, b.pitch, b.d, a, b.scale);

    if(b.anim&ANIM_TRANSLUCENT)
    {
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
    }
}

void renderbatchedmodelshadow(model *m, batchedmodel &b)
{
    int x = (int)b.o.x, y = (int)b.o.y;
    if(OUTBORD(x, y)) return;
    sqr *s = S(x, y);
    vec center(b.o.x, b.o.y, s->floor);
    if(s->type==FHF) center.z -= s->vdelta/4.0f;
    if(dynshadowquad && center.z-0.1f>b.o.z) return;
    center.z += 0.1f;
    modelattach *a = NULL;
    if(b.attached>=0) a = &modelattached[b.attached];
    float intensity = dynshadow/100.0f;
    if(dynshadowdecay) switch(b.anim&ANIM_INDEX)
    {
        case ANIM_DECAY:
        case ANIM_LYING_DEAD:
            intensity *= max(1.0f - float(lastmillis - b.basetime)/dynshadowdecay, 0.0f);
            break;
    }
    glColor4f(0, 0, 0, intensity);
    m->rendershadow(b.anim, b.varseed, b.speed, b.basetime, dynshadowquad ? center : b.o, b.yaw, a);
}

static int sortbatchedmodels(const batchedmodel *x, const batchedmodel *y)
{
    if(x->tex < y->tex) return -1;
    if(x->tex > y->tex) return 1;
    return 0;
}

struct translucentmodel
{
    model *m;
    batchedmodel *batched;
    float dist;
};

static int sorttranslucentmodels(const translucentmodel *x, const translucentmodel *y)
{
    if(x->dist > y->dist) return -1;
    if(x->dist < y->dist) return 1;
    return 0;
}

void clearmodelbatches()
{
    numbatches = -1;
}

void endmodelbatches(bool flush)
{
    vector<translucentmodel> translucent;
    loopi(numbatches)
    {
        modelbatch &b = *batches[i];
        if(b.batched.empty()) continue;
        loopvj(b.batched) if(b.batched[j].tex) { b.batched.sort(sortbatchedmodels); break; }
        b.m->startrender();
        loopvj(b.batched)
        {
            batchedmodel &bm = b.batched[j];
            if(bm.anim&ANIM_TRANSLUCENT)
            {
                translucentmodel &tm = translucent.add();
                tm.m = b.m;
                tm.batched = &bm;
                tm.dist = camera1->o.dist(bm.o);
                continue;
            }
            renderbatchedmodel(b.m, bm);
        }
        if(dynshadow && b.m->hasshadows() && (!reflecting || refracting) && (!stencilshadow || !hasstencil || stencilbits < 8))
        {
            loopvj(b.batched)
            {
                batchedmodel &bm = b.batched[j];
                if(bm.anim&ANIM_TRANSLUCENT) continue;
                renderbatchedmodelshadow(b.m, bm);
            }
        }
        b.m->endrender();
    }
    if(translucent.length())
    {
        translucent.sort(sorttranslucentmodels);
        model *lastmodel = NULL;
        loopv(translucent)
        {
            translucentmodel &tm = translucent[i];
            if(lastmodel!=tm.m)
            {
                if(lastmodel) lastmodel->endrender();
                (lastmodel = tm.m)->startrender();
            }
            renderbatchedmodel(tm.m, *tm.batched);
        }
        if(lastmodel) lastmodel->endrender();
    }
    if(flush) clearmodelbatches();
}

const int dbgmbatch = 0;
//VAR(dbgmbatch, 0, 0, 1);

VARP(popdeadplayers, 0, 0, 1);
void rendermodel(const char *mdl, int anim, int tex, float rad, const vec &o, float yaw, float pitch, float speed, int basetime, playerent *d, modelattach *a, float scale)
{
    if(popdeadplayers && d && a)
    {
        int acv = anim&ANIM_INDEX;
        if( acv == ANIM_DECAY || acv == ANIM_LYING_DEAD || acv == ANIM_CROUCH_DEATH || acv == ANIM_DEATH ) return;
    }
    model *m = loadmodel(mdl);
    if(!m || (stenciling && (m->shadowdist <= 0 || anim&ANIM_TRANSLUCENT))) return;

    if(rad >= 0)
    {
        if(!rad) rad = m->radius;
        if(isoccluded(camera1->o.x, camera1->o.y, o.x-rad, o.y-rad, rad*2)) return;
    }

    if(stenciling && d && !raycubelos(camera1->o, o, d->radius))
    {
        vec target(o);
        target.z += d->eyeheight;
        if(!raycubelos(camera1->o, target, d->radius)) return;
    }

    int varseed = 0;
    if(d) switch(anim&ANIM_INDEX)
    {
        case ANIM_DEATH:
        case ANIM_LYING_DEAD: varseed = (int)(size_t)d + d->lastpain; break;
        default: varseed = (int)(size_t)d + d->lastaction; break;
    }

    if(a) for(int i = 0; a[i].tag; i++)
    {
        if(a[i].name) a[i].m = loadmodel(a[i].name);
        //if(a[i].m && a[i].m->type()!=m->type()) a[i].m = NULL;
    }

    if(numbatches>=0 && !dbgmbatch)
    {
        batchedmodel &b = addbatchedmodel(m);
        b.o = o;
        b.anim = anim;
        b.varseed = varseed;
        b.tex = tex;
        b.yaw = yaw;
        b.pitch = pitch;
        b.speed = speed;
        b.basetime = basetime;
        b.d = d;
        b.attached = a ? modelattached.length() : -1;
        if(a) for(int i = 0;; i++) { modelattached.add(a[i]); if(!a[i].tag) break; }
        b.scale = scale;
        return;
    }

    if(stenciling)
    {
        m->startrender();
        m->render(anim|ANIM_NOSKIN, varseed, speed, basetime, o, yaw, pitch, d, a, scale);
        m->endrender();
        return;
    }

    m->startrender();

    int x = (int)o.x, y = (int)o.y;
    if(!OUTBORD(x, y))
    {
        sqr *s = S(x, y);
        if(!(anim&ANIM_TRANSLUCENT) && dynshadow && m->hasshadows() && (!reflecting || refracting) && (!stencilshadow || !hasstencil || stencilbits < 8))
        {
            vec center(o.x, o.y, s->floor);
            if(s->type==FHF) center.z -= s->vdelta/4.0f;
            if(!dynshadowquad || center.z-0.1f<=o.z)
            {
                center.z += 0.1f;
                float intensity = dynshadow/100.0f;
                if(dynshadowdecay) switch(anim&ANIM_INDEX)
                {
                    case ANIM_DECAY:
                    case ANIM_LYING_DEAD:
                        intensity *= max(1.0f - float(lastmillis - basetime)/dynshadowdecay, 0.0f);
                        break;
                }
                glColor4f(0, 0, 0, intensity);
                m->rendershadow(anim, varseed, speed, basetime, dynshadowquad ? center : o, yaw, a);
            }
        }
        glColor3ub(s->r, s->g, s->b);
    }
    else glColor3f(1, 1, 1);

    m->setskin(tex);

    if(anim&ANIM_TRANSLUCENT)
    {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        m->render(anim|ANIM_NOSKIN, varseed, speed, basetime, o, yaw, pitch, d, a, scale);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        GLfloat color[4];
        glGetFloatv(GL_CURRENT_COLOR, color);
        glColor4f(color[0], color[1], color[2], m->translucency);
    }

    m->render(anim, varseed, speed, basetime, o, yaw, pitch, d, a, scale);

    if(anim&ANIM_TRANSLUCENT)
    {
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
    }

    m->endrender();
}

int findanim(const char *name)
{
    const char *names[] = { "idle", "run", "attack", "pain", "jump", "land", "flipoff", "salute", "taunt", "wave", "point", "crouch idle", "crouch walk", "crouch attack", "crouch pain", "crouch death", "death", "lying dead", "flag", "gun idle", "gun shoot", "gun reload", "gun throw", "mapmodel", "trigger", "decay", "all" };
    loopi(sizeof(names)/sizeof(names[0])) if(!strcmp(name, names[i])) return i;
    return -1;
}

void loadskin(const char *dir, const char *altdir, Texture *&skin) // model skin sharing
{
    #define ifnoload if((skin = textureload(path))==notexture)
    defformatstring(path)("packages/models/%s/skin.jpg", dir);
    ifnoload
    {
        strcpy(path+strlen(path)-3, "png");
        ifnoload
        {
            formatstring(path)("packages/models/%s/skin.jpg", altdir);
            ifnoload
            {
                strcpy(path+strlen(path)-3, "png");
                ifnoload return;
            }
        }
    }
}

void preload_playermodels()
{
    model *playermdl = loadmodel("playermodels");
    if(dynshadow && playermdl) playermdl->genshadows(8.0f, 4.0f);
    loopi(NUMGUNS)
    {
        if (i==GUN_CPISTOL) continue; //RR 18/12/12 - Remove when cpistol is added.
        defformatstring(widn)("modmdlvwep%d", i);
        defformatstring(vwep)("weapons/%s/world", identexists(widn)?getalias(widn):guns[i].modelname);
        model *vwepmdl = loadmodel(vwep);
        if(dynshadow && vwepmdl) vwepmdl->genshadows(8.0f, 4.0f);
    }
}

void preload_entmodels()
{
     string buf;

     loopi(I_AKIMBO-I_CLIPS+1)
     {
         strcpy(buf, "pickups/");

         defformatstring(widn)("modmdlpickup%d", i-3);

         if (identexists(widn))
         strcat(buf, getalias(widn));
         else
         strcat(buf, entmdlnames[i]);

         model *mdl = loadmodel(buf);

         if(dynshadow && mdl) mdl->genshadows(8.0f, 2.0f);
     }
     static const char *bouncemdlnames[] = { "misc/gib01", "misc/gib02", "misc/gib03", "weapons/grenade/static" };
     loopi(sizeof(bouncemdlnames)/sizeof(bouncemdlnames[0]))
     {
         model *mdl = NULL;
         defformatstring(widn)("modmdlbounce%d", i);

         if (identexists(widn))
         mdl = loadmodel(getalias(widn));
         else
         mdl = loadmodel(bouncemdlnames[i]);

         if(dynshadow && mdl) mdl->genshadows(8.0f, 2.0f);
     }
}

void preload_mapmodels(bool trydl)
{
    loopv(ents)
    {
        entity &e = ents[i];
        if(e.type!=MAPMODEL || !mapmodels.inrange(e.attr2)) continue;
        loadmodel(NULL, e.attr2, trydl);
        if(e.attr4) lookuptexture(e.attr4, notexture, trydl);
    }
}

inline void renderhboxpart(playerent *d, vec top, vec bottom, vec up)
{
    if(d->state==CS_ALIVE && d->head.x >= 0)
    {
        glBegin(GL_LINE_LOOP);
        loopi(8)
        {
            vec pos(camright);
            pos.rotate(2*M_PI*i/8.0f, camdir).mul(HEADSIZE).add(d->head);
            glVertex3fv(pos.v);
        }
        glEnd();

        glBegin(GL_LINES);
        glVertex3fv(bottom.v);
        glVertex3fv(d->head.v);
        glEnd();
    }

    vec spoke;
    spoke.orthogonal(up);
    spoke.normalize().mul(d->radius);

    glBegin(GL_LINE_LOOP);
    loopi(8)
    {
        vec pos(spoke);
        pos.rotate(2*M_PI*i/8.0f, up).add(top);
        glVertex3fv(pos.v);
    }
    glEnd();
    glBegin(GL_LINE_LOOP);
    loopi(8)
    {
        vec pos(spoke);
        pos.rotate(2*M_PI*i/8.0f, up).add(bottom);
        glVertex3fv(pos.v);
    }
    glEnd();
    glBegin(GL_LINES);
    loopi(8)
    {
        vec pos(spoke);
        pos.rotate(2*M_PI*i/8.0f, up).add(bottom);
        glVertex3fv(pos.v);
        pos.sub(bottom).add(top);
        glVertex3fv(pos.v);
    }
    glEnd();
}

void renderclient(playerent *d, const char *mdlname, const char *vwepname, int tex)
{
    int varseed = (int)(size_t)d;
    int anim = ANIM_IDLE|ANIM_LOOP;
    float speed = 0.0;
    vec o(d->o);
    o.z -= d->eyeheight;
    int basetime = -((int)(size_t)d&0xFFF);
    if(d->state==CS_DEAD)
    {
        if(d==player1 && d->allowmove()) return;
        loopv(bounceents) if(bounceents[i]->bouncetype==BT_GIB && bounceents[i]->owner==d) return;
        d->pitch = 0.1f;
        anim = ANIM_DEATH;
        varseed += d->lastpain;
        basetime = d->lastpain;
        int t = lastmillis-d->lastpain;
        if(t<0 || t>20000) return;
        if(t>2000)
        {
            anim = ANIM_LYING_DEAD|ANIM_NOINTERP|ANIM_LOOP;
            basetime += 2000;
            t -= 2000;
            o.z -= t*t/10000000000.0f*t;
        }
    }
    else if(d->state==CS_EDITING)                   { anim = ANIM_JUMP|ANIM_END; }
    else if(d->state==CS_LAGGED)                    { anim = ANIM_SALUTE|ANIM_LOOP|ANIM_TRANSLUCENT; }
    else if(lastmillis-d->lastpain<300)             { anim = d->crouching ? ANIM_CROUCH_PAIN : ANIM_PAIN; speed = 300.0f/4; varseed += d->lastpain; basetime = d->lastpain; }
//     else if(!d->onfloor && d->timeinair>50)         { anim = ANIM_JUMP|ANIM_END; }
    else if(!d->onfloor && d->timeinair>50)         { anim = (d->crouching ? ANIM_CROUCH_WALK : ANIM_JUMP)|ANIM_END; }
    else if(d->weaponsel==d->lastattackweapon && lastmillis-d->lastaction<300 && d->lastpain < d->lastaction) { anim = d->crouching ? ANIM_CROUCH_ATTACK : ANIM_ATTACK; speed = 300.0f/8; basetime = d->lastaction; }
    else if(!d->move && !d->strafe)                 { anim = (d->crouching ? ANIM_CROUCH_IDLE : ANIM_IDLE)|ANIM_LOOP; }
    else                                            { anim = (d->crouching ? ANIM_CROUCH_WALK : ANIM_RUN)|ANIM_LOOP; speed = 1860/d->maxspeed; }
    if(d->move < 0) anim |= ANIM_REVERSE;
    modelattach a[3];
    int numattach = 0;
    if(vwepname)
    {
        a[numattach].name = vwepname;
        a[numattach].tag = "tag_weapon";
        numattach++;
    }

    if(!stenciling && !reflecting && !refracting)
    {
        if(d->weaponsel==d->lastattackweapon && lastmillis-d->lastaction < d->weaponsel->flashtime())
            anim |= ANIM_PARTICLE;
        if(d != player1 && d->state==CS_ALIVE)
        {
            d->head = vec(-1, -1, -1);
            a[numattach].tag = "tag_head";
            a[numattach].pos = &d->head;
            numattach++;
        }
    }
    if(player1->isspectating() && d->clientnum == player1->followplayercn && player1->spectatemode == SM_FOLLOW3RD_TRANSPARENT)
    {
        anim |= ANIM_TRANSLUCENT; // see through followed player
        if(stenciling) return;
    }
    rendermodel(mdlname, anim|ANIM_DYNALLOC, tex, 1.5f, o, d->yaw+90, d->pitch/4, speed, basetime, d, a);
    if(!stenciling && !reflecting && !refracting)
    {
        if(isteam(player1->team, d->team)) renderaboveheadicon(d);
    }
}

VARP(teamdisplaymode, 0, 1, 2);

#define SKINBASE "packages/models/playermodels"
VARP(hidecustomskins, 0, 0, 2);
static vector<char *> playerskinlist;

const char *getclientskin(const char *name, const char *suf)
{
    static string tmp;
    int suflen = (int)strlen(suf), namelen = (int)strlen(name);
    const char *s, *r = NULL;
    loopv(playerskinlist)
    {
        s = playerskinlist[i];
        int sl = (int)strlen(s) - suflen;
        if(sl > 0 && !strcmp(s + sl, suf))
        {
            if(namelen == sl && !strncmp(name, s, namelen)) return s; // exact match
            if(s[sl - 1] == '_')
            {
                copystring(tmp, s);
                tmp[sl - 1] = '\0';
                if(strstr(name, tmp)) r = s; // partial match
            }
        }
    }
    return r;
}

void updateclientname(playerent *d)
{
    static bool gotlist = false;
    if(!gotlist) listfiles(SKINBASE "/custom", "jpg", playerskinlist);
    gotlist = true;
    if(!d || !playerskinlist.length()) return;
    d->skin_noteam = getclientskin(d->name, "_ffa");
    d->skin_cla = getclientskin(d->name, "_cla");
    d->skin_rvsf = getclientskin(d->name, "_rvsf");
}

void renderclient(playerent *d)
{
    if(!d) return;
    int team = team_base(d->team);
    const char *cs = NULL, *skinbase = SKINBASE, *teamname = team_basestring(team);
    int skinid = 1 + d->skin();
    string skin;
    if(hidecustomskins == 0 || (hidecustomskins == 1 && !m_teammode))
    {
        cs = team ? d->skin_rvsf : d->skin_cla;
        if(!m_teammode && d->skin_noteam) cs = d->skin_noteam;
    }
    if(cs)
        formatstring(skin)("%s/custom/%s.jpg", skinbase, cs);
    else
    {
        if(!m_teammode || !teamdisplaymode) formatstring(skin)("%s/%s/%02i.jpg", skinbase, teamname, skinid);
        else switch(teamdisplaymode)
        {
            case 1: formatstring(skin)("%s/%s/%02i_%svest.jpg", skinbase, teamname, skinid, team ? "blue" : "red"); break;
            case 2: default: formatstring(skin)("%s/%s/%s.jpg", skinbase, teamname, team ? "blue" : "red"); break;
        }
    }
    string vwep;
    defformatstring(widn)("modmdlvwep%d", d->weaponsel->type);
    if(d->weaponsel) formatstring(vwep)("weapons/%s/world", identexists(widn)?getalias(widn):d->weaponsel->info.modelname);
    else vwep[0] = 0;
    renderclient(d, "playermodels", vwep[0] ? vwep : NULL, -(int)textureload(skin)->id);
}

void renderclients()
{
    playerent *d;
    loopv(players) if((d = players[i]) && d->state!=CS_SPAWNING && d->state!=CS_SPECTATE && (!player1->isspectating() || player1->spectatemode != SM_FOLLOW1ST || player1->followplayercn != i)) renderclient(d);
    if(player1->state==CS_DEAD || (reflecting && !refracting)) renderclient(player1);
}
