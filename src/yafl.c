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
#include <string.h>

#include "yafl.h"

/*=============================================================================
                                  Base UDEKF
=============================================================================*/
yaflStatusEn yafl_ekf_base_predict(yaflKalmanBaseSt * self)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt i;
    yaflInt nx2;

    YAFL_CHECK(self, YAFL_ST_INV_ARG_1);

#   define nx  (self->Nx)
#   define x   (self->x)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define uq  (self->Uq)
#   define dq  (self->Dq)
#   define fx  (self->f)
#   define jfx (((yaflEKFBaseSt *)self)->jf)
#   define w   (((yaflEKFBaseSt *)self)->W)
#   define d   (((yaflEKFBaseSt *)self)->D)

    YAFL_CHECK(up,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(dp,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(uq,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(dq,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(nx > 1, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(w,      YAFL_ST_INV_ARG_1);
    YAFL_CHECK(d,      YAFL_ST_INV_ARG_1);

    nx2 = nx * 2;

    /*Default f(x) = x*/
    if (0 == fx)
    {
        YAFL_CHECK(0 == jfx, YAFL_ST_INV_ARG_1);

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
        //yaflFloat * x;

        /*Must have some Jacobian function*/
        YAFL_CHECK(jfx, YAFL_ST_INV_ARG_1);

        //x = self->x;
        YAFL_TRY(status,  fx(self, x, x));  /* x = f(x_old, ...) */
        YAFL_TRY(status, jfx(self, w, x));  /* Place F(x, ...)=df/dx to W  */
    }
    /* Now W = (F|***) */
    YAFL_TRY(status, \
             YAFL_MATH_BSET_BU(nx2, 0, nx, w, nx, nx, nx2, 0, 0, w, up));
    /* Now W = (F|FUp) */
    YAFL_TRY(status, yafl_math_bset_u(nx2, w, nx, uq));
    /* Now W = (Uq|FUp) */

    /* D = concatenate([Dq, Dp]) */
    i = nx*sizeof(yaflFloat);
    memcpy((void *)       d, (void *)dq, i);
    memcpy((void *)(d + nx), (void *)dp, i);

    /* Up, Dp = MWGSU(w, d)*/
    YAFL_TRY(status, yafl_math_mwgsu(nx, nx2, up, dp, w, d));

#   undef nx
#   undef x
#   undef up
#   undef dp
#   undef uq
#   undef dq
#   undef fx
#   undef jfx
#   undef w
#   undef d
    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ekf_base_update(yaflKalmanBaseSt * self, yaflFloat * z, yaflKalmanScalarUpdateP scalar_update)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt j;

#   define nx  (self->Nx)
#   define nz  (self->Nz)
#   define x   (self->x)
#   define y   (self->y)
#   define ur  (self->Ur)
#   define zrf (self->zrf)
#   define hx  (self->h)
#   define jhx (((yaflEKFBaseSt *)self)->jh)
#   define hy  (((yaflEKFBaseSt *)self)->H)

    YAFL_CHECK(self,   YAFL_ST_INV_ARG_1);
    YAFL_CHECK(hx,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(x,      YAFL_ST_INV_ARG_1);
    YAFL_CHECK(y,      YAFL_ST_INV_ARG_1);
    YAFL_CHECK(ur,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(nx > 1, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(nz > 0, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(jhx,    YAFL_ST_INV_ARG_1);
    YAFL_CHECK(hy,     YAFL_ST_INV_ARG_1);

    YAFL_CHECK(z,      YAFL_ST_INV_ARG_2);
    YAFL_CHECK(scalar_update, YAFL_ST_INV_ARG_3);


    YAFL_TRY(status,  hx(self, y,  x)); /* self.y =  h(x,...) */
    YAFL_TRY(status, jhx(self, hy, x)); /* self.H = jh(x,...) */

    if (0 == zrf)
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
        YAFL_TRY(status, zrf(self, y, z, y)); /* self.y = zrf(z, h(x,...)) */
    }

    /* Decorrelate measurement noise */

    YAFL_TRY(status, yafl_math_ruv(nz,     y,  ur));
    YAFL_TRY(status, yafl_math_rum(nz, nx, hy, ur));

    /* Do scalar updates */
    for (j = 0; j < nz; j++)
    {
        YAFL_TRY(status, scalar_update(self, j));
    }

#   undef nx
#   undef nz
#   undef x
#   undef y
#   undef ur
#   undef zrf
#   undef hy
#   undef hx
#   undef jhx
    return status;
}
/*=============================================================================
                                Bierman filter
=============================================================================*/
static inline yaflStatusEn \
    _bierman_update_body(yaflInt    nx, yaflFloat * x, yaflFloat * u, \
                        yaflFloat * d, yaflFloat * f, yaflFloat * v, \
                        yaflFloat   r, yaflFloat  nu, yaflFloat  ac, \
                        yaflFloat   gdot)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt j;
    yaflInt k;
    yaflInt nxk;

    YAFL_CHECK(x, YAFL_ST_INV_ARG_2);
    YAFL_CHECK(u, YAFL_ST_INV_ARG_3);
    YAFL_CHECK(d, YAFL_ST_INV_ARG_4);
    YAFL_CHECK(f, YAFL_ST_INV_ARG_5);
    YAFL_CHECK(v, YAFL_ST_INV_ARG_6);

    for (k = 0, nxk = 0; k < nx; nxk += k++)
    {
        yaflFloat a;
        yaflFloat fk;
        yaflFloat vk;

        fk = gdot * f[k];
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
    x += K * nu

    Since:
    r == a

    then we have:
    K == v / a == v / r

    and so:
    K * nu == (v / r) * nu == v / r * nu == v * (nu / r)

    Finally we get:
    x += v * (nu / r)
    */
    YAFL_TRY(status, yafl_math_add_vxn(nx, x, v, nu / r));
    return status;
}

/*---------------------------------------------------------------------------*/
#define _SCALAR_UPDATE_ARGS_CHECKS()              \
do {                                              \
    YAFL_CHECK(self->Nz > i, YAFL_ST_INV_ARG_2);  \
    YAFL_CHECK(self,          YAFL_ST_INV_ARG_1); \
} while (0)
/*---------------------------------------------------------------------------*/
#define _EKF_BIERMAN_SELF_INTERNALS_CHECKS()     \
do {                                         \
    YAFL_CHECK(nx > 1,   YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(up, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(dp, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(hy, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(y,  YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(dr, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(d,  YAFL_ST_INV_ARG_1); \
} while (0)

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ekf_bierman_scalar_update(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat * h;
#   define nx  (self->Nx)
#   define x   (self->x)
#   define y   (self->y)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define dr  (self->Dr)
#   define hy  (((yaflEKFBaseSt *)self)->H)
#   define d   (((yaflEKFBaseSt *)self)->D)

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_BIERMAN_SELF_INTERNALS_CHECKS();

    h = hy + nx * i;
    /* f = h.dot(Up) */
#   define f d
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, up));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#define v h /*Don't need h any more, use it to store v*/
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, dp, f));
    YAFL_TRY(status, \
             _bierman_update_body(nx, x, up, dp, f, v, dr[i], y[i], 1.0, 1.0));

#   undef v /*Don't nee v any more*/
#   undef f

#   undef nx
#   undef x
#   undef y
#   undef up
#   undef dp
#   undef dr
#   undef hy
#   undef d

    return status;
}

/*=============================================================================
                                Joseph filter
=============================================================================*/
static inline yaflStatusEn \
    _joseph_update_body(yaflInt nx,    yaflFloat * x, yaflFloat * u, \
                        yaflFloat * d, yaflFloat * f, yaflFloat * v, \
                        yaflFloat * k, yaflFloat * w, yaflFloat  nu, \
                        yaflFloat  a2, yaflFloat   s, yaflFloat  ac, \
                        yaflFloat  gdot)
{
    yaflStatusEn status = YAFL_ST_OK;

    yaflInt nx1;

    YAFL_CHECK(x, YAFL_ST_INV_ARG_2);
    YAFL_CHECK(u, YAFL_ST_INV_ARG_3);
    YAFL_CHECK(d, YAFL_ST_INV_ARG_4);
    YAFL_CHECK(f, YAFL_ST_INV_ARG_5);
    YAFL_CHECK(v, YAFL_ST_INV_ARG_6);
    YAFL_CHECK(k, YAFL_ST_INV_ARG_7);
    YAFL_CHECK(w, YAFL_ST_INV_ARG_8);
    YAFL_CHECK(s > 0, YAFL_ST_INV_ARG_11);

    nx1 = nx + 1;

    /* k = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
    /*May be used in place*/
    YAFL_TRY(status, yafl_math_set_vxn(nx, v, v, ac / s));
    YAFL_TRY(status, yafl_math_set_uv(nx, k, u, v));

#   define D v
    /*Set W and D*/
    /*May be used in place*/
    YAFL_TRY(status, yafl_math_set_vxn(nx, f, f, gdot));
    /*How about yafl_math_bset_vvtxn ?*/
    YAFL_TRY(status, yafl_math_bset_vvt(nx1, w, nx, k, f));
    YAFL_TRY(status, yafl_math_bsub_u(nx1, w, nx, u));

    /* Now w is (gdot*kf - Up|***) */
    YAFL_TRY(status, YAFL_MATH_BSET_V(nx1, 0, nx, w, nx, k));
    /* Now w is (gdot*kf - Up|k) */

    /* D = concatenate([ac * Dp, np.array([gdot * alpha**2])]) */
    YAFL_TRY(status, yafl_math_set_vxn(nx, D, d, ac));
    D[nx] = gdot * a2;

    /* Up, Dp = MWGSU(W, D)*/
    YAFL_TRY(status, yafl_math_mwgsu(nx, nx1, u, d, w, D));

    /* x += k * nu */
    YAFL_TRY(status, yafl_math_add_vxn(nx, x, k, nu));
#   undef D  /*Don't nee D any more*/
    return status;
}

/*---------------------------------------------------------------------------*/
#define _EKF_JOSEPH_SELF_INTERNALS_CHECKS()      \
do {                                         \
    YAFL_CHECK(nx > 1, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(up,     YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(dp,     YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(hy,     YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(y,      YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(dr,     YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(w,      YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(d,      YAFL_ST_INV_ARG_1); \
} while (0)

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ekf_joseph_scalar_update(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat s = 0.0;
    yaflFloat  * f;
    yaflFloat  * h;

#   define nx  (self->Nx)
#   define x   (self->x)
#   define y   (self->y)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define dr  (self->Dr)
#   define hy  (((yaflEKFBaseSt *)self)->H)
#   define w   (((yaflEKFBaseSt *)self)->W)
#   define d   (((yaflEKFBaseSt *)self)->D)

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();

#   define v d
    f = v + nx;
    h = hy + nx * i;

    /* f = h.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, up));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, dp, f));

#   define r dr[i]
    /* s = r + f.dot(v)*/
    YAFL_TRY(status, yafl_math_vtv(nx, &s, f, v));
    s += r;

    /*K = Up.dot(v/s) = Up.dot(v)/s*/
#   define K h /*Don't need h any more, use it to store K*/
    YAFL_TRY(status, _joseph_update_body(nx, x, up, dp, f, v, K, w, y[i], r, \
                                         s, 1.0, 1.0));
#   undef K /*Don't nee K any more*/
#   undef r
#   undef v

#   undef nx
#   undef x
#   undef y
#   undef up
#   undef dp
#   undef dr
#   undef hy
#   undef w
#   undef d
    return status;
}

/*=============================================================================
                          Adaptive Bierman filter
=============================================================================*/
static inline yaflStatusEn \
    _adaptive_correction(yaflInt   nx, yaflFloat * res_ac, yaflFloat * res_s, \
                         yaflFloat * f, yaflFloat *      v, yaflFloat      r, \
                         yaflFloat  nu, yaflFloat     gdot, yaflFloat chi2)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat c  = 0.0;
    yaflFloat ac;
    yaflFloat s;

    YAFL_CHECK(res_ac, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(f,      YAFL_ST_INV_ARG_3);
    YAFL_CHECK(v,      YAFL_ST_INV_ARG_4);
    YAFL_CHECK(chi2 > 0,   YAFL_ST_INV_ARG_8);

    /* s = alpha**2 + gdot * f.dot(v)*/
    YAFL_TRY(status, yafl_math_vtv(nx, &c, f, v));
    c *= gdot;
    s = r + c;

    /* Divergence test */
    ac = gdot * (nu * (nu / chi2)) - s;
    if (ac > 0.0)
    {
        /*Adaptive correction factor*/
        ac = ac / c + 1.0;

        /*Corrected s*/
        s = ac * c + r;

        status |= YAFL_ST_MSK_ANOMALY; /*Anomaly detected!*/
    }
    else
    {
        ac = 1.0;
    }

    *res_ac = ac;

    if (res_s)
    {
        *res_s = s;
    }

    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ekf_adaptive_bierman_scalar_update(yaflKalmanBaseSt * self, \
                                                     yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat ac = 1.0;
    yaflFloat nu = 0.0;
    yaflFloat * h;

#   define nx  (self->Nx)
#   define x   (self->x)
#   define y   (self->y)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define dr  (self->Dr)
#   define hy  (((yaflEKFBaseSt *)self)->H)
#   define d   (((yaflEKFBaseSt *)self)->D)

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_BIERMAN_SELF_INTERNALS_CHECKS();

    nu = y[i];
    h = hy + nx * i;

    /* f = h.dot(Up) */
#   define f d
    //f = self->D;
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, up));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#   define v hy /*Don't need h any more, use it to store v*/
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, dp, f));

#   define r dr[i]
    YAFL_TRY(status, \
             _adaptive_correction(nx, &ac, 0, f, v, r, nu, 1.0, \
                                  ((yaflEKFAdaptiveSt *)self)->chi2));

    YAFL_TRY(status, \
             _bierman_update_body(nx, x, up, dp, f, v, r, nu, ac, 1.0));
#   undef r
#   undef v /*Don't nee v any more*/
#   undef f

#   undef nx
#   undef x
#   undef y
#   undef up
#   undef dp
#   undef dr
#   undef hy
#   undef d

    return status;
}

/*=============================================================================
                           Adaptive Joseph filter
=============================================================================*/
yaflStatusEn \
    yafl_ekf_adaptive_joseph_scalar_update(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat s  = 1.0;
    yaflFloat ac = 1.0;
    yaflFloat nu = 0.0;
    yaflFloat * h;
    yaflFloat * f;

#   define nx  (self->Nx)
#   define x   (self->x)
#   define y   (self->y)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define dr  (self->Dr)
#   define hy  (((yaflEKFBaseSt *)self)->H)
#   define w   (((yaflEKFBaseSt *)self)->W)
#   define d   (((yaflEKFBaseSt *)self)->D)

    _SCALAR_UPDATE_ARGS_CHECKS();
    //nx = self->Nx;
    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();

    //dp = self->Dp;
    //up = self->Up;

    h = hy + nx * i;

#   define v d
    //v = self->D;
    f = v + nx;

    nu = y[i];
    //r  = dr[i];

    /* f = h.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, up));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, dp, f));

#   define r dr[i]
    YAFL_TRY(status, \
             _adaptive_correction(nx, &ac, &s, f, v, r, nu, 1.0, \
                                  ((yaflEKFAdaptiveSt *)self)->chi2));

    /* K = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
#   define K h /*Don't need h any more, use it to store K*/
    YAFL_TRY(status, _joseph_update_body(nx, x, up, dp, f, v, K, w, nu, r, s, \
                                         ac, 1.0));
#   undef K /*Don't nee K any more*/
#   undef r
#   undef v

#   undef nx
#   undef x
#   undef y
#   undef up
#   undef dp
#   undef dr
#   undef hy
#   undef w
#   undef d

    return status;
}

/*=============================================================================
                                 WARNING!!!

             DO NOT USE THIS variant of Adaptive Joseph filter !!!

    It was implemented to show some flaws of the corresponding algorithm!
=============================================================================*/
//yaflStatusEn \
//    yafl_ekf_do_not_use_this_scalar_update(yaflKalmanBaseSt * self, yaflInt i)
//{
//    yaflStatusEn status = YAFL_ST_OK;
//    yaflFloat c = 0.0;
//    yaflFloat s = 0.0;
//    yaflInt nx;
//    yaflInt nx1;
//    yaflFloat * d;
//    yaflFloat * u;
//    yaflFloat * h;
//    yaflFloat * f;
//    yaflFloat * v;
//    yaflFloat * w;
//    yaflFloat nu;
//    yaflFloat r;
//    yaflFloat ac;
//
//    _SCALAR_UPDATE_ARGS_CHECKS();
//
//    nx = self->Nx;
//    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();
//
//    nx1 = nx + 1;
//
//    d = self->Dp;
//    u = self->Up;
//
//    h = self->H + nx * i;
//
//    v = self->D;
//    f = v + nx;
//
//    w = self->W;
//
//    nu = self->y[i];
//    r  = self->Dr[i];
//
//    /* f = h.dot(Up) */
//    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, u));
//
//    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
//    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, d, f));
//
//    /* s = r + f.dot(v)*/
//    YAFL_TRY(status, yafl_math_vtv(nx, &c, f, v));
//    s = c + r;
//
//    /* Divergence test */
//    ac = (nu * (nu / (((yaflEKFAdaptiveSt *)self)->chi2))) - s;
//    if (ac > 0.0)
//    {
//        /*Adaptive correction with no limitations approach*/
//        YAFL_TRY(status, yafl_math_set_uv(nx, f, u, v));
//        YAFL_TRY(status, yafl_math_udu_up(nx, u, d, (ac / c) / c, f));
//
//        /*Recompute f,v,s*/
//        /* f = h.dot(Up) */
//        YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, u));
//
//        /* v = f.dot(Dp).T = Dp.dot(f.T).T */
//        YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, d, f));
//
//        /* s = r + f.dot(v)*/
//        YAFL_TRY(status, yafl_math_vtv(nx, &c, f, v));
//        s = c + r;
//    }
//
//    /* K = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
//#define K h /*Don't need h any more, use it to store K*/
//#define D v
//    YAFL_TRY(status, yafl_math_set_uv(nx, K, u, v));
//    YAFL_TRY(status, yafl_math_set_vrn(nx, K, K, s)); /*May be used in place*/
//
//    /*Set W and D*/
//    YAFL_TRY(status, yafl_math_bset_vvt(nx1, w, nx, K, f));
//    YAFL_TRY(status, yafl_math_bsub_u(nx1, w, nx, u));
//
//    /* Now w is (Kf - Up|***) */
//    YAFL_TRY(status, YAFL_MATH_BSET_V(nx1, 0, nx, w, nx, K));
//    /* Now w is (Kf - Up|K) */
//
//    /* D = concatenate([Dp, np.array([r])]) */
//    memcpy((void *)D, (void *)d, nx * sizeof(yaflFloat));
//    D[nx] = r;
//
//    /* Up, Dp = MWGSU(W, D)*/
//    YAFL_TRY(status, yafl_math_mwgsu(nx, nx1, u, d, w, D));
//
//    /* self.x += K * nu */
//    YAFL_TRY(status, yafl_math_add_vxn(nx, self->x, K, nu));
//#undef D /*Don't nee D any more*/
//#undef K /*Don't nee K any more*/
//    return status;
//}

/*=============================================================================
                            Robust Bierman filter
=============================================================================*/
static inline yaflStatusEn \
    _scalar_robustify(yaflKalmanBaseSt * self, yaflFloat * gdot_res, \
                      yaflFloat * nu, yaflFloat r05)
{
    yaflKalmanRobFuncP g;
    yaflFloat gdot;

    YAFL_CHECK(self,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(gdot_res, YAFL_ST_INV_ARG_3);
    YAFL_CHECK(nu,       YAFL_ST_INV_ARG_4);

    g = ((yaflEKFRobustSt *)self)->g;

    if (g)
    {
        gdot = *nu / r05; /*Use gdot as temp variable*/
        *nu = r05 * g(self, gdot);

        g = ((yaflEKFRobustSt *)self)->gdot;
        YAFL_CHECK(g, YAFL_ST_INV_ARG_1);

        gdot = g(self, gdot);
    }
    else
    {
        gdot = 1.0;
    }

    *gdot_res = gdot;

    /*Detect glitches and return*/
    if (gdot < YAFL_EPS)
    {
        return YAFL_ST_MSK_GLITCH_LARGE;
    }

    if (gdot < (1.0 - 2.0*YAFL_EPS))
    {
        return YAFL_ST_MSK_GLITCH_SMALL;
    }

    return YAFL_ST_OK;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn \
    yafl_ekf_robust_bierman_scalar_update(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat gdot = 1.0;
    yaflFloat nu   = 0.0;
    yaflFloat r05;
    yaflFloat * h;

#   define nx  (self->Nx)
#   define x   (self->x)
#   define y   (self->y)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define dr  (self->Dr)
#   define hy  (((yaflEKFBaseSt *)self)->H)
#   define d   (((yaflEKFBaseSt *)self)->D)

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_BIERMAN_SELF_INTERNALS_CHECKS();

    r05 = dr[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = y[i];

    YAFL_TRY(status, _scalar_robustify(self, &gdot, &nu, r05));

    h = hy + nx * i;
    /* f = h.dot(Up) */
#   define f d
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, up));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#   define v h /*Don't need h any more, use it to store v*/
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, dp, f));

    YAFL_TRY(status, _bierman_update_body(nx, x, up, dp, f, v, r05 * r05, nu, \
                                          1.0, gdot));

#   undef v  /*Don't nee v any more*/
#   undef f

#   undef nx
#   undef x
#   undef y
#   undef up
#   undef dp
#   undef dr
#   undef hy
#   undef d

    return status;
}

/*=============================================================================
                            Robust Joseph filter
=============================================================================*/
yaflStatusEn \
    yafl_ekf_robust_joseph_scalar_update(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat gdot = 0.0;
    yaflFloat s    = 0.0;
    yaflFloat nu   = 0.0;
    yaflFloat r05;
    yaflFloat * h;
    yaflFloat * f;

#   define nx  (self->Nx)
#   define x   (self->x)
#   define y   (self->y)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define dr  (self->Dr)
#   define hy  (((yaflEKFBaseSt *)self)->H)
#   define w   (((yaflEKFBaseSt *)self)->W)
#   define d   (((yaflEKFBaseSt *)self)->D)

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();

    r05 = dr[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = y[i];

    YAFL_TRY(status, _scalar_robustify(self, &gdot, &nu, r05));

    r05 *= r05;
#   define A2 r05 /*Now it is r = alpha**2 */

    h = hy + nx * i;
#   define v d
    f = v + nx;

    /* f = h.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, up));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, dp, f));

    /* s = alpha**2 + gdot * f.dot(v)*/
    YAFL_TRY(status, yafl_math_vtv(nx, &s, f, v));
    s = A2 + gdot * s;

    /*K = Up.dot(v/s) = Up.dot(v)/s*/
#   define K h /*Don't need h any more, use it to store K*/
    YAFL_TRY(status, _joseph_update_body(nx, x, up, dp, f, v, K, w, nu, A2, s, \
                                         1.0, gdot));
#   undef K  /*Don't nee K any more*/
#   undef v
#   undef A2 /*Don't nee A2 any more*/

#   undef nx
#   undef x
#   undef y
#   undef up
#   undef dp
#   undef dr
#   undef hy
#   undef w
#   undef d

    return status;
}

/*=============================================================================
                        Adaptive robust Bierman filter
=============================================================================*/
yaflStatusEn \
    yafl_ekf_adaptive_robust_bierman_scalar_update(yaflKalmanBaseSt * self, \
                                                   yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat gdot = 1.0;
    yaflFloat ac   = 1.0;
    yaflFloat nu   = 0.0;
    yaflFloat r05;
    yaflFloat * h;

#   define nx  (self->Nx)
#   define x   (self->x)
#   define y   (self->y)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define dr  (self->Dr)
#   define hy  (((yaflEKFBaseSt *)self)->H)
#   define d   (((yaflEKFBaseSt *)self)->D)

    _SCALAR_UPDATE_ARGS_CHECKS();

    //nx = self->Nx;
    _EKF_BIERMAN_SELF_INTERNALS_CHECKS();

    r05 = dr[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = y[i];

    YAFL_TRY(status, _scalar_robustify(self, &gdot, &nu, r05));

    r05 *= r05;
#   define A2 r05 /*Now it is r = alpha**2 */
    //up = self->Up;
    //dp = self->Dp;
    h = hy + nx * i;

    /* f = h.dot(Up) */
#   define f d
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, up));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#   define v h /*Don't need h any more, use it to store v*/
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, dp, f));

    YAFL_TRY(status, \
             _adaptive_correction(nx, &ac, 0, f, v, A2, nu, gdot, \
                                  ((yaflEKFAdaptiveRobustSt *)self)->chi2));

    YAFL_TRY(status, _bierman_update_body(nx, x, up, dp, f, v, A2, nu, \
                                          ac, gdot));
#   undef v  /*Don't nee v any more*/
#   undef f
#   undef A2 /*Don't nee A2 any more*/

#   undef nx
#   undef x
#   undef y
#   undef up
#   undef dp
#   undef dr
#   undef hy
#   undef d

    return status;
}

/*=============================================================================
                        Adaptive robust Joseph filter
=============================================================================*/
yaflStatusEn \
    yafl_ekf_adaptive_robust_joseph_scalar_update(yaflKalmanBaseSt * self, \
                                                  yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat gdot = 1.0;
    yaflFloat   ac = 1.0;
    yaflFloat    s = 0.0;

#   define nx  (self->Nx)
#   define x   (self->x)
#   define y   (self->y)
#   define up  (self->Up)
#   define dp  (self->Dp)
#   define dr  (self->Dr)
#   define hy  (((yaflEKFBaseSt *)self)->H)
#   define w   (((yaflEKFBaseSt *)self)->W)
#   define d   (((yaflEKFBaseSt *)self)->D)

    //yaflInt nx;
    //yaflFloat * dp;
    //yaflFloat * up;
    yaflFloat * h;
    yaflFloat * f;
    //yaflFloat * v;
    yaflFloat r05;
    yaflFloat nu;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();

    r05 = dr[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = y[i];

    YAFL_TRY(status, _scalar_robustify(self, &gdot, &nu, r05));

    r05 *= r05;
#   define A2 r05 /*Now it is r = alpha**2 */

    h = hy + nx * i;
#   define v d
    f = v + nx;

    /* f = h.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, h, up));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, dp, f));

    YAFL_TRY(status, \
             _adaptive_correction(nx, &ac, &s, f, v, A2, nu, gdot, \
                                  ((yaflEKFAdaptiveRobustSt *)self)->chi2));

    /* K = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
#   define K h /*Don't need h any more, use it to store K*/
    YAFL_TRY(status, \
             _joseph_update_body(nx, x, up, dp, f, v, K, w, nu, A2, s, \
                                 ac, gdot));
#   undef K  /*Don't nee K any more*/
#   undef v
#   undef A2 /*Don't nee A2 any more*/

#   undef nx
#   undef x
#   undef y
#   undef up
#   undef dp
#   undef dr
#   undef hy
#   undef w
#   undef d

    return status;
}

/*=============================================================================
                    Basic UD-factorized UKF functions
=============================================================================*/
static inline yaflStatusEn _compute_res(yaflUKFBaseSt * self, yaflInt sz,    \
                                        yaflUKFResFuncP rf, yaflFloat * res, \
                                        yaflFloat * sigma, yaflFloat * pivot)
{
    yaflStatusEn status = YAFL_ST_OK;
    if (rf)
    {
        /*rf must be aware of sp and the current transform*/
        /* res = self.rf(sigma, pivot) */
        YAFL_TRY(status, rf(self, res, sigma, pivot));
    }
    else
    {
        yaflInt j;
        for (j = 0; j < sz; j++)
        {
            res[j] = sigma[j] - pivot[j];
        }
    }
    return status;
}

/*---------------------------------------------------------------------------*/
static yaflStatusEn _unscented_transform(yaflUKFBaseSt * self,   \
        yaflInt    res_sz,      \
        yaflFloat * res_v,      \
        yaflFloat * res_u,      \
        yaflFloat * res_d,      \
        yaflFloat * sp,         \
        yaflFloat * sigmas,     \
        yaflFloat * noise_u,    \
        yaflFloat * noise_d,    \
        yaflUKFFuncP    mf,     \
        yaflUKFResFuncP rf)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt np;
    yaflInt i;
    yaflUKFSigmaSt * sp_info;
    yaflFloat * wc;

    YAFL_CHECK(self,       YAFL_ST_INV_ARG_1);

    YAFL_CHECK(res_sz > 0, YAFL_ST_INV_ARG_2);
    YAFL_CHECK(res_v,      YAFL_ST_INV_ARG_3);
    YAFL_CHECK(res_u,      YAFL_ST_INV_ARG_4);
    YAFL_CHECK(res_d,      YAFL_ST_INV_ARG_5);
    YAFL_CHECK(sp,         YAFL_ST_INV_ARG_6);

    if (noise_u)
    {
        YAFL_CHECK(noise_d, YAFL_ST_INV_ARG_9);
    }

    YAFL_CHECK(self->wm, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(self->wc, YAFL_ST_INV_ARG_1);
    wc = self->wc;

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np > 1, YAFL_ST_INV_ARG_1);
    np = sp_info->np;

    if (mf)
    {
        /*mf must be aware of the current transform details...*/
        YAFL_TRY(status, mf(self, res_v, sigmas));
    }
    else
    {
        YAFL_TRY(status, yafl_math_set_vtm(np, res_sz, res_v, self->wm, sigmas));
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
        YAFL_TRY(status, \
                 _compute_res(self, res_sz, rf , sp, sigmas + res_sz * i, \
                              res_v));
        /*Update res_u and res_d*/
        /*wc should be sorted in descending order*/
        if (wc[i] >= 0.0)
        {
            YAFL_TRY(status, \
                     yafl_math_udu_up(res_sz, res_u, res_d, wc[i], sp));
        }
        else
        {
            YAFL_TRY(status, \
                     yafl_math_udu_down(res_sz, res_u, res_d, -wc[i], sp));
        }
    }
    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ukf_base_predict(yaflUKFBaseSt * self)
{
    yaflStatusEn status = YAFL_ST_OK;

    yaflInt np;
    yaflInt nx;
    yaflInt i;
    yaflUKFSigmaSt * sp_info;
    yaflUKFFuncP fx; /*State transition function*/
    yaflFloat * sigmas_x;

    /*Check some params and generate sigma points*/
    YAFL_TRY(status, yafl_ukf_gen_sigmas(self)); /*Self is checked here*/

    YAFL_CHECK(self->Nx, YAFL_ST_INV_ARG_1);
    nx = self->Nx;

    YAFL_CHECK(self->sigmas_x, YAFL_ST_INV_ARG_1);
    sigmas_x = self->sigmas_x;

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np > 1, YAFL_ST_INV_ARG_1);
    np = sp_info->np;

    /*Compute process sigmas*/
    YAFL_CHECK(self->f, YAFL_ST_INV_ARG_1);
    fx = self->f;
    if (fx)
    {
        for (i = 0; i < np; i++)
        {
            yaflFloat * sigmai;
            sigmai = sigmas_x + nx * i;
            YAFL_TRY(status, fx(self, sigmai, sigmai));
        }
    }

    /*Predict x, Up, Dp*/
    YAFL_TRY(status, \
             _unscented_transform(self, nx, self->x, self->Up, self->Dp, \
                                  self->Sx, sigmas_x, self->Uq, self->Dq, \
                                  self->xmf, self->xrf));
    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ukf_base_update(yaflUKFBaseSt * self, yaflFloat * z, \
                                  yaflUKFScalarUpdateP scalar_update)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt np;
    yaflInt nx;
    yaflInt nz;
    yaflInt i;
    yaflUKFSigmaSt * sp_info; /*Sigma point generator info*/
    yaflUKFFuncP hx;          /*State transition function*/
    yaflUKFResFuncP xrf;      /*State residual function*/
    yaflUKFResFuncP zrf;      /*Measurement residual function*/
    yaflFloat * sigmas_x;     /*State sigma points*/
    yaflFloat * sigmas_z;     /*Measurement sigma points*/
    /*Scratchpad memory*/
    yaflFloat * spx;          /*For state residuals*/
    yaflFloat * y;            /*For measurement residuals*/

    yaflFloat * x;            /*State*/
    yaflFloat * zp;           /*Predicted measurement*/
    yaflFloat * wc;           /*Covariance computation weights*/
    yaflFloat * pzx;          /*Pzx cross covariance matrix*/

    /*Output noise covariance*/
    yaflFloat * ur;           /*Unit upper triangular factor*/

    /*P covariance*/
    yaflFloat * up;           /*Unit upper triangular factor*/

    YAFL_CHECK(scalar_update, YAFL_ST_INV_ARG_3);

    YAFL_CHECK(self,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(self->Nx, YAFL_ST_INV_ARG_1);
    nx = self->Nx;

    YAFL_CHECK(self->sigmas_x, YAFL_ST_INV_ARG_1);
    sigmas_x = self->sigmas_x;

    YAFL_CHECK(self->Nz, YAFL_ST_INV_ARG_1);
    nz = self->Nz;

    YAFL_CHECK(self->sigmas_z, YAFL_ST_INV_ARG_1);
    sigmas_z = self->sigmas_z;

    YAFL_CHECK(self->wc, YAFL_ST_INV_ARG_1);
    wc = self->wc;

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np > 1, YAFL_ST_INV_ARG_1);
    np = sp_info->np;

    YAFL_CHECK(self->h, YAFL_ST_INV_ARG_1);
    hx = self->h;

    YAFL_CHECK(self->Pzx, YAFL_ST_INV_ARG_1);
    pzx = self->Pzx;

    YAFL_CHECK(self->Sx, YAFL_ST_INV_ARG_1);
    spx = self->Sx;

    YAFL_CHECK(self->y, YAFL_ST_INV_ARG_1);
    y = self->y;

    YAFL_CHECK(self->zp, YAFL_ST_INV_ARG_1);
    zp = self->zp;

    YAFL_CHECK(self->x, YAFL_ST_INV_ARG_1);
    x = self->x;

    YAFL_CHECK(self->Ur, YAFL_ST_INV_ARG_1);
    ur = self->Ur;

    YAFL_CHECK(self->Up, YAFL_ST_INV_ARG_1);
    up = self->Up;

    /* Compute measurement sigmas */
    for (i = 0; i < np; i++)
    {
        YAFL_TRY(status, hx(self, sigmas_z + nz * i, sigmas_x + nx * i));
    }

    /* Compute zp*/
    if (self->zmf)
    {
        /*mf must be aware of the current transform details...*/
        YAFL_TRY(status, self->zmf(self, zp, sigmas_z));
    }
    else
    {
        YAFL_TRY(status, yafl_math_set_vtm(np, nz, zp, self->wm, sigmas_z));
    }

    /* Compute Pzx */
    zrf = self->zrf;
    xrf = self->xrf;
    YAFL_TRY(status, _compute_res(self, nz, zrf,   y, sigmas_z, zp));
    YAFL_TRY(status, _compute_res(self, nx, xrf, spx, sigmas_x,  x));
    YAFL_TRY(status, yafl_math_set_vvtxn(nz, nx, pzx, y, spx, wc[0]));

    for (i = 1; i < np; i++)
    {
        YAFL_TRY(status, \
                 _compute_res(self, nz, zrf,   y, sigmas_z + nz * i, zp));
        YAFL_TRY(status, \
                 _compute_res(self, nx, xrf, spx, sigmas_x + nx * i,  x));
        YAFL_TRY(status, yafl_math_add_vvtxn(nz, nx, pzx, y, spx, wc[i]));
    }

    /*Compute innovation*/
    YAFL_TRY(status, _compute_res(self, nz, zrf, y, z, zp));

    /* Decorrelate measurements*/
    YAFL_TRY(status, yafl_math_ruv(nz,       y, ur));
    YAFL_TRY(status, yafl_math_rum(nz, nx, pzx, ur));

    /*Now we can do scalar updates*/
    for (i = 0; i < nz; i++)
    {
        /*
        Compute v[i] for Bierman/Joseph style scalar updates:
        v[i] = linalg.inv(Up).dot(Pzx[i].T)

        It's cheaper than computing _unscented_transform.
        */
        YAFL_TRY(status, yafl_math_ruv(nx, pzx + nx * i, up));
        YAFL_TRY(status, scalar_update(self, i));
    }
    return status;
}

/*===========================================================================*/
#define _BIERMAN_LIKE_SELF_INTERNALS_CHECKS() \
do {                                          \
    YAFL_CHECK(nx > 1,    YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(self->Up,  YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(self->Dp,  YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(self->Pzx, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(self->Sx,  YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(self->y,   YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(self->Dr,  YAFL_ST_INV_ARG_1); \
} while (0)

/*---------------------------------------------------------------------------*/
static yaflStatusEn _bierman_like_scalar_update(yaflUKFBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt nx;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * v;
    yaflFloat * f;

    _SCALAR_UPDATE_ARGS_CHECKS();

    nx = self->Nx;
    _BIERMAN_LIKE_SELF_INTERNALS_CHECKS();

    u = self->Up;
    d = self->Dp;
    v = self->Pzx + nx * i;

    /* f = linalg.inv(Dp).dot(v)*/
    f = self->Sx;
    YAFL_TRY(status, YAFL_MATH_SET_RDV(nx, f, d, v));

    YAFL_TRY(status, \
             _bierman_update_body(nx, self->x, u, d, f, v, self->Dr[i], \
                                  self->y[i], 1.0, 1.0));
    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ukf_bierman_update(yaflUKFBaseSt * self, yaflFloat * z)
{
    return yafl_ukf_base_update(self, z, _bierman_like_scalar_update);
}

/*=============================================================================
                        Adaptive Bierman-like filter
=============================================================================*/
static yaflStatusEn _adaptive_bierman_like_scalar_update(yaflUKFBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt nx;
    yaflFloat * d;
    yaflFloat * u;
    yaflFloat * v;
    yaflFloat * f;
    yaflFloat nu;
    yaflFloat r;
    yaflFloat ac;

    _SCALAR_UPDATE_ARGS_CHECKS();

    nx = self->Nx;
    _BIERMAN_LIKE_SELF_INTERNALS_CHECKS();

    u = self->Up;
    d = self->Dp;
    v = self->Pzx + nx * i;

    /* f = linalg.inv(Dp).dot(v)*/
    f = self->Sx;
    YAFL_TRY(status, YAFL_MATH_SET_RDV(nx, f, d, v));

    nu = self->y[i];
    r  = self->Dr[i];

    YAFL_TRY(status, \
             _adaptive_correction(nx, &ac, 0, f, v, r, nu, 1.0, \
                                  ((yaflUKFAdaptivedSt *)self)->chi2));

    YAFL_TRY(status, \
             _bierman_update_body(nx, self->x, u, d, f, v, r, \
                                  self->y[i], ac, 1.0));
    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ukf_adaptive_bierman_update(yaflUKFAdaptivedSt * self, \
        yaflFloat * z)
{
    return yafl_ukf_base_update((yaflUKFBaseSt *)self, z, \
                                _adaptive_bierman_like_scalar_update);
}

/*=============================================================================
                           Robust Bierman UKF
=============================================================================*/

/*=============================================================================
            Full UKF, not a sequential square root version of UKF
=============================================================================*/
yaflStatusEn yafl_ukf_update(yaflUKFBaseSt * self, yaflFloat * z)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt np;
    yaflInt nx;
    yaflInt nz;
    yaflInt i;
    yaflUKFSigmaSt * sp_info; /*Sigma point generator info*/
    yaflUKFFuncP hx;          /*State transition function*/
    yaflUKFResFuncP xrf;      /*State residual function*/
    yaflUKFResFuncP zrf;      /*Measurement residual function*/
    yaflFloat * sigmas_x;     /*State sigma points*/
    yaflFloat * sigmas_z;     /*Measurement sigma points*/
    /*Scratchpad memory*/
    yaflFloat * spx;          /*For state residuals*/
    yaflFloat * y;            /*For measurement residuals*/

    yaflFloat * x;            /*State*/
    yaflFloat * zp;           /*Predicted measurement*/
    yaflFloat * wc;           /*Covariance computation weights*/
    yaflFloat * pzx;          /*Pzx cross covariance matrix*/

    /*Pzz covariance*/
    yaflFloat * us;           /*Unit upper triangular factor*/
    yaflFloat * ds;           /*Diagonal factor*/

    /*P covariance*/
    yaflFloat * up;           /*Unit upper triangular factor*/
    yaflFloat * dp;           /*Diagonal factor*/

    YAFL_CHECK(self,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(self->Nx, YAFL_ST_INV_ARG_1);
    nx = self->Nx;

    YAFL_CHECK(self->sigmas_x, YAFL_ST_INV_ARG_1);
    sigmas_x = self->sigmas_x;

    YAFL_CHECK(self->Nz, YAFL_ST_INV_ARG_1);
    nz = self->Nz;

    YAFL_CHECK(self->sigmas_z, YAFL_ST_INV_ARG_1);
    sigmas_z = self->sigmas_z;

    YAFL_CHECK(self->wc, YAFL_ST_INV_ARG_1);
    wc = self->wc;

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np > 1, YAFL_ST_INV_ARG_1);
    np = sp_info->np;

    YAFL_CHECK(self->h, YAFL_ST_INV_ARG_1);
    hx = self->h;

    YAFL_CHECK(self->Pzx, YAFL_ST_INV_ARG_1);
    pzx = self->Pzx;

    YAFL_CHECK(self->Sx, YAFL_ST_INV_ARG_1);
    spx = self->Sx;

    YAFL_CHECK(self->y, YAFL_ST_INV_ARG_1);
    y = self->y;

    YAFL_CHECK(self->zp, YAFL_ST_INV_ARG_1);
    zp = self->zp;

    YAFL_CHECK(self->x, YAFL_ST_INV_ARG_1);
    x = self->x;

    YAFL_CHECK(self->Up, YAFL_ST_INV_ARG_1);
    up = self->Up;

    YAFL_CHECK(self->Dp, YAFL_ST_INV_ARG_1);
    dp = self->Dp;

    YAFL_CHECK(((yaflUKFSt *)self)->Us, YAFL_ST_INV_ARG_1);
    us = ((yaflUKFSt *)self)->Us;

    YAFL_CHECK(((yaflUKFSt *)self)->Ds, YAFL_ST_INV_ARG_1);
    ds = ((yaflUKFSt *)self)->Ds;

    /* Compute measurement sigmas */
    for (i = 0; i < np; i++)
    {
        YAFL_TRY(status, hx(self, sigmas_z + nz * i, sigmas_x + nx * i));
    }

    /* Compute zp, Us, Ds */
    zrf = self->zrf;
    YAFL_TRY(status, \
             _unscented_transform(self, nz, zp, us, ds, y, sigmas_z, \
                                  self->Ur, self->Dr, self->zmf, zrf));

    /* Compute Pzx */
    xrf = self->xrf;
    YAFL_TRY(status, _compute_res(self, nz, zrf, y,   sigmas_z, zp));
    YAFL_TRY(status, _compute_res(self, nx, xrf, spx, sigmas_x,  x));
    YAFL_TRY(status, yafl_math_set_vvtxn(nz, nx, pzx, y, spx, wc[0]));

    for (i = 1; i < np; i++)
    {
        YAFL_TRY(status, \
                 _compute_res(self, nz, zrf,   y, sigmas_z + nz * i, zp));
        YAFL_TRY(status, \
                 _compute_res(self, nx, xrf, spx, sigmas_x + nx * i,  x));
        YAFL_TRY(status, yafl_math_add_vvtxn(nz, nx, pzx, y, spx, wc[i]));
    }

    /*Compute innovation*/
    YAFL_TRY(status, _compute_res(self, nz, zrf, y, z, zp));

    /* Decorrelate measurements*/
    YAFL_TRY(status, yafl_math_ruv(nz,       y, us));
    YAFL_TRY(status, yafl_math_rum(nz, nx, pzx, us));

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
        YAFL_TRY(status, yafl_math_add_vxn(nx, x, pzxi, y[i] / ds[i]));

        /*
        P -= K.dot(S.dot(K.T))
        K.dot(S.dot(K.T)) = (Pzx[i].T / ds[i] * ds[i]).outer(Pzx[i] / ds[i]))
        K.dot(S.dot(K.T)) = (Pzx[i].T).outer(Pzx[i]) / ds[i]
        P -= (Pzx[i].T).outer(Pzx[i]) / ds[i]
        Up, Dp = udu(P)
        */
        YAFL_TRY(status, yafl_math_udu_down(nx, up, dp, 1.0 / ds[i], pzxi));
    }
    return status;
}

/*=============================================================================
                    Van der Merwe sigma points generator
=============================================================================*/
static yaflStatusEn _merwe_compute_weights(yaflUKFBaseSt * self)
{
    yaflStatusEn status = YAFL_ST_OK;
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

    YAFL_CHECK(self, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(self->Nx, YAFL_ST_INV_ARG_1);
    nx = self->Nx;

    YAFL_CHECK(self->wm, YAFL_ST_INV_ARG_1);
    wm = self->wm;

    YAFL_CHECK(self->wc, YAFL_ST_INV_ARG_1);
    wc = self->wc;

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np, YAFL_ST_INV_ARG_1);
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
    return status;
}

/*---------------------------------------------------------------------------*/
static inline yaflStatusEn _add_delta(yaflUKFBaseSt * self,                  \
                                      yaflUKFSigmaAddP addf, yaflInt sz,     \
                                      yaflFloat * delta, yaflFloat * pivot,  \
                                      yaflFloat mult)
{
    yaflStatusEn status = YAFL_ST_OK;
    if (addf)
    {
        /* addf must be aware of self internal structure*/
        /* delta = self.addf(delta, pivot, mult) */
        YAFL_TRY(status, addf(self, delta, pivot, mult));
    }
    else
    {
        yaflInt j;
        for (j = 0; j < sz; j++)
        {
            delta[j] = pivot[j] + mult * delta[j];
        }
    }
    return status;
}

/*---------------------------------------------------------------------------*/
static yaflStatusEn _merwe_generate_points(yaflUKFBaseSt * self)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt nx;
    yaflInt i;
    yaflFloat * x;
    yaflFloat * sigmas_x;
    yaflFloat * dp;
    yaflUKFSigmaSt * sp_info;
    yaflUKFSigmaAddP addf;
    yaflFloat lambda_p_n;
    yaflFloat alpha;

    YAFL_CHECK(self,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(self->Nx, YAFL_ST_INV_ARG_1);
    nx = self->Nx;

    YAFL_CHECK(self->x, YAFL_ST_INV_ARG_1);
    x = self->x;

    YAFL_CHECK(self->Up, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(self->Dp, YAFL_ST_INV_ARG_1);
    dp = self->Dp;

    YAFL_CHECK(self->sigmas_x, YAFL_ST_INV_ARG_1);
    sigmas_x = self->sigmas_x;

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    alpha = ((yaflUKFMerweSt *)sp_info)->alpha;
    lambda_p_n = alpha * alpha * (nx + ((yaflUKFMerweSt *)sp_info)->kappa);

    YAFL_TRY(status, yafl_math_bset_ut(nx, sigmas_x, nx, self->Up));
    memcpy((void *)(sigmas_x + nx * nx), (void *)sigmas_x, \
           nx * nx * sizeof(yaflFloat));

    addf = sp_info->addf;
    for (i = 0; i < nx; i++)
    {
        yaflFloat mult;
        mult = YAFL_SQRT(dp[i] * lambda_p_n);
        YAFL_TRY(status, \
                 _add_delta(self, addf, nx, sigmas_x + nx * i, x,   mult));
        YAFL_TRY(status, \
                 _add_delta(self, addf, nx, sigmas_x + nx * (nx + i), x, - mult));
    }
    memcpy((void *)(sigmas_x + 2 * nx * nx), (void *)x, nx * sizeof(yaflFloat));
    return status;
}

/*---------------------------------------------------------------------------*/
const yaflUKFSigmaMethodsSt yafl_ukf_merwe_spm =
{
    .wf   = _merwe_compute_weights,
    .spgf = _merwe_generate_points
};
