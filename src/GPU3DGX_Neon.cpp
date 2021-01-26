#include "types.h"

namespace GPU3D
{

void MatrixMult4x4(s32* m, s32* s)
{
    __asm__ volatile
    (
        "ld1 {v0.4s, v1.4s, v2.4s, v3.4s}, [%[m]]\n"
        "ld1 {v4.4s, v5.4s, v6.4s, v7.4s}, [%[s]]\n"

        "smull v16.2d, v0.2s, v4.4s[0]\n"
        "smull2 v17.2d, v0.4s, v4.4s[0]\n"
        "smull v18.2d, v0.2s, v5.4s[0]\n"
        "smull2 v19.2d, v0.4s, v5.4s[0]\n"
        "smull v20.2d, v0.2s, v6.4s[0]\n"
        "smull2 v21.2d, v0.4s, v6.4s[0]\n"
        "smull v22.2d, v0.2s, v7.4s[0]\n"
        "smull2 v23.2d, v0.4s, v7.4s[0]\n"

        "smlal v16.2d, v1.2s, v4.4s[1]\n"
        "smlal2 v17.2d, v1.4s, v4.4s[1]\n"
        "smlal v18.2d, v1.2s, v5.4s[1]\n"
        "smlal2 v19.2d, v1.4s, v5.4s[1]\n"
        "smlal v20.2d, v1.2s, v6.4s[1]\n"
        "smlal2 v21.2d, v1.4s, v6.4s[1]\n"
        "smlal v22.2d, v1.2s, v7.4s[1]\n"
        "smlal2 v23.2d, v1.4s, v7.4s[1]\n"

        "smlal v16.2d, v2.2s, v4.4s[2]\n"
        "smlal2 v17.2d, v2.4s, v4.4s[2]\n"
        "smlal v18.2d, v2.2s, v5.4s[2]\n"
        "smlal2 v19.2d, v2.4s, v5.4s[2]\n"
        "smlal v20.2d, v2.2s, v6.4s[2]\n"
        "smlal2 v21.2d, v2.4s, v6.4s[2]\n"
        "smlal v22.2d, v2.2s, v7.4s[2]\n"
        "smlal2 v23.2d, v2.4s, v7.4s[2]\n"

        "smlal v16.2d, v3.2s, v4.4s[3]\n"
        "smlal2 v17.2d, v3.4s, v4.4s[3]\n"
        "smlal v18.2d, v3.2s, v5.4s[3]\n"
        "smlal2 v19.2d, v3.4s, v5.4s[3]\n"
        "smlal v20.2d, v3.2s, v6.4s[3]\n"
        "smlal2 v21.2d, v3.4s, v6.4s[3]\n"
        "smlal v22.2d, v3.2s, v7.4s[3]\n"
        "smlal2 v23.2d, v3.4s, v7.4s[3]\n"

        "shrn v0.2s, v16.2d, #12\n"
        "shrn v1.2s, v18.2d, #12\n"
        "shrn v2.2s, v20.2d, #12\n"
        "shrn v3.2s, v22.2d, #12\n"
        "shrn2 v0.4s, v17.2d, #12\n"
        "shrn2 v1.4s, v19.2d, #12\n"
        "shrn2 v2.4s, v21.2d, #12\n"
        "shrn2 v3.4s, v23.2d, #12\n"

        "st1 {v0.4s, v1.4s, v2.4s, v3.4s}, [%[m]]\n"
        : "+m" (m)
        : [m] "r" (m), [s] "r" (s)
        : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q16", "q17", "q18", "q19", "q20", "q21", "q22", "q23"
    );
}

void MatrixMult4x3(s32* m, s32* s)
{
    __asm__ volatile
    (
        "ld1 {v0.4s, v1.4s, v2.4s, v3.4s}, [%[m]]\n"
        "ld1 {v4.4s, v5.4s, v6.4s}, [%[s]]\n"
        // we could also use shifting for this one
        // but the combined latency of shift + add, is comparable to smlal
        "movi v7.4s, #0x10, lsl #8\n"

        "smull v16.2d, v0.2s, v4.4s[0]\n"
        "smull2 v17.2d, v0.4s, v4.4s[0]\n"
        "smull v18.2d, v0.2s, v4.4s[3]\n"
        "smull2 v19.2d, v0.4s, v4.4s[3]\n"
        "smull v20.2d, v0.2s, v5.4s[2]\n"
        "smull2 v21.2d, v0.4s, v5.4s[2]\n"
        "smull v22.2d, v0.2s, v6.4s[1]\n"
        "smull2 v23.2d, v0.4s, v6.4s[1]\n"

        "smlal v16.2d, v1.2s, v4.4s[1]\n"
        "smlal2 v17.2d, v1.4s, v4.4s[1]\n"
        "smlal v18.2d, v1.2s, v5.4s[0]\n"
        "smlal2 v19.2d, v1.4s, v5.4s[0]\n"
        "smlal v20.2d, v1.2s, v5.4s[3]\n"
        "smlal2 v21.2d, v1.4s, v5.4s[3]\n"
        "smlal v22.2d, v1.2s, v6.4s[2]\n"
        "smlal2 v23.2d, v1.4s, v6.4s[2]\n"

        "smlal v16.2d, v2.2s, v4.4s[2]\n"
        "smlal2 v17.2d, v2.4s, v4.4s[2]\n"
        "smlal v18.2d, v2.2s, v5.4s[1]\n"
        "smlal2 v19.2d, v2.4s, v5.4s[1]\n"
        "smlal v20.2d, v2.2s, v6.4s[0]\n"
        "smlal2 v21.2d, v2.4s, v6.4s[0]\n"
        "smlal v22.2d, v2.2s, v6.4s[3]\n"
        "smlal2 v23.2d, v2.4s, v6.4s[3]\n"

        "smlal v22.2d, v3.2s, v7.4s[0]\n"
        "smlal2 v23.2d, v3.4s, v7.4s[0]\n"

        "shrn v0.2s, v16.2d, #12\n"
        "shrn v1.2s, v18.2d, #12\n"
        "shrn v2.2s, v20.2d, #12\n"
        "shrn v3.2s, v22.2d, #12\n"
        "shrn2 v0.4s, v17.2d, #12\n"
        "shrn2 v1.4s, v19.2d, #12\n"
        "shrn2 v2.4s, v21.2d, #12\n"
        "shrn2 v3.4s, v23.2d, #12\n"

        "st1 {v0.4s, v1.4s, v2.4s, v3.4s}, [%[m]]\n"
        : "+m" (m)
        : [m] "r" (m), [s] "r" (s)
        : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q16", "q17", "q18", "q19", "q20", "q21", "q22", "q23"
    );
}

void MatrixMult3x3(s32* m, s32* s)
{
    __asm__ volatile
    (
        "ld1 {v0.4s, v1.4s, v2.4s}, [%[m]]\n"
        "ld1 {v3.4s, v4.4s, v5.4s}, [%[s]]\n"

        "smull v6.2d, v0.2s, v3.4s[0]\n"
        "smull2 v7.2d, v0.4s, v3.4s[0]\n"
        "smull v16.2d, v0.2s, v3.4s[3]\n"
        "smull2 v17.2d, v0.4s, v3.4s[3]\n"
        "smull v18.2d, v0.2s, v4.4s[2]\n"
        "smull2 v19.2d, v0.4s, v4.4s[2]\n"

        "smlal v6.2d, v1.2s, v3.4s[1]\n"
        "smlal2 v7.2d, v1.4s, v3.4s[1]\n"
        "smlal v16.2d, v1.2s, v4.4s[0]\n"
        "smlal2 v17.2d, v1.4s, v4.4s[0]\n"
        "smlal v18.2d, v1.2s, v4.4s[3]\n"
        "smlal2 v19.2d, v1.4s, v4.4s[3]\n"

        "smlal v6.2d, v2.2s, v3.4s[2]\n"
        "smlal2 v7.2d, v2.4s, v3.4s[2]\n"
        "smlal v16.2d, v2.2s, v4.4s[1]\n"
        "smlal2 v17.2d, v2.4s, v4.4s[1]\n"
        "smlal v18.2d, v2.2s, v5.4s[0]\n"
        "smlal2 v19.2d, v2.4s, v5.4s[0]\n"

        "shrn v0.2s, v6.2d, #12\n"
        "shrn v1.2s, v16.2d, #12\n"
        "shrn v2.2s, v18.2d, #12\n"
        "shrn2 v0.4s, v7.2d, #12\n"
        "shrn2 v1.4s, v17.2d, #12\n"
        "shrn2 v2.4s, v19.2d, #12\n"

        "st1 {v0.4s, v1.4s, v2.4s}, [%[m]]\n"
        : "+m" (m)
        : [m] "r" (m), [s] "r" (s)
        : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q16", "q17", "q18", "q19"
    );
}

void MatrixScale(s32* m, s32* s)
{
    // assumes s has a length of atleast 4
    __asm__ volatile
    (
        "ld1 {v0.16b, v1.16b, v2.16b}, [%[m]]\n"
        "ld1 {v3.16b}, [%[s]]\n"

        "smull v5.2d, v0.2s, v3.4s[0]\n"
        "smull2 v6.2d, v0.4s, v3.4s[0]\n"

        "shrn v0.2s, v5.2d, #12\n"
        "shrn2 v0.4s, v6.2d, #12\n"

        "smull v5.2d, v1.2s, v3.4s[1]\n"
        "smull2 v6.2d, v1.4s, v3.4s[1]\n"

        "shrn v1.2s, v5.2d, #12\n"
        "shrn2 v1.4s, v6.2d, #12\n"

        "smull v5.2d, v2.2s, v3.4s[2]\n"
        "smull2 v6.2d, v2.4s, v3.4s[2]\n"

        "shrn v2.2s, v5.2d, #12\n"
        "shrn2 v2.4s, v6.2d, #12\n"

        "st1 {v0.16b, v1.16b, v2.16b}, [%[m]]\n"

        : "+m" (m)
        : [m] "r" (m), [s] "r" (s)
        : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7"
    );
}

void MatrixTranslate(s32* m, s32* s)
{
    // assumes s has a length of atleast 4
    __asm__ volatile
    (
        "ld1 {v0.16b, v1.16b, v2.16b, v3.16b}, [%[m]]\n"
        "ld1 {v4.16b}, [%[s]]\n"

        "smull v5.2d, v0.2s, v4.4s[0]\n"
        "smull2 v6.2d, v0.4s, v4.4s[0]\n"
        
        "smlal v5.2d, v1.2s, v4.4s[1]\n"
        "smlal2 v6.2d, v1.4s, v4.4s[1]\n"

        "smlal v5.2d, v2.2s, v4.4s[2]\n"
        "smlal2 v6.2d, v2.4s, v4.4s[2]\n"

        "shrn v5.2s, v5.2d, #12\n"
        "shrn2 v5.4s, v6.2d, #12\n"

        "add v3.4s, v3.4s, v5.4s\n"

        "st1 {v3.4s}, [%[mLastCol]]\n"
        : "+m" (m)
        : [m] "r" (m), [mLastCol] "r" (m + 4*3), [s] "r" (s)
        : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7"
    );
}

}