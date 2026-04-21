/*
 * Float-friendly llvm-mos example for Picocomputer 6502 MATHVM.
 *
 * The caller uses ordinary float values and receives an ordinary float result:
 *   sqrt(3.2f * 3.2f + 4.3f * 4.3f)
 *
 * This example demonstrates the intended high-level API style for a toolchain
 * that supports float well enough on the caller side.
 */

#include "mathvm/mathvm_client_llvmmos.h"
#include <stdio.h>

int main(void)
{
    const float a = 3.2f;
    const float b = 4.3f;
    float hypotenuse = 0.0f;
    mx_client_result_t call;

    call = mx_client_pitagoras_f32(a, b, &hypotenuse);

    printf("a=%.4f b=%.4f\n", (double)a, (double)b);
    printf("MATHVM status=%u words=%u\n", call.status, call.out_words);

    if (call.status == MX_OK && call.out_words == 1u)
    {
        printf("hypotenuse=%.4f\n", (double)hypotenuse);
        printf("hypotenuse bits=%08lx\n",
               (unsigned long)mx_client_llvmmos_word_from_f32(hypotenuse).u32);
        puts("expected approximately: 5.3600");
    }

    return 0;
}
