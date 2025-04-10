/****************************************************************************
 * apps/crypto/openssl_mbedtls_wrapper/mbedtls/ssl_lib.c
 *
 * SPDX-License-Identifier: Apache-2.0
 * SPDX-FileCopyrightText: 2015-2016 Espressif Systems (Shanghai) PTE LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <openssl/ssl_dbg.h>
#include <openssl/ssl3.h>
#include <openssl/ssl_local.h>
#include "ssl_port.h"

#define SSL_SEND_DATA_MAX_LENGTH 1460

struct alpn_ctx
{
  unsigned char data[23];
  unsigned char len;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static SSL_SESSION *SSL_SESSION_new(void)
{
  SSL_SESSION *session;

  session = ssl_mem_zalloc(sizeof(SSL_SESSION));
  if (!session)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "no enough memory > (session)");
      goto failed1;
    }

  session->peer = X509_new();
  if (!session->peer)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "X509_new() return NULL");
      goto failed2;
    }

  return session;

failed2:
  ssl_mem_free(session);
failed1:
  return NULL;
}

static void SSL_SESSION_free(SSL_SESSION *session)
{
  X509_free(session->peer);
  ssl_mem_free(session);
}

static void
_openssl_alpn_to_mbedtls(struct alpn_ctx *ac, char ***palpn_protos)
{
  unsigned char *p = ac->data;
  unsigned char *q;
  unsigned char len;
  char **alpn_protos;
  int count = 0;

  /* find out how many entries he gave us */

  len = *p++;
  while (p - ac->data < ac->len)
    {
      if (len--)
        {
          p++;
          continue;
        }

      count++;
      len = *p++;
      if (!len)
        {
          break;
        }
    }

  if (!len)
    {
      count++;
    }

  if (!count)
    {
      return;
    }

  /* allocate space for count + 1 pointers and the data afterwards */

  alpn_protos = ssl_mem_zalloc((count + 1) * sizeof(char *) + ac->len + 1);
  if (!alpn_protos)
    {
      return;
    }

  *palpn_protos = alpn_protos;

  /* convert to mbedtls format */

  q = (unsigned char *)alpn_protos + (count + 1) * sizeof(char *);
  p = ac->data;
  count = 0;

  len = *p++;
  alpn_protos[count] = (char *)q;
  while (p - ac->data < ac->len)
    {
      if (len--)
        {
          *q++ = *p++;
          continue;
        }

      *q++ = '\0';
      count++;
      len = *p++;
      alpn_protos[count] = (char *)q;
      if (!len)
        {
          break;
        }
    }

  if (!len)
    {
      *q++ = '\0';
      count++;
      len = *p++;
      alpn_protos[count] = (char *)q;
    }

  alpn_protos[count] = NULL; /* last pointer ends list with NULL */
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

X509_VERIFY_PARAM *SSL_get0_param(SSL *ssl)
{
  return &ssl->param;
}

X509 *SSL_get_certificate(const SSL *ssl)
{
  SSL_ASSERT2(ssl);

  return ssl->cert->x509;
}

X509 *SSL_get_peer_certificate(const SSL *ssl)
{
  SSL_ASSERT2(ssl);

  return ssl->session->peer;
}

int SSL_want(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return ssl->rwstate;
}

int SSL_want_nothing(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  if (ssl->err)
    {
      return 1;
    }

  return (SSL_want(ssl) == SSL_NOTHING);
}

int SSL_want_read(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  if (ssl->err)
    {
      return 0;
    }

  return (SSL_want(ssl) == SSL_READING);
}

int SSL_want_x509_lookup(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return (SSL_want(ssl) == SSL_WRITING);
}

int SSL_get_error(const SSL *ssl, int ret_code)
{
  int ret = SSL_ERROR_SYSCALL;

  SSL_ASSERT1(ssl);

  if (ret_code > 0)
    {
      ret = SSL_ERROR_NONE;
    }
  else if (ret_code < 0)
    {
      if (ssl->err == SSL_ERROR_WANT_READ || SSL_want_read(ssl))
        {
          ret = SSL_ERROR_WANT_READ;
        }
      else if (ssl->err == SSL_ERROR_WANT_WRITE || SSL_want_write(ssl))
        {
          ret = SSL_ERROR_WANT_WRITE;
        }
      else
        {
          ret = SSL_ERROR_SYSCALL;
        }
    }
  else
    {
      if (ssl->shutdown & SSL_RECEIVED_SHUTDOWN)
        {
          ret = SSL_ERROR_ZERO_RETURN;
        }
      else
        {
          ret = SSL_ERROR_SYSCALL;
        }
    }

  return ret;
}

SSL_CTX *SSL_CTX_new(const SSL_METHOD *method, void *rngctx)
{
  SSL_CTX *ctx;
  CERT *cert;
  X509 *client_ca;

  if (!method)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "no no_method");
      return NULL;
    }

  client_ca = X509_new();
  if (!client_ca)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "X509_new() return NULL");
      goto failed1;
    }

  cert = ssl_cert_new();
  if (!cert)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "ssl_cert_new() return NULL");
      goto failed2;
    }

  ctx = (SSL_CTX *)ssl_mem_zalloc(sizeof(SSL_CTX));
  if (!ctx)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "no enough memory > (ctx)");
      goto failed3;
    }

  ctx->method = method;
  ctx->client_CA = client_ca;
  ctx->cert = cert;
  ctx->version = method->version;
  return ctx;

failed3:
  ssl_cert_free(cert);
failed2:
  X509_free(client_ca);
failed1:
  return NULL;
}

void SSL_CTX_free(SSL_CTX *ctx)
{
  SSL_ASSERT3(ctx);

  ssl_cert_free(ctx->cert);

  X509_free(ctx->client_CA);

  if (ctx->alpn_protos)
    {
      ssl_mem_free(ctx->alpn_protos);
    }

  ssl_mem_free(ctx);
}

int SSL_CTX_set_ssl_version(SSL_CTX *ctx, const SSL_METHOD *meth)
{
  SSL_ASSERT1(ctx);
  SSL_ASSERT1(meth);

  ctx->method = meth;

  ctx->version = meth->version;

  return 1;
}

const SSL_METHOD *SSL_CTX_get_ssl_method(SSL_CTX *ctx)
{
  SSL_ASSERT2(ctx);

  return ctx->method;
}

SSL *SSL_new(SSL_CTX *ctx)
{
  int ret = 0;
  SSL *ssl;

  if (!ctx)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "no ctx");
      return NULL;
    }

  ssl = (SSL *)ssl_mem_zalloc(sizeof(SSL));
  if (!ssl)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "no enough memory > (ssl)");
      goto failed1;
    }

  ssl->session = SSL_SESSION_new();
  if (!ssl->session)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "SSL_SESSION_new() return NULL");
      goto failed2;
    }

  ssl->cert = __ssl_cert_new(ctx->cert);
  if (!ssl->cert)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "__ssl_cert_new() return NULL");
      goto failed3;
    }

  ssl->client_CA = __X509_new(ctx->client_CA);
  if (!ssl->client_CA)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "__X509_new() return NULL");
      goto failed4;
    }

  ssl->ctx = ctx;
  ssl->method = ctx->method;

  ssl->version = ctx->version;
  ssl->options = ctx->options;

  ssl->verify_mode = ctx->verify_mode;

  ret = SSL_METHOD_CALL(new, ssl);
  if (ret)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "SSL_METHOD_CALL(new) return %d", ret);
      goto failed5;
    }

  _ssl_set_alpn_list(ssl);

  ssl->rwstate = SSL_NOTHING;

  return ssl;

failed5:
  X509_free(ssl->client_CA);
failed4:
  ssl_cert_free(ssl->cert);
failed3:
  SSL_SESSION_free(ssl->session);
failed2:
  ssl_mem_free(ssl);
failed1:
  return NULL;
}

void SSL_free(SSL *ssl)
{
  SSL_ASSERT3(ssl);

  SSL_METHOD_CALL(free, ssl);

  X509_free(ssl->client_CA);

  ssl_cert_free(ssl->cert);

  SSL_SESSION_free(ssl->session);

  if (ssl->alpn_protos)
    {
      ssl_mem_free(ssl->alpn_protos);
    }

  ssl_mem_free(ssl);
}

int SSL_do_handshake(SSL *ssl)
{
  int ret;

  SSL_ASSERT1(ssl);

  ret = SSL_METHOD_CALL(handshake, ssl);

  return ret;
}

int SSL_connect(SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return SSL_do_handshake(ssl);
}

int SSL_accept(SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return SSL_do_handshake(ssl);
}

int SSL_shutdown(SSL *ssl)
{
  int ret;

  SSL_ASSERT1(ssl);

  if (SSL_get_state(ssl) != TLS_ST_OK)
    {
      return 1;
    }

  ret = SSL_METHOD_CALL(shutdown, ssl);

  return ret;
}

int SSL_clear(SSL *ssl)
{
  int ret;

  SSL_ASSERT1(ssl);

  ret = SSL_shutdown(ssl);
  if (1 != ret)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "SSL_shutdown return %d", ret);
      goto failed1;
    }

  SSL_METHOD_CALL(free, ssl);

  ret = SSL_METHOD_CALL(new, ssl);
  if (!ret)
    {
      SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "SSL_METHOD_CALL(new) return %d", ret);
      goto failed1;
    }

  return 1;

failed1:
  return ret;
}

int SSL_read(SSL *ssl, void *buffer, int len)
{
  int ret;

  SSL_ASSERT1(ssl);
  SSL_ASSERT1(buffer);
  SSL_ASSERT1(len);

  ssl->rwstate = SSL_READING;

  ret = SSL_METHOD_CALL(read, ssl, buffer, len);

  if (ret == len)
    {
      ssl->rwstate = SSL_NOTHING;
    }

  return ret;
}

int SSL_write(SSL *ssl, const void *buffer, int len)
{
  int ret;
  int send_bytes;
  int bytes;
  const unsigned char *pbuf;

  SSL_ASSERT1(ssl);
  SSL_ASSERT1(buffer);
  SSL_ASSERT1(len);

  ssl->rwstate = SSL_WRITING;

  send_bytes = len;
  pbuf = (const unsigned char *)buffer;

  do
    {
      if (send_bytes > SSL_SEND_DATA_MAX_LENGTH)
        {
          bytes = SSL_SEND_DATA_MAX_LENGTH;
        }
      else
        {
          bytes = send_bytes;
        }

      if (ssl->interrupted_remaining_write)
        {
          bytes = ssl->interrupted_remaining_write;
          ssl->interrupted_remaining_write = 0;
        }

      ret = SSL_METHOD_CALL(send, ssl, pbuf, bytes);

      /* the return is a NEGATIVE OpenSSL error code, or the length sent */

      if (ret > 0)
        {
          pbuf += ret;
          send_bytes -= ret;
        }
      else
        {
          ssl->interrupted_remaining_write = bytes;
        }
    }
  while (ret > 0 && send_bytes && ret == bytes);

  if (ret >= 0)
    {
      ret = len - send_bytes;
      if (!ret)
        {
          ssl->rwstate = SSL_NOTHING;
        }
    }
  else
    {
      if (send_bytes == len)
        {
          ret = -1;
        }
      else
        {
          ret = len - send_bytes;
        }
    }

  return ret;
}

SSL_CTX *SSL_get_SSL_CTX(const SSL *ssl)
{
  SSL_ASSERT2(ssl);

  return ssl->ctx;
}

const SSL_METHOD *SSL_get_ssl_method(SSL *ssl)
{
  SSL_ASSERT2(ssl);

  return ssl->method;
}

int SSL_set_ssl_method(SSL *ssl, const SSL_METHOD *method)
{
  int ret;

  SSL_ASSERT1(ssl);
  SSL_ASSERT1(method);

  if (ssl->version != method->version)
    {
      ret = SSL_shutdown(ssl);
      if (1 != ret)
        {
          SSL_DEBUG(SSL_LIB_ERROR_LEVEL, "SSL_shutdown return %d", ret);
          goto failed1;
        }

      SSL_METHOD_CALL(free, ssl);

      ssl->method = method;

      ret = SSL_METHOD_CALL(new, ssl);
      if (!ret)
        {
          SSL_DEBUG(SSL_LIB_ERROR_LEVEL,
                    "SSL_METHOD_CALL(new) return %d", ret);
          goto failed1;
        }
    }
  else
    {
      ssl->method = method;
    }

  return 1;

failed1:
  return ret;
}

int SSL_get_shutdown(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return ssl->shutdown;
}

void SSL_set_shutdown(SSL *ssl, int mode)
{
  SSL_ASSERT3(ssl);

  ssl->shutdown = mode;
}

int SSL_pending(const SSL *ssl)
{
  int ret;

  SSL_ASSERT1(ssl);

  ret = SSL_METHOD_CALL(pending, ssl);

  return ret;
}

int SSL_has_pending(const SSL *ssl)
{
  int ret;

  SSL_ASSERT1(ssl);

  if (SSL_pending(ssl))
    {
      ret = 1;
    }
  else
    {
      ret = 0;
    }

  return ret;
}

unsigned long SSL_CTX_clear_options(SSL_CTX *ctx, unsigned long op)
{
  SSL_ASSERT1(ctx);

  return ctx->options &= ~op;
}

unsigned long SSL_CTX_get_options(SSL_CTX *ctx)
{
  SSL_ASSERT1(ctx);

  return ctx->options;
}

unsigned long SSL_clear_options(SSL *ssl, unsigned long op)
{
  SSL_ASSERT1(ssl);

  return ssl->options & ~op;
}

unsigned long SSL_get_options(SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return ssl->options;
}

unsigned long SSL_set_options(SSL *ssl, unsigned long op)
{
  SSL_ASSERT1(ssl);

  return ssl->options |= op;
}

int SSL_get_fd(const SSL *ssl)
{
  int ret;

  SSL_ASSERT1(ssl);

  ret = SSL_METHOD_CALL(get_fd, ssl, 0);

  return ret;
}

int SSL_get_rfd(const SSL *ssl)
{
  int ret;

  SSL_ASSERT1(ssl);

  ret = SSL_METHOD_CALL(get_fd, ssl, 0);

  return ret;
}

int SSL_get_wfd(const SSL *ssl)
{
  int ret;

  SSL_ASSERT1(ssl);

  ret = SSL_METHOD_CALL(get_fd, ssl, 0);

  return ret;
}

int SSL_set_fd(SSL *ssl, int fd)
{
  SSL_ASSERT1(ssl);
  SSL_ASSERT1(fd >= 0);

  SSL_METHOD_CALL(set_fd, ssl, fd, 0);

  return 1;
}

int SSL_set_rfd(SSL *ssl, int fd)
{
  SSL_ASSERT1(ssl);
  SSL_ASSERT1(fd >= 0);

  SSL_METHOD_CALL(set_fd, ssl, fd, 0);

  return 1;
}

int SSL_set_wfd(SSL *ssl, int fd)
{
  SSL_ASSERT1(ssl);
  SSL_ASSERT1(fd >= 0);

  SSL_METHOD_CALL(set_fd, ssl, fd, 0);

  return 1;
}

int SSL_version(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return ssl->version;
}

const char *SSL_alert_type_string(int value)
{
  const char *str;

  switch (value >> 8)
    {
      case SSL3_AL_WARNING:
        str = "W";
        break;
      case SSL3_AL_FATAL:
        str = "F";
        break;
      default:
        str = "U";
        break;
    }

  return str;
}

void SSL_CTX_set_default_read_buffer_len(SSL_CTX *ctx, size_t len)
{
  SSL_ASSERT3(ctx);

  ctx->read_buffer_len = (int)len;
}

void SSL_set_default_read_buffer_len(SSL *ssl, size_t len)
{
  SSL_ASSERT3(ssl);
  SSL_ASSERT3(len);

  SSL_METHOD_CALL(set_bufflen, ssl, (int)len);
}

void SSL_set_info_callback(SSL *ssl,
                           void (*cb) (const SSL *ssl, int type, int val))
{
  SSL_ASSERT3(ssl);

  ssl->info_callback = cb;
}

int SSL_CTX_up_ref(SSL_CTX *ctx)
{
  SSL_ASSERT1(ctx);

  /* no support multi-thread SSL here */

  ctx->references++;

  return 1;
}

void SSL_set_security_level(SSL *ssl, int level)
{
  SSL_ASSERT3(ssl);

  ssl->cert->sec_level = level;
}

int SSL_get_security_level(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return ssl->cert->sec_level;
}

int SSL_CTX_get_verify_mode(const SSL_CTX *ctx)
{
  SSL_ASSERT1(ctx);

  return ctx->verify_mode;
}

long SSL_CTX_set_timeout(SSL_CTX *ctx, long t)
{
  long l;

  SSL_ASSERT1(ctx);

  l = ctx->session_timeout;
  ctx->session_timeout = t;

  return l;
}

long SSL_CTX_get_timeout(const SSL_CTX *ctx)
{
  SSL_ASSERT1(ctx);

  return ctx->session_timeout;
}

void SSL_set_read_ahead(SSL *ssl, int yes)
{
  SSL_ASSERT3(ssl);

  ssl->rlayer.read_ahead = yes;
}

void SSL_CTX_set_read_ahead(SSL_CTX *ctx, int yes)
{
  SSL_ASSERT3(ctx);

  ctx->read_ahead = yes;
}

int SSL_get_read_ahead(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return ssl->rlayer.read_ahead;
}

long SSL_CTX_get_read_ahead(SSL_CTX *ctx)
{
  SSL_ASSERT1(ctx);

  return ctx->read_ahead;
}

long SSL_CTX_get_default_read_ahead(SSL_CTX *ctx)
{
  SSL_ASSERT1(ctx);

  return ctx->read_ahead;
}

long SSL_set_time(SSL *ssl, long t)
{
  SSL_ASSERT1(ssl);

  ssl->session->time = t;

  return t;
}

long SSL_set_timeout(SSL *ssl, long t)
{
  SSL_ASSERT1(ssl);

  ssl->session->timeout = t;

  return t;
}

long SSL_get_verify_result(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return SSL_METHOD_CALL(get_verify_result, ssl);
}

int SSL_CTX_get_verify_depth(const SSL_CTX *ctx)
{
  SSL_ASSERT1(ctx);

  return ctx->param.depth;
}

void SSL_CTX_set_verify_depth(SSL_CTX *ctx, int depth)
{
  SSL_ASSERT3(ctx);

  ctx->param.depth = depth;
}

int SSL_get_verify_depth(const SSL *ssl)
{
  SSL_ASSERT1(ssl);

  return ssl->param.depth;
}

void SSL_set_verify_depth(SSL *ssl, int depth)
{
  SSL_ASSERT3(ssl);

  ssl->param.depth = depth;
}

void SSL_CTX_set_verify(SSL_CTX *ctx, int mode,
                        int (*verify_callback)(int, X509_STORE_CTX *))
{
  SSL_ASSERT3(ctx);

  ctx->verify_mode = mode;
  ctx->default_verify_callback = verify_callback;
}

void SSL_set_verify(SSL *ssl, int mode,
                    int (*verify_callback)(int, X509_STORE_CTX *))
{
  SSL_ASSERT3(ssl);

  ssl->verify_mode = mode;
  ssl->verify_callback = verify_callback;
}

void *SSL_CTX_get_ex_data(const SSL_CTX *ctx, int idx)
{
  return NULL;
}

void SSL_CTX_set_alpn_select_cb(SSL_CTX *ctx, next_proto_cb cb, void *arg)
{
  struct alpn_ctx *ac = arg;

  ctx->alpn_cb = cb;

  _openssl_alpn_to_mbedtls(ac, (char * **)&ctx->alpn_protos);
}

void SSL_set_alpn_select_cb(SSL *ssl, void *arg)
{
  struct alpn_ctx *ac = arg;

  _openssl_alpn_to_mbedtls(ac, (char * **)&ssl->alpn_protos);

  _ssl_set_alpn_list(ssl);
}
