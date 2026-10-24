/*
 * servedir PORT DIR
 *
 * Serves the files under DIR over HTTP on 127.0.0.1:PORT, GET and HEAD only,
 * one connection at a time. For browser tests on the unit: the container shares
 * the host's network namespace, so its browsers reach this at
 * http://127.0.0.1:PORT/ with no network and no adb reverse (the host adbd has
 * none). Pages saved with their requisites (wget -p -k) load the same way every
 * run. Paths with ".." are refused; a directory serves its index.html.
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static const struct {
    const char *ext, *type;
} types[] = {
    {".html", "text/html; charset=utf-8"}, {".htm", "text/html; charset=utf-8"},
    {".css", "text/css"}, {".js", "application/javascript"},
    {".json", "application/json"}, {".svg", "image/svg+xml"},
    {".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
    {".gif", "image/gif"}, {".webp", "image/webp"}, {".ico", "image/x-icon"},
    {".woff", "font/woff"}, {".woff2", "font/woff2"}, {".txt", "text/plain"},
};

static const char *type_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    unsigned i;

    for (i = 0; dot && i < sizeof(types) / sizeof(types[0]); i++)
        if (!strcasecmp(dot, types[i].ext))
            return types[i].type;
    return "application/octet-stream";
}

/* %XX in place; wget -k writes a saved name's "%3F" as "%253F" in links. */
static void unescape(char *s)
{
    char *o = s;
    unsigned v;

    for (; *s; s++, o++) {
        if (s[0] == '%' && s[1] && s[2] && sscanf(s + 1, "%2x", &v) == 1) {
            *o = (char) v;
            s += 2;
        } else {
            *o = *s;
        }
    }
    *o = 0;
}

static void reply(int c, const char *status, const char *type, long long len)
{
    char head[256];
    int n = snprintf(head, sizeof(head),
                     "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %lld\r\n"
                     "Connection: close\r\n\r\n", status, type, len);

    if (write(c, head, n) < 0)
        return;
}

static void serve(int c, const char *root)
{
    char req[4096], path[4096], buf[65536];
    char *method, *uri, *q;
    struct stat st;
    ssize_t n, len = 0;
    int fd, head;

    req[0] = 0;
    while (len < (ssize_t) sizeof(req) - 1 && !strstr(req, "\r\n")) {
        n = read(c, req + len, sizeof(req) - 1 - len);
        if (n <= 0)
            return;
        len += n;
        req[len] = 0;
    }
    method = strtok(req, " ");
    uri = strtok(NULL, " ");
    if (uri && (q = strpbrk(uri, "?#")))
        *q = 0;
    if (uri)
        unescape(uri);
    if (!method || !uri || uri[0] != '/' || strstr(uri, "..")) {
        reply(c, "400 Bad Request", "text/plain", 0);
        return;
    }
    head = !strcmp(method, "HEAD");
    if (!head && strcmp(method, "GET")) {
        reply(c, "405 Method Not Allowed", "text/plain", 0);
        return;
    }
    snprintf(path, sizeof(path), "%s%s", root, uri);
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        strncat(path, path[strlen(path) - 1] == '/' ? "index.html" : "/index.html",
                sizeof(path) - strlen(path) - 1);
    fd = open(path, O_RDONLY);
    if (fd < 0 || fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {
        reply(c, "404 Not Found", "text/plain", 0);
        if (fd >= 0)
            close(fd);
        return;
    }
    reply(c, "200 OK", type_of(path), (long long) st.st_size);
    while (!head && (n = read(fd, buf, sizeof(buf))) > 0)
        if (write(c, buf, n) != n)
            break;
    close(fd);
    printf("%s %s %lld\n", method, uri, (long long) st.st_size);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    struct sockaddr_in addr;
    int s, c, one = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: servedir PORT DIR\n");
        return 64;
    }
    signal(SIGPIPE, SIG_IGN);
    s = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s < 0 || bind(s, (struct sockaddr *) &addr, sizeof(addr)) < 0 || listen(s, 16) < 0) {
        fprintf(stderr, "servedir: port %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    for (;;) {
        c = accept(s, NULL, NULL);
        if (c < 0)
            continue;
        serve(c, argv[2]);
        close(c);
    }
}
