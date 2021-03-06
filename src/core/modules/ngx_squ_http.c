
/*
 * Copyright (C) Ngwsx
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_squ.h>


typedef struct ngx_squ_http_cleanup_ctx_s  ngx_squ_http_cleanup_ctx_t;


typedef struct {
    ngx_url_t                      u;
    ngx_str_t                      method;
    ngx_str_t                      version;
    ngx_array_t                    headers;
    ngx_str_t                      body;
    ngx_msec_t                     connect_timeout;
    ngx_msec_t                     send_timeout;
    ngx_msec_t                     read_timeout;
    ngx_pool_t                    *pool;
    ngx_peer_connection_t          peer;
    ngx_buf_t                     *request;
    ngx_buf_t                     *response;
    ngx_buf_t                     *header;
    u_char                        *body_start;
    ngx_int_t                      rc;
    ngx_uint_t                     not_event;
    ngx_squ_thread_t              *thr;
    ngx_squ_http_cleanup_ctx_t    *cln_ctx;

    ngx_uint_t                     step;
    ngx_uint_t                     state;

    /* used to parse HTTP response */

    ngx_uint_t                     status_code;
    ngx_uint_t                     status_count;
    u_char                        *status_start;
    u_char                        *status_end;

    off_t                          content_length;

    ngx_uint_t                     invalid_header;

    ngx_uint_t                     header_hash;
    ngx_uint_t                     lowcase_index;
    u_char                         lowcase_header[32];

    u_char                        *header_name_start;
    u_char                        *header_name_end;
    u_char                        *header_start;
    u_char                        *header_end;
} ngx_squ_http_ctx_t;


struct ngx_squ_http_cleanup_ctx_s {
    ngx_squ_http_ctx_t            *ctx;
};


static ngx_int_t ngx_squ_http_parse_args(HSQUIRRELVM v, ngx_squ_thread_t *thr,
    ngx_squ_http_ctx_t *ctx);

static void ngx_squ_http_connect_handler(ngx_event_t *wev);
static void ngx_squ_http_write_handler(ngx_event_t *wev);
static void ngx_squ_http_read_handler(ngx_event_t *rev);
static void ngx_squ_http_dummy_handler(ngx_event_t *ev);

static ngx_int_t ngx_squ_http_parse_status_line(ngx_squ_http_ctx_t *ctx);
static ngx_int_t ngx_squ_http_parse_header_line(ngx_squ_http_ctx_t *ctx);
static ngx_int_t ngx_squ_http_parse_headers(ngx_squ_http_ctx_t *ctx);
static ngx_int_t ngx_squ_http_parse_response(ngx_squ_http_ctx_t *ctx);

static void ngx_squ_http_finalize(ngx_squ_http_ctx_t *ctx, char *errstr);
static void ngx_squ_http_cleanup(void *data);


SQInteger
ngx_squ_http(HSQUIRRELVM v)
{
    char                        *errstr;
    ngx_int_t                    rc;
    ngx_pool_t                  *pool;
    ngx_squ_thread_t            *thr;
    ngx_squ_http_ctx_t          *ctx;
    ngx_pool_cleanup_t          *cln;
    ngx_peer_connection_t       *peer;
    ngx_squ_http_cleanup_ctx_t  *cln_ctx;

    thr = sq_getforeignptr(v);

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, thr->log, 0, "squ http");

    pool = ngx_create_pool(ngx_pagesize, ngx_cycle->log);
    if (pool == NULL) {
        errstr = "ngx_create_pool() failed";
        goto error;
    }

    ctx = ngx_pcalloc(pool, sizeof(ngx_squ_http_ctx_t));
    if (ctx == NULL) {
        ngx_destroy_pool(pool);
        errstr = "ngx_pcalloc() failed";
        goto error;
    }

    if (ngx_array_init(&ctx->headers, pool, 16, sizeof(ngx_keyval_t)) != NGX_OK)
    {
        ngx_destroy_pool(pool);
        errstr = "ngx_array_init() failed";
        goto error;
    }

    ctx->pool = pool;

    cln_ctx = ngx_pcalloc(thr->pool, sizeof(ngx_squ_http_cleanup_ctx_t));
    if (cln_ctx == NULL) {
        ngx_destroy_pool(pool);
        errstr = "ngx_pcalloc() failed";
        goto error;
    }

    cln_ctx->ctx = ctx;

    cln = ngx_pool_cleanup_add(thr->pool, 0);
    if (cln == NULL) {
        ngx_destroy_pool(pool);
        errstr = "ngx_pool_cleanup_add() failed";
        goto error;
    }

    cln->handler = ngx_squ_http_cleanup;
    cln->data = cln_ctx;

    ctx->thr = thr;
    ctx->cln_ctx = cln_ctx;

    if (ngx_squ_http_parse_args(v, thr, ctx) == NGX_ERROR) {
        return 2;
    }

    ctx->u.default_port = 80;
    ctx->u.one_addr = 1;
    ctx->u.uri_part = 1;

    if (ngx_parse_url(pool, &ctx->u) != NGX_OK) {
        if (ctx->u.err) {
            ngx_log_error(NGX_LOG_EMERG, thr->log, 0,
                          "%s in url \"%V\"", ctx->u.err, &ctx->u.url);
        }

        errstr = ctx->u.err;
        goto error;
    }

    peer = &ctx->peer;

#if (NGX_UDT)
    peer->type = SOCK_STREAM;
#endif
    peer->sockaddr = ctx->u.addrs->sockaddr;
    peer->socklen = ctx->u.addrs->socklen;
    peer->name = &ctx->u.addrs->name;
    peer->get = ngx_event_get_peer;
    peer->log = ngx_cycle->log;
    peer->log_error = NGX_ERROR_ERR;
#if (NGX_THREADS)
    peer->lock = &thr->c->lock;
#endif

    rc = ngx_event_connect_peer(peer);

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, thr->log, 0,
                   "squ http connecting to server: %i", rc);

    if (rc == NGX_ERROR || rc == NGX_BUSY || rc == NGX_DECLINED) {
        errstr = "ngx_event_connect_peer() failed";
        goto error;
    }

    peer->connection->data = ctx;
    peer->connection->pool = pool;

    peer->connection->read->handler = ngx_squ_http_dummy_handler;
    peer->connection->write->handler = ngx_squ_http_connect_handler;

    if (rc == NGX_AGAIN) {
        ngx_add_timer(peer->connection->write, ctx->connect_timeout);
        return sq_suspendvm(v);
    }

    /* rc == NGX_OK */

    ctx->rc = 0;
    ctx->not_event = 1;

    ngx_squ_http_connect_handler(peer->connection->write);

    ctx->not_event = 0;

    rc = ctx->rc;

    if (rc == NGX_AGAIN) {
        return sq_suspendvm(v);
    }

    cln_ctx->ctx = NULL;

    ngx_destroy_pool(ctx->pool);

    return rc;

error:

    sq_pushbool(v, SQFalse);
#if 0
    sq_pushstring(v, (SQChar *) errstr, -1);
#endif

    return 1;
}


static ngx_int_t
ngx_squ_http_parse_args(HSQUIRRELVM v, ngx_squ_thread_t *thr,
    ngx_squ_http_ctx_t *ctx)
{
    int            top;
    char          *errstr;
    u_char        *p, *last;
    ngx_str_t      str;
    ngx_keyval_t  *header;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, thr->log, 0, "squ http parse args");

    if (!squ_istable(l, 1)) {
        return squL_error(l, "invalid the first argument, must be a table");
    }

    top = squ_gettop(l);

    squ_getfield(l, 1, "method");
    str.data = (u_char *) squL_optlstring(l, -1, "GET", &str.len);

    ctx->method.len = str.len;
    ctx->method.data = ngx_pstrdup(ctx->pool, &str);
    if (ctx->method.data == NULL) {
        errstr = "ngx_pstrdup() failed";
        goto error;
    }

    squ_getfield(l, 1, "version");
    str.data = (u_char *) squL_optlstring(l, -1, "1.1", &str.len);

    ctx->version.len = str.len;
    ctx->version.data = ngx_pstrdup(ctx->pool, &str);
    if (ctx->version.data == NULL) {
        errstr = "ngx_pstrdup() failed";
        goto error;
    }

    squ_getfield(l, 1, "url");
    str.data = (u_char *) squL_checklstring(l, -1, &str.len);

    ctx->u.url.len = str.len;
    ctx->u.url.data = ngx_pstrdup(ctx->pool, &str);
    if (ctx->u.url.data == NULL) {
        errstr = "ngx_pstrdup() failed";
        goto error;
    }

    squ_getfield(l, 1, "headers");
    if (!squ_isnil(l, -1)) {
        if (!squ_istable(l, -1)) {
            return squL_error(l,
                              "invalid value of the argument \"headers\""
                              ", must be a table");
        }

        squ_pushnil(l);

        while (squ_next(l, -2)) {
            header = ngx_array_push(&ctx->headers);
            if (header == NULL) {
                errstr = "ngx_array_push() failed";
                goto error;
            }

            str.data = (u_char *) squL_checklstring(l, -2, &str.len);

            header->key.len = str.len;
            header->key.data = ngx_pstrdup(ctx->pool, &str);

            header->key.data[0] = ngx_toupper(header->key.data[0]);

            for (p = header->key.data, last = p + header->key.len;
                 p < last - 1;
                 p++)
            {
                if (*p == '_') {
                    *p = '-';

                    p[1] = ngx_toupper(p[1]);
                }
            }

            str.data = (u_char *) squL_checklstring(l, -1, &str.len);

            header->value.len = str.len;
            header->value.data = ngx_pstrdup(ctx->pool, &str);

            squ_pop(l, 1);
        }
    }

    squ_getfield(l, 1, "body");
    str.data = (u_char *) squL_optlstring(l, -1, "", &str.len);

    ctx->body.len = str.len;
    ctx->body.data = ngx_pstrdup(ctx->pool, &str);
    if (ctx->body.data == NULL) {
        errstr = "ngx_pstrdup() failed";
        goto error;
    }

    squ_settop(l, top);

    ctx->connect_timeout = (ngx_msec_t) squL_optnumber(l, 2, 60000);
    ctx->send_timeout = (ngx_msec_t) squL_optnumber(l, 3, 60000);
    ctx->read_timeout = (ngx_msec_t) squL_optnumber(l, 4, 60000);

    return NGX_OK;

error:

    squ_settop(l, top);
    squ_pushboolean(l, 0);
    squ_pushstring(l, errstr);

    return NGX_ERROR;
}


static void
ngx_squ_http_connect_handler(ngx_event_t *wev)
{
    size_t               size;
    ngx_buf_t           *b;
    ngx_uint_t           i;
    ngx_keyval_t        *headers;
    ngx_connection_t    *c;
    ngx_squ_http_ctx_t  *ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, wev->log, 0, "squ http connect handler");

    c = wev->data;
    ctx = c->data;

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_ERR, wev->log, NGX_ETIMEDOUT,
                      "squ http connecting %V timed out", ctx->peer.name);
        ngx_squ_http_finalize(ctx, "ngx_squ_http_connect_handler() timed out");
        return;
    }

    if (wev->timer_set) {
        ngx_del_timer(wev);
    }

    c->read->handler = ngx_squ_http_dummy_handler;
    wev->handler = ngx_squ_http_write_handler;

    size = ctx->method.len + 1 + ctx->u.uri.len + 6 + ctx->version.len + 2
           + sizeof("Host: ") - 1 + ctx->u.host.len + 1 + NGX_INT32_LEN + 2;

    headers = ctx->headers.elts;
    for (i = 0; i < ctx->headers.nelts; i++) {
        size += headers[i].key.len + 2 + headers[i].value.len + 2;
    }

    if (ctx->body.len) {
        size += sizeof("Content-Length: ") - 1 + NGX_INT32_LEN + 2;
    }

    size += 2 + ctx->body.len;

    b = ngx_create_temp_buf(ctx->pool, size);
    if (b == NULL) {
        ngx_squ_http_finalize(ctx, "ngx_create_temp_buf() failed");
        return;
    }

    b->last = ngx_slprintf(b->last, b->end, "%V %V HTTP/%V" CRLF,
                           &ctx->method, &ctx->u.uri, &ctx->version);
    b->last = ngx_slprintf(b->last, b->end, "Host: %V:%d" CRLF,
                           &ctx->u.host, (int) ctx->u.port);

    for (i = 0; i < ctx->headers.nelts; i++) {
        b->last = ngx_slprintf(b->last, b->end, "%V: %V" CRLF,
                               &headers[i].key, &headers[i].value);
    }

    if (ctx->body.len) {
        b->last = ngx_slprintf(b->last, b->end, "Content-Length: %uz" CRLF,
                               ctx->body.len);
    }

    *b->last++ = CR;
    *b->last++ = LF;

    if (ctx->body.len) {
        b->last = ngx_cpymem(b->last, ctx->body.data, ctx->body.len);
    }

    ctx->request = b;

    ctx->headers.nelts = 0;

    ctx->response = ngx_create_temp_buf(ctx->pool, ngx_pagesize);
    if (ctx->response == NULL) {
        ngx_squ_http_finalize(ctx, "ngx_create_temp_buf() failed");
        return;
    }

    ngx_squ_http_write_handler(wev);
}


static void
ngx_squ_http_write_handler(ngx_event_t *wev)
{
    ssize_t              n, size;
    ngx_buf_t           *b;
    ngx_connection_t    *c;
    ngx_squ_http_ctx_t  *ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, wev->log, 0, "squ http write handler");

    c = wev->data;
    ctx = c->data;

    if (wev->timedout) {
        ngx_log_error(NGX_LOG_ERR, wev->log, NGX_ETIMEDOUT,
                      "squ http write %V timed out", ctx->peer.name);
        ngx_squ_http_finalize(ctx, "ngx_squ_http_write_handler() timed out");
        return;
    }

    if (wev->timer_set) {
        ngx_del_timer(wev);
    }

    b = ctx->request;

    while (1) {

        size = b->last - b->pos;

        n = c->send(c, b->pos, size);

        if (n > 0) {
            b->pos += n;

            if (n < size) {
                continue;
            }

            /* n == size */

            c->read->handler = ngx_squ_http_read_handler;
            wev->handler = ngx_squ_http_dummy_handler;

            ngx_squ_http_read_handler(c->read);

            return;
        }

        if (n == NGX_AGAIN) {
            ngx_add_timer(wev, ctx->send_timeout);
            ctx->rc = NGX_AGAIN;
            return;
        }

        /* n == NGX_ERROR || n == 0 */

        ngx_squ_http_finalize(ctx, "c->send() failed");
        return;
    }
}


static void
ngx_squ_http_read_handler(ngx_event_t *rev)
{
    ssize_t              n;
    ngx_int_t            rc;
    ngx_buf_t           *b;
    ngx_connection_t    *c;
    ngx_squ_http_ctx_t  *ctx;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, rev->log, 0, "squ http read handler");

    c = rev->data;
    ctx = c->data;

    if (rev->timedout) {
        ngx_log_error(NGX_LOG_ERR, rev->log, NGX_ETIMEDOUT,
                      "squ http read %V timed out", ctx->peer.name);
        ngx_squ_http_finalize(ctx, "ngx_squ_http_read_handler() timed out");
        return;
    }

    if (rev->timer_set) {
        ngx_del_timer(rev);
    }

    while (1) {

        b = ctx->response;

        n = c->recv(c, b->last, b->end - b->last);

        if (n > 0) {
            b->last += n;

            rc = ngx_squ_http_parse_response(ctx);

            if (rc == NGX_OK) {
                return;
            }

            if (rc == NGX_AGAIN) {
                continue;
            }

            if (rc == NGX_DONE) {
                ngx_squ_http_finalize(ctx, NULL);
                return;
            }

            /* rc == NGX_ERROR */

            ngx_squ_http_finalize(ctx, "ngx_squ_http_parse_response() failed");
            return;
        }

        if (n == NGX_AGAIN) {
            ngx_add_timer(rev, ctx->read_timeout);
            ctx->rc = NGX_AGAIN;
            return;
        }

        /* n == NGX_ERROR || n == 0 */

        ngx_squ_http_finalize(ctx, "c->recv() failed");
        return;
    }
}


static void
ngx_squ_http_dummy_handler(ngx_event_t *ev)
{
    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ev->log, 0, "squ http dummy handler");
}


static ngx_int_t
ngx_squ_http_parse_status_line(ngx_squ_http_ctx_t *ctx)
{
    u_char  *p, ch;
    enum {
        sw_start = 0,
        sw_H,
        sw_HT,
        sw_HTT,
        sw_HTTP,
        sw_first_major_digit,
        sw_major_digit,
        sw_first_minor_digit,
        sw_minor_digit,
        sw_status,
        sw_space_after_status,
        sw_status_text,
        sw_almost_done
    } state;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "squ http parse status line");

    state = ctx->state;

    for (p = ctx->response->pos; p < ctx->response->last; p++) {
        ch = *p;

        switch (state) {

        /* "HTTP/" */
        case sw_start:
            switch (ch) {
            case 'H':
                state = sw_H;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        case sw_H:
            switch (ch) {
            case 'T':
                state = sw_HT;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        case sw_HT:
            switch (ch) {
            case 'T':
                state = sw_HTT;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        case sw_HTT:
            switch (ch) {
            case 'P':
                state = sw_HTTP;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        case sw_HTTP:
            switch (ch) {
            case '/':
                state = sw_first_major_digit;
                break;
            default:
                return NGX_ERROR;
            }
            break;

        /* the first digit of major HTTP version */
        case sw_first_major_digit:
            if (ch < '1' || ch > '9') {
                return NGX_ERROR;
            }

            state = sw_major_digit;
            break;

        /* the major HTTP version or dot */
        case sw_major_digit:
            if (ch == '.') {
                state = sw_first_minor_digit;
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_ERROR;
            }

            break;

        /* the first digit of minor HTTP version */
        case sw_first_minor_digit:
            if (ch < '0' || ch > '9') {
                return NGX_ERROR;
            }

            state = sw_minor_digit;
            break;

        /* the minor HTTP version or the end of the request line */
        case sw_minor_digit:
            if (ch == ' ') {
                state = sw_status;
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_ERROR;
            }

            break;

        /* HTTP status code */
        case sw_status:
            if (ch == ' ') {
                break;
            }

            if (ch < '0' || ch > '9') {
                return NGX_ERROR;
            }

            ctx->status_code = ctx->status_code * 10 + ch - '0';

            if (++ctx->status_count == 3) {
                state = sw_space_after_status;
                ctx->status_start = p - 2;
            }

            break;

        /* space or end of line */
        case sw_space_after_status:
            switch (ch) {
            case ' ':
                state = sw_status_text;
                break;
            case '.':                    /* IIS may send 403.1, 403.2, etc */
                state = sw_status_text;
                break;
            case CR:
                state = sw_almost_done;
                break;
            case LF:
                goto done;
            default:
                return NGX_ERROR;
            }
            break;

        /* any text until end of line */
        case sw_status_text:
            switch (ch) {
            case CR:
                state = sw_almost_done;

                break;
            case LF:
                goto done;
            }
            break;

        /* end of status line */
        case sw_almost_done:
            ctx->status_end = p - 1;
            switch (ch) {
            case LF:
                goto done;
            default:
                return NGX_ERROR;
            }
        }
    }

    ctx->response->pos = p;

    ctx->state = state;

    return NGX_AGAIN;

done:

    ctx->response->pos = p + 1;

    if (ctx->status_end == NULL) {
        ctx->status_end = p;
    }

    ctx->state = sw_start;

    return NGX_OK;
}


static ngx_int_t
ngx_squ_http_parse_header_line(ngx_squ_http_ctx_t *ctx)
{
    u_char      c, ch, *p;
    ngx_uint_t  hash, i;
    enum {
        sw_start = 0,
        sw_name,
        sw_space_before_value,
        sw_value,
        sw_space_after_value,
        sw_ignore_line,
        sw_almost_done,
        sw_header_almost_done
    } state;

    /* the last '\0' is not needed because string is zero terminated */

    static u_char  lowcase[] =
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0-\0\0" "0123456789\0\0\0\0\0\0"
        "\0abcdefghijklmnopqrstuvwxyz\0\0\0\0\0"
        "\0abcdefghijklmnopqrstuvwxyz\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
        "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "squ http parse header line");

    state = ctx->state;
    hash = ctx->header_hash;
    i = ctx->lowcase_index;

    for (p = ctx->response->pos; p < ctx->response->last; p++) {
        ch = *p;

        switch (state) {

        /* first char */
        case sw_start:
            ctx->header_name_start = p;
            ctx->invalid_header = 0;

            switch (ch) {
            case CR:
                ctx->header_end = p;
                state = sw_header_almost_done;
                break;
            case LF:
                ctx->header_end = p;
                goto header_done;
            default:
                state = sw_name;

                c = lowcase[ch];

                if (c) {
                    hash = ngx_hash(0, c);
                    ctx->lowcase_header[0] = c;
                    i = 1;
                    break;
                }

                ctx->invalid_header = 1;

                break;

            }
            break;

        /* header name */
        case sw_name:
            c = lowcase[ch];

            if (c) {
                hash = ngx_hash(hash, c);
                ctx->lowcase_header[i++] = c;
                i &= (NGX_HTTP_LC_HEADER_LEN - 1);
                break;
            }

            if (ch == '_') {
                hash = ngx_hash(hash, ch);
                ctx->lowcase_header[i++] = ch;
                i &= (NGX_HTTP_LC_HEADER_LEN - 1);
                break;
            }

            if (ch == ':') {
                ctx->header_name_end = p;
                state = sw_space_before_value;
                break;
            }

            if (ch == CR) {
                ctx->header_name_end = p;
                ctx->header_start = p;
                ctx->header_end = p;
                state = sw_almost_done;
                break;
            }

            if (ch == LF) {
                ctx->header_name_end = p;
                ctx->header_start = p;
                ctx->header_end = p;
                goto done;
            }

            /* IIS may send the duplicate "HTTP/1.1 ..." lines */
            if (ch == '/'
                && p - ctx->header_name_start == 4
                && ngx_strncmp(ctx->header_name_start, "HTTP", 4) == 0)
            {
                state = sw_ignore_line;
                break;
            }

            ctx->invalid_header = 1;

            break;

        /* space* before header value */
        case sw_space_before_value:
            switch (ch) {
            case ' ':
                break;
            case CR:
                ctx->header_start = p;
                ctx->header_end = p;
                state = sw_almost_done;
                break;
            case LF:
                ctx->header_start = p;
                ctx->header_end = p;
                goto done;
            default:
                ctx->header_start = p;
                state = sw_value;
                break;
            }
            break;

        /* header value */
        case sw_value:
            switch (ch) {
            case ' ':
                ctx->header_end = p;
                state = sw_space_after_value;
                break;
            case CR:
                ctx->header_end = p;
                state = sw_almost_done;
                break;
            case LF:
                ctx->header_end = p;
                goto done;
            }
            break;

        /* space* before end of header line */
        case sw_space_after_value:
            switch (ch) {
            case ' ':
                break;
            case CR:
                state = sw_almost_done;
                break;
            case LF:
                goto done;
            default:
                state = sw_value;
                break;
            }
            break;

        /* ignore header line */
        case sw_ignore_line:
            switch (ch) {
            case LF:
                state = sw_start;
                break;
            default:
                break;
            }
            break;

        /* end of header line */
        case sw_almost_done:
            switch (ch) {
            case LF:
                goto done;
            case CR:
                break;
            default:
                return NGX_ERROR;
            }
            break;

        /* end of header */
        case sw_header_almost_done:
            switch (ch) {
            case LF:
                goto header_done;
            default:
                return NGX_ERROR;
            }
        }
    }

    ctx->response->pos = p;

    ctx->state = state;
    ctx->header_hash = hash;
    ctx->lowcase_index = i;

    return NGX_AGAIN;

done:

    ctx->response->pos = p + 1;

    ctx->state = sw_start;
    ctx->header_hash = hash;
    ctx->lowcase_index = i;

    return NGX_OK;

header_done:

    ctx->response->pos = p + 1;

    ctx->state = sw_start;

    return NGX_DONE;
}


static ngx_int_t
ngx_squ_http_parse_headers(ngx_squ_http_ctx_t *ctx)
{
    u_char            *p, ch;
    ngx_int_t          rc;
    ngx_str_t          str;
    ngx_keyval_t      *header;
    ngx_squ_thread_t  *thr;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "squ http parse headers");

    for ( ;; ) {

        rc = ngx_squ_http_parse_header_line(ctx);

        if (rc != NGX_OK) {
            return rc;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                      "header name:%*s value:%*s",
                       ctx->header_name_end - ctx->header_name_start,
                       ctx->header_name_start,
                       ctx->header_end - ctx->header_start,
                       ctx->header_start);

        /* TODO */

        if (ctx->content_length == 0
            && ngx_strncmp(ctx->header_name_start, "Content-Length",
                           sizeof("Content-Length") - 1)
               == 0)
        {
            ctx->content_length = ngx_atoof(ctx->header_start,
                                           ctx->header_end - ctx->header_start);
            if (ctx->content_length == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        thr = ctx->thr;

        if (ctx->thr == NULL) {
            continue;
        }

        for (p = ctx->header_name_start; p < ctx->header_name_end - 1; p++) {
            ch = *p;

            if (ch >= 'A' && ch <= 'Z') {
                ch |= 0x20;

            } else if (ch == '-') {
                ch = '_';
            }

            *p = ch;
        }

        *ctx->header_name_end = '\0';

        header = ngx_array_push(&ctx->headers);
        if (header == NULL) {
            return NGX_ERROR;
        }

        str.len = ctx->header_name_end - ctx->header_name_start + 1;
        str.data = ctx->header_name_start;

        header->key.len = str.len;
        header->key.data = ngx_pstrdup(ctx->pool, &str);

        str.len = ctx->header_end - ctx->header_start;
        str.data = ctx->header_start;

        header->value.len = str.len;
        header->value.data = ngx_pstrdup(ctx->pool, &str);
    }
}


static ngx_int_t
ngx_squ_http_parse_response(ngx_squ_http_ctx_t *ctx)
{
    size_t     size;
    ngx_int_t  rc;
    enum {
        sw_status_line = 0,
        sw_headers,
        sw_body
    } step;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "squ http parse response");

    step = ctx->step;

    for ( ;; ) {

        switch (step) {

        case sw_status_line:
            rc = ngx_squ_http_parse_status_line(ctx);
            if (rc == NGX_OK) {
                step = sw_headers;
            }

            break;

        case sw_headers:
            rc = ngx_squ_http_parse_headers(ctx);
            if (rc == NGX_DONE) {
                rc = NGX_OK;
                ctx->body_start = ctx->response->pos;
                step = sw_body;
            }

            break;

        case sw_body:
            if (ctx->content_length > 0) {
                size = ctx->response->last - ctx->response->pos;
                ctx->response->pos = ctx->response->last;
                ctx->content_length -= size;
            }

            if (ctx->content_length > 0) {
                size = ctx->response->end - ctx->response->last;

                if (ctx->content_length > size) {
                    ctx->header = ctx->response;

                    ctx->response = ngx_create_temp_buf(ctx->pool,
                                                  (size_t) ctx->content_length);
                    if (ctx->response == NULL) {
                        rc = NGX_ERROR;
                        break;
                    }
                }

                rc = NGX_AGAIN;

            } else {
                rc = NGX_DONE;
            }

            break;

        default:
            rc = NGX_ERROR;
            break;
        }

        if (rc != NGX_OK) {
            break;
        }
    }

    ctx->step = step;

    return rc;
}


static void
ngx_squ_http_finalize(ngx_squ_http_ctx_t *ctx, char *errstr)
{
    ngx_int_t          rc;
    ngx_uint_t         i;
    ngx_keyval_t      *headers;
    ngx_squ_thread_t  *thr;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "squ http finalize");

    if (ctx->cln_ctx != NULL) {
        ctx->cln_ctx->ctx = NULL;
    }

    thr = ctx->thr;

    if (thr == NULL) {
        if (ctx->peer.connection) {
            ngx_close_connection(ctx->peer.connection);
        }

        ngx_destroy_pool(ctx->pool);
        return;
    }

    ctx->rc = 1;

    if (errstr == NULL) {
        squ_createtable(thr->l, 1, 3);

        squ_createtable(thr->l, 0, ctx->headers.nelts);

        headers = ctx->headers.elts;
        for (i = 0; i < ctx->headers.nelts; i++) {
            squ_pushlstring(thr->l, (char *) headers[i].value.data,
                            headers[i].value.len);
            squ_setfield(thr->l, -2, (char *) headers[i].key.data);
        }

        squ_setfield(thr->l, -2, "headers");

        squ_pushnumber(thr->l, ctx->status_code);
        squ_setfield(thr->l, -2, "status");

        if (ctx->header == NULL) {
            squ_pushlstring(thr->l, (char *) ctx->body_start,
                            ctx->response->last - ctx->body_start);

        } else {
            squ_pushlstring(thr->l, (char *) ctx->body_start,
                            ctx->header->last - ctx->body_start);
            squ_pushlstring(thr->l, (char *) ctx->response->start,
                            ctx->response->last - ctx->response->start);
            squ_concat(thr->l, 2);
        }

        squ_setfield(thr->l, -2, "body");

    } else {
        sq_pushbool(thr->v, SQFalse);
#if 0
        sq_pushstring(thr->v, (SQChar *) errstr, -1);

        ctx->rc++;
#endif
    }

    if (ctx->peer.connection) {
        ngx_close_connection(ctx->peer.connection);
    }

    if (ctx->not_event) {
        return;
    }

    rc = ctx->rc;

    ngx_destroy_pool(ctx->pool);

    rc = ngx_squ_thread_run(thr, rc);
    if (rc == NGX_AGAIN) {
        return;
    }

    ngx_squ_finalize(thr, rc);
}


static void
ngx_squ_http_cleanup(void *data)
{
    ngx_squ_http_cleanup_ctx_t *cln_ctx = data;

    ngx_squ_http_ctx_t  *ctx;

    ctx = cln_ctx->ctx;

    if (ctx != NULL) {
        ctx->thr = NULL;
        ctx->cln_ctx = NULL;

        ngx_squ_http_finalize(ctx, NULL);
    }
}
