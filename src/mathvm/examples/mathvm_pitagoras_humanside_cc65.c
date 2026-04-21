/*
 * Human-side cc65 example for Picocomputer 6502 MATHVM.
 *
 * The caller provides ordinary decimal strings for the triangle legs:
 *   a = "3.2"
 *   b = "4.3"
 *
 * The example hides the decimal-string -> float32 conversion required by
 * MATHVM and computes:
 *   sqrt(a*a + b*b)
 */

#include "mathvm_client.h"
#include <stdio.h>

int main(void)
{
    const char *a = "3.2";
    const char *b = "4.3";
    mx_client_result_t call;
    mx_word_t out[1];
    char hyp_text[16];

    call = mx_client_pitagoras_i16(a, b, out);

    printf("a=%s b=%s\n", a, b);
    printf("MATHVM status=%u words=%u\n", call.status, call.out_words);

    if (call.status == MX_OK && call.out_words == 1u)
    {
        mx_client_format_f32(hyp_text, sizeof(hyp_text), out[0]);
        printf("hypotenuse=%s\n", hyp_text);
        printf("hypotenuse bits=%08lx\n", (unsigned long)out[0].u32);
        puts("expected approximately: 5.3600");
    }

    return 0;
}
