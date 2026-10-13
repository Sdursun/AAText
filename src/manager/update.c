/*
 * AATextManager - update check over HTTPS (bsdsocket.library, AmiSSL 5).
 * See update.h.
 *
 * Requests are HTTP/1.0, so the answers are never "chunked": the body
 * simply runs to the end of the connection. The server certificate is
 * verified against AmiSSL's certificate store; there is no way around a
 * failed verification.
 */

#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>
#include <proto/socket.h>
#include <clib/alib_protos.h>

#include <amissl/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>

#include "update.h"

struct Library *SocketBase, *AmiSSLMasterBase, *AmiSSLBase, *AmiSSLExtBase;
static LONG upd_errno;
extern ULONG RangeSeed;         /* amiga.lib, for RangeRand() */

#define API_HOST    "api.github.com"
#define API_PATH    "/repos/Sdursun/AAText/releases/latest"
#define ASSET_NAME  "AAText.lha"
#define MAX_JSON    (256 * 1024)
#define MAX_REDIRECTS 5

/* "0.21" or "v0.21" -> 21 + 1000 * 0 (major * 1000 + minor) */
static LONG VersionNumber(const char *s)
{
    LONG major = 0, minor = 0;

    if (*s == 'v' || *s == 'V')
        s++;
    while (*s >= '0' && *s <= '9')
        major = major * 10 + *s++ - '0';
    if (*s == '.')
        s++;
    while (*s >= '0' && *s <= '9')
        minor = minor * 10 + *s++ - '0';
    return major * 1000 + minor;
}

/* The string value of "key" in json into buf, or FALSE. Values here are
   plain (tags and URLs), so escapes are not handled. */
static BOOL JsonString(const char *json, const char *key, const char *after,
                       char *buf, LONG len)
{
    char pat[48];
    const char *p, *q;
    LONG n;

    sprintf(pat, "\"%s\"", key);
    p = strstr(after ? after : json, pat);
    if (!p)
        return FALSE;
    p += strlen(pat);
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    if (*p != '"')
        return FALSE;
    q = strchr(++p, '"');
    if (!q || (n = q - p) >= len)
        return FALSE;
    memcpy(buf, p, n);
    buf[n] = 0;
    return TRUE;
}

/* "https://host/path" -> host, path */
static BOOL SplitUrl(const char *url, char *host, LONG hlen, char *path,
                     LONG plen)
{
    const char *p, *slash;
    LONG n;

    if (strncmp(url, "https://", 8))
        return FALSE;
    p = url + 8;
    slash = strchr(p, '/');
    n = slash ? slash - p : (LONG)strlen(p);
    if (n <= 0 || n >= hlen || strlen(slash ? slash : "/") >= (size_t)plen)
        return FALSE;
    memcpy(host, p, n);
    host[n] = 0;
    strcpy(path, slash ? slash : "/");
    return TRUE;
}

static LONG Connect(const char *host)
{
    struct sockaddr_in addr;
    struct hostent *he = gethostbyname((STRPTR)host);
    LONG s;

    if (!he)
        return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(443);
    addr.sin_len = sizeof(addr);
    memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        CloseSocket(s);
        return -1;
    }
    return s;
}

/*
 * GET https://host/path. The body goes to mem (room for max bytes,
 * NUL-terminated) or to the file fh. Returns the HTTP status (0 on
 * error, u->error set); a redirect's Location is left in location.
 */
static LONG HttpsGet(SSL_CTX *ctx, const char *host, const char *path,
                     const char *myversion, char *mem, LONG max, BPTR fh,
                     char *location, LONG loclen, struct AAUpdate *u)
{
    static char buf[32768];         /* GitHub headers can exceed 4 KB */
    static char req[2400];
    SSL *ssl = NULL;
    LONG sock, n, status = 0, used = 0, got = 0;
    BOOL header = TRUE;

    location[0] = 0;
    if ((sock = Connect(host)) < 0)
    {
        sprintf(u->error, "cannot connect to %s", host);
        return 0;
    }
    if (!(ssl = SSL_new(ctx)))
    {
        strcpy(u->error, "SSL_new failed");
        CloseSocket(sock);
        return 0;
    }
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, host);
    if (SSL_connect(ssl) <= 0 || SSL_get_verify_result(ssl) != X509_V_OK)
    {
        sprintf(u->error, "secure connection to %s failed (certificate?)",
                host);
        goto done;
    }

    sprintf(req, "GET %s HTTP/1.0\r\nHost: %s\r\n"
                 "User-Agent: AATextManager/%s (AmigaOS)\r\n"
                 "Accept: application/vnd.github+json, */*\r\n\r\n",
            path, host, myversion);
    if (SSL_write(ssl, req, strlen(req)) <= 0)
    {
        strcpy(u->error, "cannot send the request");
        goto done;
    }

    /* header (kept in buf until its end), then the body */
    while ((n = SSL_read(ssl, buf + used, sizeof(buf) - 1 - used)) > 0)
    {
        char *body = buf;
        LONG bodylen = n;

        if (header)
        {
            char *end, *p;

            used += n;
            buf[used] = 0;
            if (!(end = strstr(buf, "\r\n\r\n")))
            {
                if (used >= (LONG)sizeof(buf) - 1)
                    break;          /* header too long */
                continue;
            }
            *end = 0;
            if (!strncmp(buf, "HTTP/", 5) && (p = strchr(buf, ' ')))
                status = atol(p + 1);
            if ((p = strstr(buf, "\nLocation:")) ||
                (p = strstr(buf, "\nlocation:")))
            {
                char *e;

                p += 10;
                while (*p == ' ')
                    p++;
                e = strchr(p, '\r');
                n = e ? e - p : (LONG)strlen(p);
                if (n < loclen)
                {
                    memcpy(location, p, n);
                    location[n] = 0;
                }
            }
            header = FALSE;
            body = end + 4;
            bodylen = used - (body - buf);
            used = 0;
        }
        if (status != 200)
            continue;               /* drain redirects and errors */
        if (fh)
        {
            if (bodylen && Write(fh, body, bodylen) != bodylen)
            {
                strcpy(u->error, "cannot write the file");
                status = 0;
                goto done;
            }
        }
        else if (mem && got + bodylen < max)
        {
            memcpy(mem + got, body, bodylen);
            got += bodylen;
            mem[got] = 0;
        }
    }
    if (header)
    {
        sprintf(u->error, "no answer from %s", host);
        status = 0;
    }

done:
    if (ssl)
    {
        SSL_shutdown(ssl);
        SSL_free(ssl);
    }
    CloseSocket(sock);
    return status;
}

LONG upd_Check(const char *myversion, struct AAUpdate *u)
{
    /* GitHub's download addresses are long (900+ characters) */
    static char host[128], path[2048], url[2048], location[2048];
    char *json = NULL;
    SSL_CTX *ctx = NULL;
    LONG result = UPD_FAILED, status, i;
    BOOL opened = FALSE;

    memset(u, 0, sizeof(*u));
    SocketBase = OpenLibrary((CONST_STRPTR)"bsdsocket.library", 4);
    if (!SocketBase)
        return UPD_NONET;
    AmiSSLMasterBase = OpenLibrary((CONST_STRPTR)"amisslmaster.library",
                                   AMISSLMASTER_MIN_VERSION);
    if (!AmiSSLMasterBase ||
        OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
                       AmiSSL_UsesOpenSSLStructs, FALSE,
                       AmiSSL_GetAmiSSLBase, (ULONG)&AmiSSLBase,
                       AmiSSL_GetAmiSSLExtBase, (ULONG)&AmiSSLExtBase,
                       AmiSSL_SocketBase, (ULONG)SocketBase,
                       AmiSSL_ErrNoPtr, (ULONG)&upd_errno,
                       TAG_DONE) != 0)
    {
        result = UPD_NOSSL;
        goto out;
    }
    opened = TRUE;

    /* some entropy for OpenSSL on systems without a random source */
    {
        struct DateStamp ds;
        UWORD seed[64];

        DateStamp(&ds);
        RangeSeed = ds.ds_Tick ^ (ds.ds_Minute << 12) ^ (ULONG)FindTask(NULL);
        for (i = 0; i < 64; i++)
            seed[i] = RangeRand(65535);
        RAND_seed(seed, sizeof(seed));
    }

    if (!(ctx = SSL_CTX_new(TLS_client_method())))
    {
        strcpy(u->error, "SSL_CTX_new failed");
        goto out;
    }
    SSL_CTX_set_default_verify_paths(ctx);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);

    /* the latest release */
    if (!(json = AllocVec(MAX_JSON, MEMF_ANY | MEMF_CLEAR)))
        goto out;
    status = HttpsGet(ctx, API_HOST, API_PATH, myversion, json, MAX_JSON,
                      0, location, sizeof(location), u);
    if (status != 200)
    {
        if (status)
            sprintf(u->error, "GitHub answered %ld", (long)status);
        goto out;
    }
    if (!JsonString(json, "tag_name", NULL, u->tag, sizeof(u->tag)))
    {
        strcpy(u->error, "no release found");
        goto out;
    }
    if (VersionNumber(u->tag) <= VersionNumber(myversion))
    {
        result = UPD_CURRENT;
        goto out;
    }

    /* its package: the asset named AAText.lha */
    {
        const char *a = strstr(json, "\"name\": \"" ASSET_NAME "\"");

        if (!a)
            a = strstr(json, "\"name\":\"" ASSET_NAME "\"");
        if (!a || !JsonString(json, "browser_download_url", a, url,
                              sizeof(url)))
        {
            sprintf(u->error, "release %s has no " ASSET_NAME, u->tag);
            goto out;
        }
    }

    /* download, following the redirects to GitHub's file servers */
    sprintf(u->saved, "RAM:AAText-%s.lha", u->tag);
    for (i = 0; i < MAX_REDIRECTS; i++)
    {
        BPTR fh;

        if (!SplitUrl(url, host, sizeof(host), path, sizeof(path)))
        {
            sprintf(u->error, "unexpected download address");
            break;
        }
        if (!(fh = Open((CONST_STRPTR)u->saved, MODE_NEWFILE)))
        {
            sprintf(u->error, "cannot write %s", u->saved);
            break;
        }
        status = HttpsGet(ctx, host, path, myversion, NULL, 0, fh,
                          location, sizeof(location), u);
        Close(fh);
        if (status == 200)
        {
            result = UPD_NEWER;
            break;
        }
        DeleteFile((CONST_STRPTR)u->saved);
        if ((status == 301 || status == 302 || status == 303 ||
             status == 307 || status == 308) && location[0] &&
            strlen(location) < sizeof(url))
        {
            strcpy(url, location);
            continue;
        }
        if (status)
            sprintf(u->error, "download: %s answered %ld", host,
                    (long)status);
        break;
    }
    if (result != UPD_NEWER && !u->error[0])
        strcpy(u->error, "too many redirections");

out:
    if (result == UPD_NEWER || result == UPD_CURRENT)
        u->error[0] = 0;
    if (json)
        FreeVec(json);
    if (ctx)
        SSL_CTX_free(ctx);
    if (opened)
        CloseAmiSSL();
    if (AmiSSLMasterBase)
        CloseLibrary(AmiSSLMasterBase);
    CloseLibrary(SocketBase);
    AmiSSLMasterBase = AmiSSLBase = AmiSSLExtBase = NULL;
    SocketBase = NULL;
    return result;
}
