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

#define _FX  (self->f)
#define _HX  (self->h)
#define _ZRF (self->zrf)

#define _X   (self->x)
#define _Y   (self->y)

#define _UP  (self->Up)
#define _DP  (self->Dp)

#define _UQ  (self->Uq)
#define _DQ  (self->Dq)

#define _UR  (self->Ur)
#define _DR  (self->Dr)

#define _NX  (self->Nx)
#define _NZ  (self->Nz)

/*=============================================================================
                                  Base UDEKF
=============================================================================*/
#define _JFX (((yaflEKFBaseSt *)self)->jf)
#define _JHX (((yaflEKFBaseSt *)self)->jh)

#define _HY  (((yaflEKFBaseSt *)self)->H)
#define _W   (((yaflEKFBaseSt *)self)->W)
#define _D   (((yaflEKFBaseSt *)self)->D)

yaflStatusEn yafl_ekf_base_predict(yaflKalmanBaseSt * self)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt i;
    yaflInt nx2;

    YAFL_CHECK(self, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_UP,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_DP,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UQ,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_DQ,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_NX > 1, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_W,      YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_D,      YAFL_ST_INV_ARG_1);

    nx2 = _NX * 2;

    /*Default f(x) = x*/
    if (0 == _FX)
    {
        YAFL_CHECK(0 == _JFX, YAFL_ST_INV_ARG_1);

        for (i = 0; i < _NX; i++)
        {
            yaflInt j;
            yaflInt nci;

            nci = nx2 * i;
            for (j = 0; j < _NX; j++)
            {
                _W[nci + j] = (i != j) ? 0.0 : 1.0;
            }
        }
    }
    else
    {
        //yaflFloat * x;

        /*Must have some Jacobian function*/
        YAFL_CHECK(_JFX, YAFL_ST_INV_ARG_1);

        //x = self->x;
        YAFL_TRY(status,  _FX(self, _X, _X));  /* x = f(x_old, ...) */
        YAFL_TRY(status, _JFX(self, _W, _X));  /* Place F(x, ...)=df/dx to W  */
    }
    /* Now W = (F|***) */
    YAFL_TRY(status, \
             YAFL_MATH_BSET_BU(nx2, 0, _NX, _W, _NX, _NX, nx2, 0, 0, _W, _UP));
    /* Now W = (F|FUp) */
    YAFL_TRY(status, yafl_math_bset_u(nx2, _W, _NX, _UQ));
    /* Now W = (Uq|FUp) */

    /* D = concatenate([Dq, Dp]) */
    i = _NX*sizeof(yaflFloat);
    memcpy((void *)       _D, (void *)_DQ, i);
    memcpy((void *)(_D + _NX), (void *)_DP, i);

    /* Up, Dp = MWGSU(w, d)*/
    YAFL_TRY(status, yafl_math_mwgsu(_NX, nx2, _UP, _DP, _W, _D));

    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ekf_base_update(yaflKalmanBaseSt * self, yaflFloat * z, yaflKalmanScalarUpdateP scalar_update)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt j;

    YAFL_CHECK(self,   YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_HX,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_X,      YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_Y,      YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UR,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_NX > 1, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_NZ > 0, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_JHX,    YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_HY,     YAFL_ST_INV_ARG_1);

    YAFL_CHECK(z,      YAFL_ST_INV_ARG_2);
    YAFL_CHECK(scalar_update, YAFL_ST_INV_ARG_3);


    YAFL_TRY(status,  _HX(self, _Y,  _X)); /* self.y =  h(x,...) */
    YAFL_TRY(status, _JHX(self, _HY, _X)); /* self.H = jh(x,...) */

    if (0 == _ZRF)
    {
        /*Default residual*/
        for (j = 0; j < _NZ; j++)
        {
            _Y[j] = z[j] - _Y[j];
        }
    }
    else
    {
        /*zrf must be aware of self internal structure*/
        YAFL_TRY(status, _ZRF(self, _Y, z, _Y)); /* self.y = zrf(z, h(x,...)) */
    }

    /* Decorrelate measurement noise */

    YAFL_TRY(status, yafl_math_ruv(_NZ,      _Y,  _UR));
    YAFL_TRY(status, yafl_math_rum(_NZ, _NX, _HY, _UR));

    /* Do scalar updates */
    for (j = 0; j < _NZ; j++)
    {
        YAFL_TRY(status, scalar_update(self, j));
    }

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
    YAFL_CHECK(_NX > 1,   YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_UP, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_DP, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_HY, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_Y,  YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_DR, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_D,  YAFL_ST_INV_ARG_1); \
} while (0)

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ekf_bierman_update_scalar(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat * h;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_BIERMAN_SELF_INTERNALS_CHECKS();

    h = _HY + _NX * i;
    /* f = h.dot(Up) */
#   define f _D
    YAFL_TRY(status, yafl_math_set_vtu(_NX, f, h, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#define v h /*Don't need h any more, use it to store v*/
    YAFL_TRY(status, YAFL_MATH_SET_DV(_NX, v, _DP, f));
    YAFL_TRY(status, \
             _bierman_update_body(_NX, _X, _UP, _DP, f, v, _DR[i], _Y[i], \
                                  1.0, 1.0));

#   undef v /*Don't nee v any more*/
#   undef f

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
    YAFL_CHECK(_NX > 1, YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_UP,     YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_DP,     YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_HY,     YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_Y,      YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_DR,     YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_W,      YAFL_ST_INV_ARG_1); \
    YAFL_CHECK(_D,      YAFL_ST_INV_ARG_1); \
} while (0)

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ekf_joseph_update_scalar(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat s = 0.0;
    yaflFloat  * f;
    yaflFloat  * h;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();

#   define v _D
    f = v + _NX;
    h = _HY + _NX * i;

    /* f = h.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(_NX, f, h, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(_NX, v, _DP, f));

#   define r _DR[i]
    /* s = r + f.dot(v)*/
    YAFL_TRY(status, yafl_math_vtv(_NX, &s, f, v));
    s += r;

    /*K = Up.dot(v/s) = Up.dot(v)/s*/
#   define K h /*Don't need h any more, use it to store K*/
    YAFL_TRY(status, \
             _joseph_update_body(_NX, _X, _UP, _DP, f, v, K, _W, _Y[i], r, \
                                 s, 1.0, 1.0));
#   undef K /*Don't nee K any more*/
#   undef r
#   undef v

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
yaflStatusEn yafl_ekf_adaptive_bierman_update_scalar(yaflKalmanBaseSt * self, \
                                                     yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat ac = 1.0;
    yaflFloat nu = 0.0;
    yaflFloat * h;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_BIERMAN_SELF_INTERNALS_CHECKS();

    nu = _Y[i];
    h = _HY + _NX * i;

    /* f = h.dot(Up) */
#   define f _D
    //f = self->D;
    YAFL_TRY(status, yafl_math_set_vtu(_NX, f, h, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#   define v _HY /*Don't need h any more, use it to store v*/
    YAFL_TRY(status, YAFL_MATH_SET_DV(_NX, v, _DP, f));

#   define r _DR[i]
    YAFL_TRY(status, \
             _adaptive_correction(_NX, &ac, 0, f, v, r, nu, 1.0, \
                                  ((yaflEKFAdaptiveSt *)self)->chi2));

    YAFL_TRY(status, \
             _bierman_update_body(_NX, _X, _UP, _DP, f, v, r, nu, ac, 1.0));
#   undef r
#   undef v /*Don't nee v any more*/
#   undef f

    return status;
}

/*=============================================================================
                           Adaptive Joseph filter
=============================================================================*/
yaflStatusEn \
    yafl_ekf_adaptive_joseph_update_scalar(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat s  = 1.0;
    yaflFloat ac = 1.0;
    yaflFloat nu = 0.0;
    yaflFloat * h;
    yaflFloat * f;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();

    h = _HY + _NX * i;

#   define v _D
    f = v + _NX;

    nu = _Y[i];

    /* f = h.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(_NX, f, h, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(_NX, v, _DP, f));

#   define r _DR[i]
    YAFL_TRY(status, \
             _adaptive_correction(_NX, &ac, &s, f, v, r, nu, 1.0, \
                                  ((yaflEKFAdaptiveSt *)self)->chi2));

    /* K = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
#   define K h /*Don't need h any more, use it to store K*/
    YAFL_TRY(status, \
             _joseph_update_body(_NX, _X, _UP, _DP, f, v, K, _W, nu, r, s, \
                                 ac, 1.0));
#   undef K /*Don't nee K any more*/
#   undef r
#   undef v

    return status;
}

/*=============================================================================
                                 WARNING!!!

             DO NOT USE THIS variant of Adaptive Joseph filter !!!

    It was implemented to show some flaws of the corresponding algorithm!
=============================================================================*/
//yaflStatusEn \
//    yafl_ekf_do_not_use_this_update_scalar(yaflKalmanBaseSt * self, yaflInt i)
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
    _scalar_robustify(yaflKalmanBaseSt * self, \
                      yaflKalmanRobFuncP g, yaflKalmanRobFuncP gdot, \
                      yaflFloat * gdot_res, yaflFloat * nu, yaflFloat r05)
{
    yaflFloat tmp;

    YAFL_CHECK(self,     YAFL_ST_INV_ARG_1);
    YAFL_CHECK(gdot_res, YAFL_ST_INV_ARG_3);
    YAFL_CHECK(nu,       YAFL_ST_INV_ARG_4);

    if (g)
    {
        tmp = *nu / r05;
        *nu = r05 * g(self, tmp);

        YAFL_CHECK(gdot, YAFL_ST_INV_ARG_1);

        tmp = gdot(self, tmp);
    }
    else
    {
        tmp = 1.0;
    }

    *gdot_res = tmp;

    /*Detect glitches and return*/
    if (tmp < YAFL_EPS)
    {
        return YAFL_ST_MSK_GLITCH_LARGE;
    }

    if (tmp < (1.0 - 2.0*YAFL_EPS))
    {
        return YAFL_ST_MSK_GLITCH_SMALL;
    }

    return YAFL_ST_OK;
}

#define _SCALAR_ROBUSTIFY(_self, _gdot_res, _nu, _r05) \
    _scalar_robustify(self,                            \
                      ((yaflEKFRobustSt *)self)->g,    \
                      ((yaflEKFRobustSt *)self)->gdot, \
                      _gdot_res, _nu, _r05)

/*---------------------------------------------------------------------------*/
yaflStatusEn \
    yafl_ekf_robust_bierman_update_scalar(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat gdot = 1.0;
    yaflFloat nu   = 0.0;
    yaflFloat r05;
    yaflFloat * h;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_BIERMAN_SELF_INTERNALS_CHECKS();

    r05 = _DR[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = _Y[i];

    YAFL_TRY(status, _SCALAR_ROBUSTIFY(self, &gdot, &nu, r05));

    h = _HY + _NX * i;
    /* f = h.dot(Up) */
#   define f _D
    YAFL_TRY(status, yafl_math_set_vtu(_NX, f, h, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#   define v h /*Don't need h any more, use it to store v*/
    YAFL_TRY(status, YAFL_MATH_SET_DV(_NX, v, _DP, f));

    YAFL_TRY(status, \
             _bierman_update_body(_NX, _X, _UP, _DP, f, v, r05 * r05, nu, \
                                  1.0, gdot));

#   undef v  /*Don't nee v any more*/
#   undef f

    return status;
}

/*=============================================================================
                            Robust Joseph filter
=============================================================================*/
yaflStatusEn \
    yafl_ekf_robust_joseph_update_scalar(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat gdot = 0.0;
    yaflFloat s    = 0.0;
    yaflFloat nu   = 0.0;
    yaflFloat r05;
    yaflFloat * h;
    yaflFloat * f;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();

    r05 = _DR[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = _Y[i];

    YAFL_TRY(status, _SCALAR_ROBUSTIFY(self, &gdot, &nu, r05));

    r05 *= r05;
#   define A2 r05 /*Now it is r = alpha**2 */

    h = _HY + _NX * i;
#   define v _D
    f = v + _NX;

    /* f = h.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(_NX, f, h, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(_NX, v, _DP, f));

    /* s = alpha**2 + gdot * f.dot(v)*/
    YAFL_TRY(status, yafl_math_vtv(_NX, &s, f, v));
    s = A2 + gdot * s;

    /*K = Up.dot(v/s) = Up.dot(v)/s*/
#   define K h /*Don't need h any more, use it to store K*/
    YAFL_TRY(status, \
             _joseph_update_body(_NX, _X, _UP, _DP, f, v, K, _W, nu, A2, s, \
                                 1.0, gdot));
#   undef K  /*Don't nee K any more*/
#   undef v
#   undef A2 /*Don't nee A2 any more*/

    return status;
}

/*=============================================================================
                        Adaptive robust Bierman filter
=============================================================================*/
yaflStatusEn \
    yafl_ekf_adaptive_robust_bierman_update_scalar(yaflKalmanBaseSt * self, \
                                                   yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat gdot = 1.0;
    yaflFloat ac   = 1.0;
    yaflFloat nu   = 0.0;
    yaflFloat r05;
    yaflFloat * h;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_BIERMAN_SELF_INTERNALS_CHECKS();

    r05 = _DR[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = _Y[i];

    YAFL_TRY(status, _SCALAR_ROBUSTIFY(self, &gdot, &nu, r05));

    r05 *= r05;
#   define A2 r05 /*Now it is r = alpha**2 */

    h = _HY + _NX * i;

    /* f = h.dot(Up) */
#   define f _D
    YAFL_TRY(status, yafl_math_set_vtu(_NX, f, h, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
#   define v h /*Don't need h any more, use it to store v*/
    YAFL_TRY(status, YAFL_MATH_SET_DV(_NX, v, _DP, f));

    YAFL_TRY(status, \
             _adaptive_correction(_NX, &ac, 0, f, v, A2, nu, gdot, \
                                  ((yaflEKFAdaptiveRobustSt *)self)->chi2));

    YAFL_TRY(status, _bierman_update_body(_NX, _X, _UP, _DP, f, v, A2, nu, \
                                          ac, gdot));
#   undef v  /*Don't nee v any more*/
#   undef f
#   undef A2 /*Don't nee A2 any more*/

    return status;
}

/*=============================================================================
                        Adaptive robust Joseph filter
=============================================================================*/
yaflStatusEn \
    yafl_ekf_adaptive_robust_joseph_update_scalar(yaflKalmanBaseSt * self, \
                                                  yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflFloat gdot = 1.0;
    yaflFloat   ac = 1.0;
    yaflFloat    s = 0.0;
    yaflFloat * h;
    yaflFloat * f;
    yaflFloat r05;
    yaflFloat nu;

    _SCALAR_UPDATE_ARGS_CHECKS();
    _EKF_JOSEPH_SELF_INTERNALS_CHECKS();

    r05 = _DR[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = _Y[i];

    YAFL_TRY(status, _SCALAR_ROBUSTIFY(self, &gdot, &nu, r05));

    r05 *= r05;
#   define A2 r05 /*Now it is r = alpha**2 */

    h = _HY + _NX * i;
#   define v _D
    f = v + _NX;

    /* f = h.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(_NX, f, h, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(_NX, v, _DP, f));

    YAFL_TRY(status, \
             _adaptive_correction(_NX, &ac, &s, f, v, A2, nu, gdot, \
                                  ((yaflEKFAdaptiveRobustSt *)self)->chi2));

    /* K = Up.dot(v * ac / s) = Up.dot(v) * (ac / s) */
#   define K h /*Don't need h any more, use it to store K*/
    YAFL_TRY(status, \
             _joseph_update_body(_NX, _X, _UP, _DP, f, v, K, _W, nu, A2, s, \
                                 ac, gdot));
#   undef K  /*Don't nee K any more*/
#   undef v
#   undef A2 /*Don't nee A2 any more*/

    return status;
}

/*------------------------------------------------------------------------------
                                 Undef EKF stuff
------------------------------------------------------------------------------*/
#undef _SCALAR_ROBUSTIFY

#undef _JFX
#undef _JHX

#undef _HY
#undef _W
#undef _D

/*=============================================================================
                    Basic UD-factorized UKF functions
=============================================================================*/
#define _KALMAN_SELF ((yaflKalmanBaseSt *)self)

#define _UFX  (_KALMAN_SELF->f)
#define _UHX  (_KALMAN_SELF->h)
#define _UZRF (_KALMAN_SELF->zrf)

#define _UX   (_KALMAN_SELF->x)
#define _UY   (_KALMAN_SELF->y)

#define _UUP  (_KALMAN_SELF->Up)
#define _UDP  (_KALMAN_SELF->Dp)

#define _UUQ  (_KALMAN_SELF->Uq)
#define _UDQ  (_KALMAN_SELF->Dq)

#define _UUR  (_KALMAN_SELF->Ur)
#define _UDR  (_KALMAN_SELF->Dr)

#define _UNX  (_KALMAN_SELF->Nx)
#define _UNZ  (_KALMAN_SELF->Nz)

/*UKF stuff*/
#define _XRF       (self->xrf)
#define _XMF       (self->xmf)

#define _ZMF       (self->zmf)

#define _ZP        (self->zp)

#define _SX        (self->Sx)

#define _PZX       (self->Pzx)

#define _SIGMAS_X  (self->sigmas_x)
#define _SIGMAS_Z  (self->sigmas_z)

#define _WM        (self->wm)
#define _WC        (self->wc)

/*---------------------------------------------------------------------------*/
static inline yaflStatusEn _compute_res(yaflKalmanBaseSt * self, yaflInt sz,    \
                                        yaflKalmanResFuncP rf, yaflFloat * res, \
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
        yaflKalmanFuncP    mf,  \
        yaflKalmanResFuncP rf)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt np;
    yaflInt i;
    yaflUKFSigmaSt * sp_info;

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

    YAFL_CHECK(_WM, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_WC, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np > 1, YAFL_ST_INV_ARG_1);
    np = sp_info->np;

    if (mf)
    {
        /*mf must be aware of the current transform details...*/
        YAFL_TRY(status, mf(_KALMAN_SELF, res_v, sigmas));
    }
    else
    {
        YAFL_TRY(status, yafl_math_set_vtm(np, res_sz, res_v, _WM, sigmas));
    }

    if (noise_u)
    {
        YAFL_CHECK(noise_d, YAFL_ST_INV_ARG_8);
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
        yaflFloat wci;

        YAFL_TRY(status, _compute_res(_KALMAN_SELF, res_sz, rf , sp, \
                                      sigmas + res_sz * i, res_v));
        /*Update res_u and res_d*/
        /*wc should be sorted in descending order*/
        wci = _WC[i];
        if (wci >= 0.0)
        {
            YAFL_TRY(status, \
                     yafl_math_udu_up(res_sz, res_u, res_d, wci, sp));
        }
        else
        {
            YAFL_TRY(status, \
                     yafl_math_udu_down(res_sz, res_u, res_d, -wci, sp));
        }
    }
    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ukf_base_predict(yaflUKFBaseSt * self)
{
    yaflStatusEn status = YAFL_ST_OK;

    yaflInt np;
    yaflInt i;
    yaflUKFSigmaSt * sp_info;

    /*Check some params and generate sigma points*/
    YAFL_TRY(status, yafl_ukf_gen_sigmas(self)); /*Self is checked here*/

    YAFL_CHECK(_UNX, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_SIGMAS_X, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np > 1, YAFL_ST_INV_ARG_1);
    np = sp_info->np;

    /*Compute process sigmas*/
    YAFL_CHECK(_UFX, YAFL_ST_INV_ARG_1);

    if (_UFX)
    {
        for (i = 0; i < np; i++)
        {
            yaflFloat * sigmai;
            sigmai = _SIGMAS_X + _UNX * i;
            YAFL_TRY(status, _UFX(_KALMAN_SELF, sigmai, sigmai));
        }
    }

    /*Predict x, Up, Dp*/
    YAFL_TRY(status, \
             _unscented_transform(self, _UNX, _UX, _UUP, _UDP, _SX, _SIGMAS_X, \
                                  _UUQ, _UDQ, _XMF, _XRF));
    return status;
}

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ukf_base_update(yaflUKFBaseSt * self, yaflFloat * z, \
                                  yaflKalmanScalarUpdateP scalar_update)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt np;
    yaflInt nx;
    yaflInt nz;
    yaflInt i;
    yaflUKFSigmaSt * sp_info; /*Sigma point generator info*/

    YAFL_CHECK(scalar_update, YAFL_ST_INV_ARG_3);

    YAFL_CHECK(self,     YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_UHX, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UX,  YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UY,  YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UUP, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UUR, YAFL_ST_INV_ARG_1);

    nx = _UNX;
    YAFL_CHECK(nx, YAFL_ST_INV_ARG_1);
    nz = _UNZ;
    YAFL_CHECK(nz, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_ZP,  YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_SX,  YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_PZX, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_SIGMAS_X, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_SIGMAS_Z, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_WC, YAFL_ST_INV_ARG_1);

    sp_info = self->sp_info;
    YAFL_CHECK(sp_info, YAFL_ST_INV_ARG_1);

    np = sp_info->np;
    YAFL_CHECK(np > 1, YAFL_ST_INV_ARG_1);

    /* Compute measurement sigmas */
    for (i = 0; i < np; i++)
    {
        YAFL_TRY(status, _UHX(_KALMAN_SELF, _SIGMAS_Z + nz * i, \
                              _SIGMAS_X + nx * i));
    }

    /* Compute zp*/
    if (_ZMF)
    {
        /*mf must be aware of the current transform details...*/
        YAFL_TRY(status, _ZMF(_KALMAN_SELF, _ZP, _SIGMAS_Z));
    }
    else
    {
        YAFL_TRY(status, yafl_math_set_vtm(np, nz, _ZP, _WM, _SIGMAS_Z));
    }

    /* Compute Pzx */
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF,  _UY, _SIGMAS_Z, _ZP));
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nx,  _XRF,  _SX, _SIGMAS_X, _UX));
    YAFL_TRY(status, yafl_math_set_vvtxn(nz, nx, _PZX, _UY, _SX, _WC[0]));

    for (i = 1; i < np; i++)
    {
        YAFL_TRY(status, _compute_res(_KALMAN_SELF,   nz, _UZRF, _UY, \
                                      _SIGMAS_Z + nz * i, _ZP));
        YAFL_TRY(status, _compute_res(_KALMAN_SELF, nx,  _XRF, _SX, \
                                      _SIGMAS_X + nx * i, _UX));
        YAFL_TRY(status, yafl_math_add_vvtxn(nz, nx, _PZX, _UY, _SX, _WC[i]));
    }

    /*Compute innovation*/
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF, _UY, z, _ZP));

    /* Decorrelate measurements*/
    YAFL_TRY(status, yafl_math_ruv(nz,      _UY, _UUR));
    YAFL_TRY(status, yafl_math_rum(nz, nx, _PZX, _UUR));

#   ifndef YAFL_USE_FAST_UKF
    for (i = 0; i < nz; i++)
    {
        yaflFloat * h;
        h = _PZX + nx * i;
        YAFL_TRY(status, yafl_math_ruv    (nx, h, _UUP));
        YAFL_TRY(status, YAFL_MATH_SET_RDV(nx, h, _UDP, h));
        YAFL_TRY(status, yafl_math_rutv   (nx, h, _UUP));
    }
#   endif/*YAFL_USE_FAST_UKF*/

    /*Now we can do scalar updates*/
    for (i = 0; i < nz; i++)
    {
#       ifdef YAFL_USE_FAST_UKF
        /*
        Compute v[i] for Bierman/Joseph style scalar updates:
        v[i] = linalg.inv(Up).dot(Pzx[i].T)

        It's cheaper than computing _unscented_transform.
        */
        YAFL_TRY(status, yafl_math_ruv(nx, _PZX + nx * i, _UUP));
#       endif/*YAFL_USE_FAST_UKF*/
        YAFL_TRY(status, scalar_update(_KALMAN_SELF, i));
    }
    return status;
}

/*=============================================================================
                                 Bierman UKF
=============================================================================*/
#define _UKF_SELF ((yaflUKFBaseSt *)self)

#define _UPZX (_UKF_SELF->Pzx)
#define _USX  (_UKF_SELF->Sx)

#define _UKF_BIERMAN_SELF_INTERNALS_CHECKS()  \
do {                                          \
    YAFL_CHECK(_NX > 1, YAFL_ST_INV_ARG_1);   \
    YAFL_CHECK(_UP,     YAFL_ST_INV_ARG_1);   \
    YAFL_CHECK(_DP,     YAFL_ST_INV_ARG_1);   \
    YAFL_CHECK(_UPZX,   YAFL_ST_INV_ARG_1);   \
    YAFL_CHECK(_USX,    YAFL_ST_INV_ARG_1);   \
    YAFL_CHECK(_Y,      YAFL_ST_INV_ARG_1);   \
    YAFL_CHECK(_DR,     YAFL_ST_INV_ARG_1);   \
} while (0)

/*---------------------------------------------------------------------------*/
yaflStatusEn yafl_ukf_bierman_update_scalar(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt nx;
    yaflFloat * v;

    _SCALAR_UPDATE_ARGS_CHECKS();

    nx = _NX;
    _UKF_BIERMAN_SELF_INTERNALS_CHECKS();

    v = _UPZX + nx * i;/*h is stored in v*/

#   define f _USX
#   ifdef YAFL_USE_FAST_UKF
    /* f = linalg.inv(Dp).dot(v)*/
    YAFL_TRY(status, YAFL_MATH_SET_RDV(nx, f, _DP, v));
#   else /*YAFL_USE_FAST_UKF*/
    /* f = h.T.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, v, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, _DP, f));
#   endif/*YAFL_USE_FAST_UKF*/

    YAFL_TRY(status, \
             _bierman_update_body(nx, _X, _UP, _DP, f, v, _DR[i], _Y[i], \
                                  1.0, 1.0));
#   undef f
    return status;
}

/*=============================================================================
                             Adaptive Bierman UKF
=============================================================================*/
yaflStatusEn yafl_ukf_adaptive_bierman_update_scalar(yaflKalmanBaseSt * self, yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt nx;
    yaflFloat * v;
    yaflFloat nu;
    yaflFloat r;
    yaflFloat ac;

    _SCALAR_UPDATE_ARGS_CHECKS();
    nx = _NX;
    _UKF_BIERMAN_SELF_INTERNALS_CHECKS();

    v = _UPZX + nx * i;/*h is stored in v*/

#   define f _USX
#   ifdef YAFL_USE_FAST_UKF
    /* f = linalg.inv(Dp).dot(v)*/
    YAFL_TRY(status, YAFL_MATH_SET_RDV(nx, f, _DP, v));
#   else /*YAFL_USE_FAST_UKF*/
    /* f = h.T.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, v, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, _DP, f));
#   endif/*YAFL_USE_FAST_UKF*/

    nu = self->y[i];
    r  = self->Dr[i];

    YAFL_TRY(status, \
             _adaptive_correction(nx, &ac, 0, f, v, r, nu, 1.0, \
                                  ((yaflUKFAdaptivedSt *)self)->chi2));

    YAFL_TRY(status, \
             _bierman_update_body(nx, _X, _UP, _DP, f, v, r, _Y[i], ac, 1.0));
#   undef f
    return status;
}

/*=============================================================================
                           Robust Bierman UKF
=============================================================================*/
#define _SCALAR_ROBUSTIFY(_self, _gdot_res, _nu, _r05) \
    _scalar_robustify(self,                            \
                      ((yaflUKFRobustSt *)self)->g,    \
                      ((yaflUKFRobustSt *)self)->gdot, \
                      _gdot_res, _nu, _r05)

yaflStatusEn yafl_ukf_robust_bierman_update_scalar(yaflKalmanBaseSt * self, \
                                                   yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt nx;
    yaflFloat gdot = 1.0;
    yaflFloat nu   = 0.0;
    yaflFloat r05;
    yaflFloat * v;

    _SCALAR_UPDATE_ARGS_CHECKS();

    nx = _NX;
    _UKF_BIERMAN_SELF_INTERNALS_CHECKS();

    r05 = _DR[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = _Y[i];

    YAFL_TRY(status, _SCALAR_ROBUSTIFY(self, &gdot, &nu, r05));

    v = _UPZX + nx * i;/*h is stored in v*/

#   define f _USX
#   ifdef YAFL_USE_FAST_UKF
    /* f = linalg.inv(Dp).dot(v)*/
    YAFL_TRY(status, YAFL_MATH_SET_RDV(nx, f, _DP, v));
#   else /*YAFL_USE_FAST_UKF*/
    /* f = h.T.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, v, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, _DP, f));
#   endif/*YAFL_USE_FAST_UKF*/

    YAFL_TRY(status, \
             _bierman_update_body(nx, _X, _UP, _DP, f, v, r05 * r05, nu, \
                                  1.0, gdot));
#   undef f
    return status;
}

/*=============================================================================
                         Adaptive robust Bierman UKF
=============================================================================*/
yaflStatusEn \
    yafl_ukf_adaptive_robust_bierman_update_scalar(yaflKalmanBaseSt * self, \
                                                   yaflInt i)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt nx;
    yaflFloat gdot = 1.0;
    yaflFloat ac   = 1.0;
    yaflFloat nu   = 0.0;
    yaflFloat r05;
    yaflFloat * v;

    _SCALAR_UPDATE_ARGS_CHECKS();

    nx = _NX;
    _UKF_BIERMAN_SELF_INTERNALS_CHECKS();

    r05 = _DR[i]; /* alpha = r**0.5 is stored in Dr*/
    nu  = _Y[i];

    YAFL_TRY(status, _SCALAR_ROBUSTIFY(self, &gdot, &nu, r05));

    r05 *= r05;
#   define A2 r05 /*Now it is r = alpha**2 */

    v = _UPZX + nx * i;/*h is stored in v*/

#   define f _USX
#   ifdef YAFL_USE_FAST_UKF
    /* f = linalg.inv(Dp).dot(v)*/
    YAFL_TRY(status, YAFL_MATH_SET_RDV(nx, f, _DP, v));
#   else /*YAFL_USE_FAST_UKF*/
    /* f = h.T.dot(Up) */
    YAFL_TRY(status, yafl_math_set_vtu(nx, f, v, _UP));

    /* v = f.dot(Dp).T = Dp.dot(f.T).T */
    YAFL_TRY(status, YAFL_MATH_SET_DV(nx, v, _DP, f));
#   endif/*YAFL_USE_FAST_UKF*/


    YAFL_TRY(status, \
             _adaptive_correction(nx, &ac, 0, f, v, A2, nu, gdot, \
                                  ((yaflUKFAdaptiveRobustSt *)self)->chi2));

    YAFL_TRY(status, \
             _bierman_update_body(nx, _X, _UP, _DP, f, v, A2, nu, ac, gdot));
#   undef f
#   undef A2 /*Don't nee A2 any more*/
    return status;
}

/*=============================================================================
            Full UKF, not a sequential square root version of UKF
=============================================================================*/
#define _UUS (((yaflUKFSt *)self)->Us)
#define _UDS (((yaflUKFSt *)self)->Ds)

yaflStatusEn yafl_ukf_update(yaflUKFBaseSt * self, yaflFloat * z)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt np;
    yaflInt nz;
    yaflInt nx;
    yaflInt i;
    yaflUKFSigmaSt * sp_info; /*Sigma point generator info*/
    yaflFloat * y;
    yaflFloat * ds;

    YAFL_CHECK(self, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UHX, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UX,  YAFL_ST_INV_ARG_1);

    y = _UY;
    YAFL_CHECK(y,  YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UUP, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UDP, YAFL_ST_INV_ARG_1);

    nx = _UNX;
    YAFL_CHECK(nx, YAFL_ST_INV_ARG_1);

    nz = _UNZ;
    YAFL_CHECK(nz, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_SIGMAS_X, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_SIGMAS_Z, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_WC, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np > 1, YAFL_ST_INV_ARG_1);
    np = sp_info->np;

    YAFL_CHECK(_PZX, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_SX, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_ZP, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_UUS, YAFL_ST_INV_ARG_1);

    ds = _UDS;
    YAFL_CHECK(ds, YAFL_ST_INV_ARG_1);

    /* Compute measurement sigmas */
    for (i = 0; i < np; i++)
    {
        YAFL_TRY(status, _UHX(_KALMAN_SELF, _SIGMAS_Z + nz * i, \
                              _SIGMAS_X + nx * i));
    }

    /* Compute zp, Us, Ds */
    YAFL_TRY(status, \
             _unscented_transform(self, nz, _ZP, _UUS, ds, y, _SIGMAS_Z, \
                                  _UUR, _UDR, _ZMF, _UZRF));

    /* Compute Pzx */
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF,   y, _SIGMAS_Z, _ZP));
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nx,  _XRF, _SX, _SIGMAS_X, _UX));
    YAFL_TRY(status, yafl_math_set_vvtxn(nz, nx, _PZX, y, _SX, _WC[0]));

    for (i = 1; i < np; i++)
    {
        YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz,  _UZRF, y, \
                                      _SIGMAS_Z + nz * i,  _ZP));
        YAFL_TRY(status, _compute_res(_KALMAN_SELF, nx, _XRF, _SX, \
                                      _SIGMAS_X + nx * i, _UX));
        YAFL_TRY(status, yafl_math_add_vvtxn(nz, nx, _PZX, y, _SX, _WC[i]));
    }

    /*Compute innovation*/
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF, y, z, _ZP));

    /* Decorrelate measurements*/
    YAFL_TRY(status, yafl_math_ruv(nz,        y, _UUS));
    YAFL_TRY(status, yafl_math_rum(nz, nx, _PZX, _UUS));

    /*Now we can do scalar updates*/
    for (i = 0; i < nz; i++)
    {
        yaflFloat * pzxi;
        pzxi = _PZX + nx * i;
        /*
        self.x += K * y[i]

        K * y[i] = Pzx[i].T / ds[i] * y[i] = Pzx[i].T * (y[i] / ds[i])

        self.x += Pzx[i].T * (y[i] / ds[i])
        */
        YAFL_TRY(status, yafl_math_add_vxn(nx, _UX, pzxi, y[i] / ds[i]));

        /*
        P -= K.dot(S.dot(K.T))
        K.dot(S.dot(K.T)) = (Pzx[i].T / ds[i] * ds[i]).outer(Pzx[i] / ds[i]))
        K.dot(S.dot(K.T)) = (Pzx[i].T).outer(Pzx[i]) / ds[i]
        P -= (Pzx[i].T).outer(Pzx[i]) / ds[i]
        Up, Dp = udu(P)
        */
        YAFL_TRY(status, yafl_math_udu_down(nx, _UUP, _UDP, 1.0 / ds[i], pzxi));
    }
    return status;
}

/*=============================================================================
       Full adaptive UKF, not a sequential square root version of UKF
=============================================================================*/
static inline yaflFloat _ukf_compute_md(yaflInt nz, yaflFloat * y, yaflFloat * ds)
{
    yaflInt i;
    yaflFloat md;

    YAFL_CHECK(y,  YAFL_ST_INV_ARG_2);
    YAFL_CHECK(ds, YAFL_ST_INV_ARG_3);

    md = 0;
    for (i = 0; i < nz; i++)
    {
        /* Since ds is positive definite we won't check values. */
        md += y[i] * y[i] / ds[i];
    }
    return md;
}

yaflStatusEn yafl_ukf_adaptive_update(yaflUKFBaseSt * self, yaflFloat * z)
{
    yaflStatusEn status = YAFL_ST_OK;
    yaflInt np;
    yaflInt nz;
    yaflInt nx;
    yaflInt i;
    yaflUKFSigmaSt * sp_info; /*Sigma point generator info*/
    yaflFloat delta;
    yaflFloat * y;
    yaflFloat * ds;

    YAFL_CHECK(self, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UHX, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UX,  YAFL_ST_INV_ARG_1);

    y = _UY;
    YAFL_CHECK(y,  YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UUP, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_UDP, YAFL_ST_INV_ARG_1);

    nx = _UNX;
    YAFL_CHECK(nx, YAFL_ST_INV_ARG_1);

    nz = _UNZ;
    YAFL_CHECK(nz, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_SIGMAS_X, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_SIGMAS_Z, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_WC, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    YAFL_CHECK(sp_info->np > 1, YAFL_ST_INV_ARG_1);
    np = sp_info->np;

    YAFL_CHECK(_PZX, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_SX, YAFL_ST_INV_ARG_1);
    YAFL_CHECK(_ZP, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_UUS, YAFL_ST_INV_ARG_1);
    ds = _UDS;
    YAFL_CHECK(ds, YAFL_ST_INV_ARG_1);

    /* Compute measurement sigmas */
    for (i = 0; i < np; i++)
    {
        YAFL_TRY(status, _UHX(_KALMAN_SELF, _SIGMAS_Z + nz * i, \
                              _SIGMAS_X + nx * i));
    }

    /* Compute zp, Us, Ds */
    YAFL_TRY(status, \
             _unscented_transform(self, nz, _ZP, _UUS, ds, y, _SIGMAS_Z, \
                                  _UUR, _UDR, _ZMF, _UZRF));


    /* Divergence test */
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF, y, z, _ZP));
    YAFL_TRY(status, yafl_math_ruv(nz, y, _UUS));
    delta = _ukf_compute_md(nz, y, ds);

#   define _CHI2 (((yaflUKFFullAdapiveSt *)self)->chi2)
    if (delta > _CHI2)
    {
        /* Adaptive correction */
        yaflFloat ac;

        /* Compute correction factor, we don't need old _ZP and _SIGMAS_Z now*/
        YAFL_TRY(status, \
                 _unscented_transform(self, nz, _ZP, _UUS, ds, y, _SIGMAS_Z, \
                                      0, 0, _ZMF, _UZRF));
        YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF, y, z, _ZP));
        YAFL_TRY(status, yafl_math_ruv(nz, y, _UUS));
        ac  = _ukf_compute_md(nz, y, ds);
        ac *= 1.0 / _CHI2 - 1.0 / delta;

        /* Correct _UDP*/
        YAFL_TRY(status, yafl_math_set_vxn(nx, _UDP, _UDP, 1.0 + ac));

        /* Generate new sigmas */
        YAFL_TRY(status, yafl_ukf_gen_sigmas(self));

        /* Now begin update with new _SIGMAS_X */
        /*  Recompute measurement sigmas */
        for (i = 0; i < np; i++)
        {
            YAFL_TRY(status, _UHX(_KALMAN_SELF, _SIGMAS_Z + nz * i, \
                              _SIGMAS_X + nx * i));
        }

        /*  Recompute zp, Us, Ds */
        YAFL_TRY(status, \
                 _unscented_transform(self, nz, _ZP, _UUS, ds, y, _SIGMAS_Z, \
                                      _UUR, _UDR, _ZMF, _UZRF));
        /*  Recompute innovation*/
        YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF, y, z, _ZP));

        /*  Decorrelate measurements part 1*/
        YAFL_TRY(status, yafl_math_ruv(nz, y, _UUS));
    }
#   undef _CHI2

    /* Compute Pzx */
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF,   y, _SIGMAS_Z, _ZP));
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nx,  _XRF, _SX, _SIGMAS_X, _UX));
    YAFL_TRY(status, yafl_math_set_vvtxn(nz, nx, _PZX, y, _SX, _WC[0]));

    for (i = 1; i < np; i++)
    {
        YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz,  _UZRF, y, \
                                      _SIGMAS_Z + nz * i,  _ZP));
        YAFL_TRY(status, _compute_res(_KALMAN_SELF, nx, _XRF, _SX, \
                                      _SIGMAS_X + nx * i, _UX));
        YAFL_TRY(status, yafl_math_add_vvtxn(nz, nx, _PZX, y, _SX, _WC[i]));
    }

    /*Compute innovation*/
    YAFL_TRY(status, _compute_res(_KALMAN_SELF, nz, _UZRF, y, z, _ZP));

    /* Decorrelate measurements*/
    YAFL_TRY(status, yafl_math_ruv(nz, y, _UUS));
    YAFL_TRY(status, yafl_math_rum(nz, nx, _PZX, _UUS));


    /*Now we can do scalar updates*/
    for (i = 0; i < nz; i++)
    {
        yaflFloat * pzxi;
        pzxi = _PZX + nx * i;
        /*
        self.x += K * y[i]

        K * y[i] = Pzx[i].T / ds[i] * y[i] = Pzx[i].T * (y[i] / ds[i])

        self.x += Pzx[i].T * (y[i] / ds[i])
        */
        YAFL_TRY(status, yafl_math_add_vxn(nx, _UX, pzxi, y[i] / ds[i]));

        /*
        P -= K.dot(S.dot(K.T))
        K.dot(S.dot(K.T)) = (Pzx[i].T / ds[i] * ds[i]).outer(Pzx[i] / ds[i]))
        K.dot(S.dot(K.T)) = (Pzx[i].T).outer(Pzx[i]) / ds[i]
        P -= (Pzx[i].T).outer(Pzx[i]) / ds[i]
        Up, Dp = udu(P)
        */
        YAFL_TRY(status, yafl_math_udu_down(nx, _UUP, _UDP, 1.0 / ds[i], pzxi));
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
    nx = _UNX;
    YAFL_CHECK(_UNX, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_WM, YAFL_ST_INV_ARG_1);
    wm = _WM;

    YAFL_CHECK(_WC, YAFL_ST_INV_ARG_1);
    wc = _WC;

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
    nx = _UNX;
    YAFL_CHECK(nx, YAFL_ST_INV_ARG_1);

    x = _UX;
    YAFL_CHECK(x, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(_UUP, YAFL_ST_INV_ARG_1);

    dp = _UDP;
    YAFL_CHECK(dp, YAFL_ST_INV_ARG_1);

    sigmas_x = _SIGMAS_X;
    YAFL_CHECK(sigmas_x, YAFL_ST_INV_ARG_1);

    YAFL_CHECK(self->sp_info, YAFL_ST_INV_ARG_1);
    sp_info = self->sp_info;

    alpha = ((yaflUKFMerweSt *)sp_info)->alpha;
    lambda_p_n = alpha * alpha * (nx + ((yaflUKFMerweSt *)sp_info)->kappa);

    YAFL_TRY(status, yafl_math_bset_ut(nx, sigmas_x, nx, _UUP));
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

/*=============================================================================
                          Undef UKF stuff
=============================================================================*/
#undef _KALMAN_SELF

#undef _UFX
#undef _UHX
#undef _UZRF

#undef _UX
#undef _UY

#undef _UUP
#undef _UDP

#undef _UUQ
#undef _UDQ

#undef _UUR
#undef _UDR

#undef _UNX
#undef _UNZ

/*----------------------------------------------------------------------------*/
#undef _UKF_SELF

#undef _UPZX
#undef _USX

/*----------------------------------------------------------------------------*/
#undef _SCALAR_ROBUSTIFY

/*----------------------------------------------------------------------------*/
#undef _UUS
#undef _UDS
/*=============================================================================
                          Undef Kalman filter stuff
=============================================================================*/
#undef _FX
#undef _HX
#undef _ZRF

#undef _X
#undef _Y

#undef _UP
#undef _DP

#undef _UQ
#undef _DQ

#undef _UR
#undef _DR

#undef _NX
#undef _NZ
