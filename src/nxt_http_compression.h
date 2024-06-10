/*
 * Copyright (C) Andrew Clayton
 * Copyright (C) F5, Inc.
 */

#ifndef _NXT_COMPRESSION_H_INCLUDED_
#define _NXT_COMPRESSION_H_INCLUDED_

#include <nxt_auto_config.h>

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if NXT_HAVE_ZLIB
#include <zlib.h>
#endif

#if NXT_HAVE_ZSTD
#include <zstd.h>
#endif

#if NXT_HAVE_BROTLI
#include <brotli/encode.h>
#endif

#include <nxt_main.h>
#include <nxt_router.h>
#include <nxt_string.h>
#include <nxt_conf.h>

#if NXT_HAVE_ZLIB
#define NXT_HTTP_COMP_ZLIB_DEFAULT_LEVEL       Z_DEFAULT_COMPRESSION
#endif
#if NXT_HAVE_ZSTD
#define NXT_HTTP_COMP_ZSTD_DEFAULT_LEVEL       ZSTD_CLEVEL_DEFAULT
#endif
#if NXT_HAVE_BROTLI
#define NXT_HTTP_COMP_BROTLI_DEFAULT_LEVEL     BROTLI_DEFAULT_QUALITY
#endif

typedef struct nxt_http_comp_compressor_ctx_s  nxt_http_comp_compressor_ctx_t;
typedef struct nxt_http_comp_operations_s      nxt_http_comp_operations_t;

struct nxt_http_comp_compressor_ctx_s {
    int8_t level;

    union {
#if NXT_HAVE_ZLIB
        z_stream zlib_ctx;
#endif
#if NXT_HAVE_ZSTD
        ZSTD_CStream *zstd_ctx;
#endif
#if NXT_HAVE_BROTLI
        BrotliEncoderState *brotli_ctx;
#endif
    };
};

struct nxt_http_comp_operations_s {
    void     (*init)(nxt_http_comp_compressor_ctx_t *ctx);
    size_t   (*bound)(const nxt_http_comp_compressor_ctx_t *ctx,
                      size_t in_len);
    ssize_t  (*deflate)(const nxt_http_comp_compressor_ctx_t *ctx,
                        const uint8_t *in_buf, size_t in_len,
                        uint8_t *out_buf, size_t out_len, bool last);
    void     (*free_ctx)(const nxt_http_comp_compressor_ctx_t *ctx);
};

#if NXT_HAVE_ZLIB
extern const nxt_http_comp_operations_t  nxt_comp_deflate_ops;
extern const nxt_http_comp_operations_t  nxt_comp_gzip_ops;
#endif

#if NXT_HAVE_ZSTD
extern const nxt_http_comp_operations_t  nxt_comp_zstd_ops;
#endif

#if NXT_HAVE_BROTLI
extern const nxt_http_comp_operations_t  nxt_comp_brotli_ops;
#endif

extern bool nxt_http_comp_wants_compression(void);
extern size_t nxt_http_comp_bound(size_t size);
extern ssize_t nxt_http_comp_compress(uint8_t *dst, size_t dst_size,
    const uint8_t *src, size_t src_size, bool last);
extern bool nxt_http_comp_compressor_is_valid(const nxt_str_t *token);
extern nxt_int_t nxt_http_comp_check_compression(nxt_task_t *task,
    nxt_http_request_t *r);
extern nxt_int_t nxt_http_comp_compression_init(nxt_task_t *task,
    nxt_router_conf_t *rtcf, const nxt_conf_value_t *comp_conf);

#endif  /* _NXT_COMPRESSION_H_INCLUDED_ */