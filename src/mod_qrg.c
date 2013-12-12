
#include "httpd.h"
#include "http_config.h"
#include "http_protocol.h"
#include "ap_config.h"
#include "apr_tables.h"
#include "apr_strings.h"

#include "apreq2/apreq_module_apache2.h"
#include "apreq2/apreq_module.h"
#include "apreq2/apreq_util.h"

#include "qrencode.h"


static int qrg_handler(request_rec * r)
{
    QRcode *code;
    apreq_handle_t *apreq;
    const apr_table_t *params;
    const char *text, *level, *cas;
    size_t textlen;
    apr_size_t txtlen;
    QRecLevel l;
    short c, row, col;
    char *svg, *txt;

    if (strcmp(r->handler, "qrg")) {
        return DECLINED;
    }

    apreq = apreq_handle_apache2(r);
    if (apreq == NULL) {
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    params = apreq_params(apreq, r->pool);
    text = apr_table_get(params, "text");
    level = apr_table_get(params, "level");
    cas = apr_table_get(params, "case");

    if (text == NULL || (textlen = strlen(text)) == 0) {
        return HTTP_BAD_REQUEST;
    }
    else {
        txt = apr_palloc(r->pool, textlen + 1);
        if (apreq_decode(txt, &txtlen, text, textlen)) {
            return HTTP_BAD_REQUEST;
        }
    }
    if (level == NULL || strlen(level) != 1) {
        l = QR_ECLEVEL_L;
    }
    else {
        switch ((int) *level) {
        case 'M':
        case 'm':
            l = QR_ECLEVEL_M;
            break;
        case 'Q':
        case 'q':
            l = QR_ECLEVEL_Q;
            break;
        case 'H':
        case 'h':
            l = QR_ECLEVEL_H;
            break;
        default:
            l = QR_ECLEVEL_L;
        }
    }
    if (cas == NULL || strlen(cas) == 0) {
        c = 0;
    }
    else {
        c = (*cas == '0' ? 0 : 1);
    }

    apr_table_set(r->headers_out, "Cache-Control", "no-cache");
    r->content_type = "text/plain";

    code = QRcode_encodeString(txt, 0, l, QR_MODE_8, c);
    if (code == NULL) {
        ap_rputs("ERROR\n", r);
        return HTTP_INTERNAL_SERVER_ERROR;
    }

    r->content_type = "image/svg+xml";

    svg = apr_psprintf(r->pool,
                       "<?xml version=\"1.0\" standalone=\"no\"?>\n"
                       "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
                       "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n"
                       "<svg xmlns=\"http://www.w3.org/2000/svg\" "
                       "version=\"1.1\" viewBox=\"0 0 %d %d\">\n"
                       "<path d=\"\n", code->width, code->width);

    /* 1 = black */
    for (row = 0; row < code->width; ++row) {
        for (col = 0; col < code->width; ++col) {
            if (code->data[(code->width * row) + col] & 0x01) {
                svg = apr_pstrcat(r->pool, svg,
                                  apr_psprintf(r->pool, "M%d %dh1v1h-1z ",
                                               row, col), NULL);
            }
        }
        svg = apr_pstrcat(r->pool, svg, "\n", NULL);
    }

    svg = apr_pstrcat(r->pool, svg, "\"/>\n</svg>\n", NULL);
    ap_rputs(svg, r);

    if (code)
        QRcode_free(code);

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
