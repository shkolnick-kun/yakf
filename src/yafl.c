/*******************************************************************************
    Copyright 2020 anonimous <shkolnick-kun@gmail.com> and contributors.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing,
    software distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.

    See the License for the specific language governing permissions
    and limitations under the License.
******************************************************************************/

#include "yafl.h"
#include <string.h>

/*=============================================================================
                                  Base UDEKF
=============================================================================*/
void yafl_ekf_base_predict(yaflEKFBaseSt * self)
{
    yaflInt i;
    yaflInt nx;
    yaflInt nx2;
    yaflFloat * w;
    yaflFloat * d;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->Uq);
    YAFL_ASSERT(self->Dq);
    YAFL_ASSERT(self->W);
    YAFL_ASSERT(self->D);

    nx2 = nx * 2;
    w  = self->W;

    /*Default f(x) = x*/
    if (0 == self->f)
    {
        YAFL_ASSERT(0 == self->jf);

        for (i = 0; i < nx; i++)
        {
            yaflInt j;
            yaflInt nci;

            nci = nx2 * i;
            for (j = 0; j < nx; j++)
            {
                w[nci + j] = (i != j) ? 0.0 : 1.0;
            }
        }
    }
    else
    {
        /*Must have some Jacobian function*/
        YAFL_ASSERT(self->jf);

        self->f(self);  /* x = f(x_old, ...) */
        self->jf(self); /* Place F(x, ...)=df/dx to W  */
    }
    /* Now W = (F|***) */
    YAFLM_BSET_BU(nx2, 0, nx, w, nx, nx, nx2, 0, 0, w, self->Up);
    /* Now W = (F|FUp) */
    yaflm_bset_u(nx2, w, nx, self->Uq);
    /* Now W = (Uq|FUp) */

    /* D = concatenate([Dq, Dp]) */
    d = self->D;
    i = nx*sizeof(yaflFloat);
    memcpy((void *)       d, (void *)self->Dq, i);
    memcpy((void *)(d + nx), (void *)self->Dp, i);

    /* Up, Dp = MWGSU(w, d)*/
    yaflm_mwgsu(nx, nx2, self->Up, self->Dp, w, d);
}

void yafl_ekf_base_update(yaflEKFBaseSt * self, yaflFloat * z, yaflEKFScalarUpdateP scalar_update)
{
    yaflInt j;
    yaflInt nx;
    yaflInt nz;
    yaflFloat * y;
    yaflFloat * ur;
    yaflFloat * h;

    YAFL_ASSERT(scalar_update);

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Ur);

    YAFL_ASSERT(self->h);
    YAFL_ASSERT(self->jh);
    YAFL_ASSERT(self->H);

    nz = self->Nz;
    YAFL_ASSERT(nz > 0);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(z);

    y  = self->y;

    self->h(self);  /* self.y =  h(x,...) */
    self->jh(self); /* self.H = jh(x,...) */

    if (0 == self->zrf)
    {
        /*Default residual*/
        for (j = 0; j < nz; j++)
        {
            y[j] = z[j] - y[j];
        }
    }
    else
    {
        /*zrf must be aware of self internal structure*/
        self->zrf(self, z); /* self.y = zrf(z, h(x,...)) */
    }

    /* Decorrelate measurement noise */
    ur = self->Ur;
    h  = self->H;
    yaflm_ruv(nz,     y, ur);
    yaflm_rum(nz, nx, h, ur);

    /* Do scalar updates */
    for (j = 0; j < nz; j++)
    {
        scalar_update(self, j);
    }
}
/*=============================================================================
                                Bierman filter
=============================================================================*/
static void _bierman_scalar_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt j;
    yaflInt k;
    yaflInt nx;
    yaflInt nxk;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat r;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->D);

    u = self->Up;
    d = self->Dp;

    h = self->H + nx * i;

    /* f = h.dot(Up) */
    f = self->D;
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#define v h /*Don't need h any more, use it to store v*/
    YAFLM_SET_DV(nx, v, d, f);

    r = self->Dr[i];
    for (k = 0, nxk = 0; k < nx; nxk += k++)
    {
        yaflFloat a;
        yaflFloat fk;
        yaflFloat vk;

        fk = f[k];
        vk = v[k];
        a = r + fk * vk;
        d[k] *= r / a;
#define p fk /*No need for separate p variable*/
        p = - fk / r;
        for (j = 0; j < k; j++)
        {
            yaflFloat ujk;
            yaflFloat vj;

            ujk = u[j + nxk];
            vj  = v[j];

            u[j + nxk] = ujk +   p * vj;
            v[j]       = vj  + ujk * vk;
        }
#undef  p /*Don't need p any more...*/
        r = a;
    }
    /*
    Now we must do:
    self.x += K * y[i]

    Since:
    r == a

    then we have:
    K == v / a == v / r

    and so:
    K * y[i] == (v / r) * y[i] == v / r * y[i] == v * (y[i] / r)

    Finally we get:
    self.x += v * (y[i] / r)
    */
    yaflm_add_vxn(nx, self->x, v, self->y[i] / r);
#undef v /*Don't nee v any more*/
}

void yafl_ekf_bierman_update(yaflEKFBaseSt * self, yaflFloat * z)
{
    yafl_ekf_base_update(self, z, _bierman_scalar_update);
}

/*=============================================================================
                                Joseph filter
=============================================================================*/
static void _joseph_scalar_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt nx;
    yaflInt nx1;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat * v;
    yaflFloat * w;
    yaflFloat r;
    yaflFloat s;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->W);
    YAFL_ASSERT(self->D);

    nx1 = nx + 1;

    d = self->Dp;
    u = self->Up;

    h = self->H + nx * i;

    v = self->D;
    f = v + nx;

    w = self->W;

    /* f = h.dot(Up) */
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFLM_SET_DV(nx, v, d, f);

    r = self->Dr[i];

    /* s = r + f.dot(v)*/
    s = r + yaflm_vtv(nx, f, v);

    /*K = Up.dot(v/s) = Up.dot(v)/s*/
#define K h /*Don't need h any more, use it to store K*/
#define D v
    yaflm_set_uv(nx, K, u, v);
    yaflm_set_vrn(nx, K, K, s); /*May be used in place*/

    /*Set W and D*/
    yaflm_bset_vvt(nx1, w, nx, K, f);
    yaflm_bsub_u(nx1, w, nx, u);

    /* Now w is (Kf - Up|***) */
    YAFLM_BSET_V(nx1, 0, nx, w, nx, K);
    /* Now w is (Kf - Up|K) */

    /* D = concatenate([Dp, np.array([r])]) */
    memcpy((void *)D, (void *)d, nx * sizeof(yaflFloat));
    D[nx] = r;

    /* Up, Dp = MWGSU(W, D)*/
    yaflm_mwgsu(nx, nx1, u, d, w, D);

    /* self.x += K * y[i] */
    yaflm_add_vxn(nx, self->x, K, self->y[i]);
#undef D /*Don't nee D any more*/
#undef K /*Don't nee K any more*/
}

void yafl_ekf_joseph_update(yaflEKFBaseSt * self, yaflFloat * z)
{
    yafl_ekf_base_update(self, z, _joseph_scalar_update);
}

/*=============================================================================
                          Adaptive Bierman filter
=============================================================================*/
static void _adaptive_bierman_scalar_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt j;
    yaflInt k;
    yaflInt nx;
    yaflInt nxk;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat nu;
    yaflFloat r;
    yaflFloat c;
    yaflFloat s;
    yaflFloat ac;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->D);

    u = self->Up;
    d = self->Dp;

    h = self->H + nx * i;

    nu = self->y[i];
    r  = self->Dr[i];

    /* f = h.dot(Up) */
    f = self->D;
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#define v h /*Don't need h any more, use it to store v*/
    YAFLM_SET_DV(nx, v, d, f);

    /* s = r + f.dot(v)*/
    c = yaflm_vtv(nx, f, v);
    s = c + r;

    /* Divergence test */
    ac = (nu * (nu / (((yaflEKFAdaptiveSt *)self)->chi2))) - s;
    if (ac > 0.0)
    {
        /*Adaptive correction factor*/
        ac = ac / c + 1.0;
    }
    else
    {
        ac = 1.0;
    }

    for (k = 0, nxk = 0; k < nx; nxk += k++)
    {
        yaflFloat a;
        yaflFloat fk;
        yaflFloat vk;

        fk = f[k];
        /*Correct v in place*/
        vk = ac * v[k];
        v[k] = vk;
        a = r + fk * vk;
        /*Correct d in place*/
        d[k] *= ac * r / a;
#define p fk /*No need for separate p variable*/
        p = - fk / r;
        for (j = 0; j < k; j++)
        {
            yaflFloat ujk;
            yaflFloat vj;

            ujk = u[j + nxk];
            vj  = v[j];

            u[j + nxk] = ujk +   p * vj;
            v[j]       = vj  + ujk * vk;
        }
#undef  p /*Don't need p any more...*/
        r = a;
    }
    /*
    Now we must do:
    self.x += K * nu

    Since:
    r == a

    then we have:
    K == v / a == v / r

    and so:
    K * nu == (v / r) * nu == v / r * nu == v * (nu / r)

    Finally we get:
    self.x += v * (nu / r)
    */
    yaflm_add_vxn(nx, self->x, v, nu / r);
#undef v /*Don't nee v any more*/
}

void yafl_ekf_adaptive_bierman_update(yaflEKFAdaptiveSt * self, yaflFloat * z)
{
    yafl_ekf_base_update((yaflEKFBaseSt *)self, z, _adaptive_bierman_scalar_update);
}

/*=============================================================================
                           Adaptive Joseph filter
=============================================================================*/
static void _adaptive_joseph_scalar_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt nx;
    yaflInt nx1;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat * v;
    yaflFloat * w;
    yaflFloat nu;
    yaflFloat r;
    yaflFloat c;
    yaflFloat s;
    yaflFloat ac;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->W);
    YAFL_ASSERT(self->D);

    nx1 = nx + 1;

    d = self->Dp;
    u = self->Up;

    h = self->H + nx * i;

    v = self->D;
    f = v + nx;

    w = self->W;

    nu = self->y[i];
    r  = self->Dr[i];

    /* f = h.dot(Up) */
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFLM_SET_DV(nx, v, d, f);

    /* s = r + f.dot(v)*/
    c = yaflm_vtv(nx, f, v);
    s = c + r;

    /* Divergence test */
    ac = (nu * (nu / (((yaflEKFAdaptiveSt *)self)->chi2))) - s;
    if (ac > 0.0)
    {
        /*Adaptive correction factor*/
        ac = ac / c + 1.0;

        /*Corrected s*/
        s  = ac * c + r;
    }
    else
    {
        ac = 1.0;
    }

    /* K = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
#define K h /*Don't need h any more, use it to store K*/
#define D v
    yaflm_set_vxn(nx, v, v, ac / s); /*May be used in place*/
    yaflm_set_uv(nx, K, u, v);

    /*Set W and D*/
    yaflm_bset_vvt(nx1, w, nx, K, f);
    yaflm_bsub_u(nx1, w, nx, u);

    /* Now w is (Kf - Up|***) */
    YAFLM_BSET_V(nx1, 0, nx, w, nx, K);
    /* Now w is (Kf - Up|K) */

    /* D = concatenate([ac * Dp, np.array([r])]) */
    yaflm_set_vxn(nx, D, d, ac);
    D[nx] = r;

    /* Up, Dp = MWGSU(W, D)*/
    yaflm_mwgsu(nx, nx1, u, d, w, D);

    /* self.x += K * nu */
    yaflm_add_vxn(nx, self->x, K, nu);
#undef D /*Don't nee D any more*/
#undef K /*Don't nee K any more*/
}

void yafl_ekf_adaptive_joseph_update(yaflEKFAdaptiveSt * self, yaflFloat * z)
{
    yafl_ekf_base_update((yaflEKFBaseSt *)self, z, _adaptive_joseph_scalar_update);
}

/*=============================================================================
                                 WARNING!!!

             DO NOT USE THIS variant of Adaptive Joseph filter !!!

    It was implemented to show some flaws of the corresponding algorithm!
=============================================================================*/
static void _do_not_use_this_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt nx;
    yaflInt nx1;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat * v;
    yaflFloat * w;
    yaflFloat nu;
    yaflFloat r;
    yaflFloat c;
    yaflFloat s;
    yaflFloat ac;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->W);
    YAFL_ASSERT(self->D);

    nx1 = nx + 1;

    d = self->Dp;
    u = self->Up;

    h = self->H + nx * i;

    v = self->D;
    f = v + nx;

    w = self->W;

    nu = self->y[i];
    r  = self->Dr[i];

    /* f = h.dot(Up) */
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFLM_SET_DV(nx, v, d, f);

    /* s = r + f.dot(v)*/
    c = yaflm_vtv(nx, f, v);
    s = c + r;

    /* Divergence test */
    ac = (nu * (nu / (((yaflEKFAdaptiveSt *)self)->chi2))) - s;
    if (ac > 0.0)
    {
        /*Adaptive correction with no limitations approach*/
        yaflm_set_uv(nx, f, u, v);
        yaflm_udu_up(nx, u, d, (ac / c) / c, f);

        /*Recompute f,v,s*/
        /* f = h.dot(Up) */
        yaflm_set_vtu(nx, f, h, u);

        /* v = f.dot(Dp).T = Dp.dot(f.T).T */
        YAFLM_SET_DV(nx, v, d, f);

        /* s = r + f.dot(v)*/
        s  = r + yaflm_vtv(nx, f, v);;
    }

    /* K = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
#define K h /*Don't need h any more, use it to store K*/
#define D v
    yaflm_set_uv(nx, K, u, v);
    yaflm_set_vrn(nx, K, K, s); /*May be used in place*/

    /*Set W and D*/
    yaflm_bset_vvt(nx1, w, nx, K, f);
    yaflm_bsub_u(nx1, w, nx, u);

    /* Now w is (Kf - Up|***) */
    YAFLM_BSET_V(nx1, 0, nx, w, nx, K);
    /* Now w is (Kf - Up|K) */

    /* D = concatenate([Dp, np.array([r])]) */
    memcpy((void *)D, (void *)d, nx * sizeof(yaflFloat));
    D[nx] = r;

    /* Up, Dp = MWGSU(W, D)*/
    yaflm_mwgsu(nx, nx1, u, d, w, D);

    /* self.x += K * nu */
    yaflm_add_vxn(nx, self->x, K, nu);
#undef D /*Don't nee D any more*/
#undef K /*Don't nee K any more*/
}

void yafl_ekf_do_not_use_this_update(yaflEKFAdaptiveSt * self, yaflFloat * z)
{
    yafl_ekf_base_update((yaflEKFBaseSt *)self, z, _do_not_use_this_update);
}

/*=============================================================================
                            Robust Bierman filter
=============================================================================*/
static void _robust_bierman_scalar_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt j;
    yaflInt k;
    yaflInt nx;
    yaflInt nxk;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat r05;
    yaflFloat gdot;
    yaflFloat y;
    yaflEKFRobFuncP g;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->D);

    r05 = self->Dr[i]; /* alpha = r**0.5 is stored in Dr*/
    y   = self->y[i];
    g   = ((yaflEKFRobustSt *)self)->g;
    if (g)
    {
        gdot = y / r05; /*Use gdot as temp variable*/
        y = r05 * g(self, gdot);

        g = ((yaflEKFRobustSt *)self)->gdot;
        YAFL_ASSERT(g);

        gdot = g(self, gdot);
    }
    else
    {
        gdot = 1.0;
    }

    r05 *= r05;
#define A2 r05 /*Now it is r = alpha**2 */

    u = self->Up;
    d = self->Dp;

    h = self->H + nx * i;

    /* f = h.dot(Up) */
    f = self->D;
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#define v h /*Don't need h any more, use it to store v*/
    YAFLM_SET_DV(nx, v, d, f);

    for (k = 0, nxk = 0; k < nx; nxk += k++)
    {
        yaflFloat a;
        yaflFloat fk;
        yaflFloat vk;

        fk = gdot * f[k];
        vk = v[k];
        a = A2 + fk * vk;
        d[k] *= A2 / a;
#define p fk /*No need for separate p variable*/
        p = - fk / A2;
        for (j = 0; j < k; j++)
        {
            yaflFloat ujk;
            yaflFloat vj;

            ujk = u[j + nxk];
            vj  = v[j];

            u[j + nxk] = ujk +   p * vj;
            v[j]       = vj  + ujk * vk;
        }
#undef  p /*Don't need p any more...*/
        A2 = a;
    }
    /*
    Now we must do:
    y = alpha * g(y[i] / alpha)
    self.x += K * y

    Since:
    A2 == a

    then we have:
    K == v / a == v / A2

    and so:
    K * nu == (v / A2) * y == v / A2 * y == v * (y / A2)

    Finally we get:
    self.x += v * (y / A2)
    */
    yaflm_add_vxn(nx, self->x, v, y / A2);
#undef v  /*Don't nee v any more*/
#undef A2 /*Don't nee A2 any more*/
}

void yafl_ekf_robust_bierman_update(yaflEKFRobustSt * self, yaflFloat * z)
{
    yafl_ekf_base_update((yaflEKFBaseSt *)self, z, _robust_bierman_scalar_update);
}

/*=============================================================================
                            Robust Joseph filter
=============================================================================*/
static void _robust_joseph_scalar_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt nx;
    yaflInt nx1;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat * v;
    yaflFloat * w;
    yaflFloat r05;
    yaflFloat gdot;
    yaflFloat s;
    yaflFloat y;
    yaflEKFRobFuncP g;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->W);
    YAFL_ASSERT(self->D);

    r05 = self->Dr[i]; /* alpha = r**0.5 is stored in Dr*/
    y   = self->y[i];
    g   = ((yaflEKFRobustSt *)self)->g;
    if (g)
    {
        s = y / r05;
        y = r05 * g(self, s);

        g = ((yaflEKFRobustSt *)self)->gdot;
        YAFL_ASSERT(g);

        gdot = g(self, s);
    }
    else
    {
        gdot = 1.0;
    }

    r05 *= r05;
#define A2 r05 /*Now it is r = alpha**2 */

    nx1 = nx + 1;

    d = self->Dp;
    u = self->Up;

    h = self->H + nx * i;

    v = self->D;
    f = v + nx;

    w = self->W;

    /* f = h.dot(Up) */
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFLM_SET_DV(nx, v, d, f);

    /* s = alpha**2 + gdot * f.dot(v)*/
    s = A2 + gdot * yaflm_vtv(nx, f, v);

    /*K = Up.dot(v/s) = Up.dot(v)/s*/
#define K h /*Don't need h any more, use it to store K*/
#define D v
    yaflm_set_uv(nx, K, u, v);
    yaflm_set_vrn(nx, K, K, s); /*May be used in place*/

    /*Set W and D*/
    yaflm_set_vxn(nx, f, f, gdot); /*May be used in place*/
    yaflm_bset_vvt(nx1, w, nx, K, f); /*How about yaflm_bset_vvtxn ?*/
    yaflm_bsub_u(nx1, w, nx, u);

    /* Now w is (gdot*Kf - Up|***) */
    YAFLM_BSET_V(nx1, 0, nx, w, nx, K);
    /* Now w is (gdot*Kf - Up|K) */

    /* D = concatenate([Dp, np.array([gdot * alpha**2])]) */
    memcpy((void *)D, (void *)d, nx * sizeof(yaflFloat));
    D[nx] = gdot * A2;

    /* Up, Dp = MWGSU(W, D)*/
    yaflm_mwgsu(nx, nx1, u, d, w, D);

    /* self.x += K * alpha * g(y[i] / alpha) */
    yaflm_add_vxn(nx, self->x, K, y);
#undef D  /*Don't nee D any more*/
#undef K  /*Don't nee K any more*/
#undef A2 /*Don't nee A2 any more*/
}

void yafl_ekf_robust_joseph_update(yaflEKFRobustSt * self, yaflFloat * z)
{
    yafl_ekf_base_update((yaflEKFBaseSt *)self, z, _robust_joseph_scalar_update);
}

/*=============================================================================
                        Adaptive robust Bierman filter
=============================================================================*/
static void _ada_rob_bierman_scalar_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt j;
    yaflInt k;
    yaflInt nx;
    yaflInt nxk;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat r05;
    yaflFloat gdot;
    yaflFloat nu;
    yaflFloat c;
    yaflFloat s;
    yaflFloat ac;
    yaflEKFRobFuncP g;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->D);

    r05 = self->Dr[i]; /* alpha = r**0.5 is stored in Dr*/
    nu   = self->y[i];
    g   = ((yaflEKFRobustSt *)self)->g;
    if (g)
    {
        gdot = nu / r05; /*Use gdot as temp variable*/
        nu = r05 * g(self, gdot);

        g = ((yaflEKFRobustSt *)self)->gdot;
        YAFL_ASSERT(g);

        gdot = g(self, gdot);
    }
    else
    {
        gdot = 1.0;
    }

    r05 *= r05;
#define A2 r05 /*Now it is r = alpha**2 */

    u = self->Up;
    d = self->Dp;

    h = self->H + nx * i;

    /* f = h.dot(Up) */
    f = self->D;
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#define v h /*Don't need h any more, use it to store v*/
    YAFLM_SET_DV(nx, v, d, f);

    /* s = alpha**2 + gdot * f.dot(v)*/
    c = gdot * yaflm_vtv(nx, f, v);
    s = A2 + c;

    /* Divergence test */
    ac = gdot * (nu * (nu / (((yaflEKFAdaptiveRobustSt *)self)->chi2))) - s;
    if (ac > 0.0)
    {
        /*Adaptive correction factor*/
        ac = ac / c + 1.0;

        /*Corrected s*/
        s  = A2 + ac * c;
    }
    else
    {
        ac = 1.0;
    }

    for (k = 0, nxk = 0; k < nx; nxk += k++)
    {
        yaflFloat a;
        yaflFloat fk;
        yaflFloat vk;

        fk = gdot * f[k];
        /*Correct v in place*/
        vk = ac * v[k];
        v[k] = vk;
        a = A2 + fk * vk;
        /*Correct d in place*/
        d[k] *= ac * A2 / a;
#define p fk /*No need for separate p variable*/
        p = - fk / A2;
        for (j = 0; j < k; j++)
        {
            yaflFloat ujk;
            yaflFloat vj;

            ujk = u[j + nxk];
            vj  = v[j];

            u[j + nxk] = ujk +   p * vj;
            v[j]       = vj  + ujk * vk;
        }
#undef  p /*Don't need p any more...*/
        A2 = a;
    }
    /*
    Now we must do:
    nu = alpha * g(y[i] / alpha)
    self.x += K * nu

    Since:
    A2 == a

    then we have:
    K == v / a == v / A2

    and so:
    K * nu == (v / A2) * nu == v / A2 * nu == v * (nu / A2)

    Finally we get:
    self.x += v * (nu / A2)
    */
    yaflm_add_vxn(nx, self->x, v, nu / A2);
#undef v  /*Don't nee v any more*/
#undef A2 /*Don't nee A2 any more*/
}

void yafl_ekf_adaptive_robust_bierman_update(yaflEKFAdaptiveRobustSt * self, yaflFloat * z)
{
    yafl_ekf_base_update((yaflEKFBaseSt *)self, z, _ada_rob_bierman_scalar_update);
}

/*=============================================================================
                        Adaptive robust Joseph filter
=============================================================================*/
static void _ada_rob_joseph_scalar_update(yaflEKFBaseSt * self, yaflInt i)
{
    yaflInt nx;
    yaflInt nx1;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat * v;
    yaflFloat * w;
    yaflFloat r05;
    yaflFloat gdot;
    yaflFloat ac;
    yaflFloat c;
    yaflFloat s;
    yaflFloat nu;
    yaflEKFRobFuncP g;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->H);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);
    YAFL_ASSERT(self->W);
    YAFL_ASSERT(self->D);

    r05 = self->Dr[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = self->y[i];
    g   = ((yaflEKFRobustSt *)self)->g;
    if (g)
    {
        s  = nu / r05;
        nu = r05 * g(self, s); /*nu = alpha * g(y[i] / alpha)*/

        g = ((yaflEKFRobustSt *)self)->gdot;
        YAFL_ASSERT(g);

        gdot = g(self, s);
    }
    else
    {
        gdot = 1.0;
    }

    r05 *= r05;
#define A2 r05 /*Now it is r = alpha**2 */

    nx1 = nx + 1;

    d = self->Dp;
    u = self->Up;

    h = self->H + nx * i;

    v = self->D;
    f = v + nx;

    w = self->W;

    /* f = h.dot(Up) */
    yaflm_set_vtu(nx, f, h, u);

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFLM_SET_DV(nx, v, d, f);

    /* s = alpha**2 + gdot * f.dot(v)*/
    c = gdot * yaflm_vtv(nx, f, v);
    s = A2 + c;

    /* Divergence test */
    ac = gdot * (nu * (nu / (((yaflEKFAdaptiveRobustSt *)self)->chi2))) - s;
    if (ac > 0.0)
    {
        /*Adaptive correction factor*/
        ac = ac / c + 1.0;

        /*Corrected s*/
        s  = A2 + ac * c;
    }
    else
    {
        ac = 1.0;
    }

    /* K = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
#define K h /*Don't need h any more, use it to store K*/
#define D v
    yaflm_set_vxn(nx, v, v, ac / s); /*May be used in place*/
    yaflm_set_uv(nx, K, u, v);

    /*Set W and D*/
    yaflm_set_vxn(nx, f, f, gdot); /*May be used in place*/
    yaflm_bset_vvt(nx1, w, nx, K, f); /*How about yaflm_bset_vvtxn ?*/
    yaflm_bsub_u(nx1, w, nx, u);

    /* Now w is (gdot*Kf - Up|***) */
    YAFLM_BSET_V(nx1, 0, nx, w, nx, K);
    /* Now w is (gdot*Kf - Up|K) */

    /* D = concatenate([ac * Dp, np.array([gdot * alpha**2])]) */
    yaflm_set_vxn(nx, D, d, ac);
    D[nx] = gdot * A2;

    /* Up, Dp = MWGSU(W, D)*/
    yaflm_mwgsu(nx, nx1, u, d, w, D);

    /* self.x += K * nu */
    yaflm_add_vxn(nx, self->x, K, nu);
#undef D  /*Don't nee D any more*/
#undef K  /*Don't nee K any more*/
#undef A2 /*Don't nee A2 any more*/
}

void yafl_ekf_adaptive_robust_joseph_update(yaflEKFAdaptiveRobustSt * self, yaflFloat * z)
{
    yafl_ekf_base_update((yaflEKFBaseSt *)self, z, _ada_rob_joseph_scalar_update);
}

/*=============================================================================
                    Basic UD-factorized UKF functions
=============================================================================*/
static inline void _compute_res(yaflUKFBaseSt * self, yaflInt sz,          \
                                yaflUKFResFuncP rf, yaflFloat * sigma, \
                                yaflFloat * pivot, yaflFloat * res)
{
    if (rf)
    {
        /*rf must be aware of sp and the current transform*/
        rf(self, res, sigma, pivot); /* res = self.rf(sigma, pivot) */
    }
    else
    {
        yaflInt j;
        for (j = 0; j < sz; j++)
        {
            res[j] = sigma[j] - pivot[j];
        }
    }
}

static void _unscented_transform(yaflUKFBaseSt * self, \
                                 yaflInt    res_sz,      \
                                 yaflFloat * res_v,      \
                                 yaflFloat * res_u,      \
                                 yaflFloat * res_d,      \
                                 yaflFloat * sp,         \
                                 yaflFloat * sigmas,     \
                                 yaflFloat * noise_u,    \
                                 yaflFloat * noise_d,    \
                                 yaflUKFFuncP mf,  \
                                 yaflUKFResFuncP rf)
{
    yaflInt np;
    yaflInt i;
    yaflUKFSigmaSt * sp_info;
    yaflFloat * wc;

    YAFL_ASSERT(self);

    YAFL_ASSERT(res_sz > 0);
    YAFL_ASSERT(res_v);
    YAFL_ASSERT(res_u);
    YAFL_ASSERT(res_d);
    YAFL_ASSERT(sp);

    if (noise_u)
    {
        YAFL_ASSERT(noise_d);
    }

    YAFL_ASSERT(self->wm);
    YAFL_ASSERT(self->wc);
    wc = self->wc;

    YAFL_ASSERT(self->sp_info);
    sp_info = self->sp_info;

    YAFL_ASSERT(sp_info->np > 1);
    np = sp_info->np;

    if (mf)
    {
        mf(self, res_v, sigmas); /*mf must be aware of the current transform details...*/
    }
    else
    {
        yaflm_set_vtm(np, res_sz, res_v, self->wm, sigmas);
    }

    if (noise_u)
    {
        /*res_u, res_d = noise_u.copy(), noise_d.copy()*/
        memcpy((void *)res_u, (void *)noise_u, (res_sz * (res_sz - 1)) / 2 * sizeof(yaflFloat));
        memcpy((void *)res_d, (void *)noise_d, res_sz * sizeof(yaflFloat));

    }
    else
    {
        /* Zero res_u and res_d */
        for (i = res_sz - 1; i >= 0; i--)
        {
            res_d[i] = 0.0;
        }
        for (i = ((res_sz * (res_sz - 1)) / 2) - 1; i >= 0; i--)
        {
            res_u[i] = 0.0;
        }
    }

    for (i = 0; i < np; i++)
    {
        _compute_res(self, res_sz, rf, sigmas + res_sz * i, res_v, sp);
        /*Update res_u and res_d*/
        /*wc should be sorted in descending order*/
        if (wc[i] >= 0.0)
        {
            yaflm_udu_up(res_sz, res_u, res_d, wc[i], sp);
        }
        else
        {
            yaflm_udu_down(res_sz, res_u, res_d, -wc[i], sp);
        }
    }
}

void yafl_ukf_predict(yaflUKFBaseSt * self)
{
    yaflInt np;
    yaflInt nx;
    yaflInt i;
    yaflUKFSigmaSt * sp_info;
    yaflUKFFuncP fx; /*State transition function*/
    yaflFloat * sigmas_x;

    /*Check some params and generate sigma points*/
    yafl_ukf_gen_sigmas(self); /*Self is checked here*/

    YAFL_ASSERT(self->Nx);
    nx = self->Nx;

    YAFL_ASSERT(self->sigmas_x);
    sigmas_x = self->sigmas_x;

    YAFL_ASSERT(self->sp_info);
    sp_info = self->sp_info;

    YAFL_ASSERT(sp_info->np > 1);
    np = sp_info->np;

    /*Compute process sigmas*/
    YAFL_ASSERT(self->f);
    fx = self->f;
    if (fx)
    {
        for (i = 0; i < np; i++)
        {
            yaflFloat * sigmai;
            sigmai = sigmas_x + nx * i;
            fx(self, sigmai, sigmai);
        }
    }

    /*Predict x, Up, Dp*/
    _unscented_transform(self, nx, self->x, self->Up, self->Dp, self->Sx, \
                         sigmas_x, self->Uq, self->Dq, self->xmf, self->xrf);
}

void yafl_ukf_update(yaflUKFBaseSt * self, yaflFloat * z)
{
    yaflInt np;
    yaflInt nx;
    yaflInt nz;
    yaflInt i;
    yaflUKFSigmaSt * sp_info;     /*Sigma point generator info*/
    yaflUKFFuncP hx;     /*State transition function*/
    yaflUKFResFuncP xrf; /*State residual function*/
    yaflUKFResFuncP zrf; /*Measurement residual function*/
    yaflFloat * sigmas_x;      /*State sigma points*/
    yaflFloat * sigmas_z;      /*Measurement sigma points*/
    /*Scratchpad memory*/
    yaflFloat * spx;           /*For state residuals*/
    yaflFloat * y;           /*For measurement residuals*/

    yaflFloat * x;             /*State*/
    yaflFloat * zp;            /*Predicted measurement*/
    yaflFloat * wc;            /*Covariance computation weights*/
    yaflFloat * pzx;           /*Pzx cross covariance matrix*/

    /*Pzz covariance*/
    yaflFloat * us;            /*Unit upper triangular factor*/
    yaflFloat * ds;            /*Diagonal factor*/

    /*P covariance*/
    yaflFloat * up;            /*Unit upper triangular factor*/
    yaflFloat * dp;            /*Diagonal factor*/

    YAFL_ASSERT(self);
    YAFL_ASSERT(self->Nx);
    nx = self->Nx;

    YAFL_ASSERT(self->sigmas_x);
    sigmas_x = self->sigmas_x;

    YAFL_ASSERT(self->Nz);
    nz = self->Nz;

    YAFL_ASSERT(self->sigmas_z);
    sigmas_z = self->sigmas_z;

    YAFL_ASSERT(self->wc);
    wc = self->wc;

    YAFL_ASSERT(self->sp_info);
    sp_info = self->sp_info;

    YAFL_ASSERT(sp_info->np > 1);
    np = sp_info->np;

    YAFL_ASSERT(self->h);
    hx = self->h;

    YAFL_ASSERT(self->Pzx);
    pzx = self->Pzx;

    YAFL_ASSERT(self->Sx);
    spx = self->Sx;

    YAFL_ASSERT(self->y);
    y = self->y;

    YAFL_ASSERT(self->zp);
    zp = self->zp;

    YAFL_ASSERT(self->x);
    x = self->x;

    YAFL_ASSERT(self->Us);
    us = self->Us;

    YAFL_ASSERT(self->Ds);
    ds = self->Ds;

    YAFL_ASSERT(self->Up);
    up = self->Up;

    YAFL_ASSERT(self->Dp);
    dp = self->Dp;

    /* Compute measurement sigmas */
    for (i = 0; i < np; i++)
    {
        hx(self, sigmas_z + nz * i, sigmas_x + nx * i);
    }

    /* Compute zp, Us, Ds */
    zrf = self->zrf;
    _unscented_transform(self, nz, zp, us, ds, y, \
                         sigmas_z, self->Ur, self->Dr, self->zmf, zrf);

    /* Compute Pzx */
    xrf = self->xrf;
    _compute_res(self, nz, zrf, sigmas_z, zp, y);
    _compute_res(self, nx, xrf, sigmas_x,  x, spx);
    yaflm_set_vvtxn(nz, nx, pzx, y, spx, wc[0]);

    for (i = 1; i < np; i++)
    {
        _compute_res(self, nz, zrf, sigmas_z + nz * i, zp, y);
        _compute_res(self, nx, xrf, sigmas_x + nx * i,  x, spx);
        yaflm_add_vvtxn(nz, nx, pzx, y, spx, wc[i]);
    }

    /*Compute innovation*/
    _compute_res(self, nz, zrf, z, zp, y);

    /* Decorrelate measurements*/
    yaflm_ruv(nz,       y, us);
    yaflm_rum(nz, nx, pzx, us);

    /*Now we can do scalar updates*/
    for (i = 0; i < nz; i++)
    {
        yaflFloat * pzxi;
        pzxi = pzx + nx * i;
        /*
        self.x += K * y[i]

        K * y[i] = Pzx[i].T / ds[i] * y[i] = Pzx[i].T * (y[i] / ds[i])

        self.x += Pzx[i].T * (y[i] / ds[i])
        */
        yaflm_add_vxn(nx, x, pzxi, y[i] / ds[i]);

        /*
        P -= K.dot(S.dot(K.T))
        K.dot(S.dot(K.T)) = (Pzx[i].T / ds[i] * ds[i]).outer(Pzx[i] / ds[i]))
        K.dot(S.dot(K.T)) = (Pzx[i].T).outer(Pzx[i]) / ds[i]
        P -= (Pzx[i].T).outer(Pzx[i]) / ds[i]
        Up, Dp = udu(P)
        */
        yaflm_udu_down(nx, up, dp, 1.0 / ds[i], pzxi);
    }
}

/*---------------------------------------------------------------------------*/
void yafl_ukf_base_update(yaflUKFBaseSt * self, yaflFloat * z, \
                               yaflUKFScalarUpdateP scalar_update)
{
    yaflInt np;
    yaflInt nx;
    yaflInt nz;
    yaflInt i;
    yaflUKFSigmaSt * sp_info;     /*Sigma point generator info*/
    yaflUKFFuncP hx;     /*State transition function*/
    yaflUKFResFuncP xrf; /*State residual function*/
    yaflUKFResFuncP zrf; /*Measurement residual function*/
    yaflFloat * sigmas_x;      /*State sigma points*/
    yaflFloat * sigmas_z;      /*Measurement sigma points*/
    /*Scratchpad memory*/
    yaflFloat * spx;           /*For state residuals*/
    yaflFloat * y;           /*For measurement residuals*/

    yaflFloat * x;             /*State*/
    yaflFloat * zp;            /*Predicted measurement*/
    yaflFloat * wc;            /*Covariance computation weights*/
    yaflFloat * pzx;           /*Pzx cross covariance matrix*/

    /*Output noise covariance*/
    yaflFloat * ur;            /*Unit upper triangular factor*/

    /*P covariance*/
    yaflFloat * up;            /*Unit upper triangular factor*/

    YAFL_ASSERT(scalar_update);

    YAFL_ASSERT(self);
    YAFL_ASSERT(self->Nx);
    nx = self->Nx;

    YAFL_ASSERT(self->sigmas_x);
    sigmas_x = self->sigmas_x;

    YAFL_ASSERT(self->Nz);
    nz = self->Nz;

    YAFL_ASSERT(self->sigmas_z);
    sigmas_z = self->sigmas_z;

    YAFL_ASSERT(self->wc);
    wc = self->wc;

    YAFL_ASSERT(self->sp_info);
    sp_info = self->sp_info;

    YAFL_ASSERT(sp_info->np > 1);
    np = sp_info->np;

    YAFL_ASSERT(self->h);
    hx = self->h;

    YAFL_ASSERT(self->Pzx);
    pzx = self->Pzx;

    YAFL_ASSERT(self->Sx);
    spx = self->Sx;

    YAFL_ASSERT(self->y);
    y = self->y;

    YAFL_ASSERT(self->zp);
    zp = self->zp;

    YAFL_ASSERT(self->x);
    x = self->x;

    YAFL_ASSERT(self->Ur);
    ur = self->Ur;

    YAFL_ASSERT(self->Up);
    up = self->Up;

    /* Compute measurement sigmas */
    for (i = 0; i < np; i++)
    {
        hx(self, sigmas_z + nz * i, sigmas_x + nx * i);
    }

    /* Compute zp*/
    if (self->zmf)
    {
        self->zmf(self, zp, sigmas_z); /*mf must be aware of the current transform details...*/
    }
    else
    {
        yaflm_set_vtm(np, nz, zp, self->wm, sigmas_z);
    }

    /* Compute Pzx */
    zrf = self->zrf;
    xrf = self->xrf;
    _compute_res(self, nz, zrf, sigmas_z, zp, y);
    _compute_res(self, nx, xrf, sigmas_x,  x, spx);
    yaflm_set_vvtxn(nz, nx, pzx, y, spx, wc[0]);

    for (i = 1; i < np; i++)
    {
        _compute_res(self, nz, zrf, sigmas_z + nz * i, zp, y);
        _compute_res(self, nx, xrf, sigmas_x + nx * i,  x, spx);
        yaflm_add_vvtxn(nz, nx, pzx, y, spx, wc[i]);
    }

    /*Compute innovation*/
    _compute_res(self, nz, zrf, z, zp, y);

    /* Decorrelate measurements*/
    yaflm_ruv(nz,       y, ur);
    yaflm_rum(nz, nx, pzx, ur);

    /*Now we can do scalar updates*/
    for (i = 0; i < nz; i++)
    {
        /*
        Compute v[i] for Bierman/Joseph style scalar updates:
        v[i] = linalg.inv(Up).dot(Pzx[i].T)

        It's cheaper than computing _unscented_transform.
        */
        yaflm_ruv(nx, pzx + nx * i, up);
        scalar_update(self, i);
    }
}

/*===========================================================================*/
static void _bierman_like_scalar_update(yaflUKFBaseSt * self, yaflInt i)
{
    yaflInt j;
    yaflInt k;
    yaflInt nx;
    yaflInt nxk;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * v;
    yaflFloat * f;
    yaflFloat r;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->Pzx);
    YAFL_ASSERT(self->Sx);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);

    u = self->Up;
    d = self->Dp;

    v = self->Pzx + nx * i;

    /* f = linalg.inv(Dp).dot(v)*/
    f = self->Sx;
    YAFLM_SET_RDV(nx, f, d, v);

    r = self->Dr[i];
    for (k = 0, nxk = 0; k < nx; nxk += k++)
    {
        yaflFloat a;
        yaflFloat fk;
        yaflFloat vk;

        fk = f[k];
        vk = v[k];
        a = r + fk * vk;
        d[k] *= r / a;
#define p fk /*No need for separate p variable*/
        p = - fk / r;
        for (j = 0; j < k; j++)
        {
            yaflFloat ujk;
            yaflFloat vj;

            ujk = u[j + nxk];
            vj  = v[j];

            u[j + nxk] = ujk +   p * vj;
            v[j]       = vj  + ujk * vk;
        }
#undef  p /*Don't need p any more...*/
        r = a;
    }
    /*
    Now we must do:
    self.x += K * y[i]

    Since:
    r == a

    then we have:
    K == v / a == v / r

    and so:
    K * y[i] == (v / r) * y[i] == v / r * y[i] == v * (y[i] / r)

    Finally we get:
    self.x += v * (y[i] / r)
    */
    yaflm_add_vxn(nx, self->x, v, self->y[i] / r);
}

void yafl_ukf_bierman_update(yaflUKFBaseSt * self, yaflFloat * z)
{
    yafl_ukf_base_update(self, z, _bierman_like_scalar_update);
}

/*=============================================================================
                        Adaptive Bierman-like filter
=============================================================================*/
static void _adaptive_bierman_like_scalar_update(yaflUKFBaseSt * self, yaflInt i)
{
    yaflInt j;
    yaflInt k;
    yaflInt nx;
    yaflInt nxk;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * v;
    yaflFloat * f;
    yaflFloat nu;
    yaflFloat r;
    yaflFloat c;
    yaflFloat s;
    yaflFloat ac;

    YAFL_ASSERT(self);

    nx = self->Nx;
    YAFL_ASSERT(nx > 1);
    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    YAFL_ASSERT(self->Pzx);
    YAFL_ASSERT(self->Sx);
    YAFL_ASSERT(self->y);
    YAFL_ASSERT(self->Dr);

    u = self->Up;
    d = self->Dp;

    v = self->Pzx + nx * i;

    /* f = linalg.inv(Dp).dot(v)*/
    f = self->Sx;
    YAFLM_SET_RDV(nx, f, d, v);

    nu = self->y[i];
    r  = self->Dr[i];

    /* s = r + f.dot(v)*/
    c = yaflm_vtv(nx, f, v);
    s = c + r;

    /* Divergence test */
    ac = (nu * (nu / (((yaflUKFAdaptivedSt *)self)->chi2))) - s;
    if (ac > 0.0)
    {
        /*Adaptive correction factor*/
        ac = ac / c + 1.0;
    }
    else
    {
        ac = 1.0;
    }

    for (k = 0, nxk = 0; k < nx; nxk += k++)
    {
        yaflFloat a;
        yaflFloat fk;
        yaflFloat vk;

        fk = f[k];
        /*Correct v in place*/
        vk = ac * v[k];
        v[k] = vk;
        a = r + fk * vk;
        /*Correct d in place*/
        d[k] *= ac * r / a;
#define p fk /*No need for separate p variable*/
        p = - fk / r;
        for (j = 0; j < k; j++)
        {
            yaflFloat ujk;
            yaflFloat vj;

            ujk = u[j + nxk];
            vj  = v[j];

            u[j + nxk] = ujk +   p * vj;
            v[j]       = vj  + ujk * vk;
        }
#undef  p /*Don't need p any more...*/
        r = a;
    }
    /*
    Now we must do:
    self.x += K * nu

    Since:
    r == a

    then we have:
    K == v / a == v / r

    and so:
    K * nu == (v / r) * nu == v / r * nu == v * (nu / r)

    Finally we get:
    self.x += v * (nu / r)
    */
    yaflm_add_vxn(nx, self->x, v, nu / r);
}

void yafl_ukf_adaptive_bierman_update(yaflUKFAdaptivedSt * self, \
                                            yaflFloat * z)
{
    yafl_ukf_base_update((yaflUKFBaseSt *)self, z, \
                               _adaptive_bierman_like_scalar_update);
}

/*=============================================================================
                    Van der Merwe sigma points generator
=============================================================================*/
static void _merwe_compute_weights(yaflUKFBaseSt * self)
{
    yaflInt np;
    yaflInt nx;
    yaflInt i;
    yaflFloat * wc;
    yaflFloat * wm;
    yaflUKFSigmaSt * sp_info;
    yaflFloat lambda;
    yaflFloat alpha;
    yaflFloat c;
    yaflFloat d;

    YAFL_ASSERT(self);
    YAFL_ASSERT(self->Nx);
    nx = self->Nx;

    YAFL_ASSERT(self->wm);
    wm = self->wm;

    YAFL_ASSERT(self->wc);
    wc = self->wc;

    YAFL_ASSERT(self->sp_info);
    sp_info = self->sp_info;

    YAFL_ASSERT(sp_info->np);
    np = sp_info->np - 1;    /*Achtung!!!*/



    alpha = ((yaflUKFMerweSt *)sp_info)->alpha;
    alpha *= alpha;
#define ALPHA2 alpha

    lambda = ALPHA2 * (nx + ((yaflUKFMerweSt *)sp_info)->kappa) - nx;

    d = lambda / (nx + lambda);
    wm[np] = d;
    wc[np] = d + (1.0 - ALPHA2 + ((yaflUKFMerweSt *)sp_info)->beta);

    c = 0.5 / (nx + lambda);
    for (i = np - 1; i >= 0; i--)
    {
        wm[i] = c;
        wc[i] = c;
    }
#undef ALPHA2
}

static inline void _add_delta(yaflUKFBaseSt * self, yaflUKFSigmaAddP addf, yaflInt sz, \
                              yaflFloat * delta, yaflFloat * pivot,  yaflFloat mult)
{
    if (addf)
    {
        /* addf must be aware of self internal structure*/
        addf(self, delta, pivot, mult); /* delta = self.addf(delta, pivot, mult) */
    }
    else
    {
        yaflInt j;
        for (j = 0; j < sz; j++)
        {
            delta[j] = pivot[j] + mult * delta[j];
        }
    }
}

static void _merwe_generate_points(yaflUKFBaseSt * self)
{
    yaflInt nx;
    yaflInt i;
    yaflFloat * x;
    yaflFloat * sigmas_x;
    yaflFloat * dp;
    yaflUKFSigmaSt * sp_info;
    yaflUKFSigmaAddP addf;
    yaflFloat lambda_p_n;
    yaflFloat alpha;

    YAFL_ASSERT(self);
    YAFL_ASSERT(self->Nx);
    nx = self->Nx;

    YAFL_ASSERT(self->x);
    x = self->x;

    YAFL_ASSERT(self->Up);
    YAFL_ASSERT(self->Dp);
    dp = self->Dp;

    YAFL_ASSERT(self->sigmas_x);
    sigmas_x = self->sigmas_x;

    YAFL_ASSERT(self->sp_info);
    sp_info = self->sp_info;

    alpha = ((yaflUKFMerweSt *)sp_info)->alpha;
    lambda_p_n = alpha * alpha * (nx + ((yaflUKFMerweSt *)sp_info)->kappa);

    yaflm_bset_ut(nx, sigmas_x, nx, self->Up);
    memcpy((void *)(sigmas_x + nx * nx), (void *)sigmas_x, \
           nx * nx * sizeof(yaflFloat));

    addf = sp_info->addf;
    for (i = 0; i < nx; i++)
    {
        yaflFloat mult;
        mult = YAFL_SQRT(dp[i] * lambda_p_n);
        _add_delta(self, addf, nx, sigmas_x + nx * i       , x,   mult);
        _add_delta(self, addf, nx, sigmas_x + nx * (nx + i), x, - mult);
    }
    memcpy((void *)(sigmas_x + 2 * nx * nx), (void *)x, nx * sizeof(yaflFloat));
}

const yaflUKFSigmaMethodsSt yafl_ukf_merwe_spm = {
    .wf   = _merwe_compute_weights,
    .spgf = _merwe_generate_points
};
