/*
 * First GPU check for the Android userland: a pbuffer context on the FireOS
 * Mali blobs, one shader draw, and a readback. Needs no window system, so it
 * runs in a chroot of the Fire 7 /system with only /dev, /proc and /sys bound
 * in, after mali_r4p0.ko has replaced the stock driver.
 *
 *   egltest        prints EGL/GL strings and the pixel it read back
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>

static const char *vs =
    "attribute vec4 pos;\n"
    "void main() { gl_Position = pos; }\n";
static const char *fs =
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(0.2, 0.4, 0.6, 1.0); }\n";

static GLuint shader(GLenum type, const char *src)
{
    GLint ok = 0;
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        printf("shader: %s\n", log);
    }
    return s;
}

int main(void)
{
    static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
    EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
                          EGL_ALPHA_SIZE, 8, EGL_NONE };
    EGLint pb_attr[] = { EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE };
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint major, minor, n = 0;
    EGLConfig cfg;
    GLubyte px[4] = { 0 };
    GLuint prog;

    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(d, &major, &minor)) {
        printf("eglInitialize failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("EGL %d.%d %s / %s\n", major, minor, eglQueryString(d, EGL_VENDOR),
           eglQueryString(d, EGL_VERSION));
    if (!eglChooseConfig(d, cfg_attr, &cfg, 1, &n) || n < 1) {
        printf("eglChooseConfig: none (0x%x)\n", eglGetError());
        return 1;
    }
    EGLSurface s = eglCreatePbufferSurface(d, cfg, pb_attr);
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (s == EGL_NO_SURFACE || c == EGL_NO_CONTEXT || !eglMakeCurrent(d, s, s, c)) {
        printf("surface/context failed: 0x%x\n", eglGetError());
        return 1;
    }
    printf("GL %s | %s | %s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
           glGetString(GL_VERSION));

    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(prog, 0, "pos");
    glLinkProgram(prog);
    glUseProgram(prog);
    glViewport(0, 0, 64, 64);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    printf("pixel %u %u %u %u (want about 51 102 153 255; 255 0 0 255 means the draw did not run)\n",
           px[0], px[1], px[2], px[3]);
    printf("glGetError 0x%x\n", glGetError());
    eglTerminate(d);
    return 0;
}
