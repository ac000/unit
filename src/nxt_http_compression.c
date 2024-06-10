/*
 *
 */

#include <nxt_auto_config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <nxt_router.h>
#include <nxt_http.h>
#include <nxt_tstr.h>
#include <nxt_conf.h>
#include <nxt_http_compression.h>

#define NXT_COMP_LEVEL_UNSET               INT8_MIN

typedef enum nxt_http_comp_scheme_e        nxt_http_comp_scheme_t;
typedef struct nxt_http_comp_type_s        nxt_http_comp_type_t;
typedef struct nxt_http_comp_opts_s        nxt_http_comp_opts_t;
typedef struct nxt_http_comp_compressor_s  nxt_http_comp_compressor_t;
typedef struct nxt_http_comp_ctx_s         nxt_http_comp_ctx_t;

enum nxt_http_comp_scheme_e {
    NXT_HTTP_COMP_SCHEME_IDENTITY = 0,
#if NXT_HAVE_ZLIB
    NXT_HTTP_COMP_SCHEME_DEFLATE,
    NXT_HTTP_COMP_SCHEME_GZIP,
#endif
#if NXT_HAVE_ZSTD
    NXT_HTTP_COMP_SCHEME_ZSTD,
#endif
#if NXT_HAVE_BROTLI
    NXT_HTTP_COMP_SCHEME_BROTLI,
#endif

    /* keep last */
    NXT_HTTP_COMP_SCHEME_UNKNOWN
};
#define NXT_NR_COMPRESSORS  NXT_HTTP_COMP_SCHEME_UNKNOWN

struct nxt_http_comp_type_s {
    nxt_str_t                         token;
    nxt_http_comp_scheme_t            scheme;
    int8_t                            def_compr;

    const nxt_http_comp_operations_t  *cops;
};

struct nxt_http_comp_opts_s {
    int8_t                      level;
    nxt_off_t                   min_len;
};

struct nxt_http_comp_compressor_s {
    const nxt_http_comp_type_t  *type;
    nxt_http_comp_opts_t        opts;
};

struct nxt_http_comp_ctx_s {
    nxt_uint_t idx;

    nxt_off_t resp_clen;

    nxt_http_comp_compressor_ctx_t ctx;
};

static nxt_tstr_t                  *accept_encoding_query;
static nxt_http_route_rule_t       *mime_types_rule;
static nxt_http_comp_compressor_t  *enabled_compressors;
static nxt_uint_t                  nr_enabled_compressors;

static nxt_thread_declare_data(nxt_http_comp_ctx_t, compressor_ctx);

static const nxt_conf_map_t  compressors_opts_map[] = {
    {
        nxt_string("level"),
        NXT_CONF_MAP_INT,
        offsetof(nxt_http_comp_opts_t, level),
    }, {
        nxt_string("min_length"),
        NXT_CONF_MAP_SIZE,
        offsetof(nxt_http_comp_opts_t, min_len),
    },
};

static const nxt_http_comp_type_t  compressors[] = {
    /* Keep this first */
    {
        .token      = nxt_string("identity"),
        .scheme     = NXT_HTTP_COMP_SCHEME_IDENTITY,
    },
#if NXT_HAVE_ZLIB
    {
        .token      = nxt_string("deflate"),
        .scheme     = NXT_HTTP_COMP_SCHEME_DEFLATE,
        .def_compr  = NXT_HTTP_COMP_ZLIB_DEFAULT_LEVEL,
        .cops       = &nxt_comp_deflate_ops,
    }, {
        .token      = nxt_string("gzip"),
        .scheme     = NXT_HTTP_COMP_SCHEME_GZIP,
        .def_compr  = NXT_HTTP_COMP_ZLIB_DEFAULT_LEVEL,
        .cops       = &nxt_comp_gzip_ops,
    },
#endif
#if NXT_HAVE_ZSTD
    {
        .token      = nxt_string("zstd"),
        .scheme     = NXT_HTTP_COMP_SCHEME_ZSTD,
        .def_compr  = NXT_HTTP_COMP_ZSTD_DEFAULT_LEVEL,
        .cops       = &nxt_comp_zstd_ops,
    },
#endif
#if NXT_HAVE_BROTLI
    {
        .token      = nxt_string("br"),
        .scheme     = NXT_HTTP_COMP_SCHEME_BROTLI,
        .def_compr  = NXT_HTTP_COMP_BROTLI_DEFAULT_LEVEL,
        .cops       = &nxt_comp_brotli_ops,
    },
#endif
};

static void print_compressor(const nxt_http_comp_compressor_t *c)
{
    printf("token    : %s\n", c->type->token.start);
    printf("scheme   : %d\n", c->type->scheme);
    printf("level    : %d\n", c->opts.level);
    printf("min_len  : %ld\n", c->opts.min_len);
}

static void print_comp_config(size_t n)
{
    for (size_t i = 0; i < n; i++) {
        nxt_http_comp_compressor_t *compr = enabled_compressors + i;

        print_compressor(compr);
        printf("\n");
    }
}

bool
nxt_http_comp_wants_compression(void)
{
    printf("%s: compression [%s]\n", __func__,
           compressor_ctx.idx > 0 ? "true" : "false");
    return compressor_ctx.idx;
}

size_t
nxt_http_comp_bound(size_t size)
{
    nxt_http_comp_compressor_t        *compressor;
    const nxt_http_comp_operations_t  *cops;
    const nxt_http_comp_ctx_t *ctx = &compressor_ctx;

    compressor = &enabled_compressors[ctx->idx];
    cops = compressor->type->cops;

    return cops->bound(&ctx->ctx, size);
}

ssize_t
nxt_http_comp_compress(uint8_t *dst, size_t dst_size, const uint8_t *src,
                       size_t src_size, bool last)
{
    nxt_http_comp_compressor_t        *compressor;
    const nxt_http_comp_operations_t  *cops;
    const nxt_http_comp_ctx_t *ctx = &compressor_ctx;

    compressor = &enabled_compressors[ctx->idx];
    cops = compressor->type->cops;

    return cops->deflate(&ctx->ctx, src, src_size, dst, dst_size, last);
}

static nxt_uint_t
nxt_http_comp_compressor_lookup_enabled(const nxt_str_t *token)
{
    if (token->start[0] == '*') {
        return NXT_HTTP_COMP_SCHEME_IDENTITY;
    }

    for (size_t i = 0, n = nr_enabled_compressors; i < n; i++) {
        if (nxt_strstr_eq(token, &enabled_compressors[i].type->token)) {
            return i;
        }
    }

    return NXT_HTTP_COMP_SCHEME_UNKNOWN;
}

/*
 * We need to parse the 'Accept-Encoding` header as described by
 * <https://www.rfc-editor.org/rfc/rfc9110.html#field.accept-encoding>
 * which can take forms such as
 *
 *  Accept-Encoding: compress, gzip
 *  Accept-Encoding:
 *  Accept-Encoding: *
 *  Accept-Encoding: compress;q=0.5, gzip;q=1.0
 *  Accept-Encoding: gzip;q=1.0, identity;q=0.5, *;q=0
 *
 *  '*:q=0' means if the content being served has no 'Content-Coding'
 *  matching an 'Accept-Encoding' entry then don't send any response.
 *
 * 'indentity;q=0' seems to basically mean the same thing...
 */
static nxt_int_t
nxt_http_comp_select_compressor(const nxt_str_t *token)
{
    bool identity_allowed = true;
    char *str;
    char *saveptr;
    double weight = 0.0;
    nxt_int_t idx = 0;

    str = strndup((char *)token->start, token->length);

    for ( ; ; str = NULL) {
        char *tkn, *qptr;
        double qval = -1.0;
        nxt_uint_t ecidx;
        nxt_str_t enc;
        nxt_http_comp_scheme_t scheme;

        tkn = strtok_r(str, ", ", &saveptr);
        if (tkn == NULL) {
            break;
        }

        qptr = strstr(tkn, ";q=");
        if (qptr != NULL) {
            qval = atof(qptr + 3);
        }

        enc.start = (u_char *)tkn;
        enc.length = qptr != NULL ? (size_t)(qptr - tkn) : strlen(tkn);

        ecidx = nxt_http_comp_compressor_lookup_enabled(&enc);
        if (ecidx == NXT_HTTP_COMP_SCHEME_UNKNOWN) {
            continue;
        }

        scheme = enabled_compressors[ecidx].type->scheme;

        printf("%s: %.*s [%f] [%d:%d]\n", __func__, (int)enc.length, enc.start,
               qval, ecidx, scheme);

        if (qval == 0.0 && scheme == NXT_HTTP_COMP_SCHEME_IDENTITY) {
            identity_allowed = false;
        }

        if (qval != -1.0 && (qval == 0.0 || qval < weight)) {
            continue;
        }

        idx = ecidx;
        weight = qval;
    }

    free(str);

    printf("%s: Selected compressor : %s\n", __func__,
           enabled_compressors[idx].type->token.start);

    printf("%s: idx [%u], identity_allowed [%s]\n", __func__, idx,
           identity_allowed ? "true" : "false");

    if (idx == NXT_HTTP_COMP_SCHEME_IDENTITY && !identity_allowed) {
        return -1;
    }

    return idx;
}

static nxt_int_t
nxt_http_comp_set_header(nxt_http_request_t *r, nxt_uint_t comp_idx)
{
    const nxt_str_t *token;
    nxt_http_field_t *f;

    static const nxt_str_t  content_encoding_str =
                                    nxt_string("Content-Encoding");

    f = nxt_list_add(r->resp.fields);
    if (nxt_slow_path(f == NULL)) {
        return NXT_ERROR;
    }

    token = &enabled_compressors[comp_idx].type->token;

    *f = (nxt_http_field_t){ };

    f->name = content_encoding_str.start;
    f->name_length = content_encoding_str.length;
    f->value = token->start;
    f->value_length = token->length;

    return NXT_OK;
}

static bool
nxt_http_comp_is_resp_content_encoded(const nxt_http_request_t *r)
{
    nxt_http_field_t *f;

    printf("%s: \n", __func__);

    nxt_list_each(f, r->resp.fields) {
        printf("%s: %s: %s\n", __func__, f->name, f->value);
        if (nxt_strcasecmp(f->name, (const u_char *)"Content-Encoding") == 0) {
            return true;
        }
    } nxt_list_loop;

    return false;
}

nxt_int_t
nxt_http_comp_check_compression(nxt_task_t *task, nxt_http_request_t *r)
{
    int8_t level;
    nxt_str_t mime_type = { };
    nxt_int_t ret, idx;
    nxt_off_t min_len;
    nxt_str_t accept_encoding;
    nxt_router_conf_t *rtcf;
    nxt_http_comp_compressor_t *compressor;

    printf("%s: \n", __func__);

    compressor_ctx = (nxt_http_comp_ctx_t){ .resp_clen = -1 };

    if (nr_enabled_compressors == 0) {
        return NXT_OK;
    }

    if (r->resp.content_length_n == 0) {
        return NXT_OK;
    }

    if (r->resp.mime_type != NULL) {
        mime_type = *r->resp.mime_type;
    } else if (r->resp.content_type != NULL) {
        mime_type.start = r->resp.content_type->value;
        mime_type.length = r->resp.content_type->value_length;
    }

    if (mime_type.start == NULL) {
        return NXT_OK;
    }

    printf("%s: Response Content-Type [%.*s]\n", __func__,
           (int)mime_type.length, mime_type.start);

    if (mime_types_rule != NULL) {
        ret = nxt_http_route_test_rule(r, mime_types_rule,
                                       mime_type.start,
                                       mime_type.length);
        printf("%s: mime_type : %d (%.*s)\n", __func__, ret,
               (int)mime_type.length, mime_type.start);

        if (ret == 0) {
            return NXT_OK;
        }
    }

    rtcf = r->conf->socket_conf->router_conf;

    if (nxt_http_comp_is_resp_content_encoded(r)) {
        return NXT_OK;
    }

    /* XXX Should checking the Accept-Encoding header come first? */
    ret = nxt_tstr_query_init(&r->tstr_query, rtcf->tstr_state, &r->tstr_cache,
                              r, r->mem_pool);
    if (nxt_slow_path(ret == NXT_ERROR)) {
        return NXT_ERROR;
    }

    ret = nxt_tstr_query(task, r->tstr_query, accept_encoding_query,
                         &accept_encoding);
    if (nxt_slow_path(ret != NXT_OK)) {
        return NXT_ERROR;
    }

    printf("%s: Accept-Encoding: %s\n", __func__, accept_encoding.start);

    idx = nxt_http_comp_select_compressor(&accept_encoding);
    if (idx == -1) {
        /*
         * XXX Needs to trigger the right HTTP error status code
         * for unsuppported media or something...
         */
        return NXT_ERROR;
    }

    if (idx == NXT_HTTP_COMP_SCHEME_IDENTITY) {
        return NXT_OK;
    }

    compressor = &enabled_compressors[idx];

    if (r->resp.content_length_n > -1) {
        compressor_ctx.resp_clen = r->resp.content_length_n;
    } else if (r->resp.content_length != NULL) {
        compressor_ctx.resp_clen =
                    strtol((char *)r->resp.content_length->value, NULL, 10);
    }

    min_len = compressor->opts.min_len;

    printf("%s: content_length [%ld] min_len [%ld]\n", __func__,
           compressor_ctx.resp_clen, min_len);
    if (compressor_ctx.resp_clen > -1 && compressor_ctx.resp_clen < min_len) {
        printf("%s: %ld < %ld [skipping/clen]\n", __func__,
               compressor_ctx.resp_clen , min_len);
        return NXT_OK;
    }

    nxt_http_comp_set_header(r, idx);

    compressor_ctx.idx = idx;

    level = enabled_compressors[idx].opts.level;
    compressor_ctx.ctx.level = level == NXT_COMP_LEVEL_UNSET ?
                            enabled_compressors[idx].type->def_compr : level;

    compressor->type->cops->init(&compressor_ctx.ctx);

    return NXT_OK;
}

static nxt_uint_t
nxt_http_comp_compressor_token2idx(const nxt_str_t *token)
{
    for (nxt_uint_t i = 0, n = nxt_nitems(compressors); i < n; i++) {
        if (nxt_strstr_eq(token, &compressors[i].token)) {
            return i;
        }
    }

    return NXT_HTTP_COMP_SCHEME_UNKNOWN;
}

bool
nxt_http_comp_compressor_is_valid(const nxt_str_t *token)
{
    nxt_uint_t  idx;

    idx = nxt_http_comp_compressor_token2idx(token);
    if (idx != NXT_HTTP_COMP_SCHEME_UNKNOWN) {
        return true;
    }

    return false;
}

static nxt_int_t
nxt_http_comp_set_compressor(nxt_router_conf_t *rtcf,
                             const nxt_conf_value_t *comp, nxt_uint_t index)
{
    nxt_int_t               ret;
    nxt_str_t               token;
    nxt_uint_t              cidx;
    nxt_conf_value_t        *obj;

    static const nxt_str_t  token_str = nxt_string("encoding");

    printf("%s: \n", __func__);

    obj = nxt_conf_get_object_member(comp, &token_str, NULL);
    if (obj == NULL) {
        return NXT_ERROR;
    }

    nxt_conf_get_string(obj, &token);
    cidx = nxt_http_comp_compressor_token2idx(&token);

    enabled_compressors[index].type = &compressors[cidx];
    enabled_compressors[index].opts.level = NXT_COMP_LEVEL_UNSET;
    enabled_compressors[index].opts.min_len = -1;
    printf("%s: %s\n", __func__, enabled_compressors[index].type->token.start);

    ret = nxt_conf_map_object(rtcf->mem_pool, comp, compressors_opts_map,
                              nxt_nitems(compressors_opts_map),
                              &enabled_compressors[index].opts);
    if (nxt_slow_path(ret == NXT_ERROR)) {
        return NXT_ERROR;
    }

    return NXT_OK;
}

nxt_int_t
nxt_http_comp_compression_init(nxt_task_t *task, nxt_router_conf_t *rtcf,
                               const nxt_conf_value_t *comp_conf)
{
    nxt_int_t         ret;
    nxt_uint_t        n = 1;  /* 'identity' */
    nxt_conf_value_t  *comps, *mimes;

    static const nxt_str_t  accept_enc_str =
                                    nxt_string("$header_accept_encoding");
    static const nxt_str_t  comps_str = nxt_string("compressors");
    static const nxt_str_t  mimes_str = nxt_string("types");

    printf("%s: \n", __func__);

    mimes = nxt_conf_get_object_member(comp_conf, &mimes_str, NULL);
    if (mimes != NULL) {
        mime_types_rule = nxt_http_route_types_rule_create(task,
                                                           rtcf->mem_pool,
                                                           mimes);
        if (nxt_slow_path(mime_types_rule == NULL)) {
            return NXT_ERROR;
        }
    }

    accept_encoding_query = nxt_tstr_compile(rtcf->tstr_state, &accept_enc_str,
                                             NXT_TSTR_STRZ);
    if (nxt_slow_path(accept_encoding_query == NULL)) {
        return NXT_ERROR;
    }

    comps = nxt_conf_get_object_member(comp_conf, &comps_str, NULL);
    if (nxt_slow_path(comps == NULL)) {
        return NXT_ERROR;
    }

    if (nxt_conf_type(comps) == NXT_CONF_OBJECT) {
        n++;
    } else {
        n += nxt_conf_object_members_count(comps);
    }
    nr_enabled_compressors = n;

    enabled_compressors = nxt_mp_zalloc(rtcf->mem_pool,
                                        sizeof(nxt_http_comp_compressor_t) * n);

    enabled_compressors[0] =
        (nxt_http_comp_compressor_t){ .type = &compressors[0],
                                      .opts.level = NXT_COMP_LEVEL_UNSET,
                                      .opts.min_len = -1 };

    if (nxt_conf_type(comps) == NXT_CONF_OBJECT) {
        /* XXX Remove me... */
        print_comp_config(nr_enabled_compressors);
        return nxt_http_comp_set_compressor(rtcf, comps, 1);
    }

    for (nxt_uint_t i = 1; i < n; i++) {
        nxt_conf_value_t  *obj;

        obj = nxt_conf_get_array_element(comps, i - 1);
        ret = nxt_http_comp_set_compressor(rtcf, obj, i);
        if (ret == NXT_ERROR) {
            return NXT_ERROR;
        }
    }

    nr_enabled_compressors = n;
    /* XXX Remove me... */
    print_comp_config(nr_enabled_compressors);

    return NXT_OK;
}
