/*
 * 卡端：N 体引力模拟（重计算示例）。
 *   直接求和 O(N^2) + 蛙跳积分 + OpenMP；初始条件由卡端按确定性公式自行生成，
 *   与宿主参考实现逐位一致（因此不需要 COI 缓冲区）。
 *
 * 注意（KNC 实测约束）：
 *   本文件必须用 -O0 编译：-O1 与 -O2 都会生成掩码压缩存储（vpackstorelpd），
 *   而 Knights Corner 执行该指令会出错（卡端内核日志表现为 segfault at 0）。
 *   跑通优先，优化其次；要开优化需先确认 objdump 里 vpackstore/vscatter 为 0 条。
 *
 * 构建：k1om-cxx -O0 -fopenmp -rdynamic -I<sysroot>/usr/include nbody_sink.cpp \
 *          -L<sysroot>/usr/lib64 -lcoi_device -L<libgomp 目录> -lgomp -lpthread -ldl -lrt \
 *          -Wl,-rpath,/tmp -o nbody_sink
 */
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <omp.h>

#include <intel-coi/sink/COIPipeline_sink.h>
#include <intel-coi/sink/COIProcess_sink.h>
#include <intel-coi/common/COIMacros_common.h>

struct Params {
    int    n;
    int    steps;
    double dt;
    double soft2;
    int    report;
};

struct Result {
    int    n;
    int    steps;
    int    threads;
    double e0, e1;
    double rel_energy_err;
    double checksum;
    double secs;
    double gflops;
};

/* 位置与速度一律用「分离数组」，避免 x[3i+1] 这类交错别名引起的优化歧义 */
static void accel(int n, const double *x, const double *y, const double *z,
                  const double *m, double soft2,
                  double *ax, double *ay, double *az)
{
    #pragma omp parallel for schedule(dynamic, 16)
    for (int i = 0; i < n; i++) {
        double axi = 0.0, ayi = 0.0, azi = 0.0;
        const double xi = x[i], yi = y[i], zi = z[i];
        for (int j = 0; j < n; j++) {
            if (j == i) continue;
            const double dx = x[j] - xi, dy = y[j] - yi, dz = z[j] - zi;
            const double r2 = dx * dx + dy * dy + dz * dz + soft2;
            const double inv = m[j] / (r2 * sqrt(r2));
            axi += dx * inv; ayi += dy * inv; azi += dz * inv;
        }
        ax[i] = axi; ay[i] = ayi; az[i] = azi;
    }
}

static double energy(int n, const double *x, const double *y, const double *z,
                     const double *vx, const double *vy, const double *vz,
                     const double *m, double soft2, double *kin_out)
{
    double kin = 0.0, pot = 0.0;
    #pragma omp parallel for reduction(+:kin) schedule(dynamic, 16)
    for (int i = 0; i < n; i++)
        kin += 0.5 * m[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            const double dx = x[j] - x[i], dy = y[j] - y[i], dz = z[j] - z[i];
            pot -= m[i] * m[j] / sqrt(dx * dx + dy * dy + dz * dz + soft2);
        }
    if (kin_out) *kin_out = kin;
    return kin + pot;
}

COINATIVELIBEXPORT
void NBodyRun(uint32_t        in_BufferCount,
              void          **in_ppBufferPointers,
              uint64_t       *in_pBufferLengths,
              void           *in_pMiscData,
              uint16_t        in_MiscDataLength,
              void           *in_pReturnValue,
              uint16_t        in_ReturnValueLength)
{
    (void)in_BufferCount; (void)in_ppBufferPointers; (void)in_pBufferLengths;

    Result *r = (Result *)in_pReturnValue;
    if (!r || in_ReturnValueLength < sizeof(*r)) return;
    memset(r, 0, sizeof(*r));
    if (!in_pMiscData || in_MiscDataLength < sizeof(Params)) return;

    Params p;
    memcpy(&p, in_pMiscData, sizeof(p));
    const int n = p.n;
    r->n = n; r->steps = p.steps;
    if (n <= 0) return;

    double *x  = (double *)calloc((size_t)n, sizeof(double));
    double *y  = (double *)calloc((size_t)n, sizeof(double));
    double *z  = (double *)calloc((size_t)n, sizeof(double));
    double *vx = (double *)calloc((size_t)n, sizeof(double));
    double *vy = (double *)calloc((size_t)n, sizeof(double));
    double *vz = (double *)calloc((size_t)n, sizeof(double));
    double *m  = (double *)calloc((size_t)n, sizeof(double));
    double *ax = (double *)calloc((size_t)n, sizeof(double));
    double *ay = (double *)calloc((size_t)n, sizeof(double));
    double *az = (double *)calloc((size_t)n, sizeof(double));
    if (!x || !y || !z || !vx || !vy || !vz || !m || !ax || !ay || !az) {
        free(x); free(y); free(z); free(vx); free(vy); free(vz);
        free(m); free(ax); free(ay); free(az);
        return;
    }

    /* 初始条件：与宿主参考实现同一公式（顺序一致 → 逐位可对） */
    {
        int side = 1;
        while ((double)side * side * side < (double)n) side++;
        const double inv = 1.0 / (double)side;
        double px = 0.0, py = 0.0, pz = 0.0;
        for (int i = 0; i < n; i++) {
            const int ix = i % side, iy = (i / side) % side, iz = i / (side * side);
            const double jitter = 0.01 * sin(12.9898 * (double)i + 78.233);
            x[i]  = (ix + 0.5) * inv + jitter * inv;
            y[i]  = (iy + 0.5) * inv + jitter * inv;
            z[i]  = (iz + 0.5) * inv + jitter * inv;
            vx[i] = 0.1  * sin(4.1 * (double)i);
            vy[i] = 0.1  * cos(2.7 * (double)i);
            vz[i] = 0.05 * sin(1.3 * (double)i + 1.0);
            m[i]  = 1.0 / (double)n;
            px += vx[i]; py += vy[i]; pz += vz[i];
        }
        for (int i = 0; i < n; i++) {
            vx[i] -= px / (double)n;
            vy[i] -= py / (double)n;
            vz[i] -= pz / (double)n;
        }
    }

    double k0 = 0.0;
    r->e0 = energy(n, x, y, z, vx, vy, vz, m, p.soft2, &k0);

    const double t0 = omp_get_wtime();
    for (int s = 0; s < p.steps; s++) {
        accel(n, x, y, z, m, p.soft2, ax, ay, az);
        for (int i = 0; i < n; i++) {
            vx[i] += ax[i] * p.dt; vy[i] += ay[i] * p.dt; vz[i] += az[i] * p.dt;
            x[i]  += vx[i] * p.dt; y[i]  += vy[i] * p.dt; z[i]  += vz[i] * p.dt;
        }
    }
    const double t1 = omp_get_wtime();

    double k1 = 0.0;
    r->e1 = energy(n, x, y, z, vx, vy, vz, m, p.soft2, &k1);
    r->rel_energy_err = (r->e0 != 0.0) ? fabs((r->e1 - r->e0) / r->e0) : 0.0;
    r->secs = t1 - t0;
    r->threads = omp_get_max_threads();

    double cs = 0.0;
    for (int i = 0; i < n; i++)
        cs += fabs(x[i]) + 2.0 * fabs(y[i]) + 3.0 * fabs(z[i]);
    r->checksum = cs;

    const double pairs = (double)n * (double)(n - 1) * 0.5;
    r->gflops = (p.steps > 0 && r->secs > 0.0) ? (20.0 * pairs * p.steps / r->secs / 1e9) : 0.0;

    if (p.report)
        printf("NBodyRun: n=%d steps=%d threads=%d E0=%.6e E1=%.6e rel=%.3e cs=%.9f %.3f s %.2f GFLOPS\n",
               n, p.steps, r->threads, r->e0, r->e1, r->rel_energy_err, r->checksum, r->secs, r->gflops);
    fflush(stdout);

    free(x); free(y); free(z); free(vx); free(vy); free(vz);
    free(m); free(ax); free(ay); free(az);
}

int main(int, char **)
{
    COIPipelineStartExecutingRunFunctions();
    COIProcessWaitForShutdown();
    return 0;
}
