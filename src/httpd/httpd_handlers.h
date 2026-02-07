/*
 * HTTP API endpoint handlers for NetHID
 */

#ifndef NETHID_HTTPD_HANDLERS_H
#define NETHID_HTTPD_HANDLERS_H

#include "httpd_server.h"

// Route table accessors
const http_route_t *httpd_get_routes(void);
int httpd_get_route_count(void);

#endif // NETHID_HTTPD_HANDLERS_H
