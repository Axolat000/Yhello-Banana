/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS SDK math library: the rest of C99 <math.h> (what C++'s <cmath>
 * names): inverse hyperbolics, erfc, gamma, fma, nextafter, remainder...,
 * the float versions of everything, and the long double versions computed
 * in double precision. Portable C like math.c (no libgcc on i686). */
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>

typedef union { double d; uint64_t u; } dbits_t;

double acosh(double x) { return x < 1 ? NAN : log(x + sqrt(x * x - 1)); }
double asinh(double x) {
    double a = fabs(x);
    double r = a < 1e-8 ? a : log(a + sqrt(a * a + 1));
    return x < 0 ? -r : r;
}
double atanh(double x) {
    if (x <= -1 || x >= 1) return x == 1 ? INFINITY : x == -1 ? -INFINITY : NAN;
    return fabs(x) < 1e-8 ? x : 0.5 * log1p(2 * x / (1 - x));
}

/* erfc: 1 - erf, with a continued fraction for large x (no cancellation) */
double erfc(double x) {
    if (x < 2) return 1 - erf(x);
    if (x > 27) return 0;
    double f = x;                                   /* Lentz: x + 1/2/(x + 1/(x + 3/2/(x + ...))) */
    for (int k = 60; k >= 1; k--) f = x + (k * 0.5) / f;
    return exp(-x * x) / (f * 1.7724538509055160273);
}

double fdim(double x, double y) { return isnan(x) || isnan(y) ? NAN : x > y ? x - y : 0; }
double fma(double x, double y, double z) { return (double)((long double)x * y + z); }

int ilogb(double x) {
    if (x == 0) return FP_ILOGB0;
    if (isnan(x)) return FP_ILOGBNAN;
    if (isinf(x)) return 2147483647;
    int e;
    frexp(x, &e);
    return e - 1;
}
double logb(double x) {
    if (x == 0) return -INFINITY;
    if (isinf(x)) return INFINITY;
    if (isnan(x)) return x;
    return (double)ilogb(x);
}

/* gamma: Lanczos (g = 7, 9 terms), reflection below 0.5 */
static const double g_lanczos[9] = {
    0.99999999999980993, 676.5203681218851, -1259.1392167224028, 771.32342877765313,
    -176.61502916214059, 12.507343278686905, -0.13857109526572012, 9.9843695780195716e-6,
    1.5056327351493116e-7,
};
static double lanczos_log(double x) {               /* log(gamma(x)), x >= 0.5 */
    x -= 1;
    double a = g_lanczos[0], t = x + 7.5;
    for (int i = 1; i < 9; i++) a += g_lanczos[i] / (x + i);
    return 0.91893853320467274178 + (x + 0.5) * log(t) - t + log(a);
}
double tgamma(double x) {
    if (isnan(x)) return x;
    if (x == 0) return copysign(INFINITY, x);
    if (x < 0 && x == floor(x)) return NAN;
    if (x < 0.5) return 3.14159265358979323846 / (sin(3.14159265358979323846 * x) * tgamma(1 - x));
    if (x > 171.7) return INFINITY;
    return exp(lanczos_log(x));
}
int signgam;
double lgamma(double x) {
    if (isnan(x)) return x;
    if (x <= 0 && x == floor(x)) return INFINITY;
    if (x < 0.5) {
        double s = sin(3.14159265358979323846 * x);
        signgam = s < 0 ? -1 : 1;
        return log(3.14159265358979323846 / fabs(s)) - lgamma(1 - x);
    }
    signgam = 1;
    return lanczos_log(x);
}

double nan(const char* tag) { (void)tag; return NAN; }

double nextafter(double x, double y) {
    if (isnan(x) || isnan(y)) return x + y;
    if (x == y) return y;
    dbits_t b = { x };
    if (x == 0) { b.u = 1; return y > 0 ? b.d : -b.d; }
    if ((x < y) == (x > 0)) b.u++; else b.u--;
    return b.d;
}
double nexttoward(double x, long double y) { return nextafter(x, (double)y); }

double remquo(double x, double y, int* q) {
    if (y == 0 || isnan(x) || isnan(y) || isinf(x)) { *q = 0; return NAN; }
    double n = rint(x / y);
    double r = x - n * y;
    long long k = llrint(fmod(fabs(n), 8.0));
    *q = (int)(n < 0 ? -k : k);
    return r;
}
double remainder(double x, double y) { int q; return remquo(x, y, &q); }
double scalbln(double x, long n) { return scalbn(x, n > 100000 ? 100000 : n < -100000 ? -100000 : (int)n); }

/* float */
float acoshf(float x) { return (float)acosh(x); }
float asinhf(float x) { return (float)asinh(x); }
float atanhf(float x) { return (float)atanh(x); }
float erff(float x) { return (float)erf(x); }
float erfcf(float x) { return (float)erfc(x); }
float expm1f(float x) { return (float)expm1(x); }
float fdimf(float x, float y) { return (float)fdim(x, y); }
float fmaf(float x, float y, float z) { return (float)((double)x * y + z); }
int   ilogbf(float x) { return ilogb(x); }
float lgammaf(float x) { return (float)lgamma(x); }
float tgammaf(float x) { return (float)tgamma(x); }
float log1pf(float x) { return (float)log1p(x); }
float logbf(float x) { return (float)logb(x); }
long  lroundf(float x) { return lround(x); }
long long llroundf(float x) { return llround(x); }
float modff(float x, float* ip) { double i; float r = (float)modf(x, &i); *ip = (float)i; return r; }
float nanf(const char* tag) { (void)tag; return NAN; }
float nearbyintf(float x) { return (float)nearbyint(x); }
float nextafterf(float x, float y) {
    if (isnan(x) || isnan(y)) return x + y;
    if (x == y) return y;
    union { float f; uint32_t u; } b = { x };
    if (x == 0) { b.u = 1; return y > 0 ? b.f : -b.f; }
    if ((x < y) == (x > 0)) b.u++; else b.u--;
    return b.f;
}
float nexttowardf(float x, long double y) { return nextafterf(x, (float)y); }
float remainderf(float x, float y) { return (float)remainder(x, y); }
float remquof(float x, float y, int* q) { return (float)remquo(x, y, q); }
float scalbnf(float x, int n) { return (float)scalbn(x, n); }
float scalblnf(float x, long n) { return (float)scalbln(x, n); }

/* long double, in double precision */
#define L1(name, f) long double name(long double x) { return f((double)x); }
#define L2(name, f) long double name(long double x, long double y) { return f((double)x, (double)y); }
L1(fabsl, fabs) L1(sqrtl, sqrt) L1(cbrtl, cbrt) L1(floorl, floor) L1(ceill, ceil) L1(truncl, trunc)
L1(roundl, round) L1(rintl, rint) L1(nearbyintl, nearbyint) L1(logbl, logb) L1(expl, exp) L1(exp2l, exp2)
L1(expm1l, expm1) L1(logl, log) L1(log2l, log2) L1(log10l, log10) L1(log1pl, log1p) L1(sinl, sin)
L1(cosl, cos) L1(tanl, tan) L1(asinl, asin) L1(acosl, acos) L1(atanl, atan) L1(sinhl, sinh) L1(coshl, cosh)
L1(tanhl, tanh) L1(asinhl, asinh) L1(acoshl, acosh) L1(atanhl, atanh) L1(erfl, erf) L1(erfcl, erfc)
L1(lgammal, lgamma) L1(tgammal, tgamma)
L2(fmodl, fmod) L2(remainderl, remainder) L2(powl, pow) L2(hypotl, hypot) L2(atan2l, atan2)
L2(copysignl, copysign) L2(fdiml, fdim) L2(fmaxl, fmax) L2(fminl, fmin) L2(nextafterl, nextafter)
L2(nexttowardl, nextafter)
long lroundl(long double x) { return lround((double)x); }
long long llroundl(long double x) { return llround((double)x); }
long lrintl(long double x) { return lrint((double)x); }
long long llrintl(long double x) { return llrint((double)x); }
long double remquol(long double x, long double y, int* q) { return remquo((double)x, (double)y, q); }
long double modfl(long double x, long double* ip) { double i; double r = modf((double)x, &i); *ip = i; return r; }
long double frexpl(long double x, int* e) { return frexp((double)x, e); }
long double scalbnl(long double x, int n) { return scalbn((double)x, n); }
long double scalblnl(long double x, long n) { return scalbln((double)x, n); }
int ilogbl(long double x) { return ilogb((double)x); }
long double fmal(long double x, long double y, long double z) { return x * y + z; }
long double nanl(const char* tag) { (void)tag; return NAN; }

/* <inttypes.h> */
intmax_t imaxabs(intmax_t x) { return x < 0 ? -x : x; }
imaxdiv_t imaxdiv(intmax_t a, intmax_t b) { imaxdiv_t r = { a / b, a % b }; return r; }

/* sine and cosine at once (GNU) */
void sincos(double x, double* s, double* c) { *s = sin(x); *c = cos(x); }
void sincosf(float x, float* s, float* c) { *s = sinf(x); *c = cosf(x); }
void sincosl(long double x, long double* s, long double* c) { *s = sin((double)x); *c = cos((double)x); }
