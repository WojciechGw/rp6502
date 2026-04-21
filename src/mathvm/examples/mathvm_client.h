/*
 * Standalone copy for examples.
 * Keep in sync with ../mathvm_client.h as needed by on-target callers.
 */

#ifndef _MATHVM_EXAMPLES_MATHVM_CLIENT_H_
#define _MATHVM_EXAMPLES_MATHVM_CLIENT_H_

#include "mathvm.h"

/* Holds the status code returned by OS $80 and the number of 32-bit words
 * produced by the call. The actual payload is returned separately through
 * the output buffer or through XRAM for batch kernels. */
typedef struct
{
    uint8_t status;
    uint8_t out_words;
} mx_client_result_t;

/* Describes one 3D point using ordinary signed integer coordinates.
 * This is the public, caller-friendly input type used by the cc65-side
 * helpers before the client converts values to float32 for MATHVM. */
typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} mx_vec3i_t;

/* Describes one projected 2D point returned by a batch projection kernel.
 * Each coordinate is a signed 16-bit screen-space value unpacked from the
 * 32-bit records written by MATHVM to XRAM. */
typedef struct
{
    int16_t x;
    int16_t y;
} mx_point2i_t;

/* Describes a batch MATHVM call at the frame level.
 * The descriptor mirrors the binary MATHVM frame layout: execution flags,
 * optional XRAM ranges, batch element count, local words, bytecode program,
 * expected direct output words, and VM stack size. */
typedef struct
{
    uint8_t flags;
    uint16_t xram_in;
    uint16_t xram_out;
    uint16_t count;
    const mx_word_t *locals;
    uint8_t local_words;
    const uint8_t *program;
    uint8_t prog_len;
    uint8_t out_words;
    uint8_t stack_words;
} mx_client_batch_desc_t;

/* Executes one already-built MATHVM frame through OS $80.
 * The function pushes the frame to XSTACK, waits for RIA completion, and
 * copies any direct result words back into `out` when capacity is sufficient. */
mx_client_result_t mx_client_call_frame(const uint8_t *frame,
                                        uint16_t frame_len,
                                        mx_word_t *out,
                                        uint8_t out_cap_words);

/* Builds a binary frame from a high-level batch descriptor and executes it.
 * This is the generic entry point for batch kernels that use XRAM-backed
 * input and/or output but still share the same frame ABI as scalar kernels. */
mx_client_result_t mx_client_call_batch(const mx_client_batch_desc_t *desc,
                                        mx_word_t *out,
                                        uint8_t out_cap_words);

/* Executes the fixed M3V3L kernel for one mat3 x vec3 multiply.
 * Inputs are provided as 12 float32 words in locals and the function returns
 * the 3-word float32 result directly through XSTACK. */
mx_client_result_t mx_client_m3v3l(const mx_word_t mat3[9],
                                   const mx_word_t vec3[3],
                                   mx_word_t out[3]);

/* Executes the SPR2L kernel in bounding-box mode.
 * The result is four float32 words: min_x, min_y, max_x, max_y. */
mx_client_result_t mx_client_spr2l_bbox(const mx_word_t affine2x3[6],
                                        const mx_word_t sprite[4],
                                        mx_word_t out[4]);

/* Executes the SPR2L kernel in four-corners mode.
 * The result is eight float32 words representing the transformed sprite
 * corners in the order produced by the firmware kernel. */
mx_client_result_t mx_client_spr2l_corners(const mx_word_t affine2x3[6],
                                           const mx_word_t sprite[4],
                                           mx_word_t out[8]);

/* Writes an array of integer vec3 values to XRAM as packed float32 triples.
 * Each input component is converted to an exact float32 integer value and
 * stored as one 12-byte record expected by MX_M3V3P2X. */
void mx_client_xram_write_vec3i_array(uint16_t xram_addr,
                                      const mx_vec3i_t *vecs,
                                      uint16_t count);

/* Reads an array of projected 2D points from XRAM.
 * Each record is interpreted as one packed 32-bit value containing int16 x in
 * the low half and int16 y in the high half. */
void mx_client_xram_read_point2i_array(uint16_t xram_addr,
                                       mx_point2i_t *points,
                                       uint16_t count);

/* Executes the MX_M3V3P2X batch kernel using already prepared float32 locals.
 * `mat3` contains the transform matrix, `camera` contains perspective and
 * screen-center parameters, and points flow through XRAM in/out buffers. */
mx_client_result_t mx_client_m3v3p2x(const mx_word_t mat3[9],
                                     const mx_word_t camera[3],
                                     uint16_t xram_in,
                                     uint16_t xram_out,
                                     uint16_t count);

/* Executes MX_M3V3P2X from a matrix expressed in Q8.8 fixed-point form.
 * The helper converts the matrix and camera parameters to float32 words and
 * then dispatches the regular batch kernel. */
mx_client_result_t mx_client_m3v3p2x_q8_8(const int16_t mat3_q8_8[9],
                                          int16_t persp_d,
                                          int16_t screen_cx,
                                          int16_t screen_cy,
                                          uint16_t xram_in,
                                          uint16_t xram_out,
                                          uint16_t count);

/* Executes MX_M3V3P2X using a built-in "rotate around Y, then tilt by 30°"
 * camera model. The caller supplies only the angle and projection settings,
 * while the helper constructs the matrix internally in Q8.8 form. */
mx_client_result_t mx_client_m3v3p2x_yrot30(int angle_deg,
                                            int16_t persp_d,
                                            int16_t screen_cx,
                                            int16_t screen_cy,
                                            uint16_t xram_in,
                                            uint16_t xram_out,
                                            uint16_t count);

/* High-level helper for the common cc65 batch-projection path.
 * It uploads integer vec3 input points to XRAM, runs the standard Y-rotation
 * batch projection kernel, and reads back packed int16 screen points. */
mx_client_result_t mx_client_project_vec3i_batch_yrot30(int angle_deg,
                                                        int16_t persp_d,
                                                        int16_t screen_cx,
                                                        int16_t screen_cy,
                                                        uint16_t xram_base,
                                                        const mx_vec3i_t *vecs,
                                                        mx_point2i_t *points,
                                                        uint16_t count);

/* Convenience helper for a human-side Pythagorean example.
 * The caller supplies decimal strings such as "3.2" and "4.3"; the helper
 * parses them without using cc65 floating point, converts them to float32
 * words for MATHVM, and returns one float32 hypotenuse result. */
mx_client_result_t mx_client_pitagoras_i16(const char *a_text,
                                           const char *b_text,
                                           mx_word_t out[1]);

/* Converts raw float32 bits to a signed Q16.16 approximation.
 * This is useful for presenting MATHVM float32 results in a human-readable
 * decimal form without requiring runtime floating point on cc65. */
int32_t mx_client_f32_to_q16_16(uint32_t bits);

/* Formats one signed Q16.16 value as a decimal string with four fractional
 * digits. The output is always NUL-terminated when `buffer_size` is non-zero. */
void mx_client_format_q16_16(char *buffer, uint8_t buffer_size, int32_t value);

/* Formats one raw float32 result word as a decimal string with four fractional
 * digits by first converting it to Q16.16. */
void mx_client_format_f32(char *buffer, uint8_t buffer_size, mx_word_t value);

#endif /* _MATHVM_EXAMPLES_MATHVM_CLIENT_H_ */
