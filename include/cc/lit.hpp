#pragma once

#ifndef CC_ENABLE_COROUTINE
#    error "Expect c++20 coroutines !!!"
#endif

#include <cc/lit/middleware.h>
#include <cc/lit/multipart_parser.h>
#include <cc/lit/router.h>
#include <cc/lit/server.h>

namespace lit = cc::lit;  // NOLINT

using lit_request_body_t  = typename lit::App::ReqBody;
using lit_response_body_t = typename lit::App::RespBody;
