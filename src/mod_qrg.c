
#include "httpd.h"
#include "http_config.h"
#include "http_log.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "apr_tables.h"
#include "apr_strings.h"

#include "apreq2/apreq_module_apache2.h"
#include "apreq2/apreq_module.h"
#include "apreq2/apreq_util.h"

#include "qrencode.h"


#define QRG_OK 0
#define QRG_GENERAL 1
#define QRG_MISSING_TEXT 2
#define QRG_INVALID_TEXT 3

typedef struct
{
    const char *text;
    QRecLevel level;
    short case_sensitive;
} qrg_request_rec;

static int qrg_parse_request(request_rec * r, qrg_request_rec * req)
{
    apreq_handle_t *apreq;
    const apr_table_t *params;
    const char *level, *cas;
    apr_ssize_t textlen;

    /* Retrieve the apreq handle */
    apreq = apreq_handle_apache2(r);
    if (apreq == NULL) {
        ap_log_rerror(APLOG_MARK, LOG_ERR, HTTP_INTERNAL_SERVER_ERROR, r,
                      "Unable to get the apreq handle");
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Load the request parameters */
    params = apreq_params(apreq, r->pool);
    if (params == NULL) {
        ap_log_rerror(APLOG_MARK, LOG_WARNING, HTTP_BAD_REQUEST, r,
                      "Request did not have any parameters");
        return HTTP_BAD_REQUEST;
    }

    /* Get intermediate values */
    level = apr_table_get(params, "level");
    cas = apr_table_get(params, "case");

    /* Do we have the required 'text' param */
    req->text = apr_table_get(params, "text");
    if (req->text == NULL || (textlen = strlen(req->text)) == 0) {
        ap_log_rerror(APLOG_MARK, LOG_WARNING, HTTP_BAD_REQUEST, r,
                      "Request did not have a 'text' parameter");
        return HTTP_BAD_REQUEST;
    }

    /* Do we have the 'level' param */
    if (level == NULL || strlen(level) != 1) {
        ap_log_rerror(APLOG_MARK, LOG_DEBUG, 0, r,
                      "Request did not have a 'level' parameter, "
                      "using the default 'L'");
        req->level = QR_ECLEVEL_L;
    }
    /* Parse the 'level' param into a QRecLevel enum */
    else {
        switch ((int) *level) {
        case 'L':
        case 'l':
            req->level = QR_ECLEVEL_L;
            break;
        case 'M':
        case 'm':
            req->level = QR_ECLEVEL_M;
            break;
        case 'Q':
        case 'q':
            req->level = QR_ECLEVEL_Q;
            break;
        case 'H':
        case 'h':
            req->level = QR_ECLEVEL_H;
            break;
        default:
            ap_log_rerror(APLOG_MARK, LOG_WARNING, 0, r,
                          "Request has an invalid 'level' parameter, "
                          "using the default 'L'");
            req->level = QR_ECLEVEL_L;
        }
    }

    /* Do we have the 'case' param */
    if (cas == NULL || strlen(cas) == 0) {
        req->case_sensitive = 0;
    }
    else {
        req->case_sensitive = (*cas == '0' ? 0 : 1);
    }

    return OK;
}

#define HEADER \
"<?xml version=\"1.0\" standalone=\"no\"?>\n\
<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \
\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n\
<svg xmlns=\"http://www.w3.org/2000/svg\" \
version=\"1.1\" viewBox=\"0 0 %d %d\">\n<path d=\"\n"
#define FOOTER "\"/>\n</svg>\n"
#define DOT_PATH "M%d %dh1v1h-1z"

static char *qrcode_to_svg(apr_pool_t * p, QRcode *code)
{
    char *svg, *s;
    short row, col;
    size_t header_len = strlen(HEADER);
    size_t footer_len = strlen(FOOTER);
    size_t width_len, dot_len = 10U;

    /* How many characters are needed to display the width */
    /* QR Codes have a maximum width of 177 at this time */
    if (code->width > 9) {
        if (code->width > 99) {
            width_len = 3;
        }
        else {
            width_len = 2;
        }
    }
    else {
        width_len = 1;
    }

    /* Allocate the svg buffer */
    s = svg = apr_palloc(p,
                         /* total number of cells */
                         (code->width * code->width)
                         /* times the size of a dot */
                         * (width_len * 2 + dot_len)
                         /* plus the header and footer */
                         + (header_len + footer_len)
                         /* plus a '\n' after each row */
                         + code->width);

    s += sprintf(s, HEADER, code->width, code->width);

    for (row = 0; row < code->width; ++row) {
        for (col = 0; col < code->width; ++col) {
            /* 0x01 = black */
            if (code->data[(code->width * row) + col] & 0x01) {
                s += sprintf(s, DOT_PATH, row, col);
            }
        }
        *s++ = '\n';
    }

    memcpy(s, FOOTER, footer_len);
    *(s + footer_len) = '\0';

    return svg;
}

static apr_status_t write_out(request_rec * r, const char *response)
{
    apr_bucket_brigade *bb;
    apr_bucket *b;

    bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
    b = apr_bucket_immortal_create(response, strlen(response),
                                   bb->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, b);
    APR_BRIGADE_INSERT_TAIL(bb, apr_bucket_eos_create(bb->bucket_alloc));
    return ap_pass_brigade(r->output_filters, bb);
}

static int qrg_handler(request_rec * r)
{
    QRcode *code;
    char *svg;
    int rv;
    qrg_request_rec req;

    if (strcmp(r->handler, "qrg")) {
        return DECLINED;
    }

    /* Parse the request data */
    rv = qrg_parse_request(r, &req);
    if (rv != OK) {
        return rv;
    }

    /* Make sure the client does not cache this stuff */
    apr_table_set(r->headers_out, "Cache-Control", "no-cache");

    /* Generate the QR Code */
    code = QRcode_encodeString(req.text, 0, req.level, QR_MODE_8,
                               req.case_sensitive);
    if (code == NULL) {
        r->content_type = "text/plain";
        ap_rputs("ERROR", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    /* Transorm the code into svg */
    svg = qrcode_to_svg(r->pool, code);

    /* We are done with the code now */
    QRcode_free(code);

    /* Write the response */
    r->content_type = "image/svg+xml";
    rv = write_out(r, svg);
    if (rv != APR_SUCCESS) {
        r->content_type = "text/plain";
        ap_rputs("ERROR", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    return OK;
}

static void qrg_register_hooks(apr_pool_t * p)
{
    ap_hook_handler(qrg_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

/* Dispatch list for API hooks */
module AP_MODULE_DECLARE_DATA qrg_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                       /* create per-dir    config structures */
    NULL,                       /* merge  per-dir    config structures */
    NULL,                       /* create per-server config structures */
    NULL,                       /* merge  per-server config structures */
    NULL,                       /* table of config file commands       */
    qrg_register_hooks          /* register hooks                      */
};
