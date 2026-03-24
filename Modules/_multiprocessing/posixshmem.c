/*
posixshmem - A Python extension that provides shm_open() and shm_unlink()
*/

// Need limited C API version 3.13 for Py_mod_gil
#include "pyconfig.h"   // Py_GIL_DISABLED
#ifndef Py_GIL_DISABLED
#  define Py_LIMITED_API 0x030d0000
#endif

#include <Python.h>

#include <string.h>               // strlen()
#include <errno.h>                // EINTR
#ifdef HAVE_SYS_MMAN_H
#  include <sys/mman.h>           // shm_open(), shm_unlink()
#endif


#ifdef __ANDROID__
#include <errno.h>
#include <fcntl.h>      /* open, O_CLOEXEC          */
#include <limits.h>     /* PATH_MAX                  */
#include <pthread.h>    /* pthread_once              */
#include <stdlib.h>     /* getenv                    */
#include <string.h>     /* stpncpy, strnlen, memchr  */
#include <sys/stat.h>   /* mkdir                     */
#include <unistd.h>     /* close, unlink             */

/* All internal helpers: static + hidden visibility + no name mangling. */
#define _ASHM \
    __attribute__((visibility("hidden"))) static

/* ------------------------------------------------------------------ */
/* Basedir: resolved and cached exactly once via pthread_once           */
/* ------------------------------------------------------------------ */

static const char         *_ashm_basedir = NULL;
static pthread_once_t      _ashm_once    = PTHREAD_ONCE_INIT;

__attribute__((cold))
_ASHM void
_ashm_init(void)
{
    /*
     * @TERMUX_PREFIX@ is substituted by Termux's configure wrapper.
     * When it was not substituted the first byte is still a literal '@'.
     */
    static const char termux_tmp[] = "@TERMUX_PREFIX@/tmp";

    if (termux_tmp[0] != '@') {
        _ashm_basedir = termux_tmp;
    } else {
        const char *t = getenv("TMPDIR");
        _ashm_basedir = (t != NULL && t[0] != '\0') ? t : "/data/local/tmp";
    }

    /* Best-effort mkdir; EEXIST is fine. Errors surface later via open(2). */
    (void)mkdir(_ashm_basedir, 0700);
}

_ASHM const char *
ashm_basedir(void)
{
    (void)pthread_once(&_ashm_once, _ashm_init);
    return _ashm_basedir;
}

/* ------------------------------------------------------------------ */
/* Path construction — no printf, single-pass name validation           */
/* ------------------------------------------------------------------ */

/*
 * Build the backing-file path for POSIX shm name into buf[bufsz].
 *
 * POSIX semantics enforced:
 *   - Strip all leading '/' (spec requires at least one).
 *   - Bare name must be non-empty.
 *   - No '/' within the bare name (flat namespace).
 *
 * Returns 0 on success, -1 with errno set on error.
 */
_ASHM int
ashm_path(const char *name, char *buf, size_t bufsz)
{
    /* Strip mandatory leading slash(es). */
    while (*name == '/')
        ++name;

    /*
     * Validate bare name in one pass:
     *   strnlen → catches empty string
     *   memchr  → rejects embedded slashes (flat namespace)
     */
    size_t nlen = strnlen(name, PATH_MAX);
    if (nlen == 0 || memchr(name, '/', nlen) != NULL) {
        errno = EINVAL;
        return -1;
    }

    const char *base  = ashm_basedir();
    size_t      blen  = strnlen(base, PATH_MAX);

    /* Need: blen + '/' + nlen + '\0' */
    if (blen + 1 + nlen + 1 > bufsz) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* Assemble path with stpncpy — no printf overhead. */
    char *p  = stpncpy(buf,  base, blen);
    *p++     = '/';
    p        = stpncpy(p, name, nlen);
    *p       = '\0';

    return 0;
}

/* ------------------------------------------------------------------ */
/* shm_open(3) emulation                                                */
/* ------------------------------------------------------------------ */

/*
 * Pass O_CLOEXEC straight into open(2) — present on every Android API
 * level we care about (API 9+, Linux ≥ 2.6.23) — so no fcntl round-trip.
 * POSIX shm_open is specified to always return a close-on-exec descriptor.
 */
_ASHM int
shm_open(const char *name, int oflag, mode_t mode)
{
    char path[PATH_MAX];
    if (ashm_path(name, path, sizeof(path)) != 0)
        return -1;

    return open(path, oflag | O_CLOEXEC, mode);
}

/* ------------------------------------------------------------------ */
/* shm_unlink(3) emulation                                              */
/* ------------------------------------------------------------------ */

_ASHM int
shm_unlink(const char *name)
{
    char path[PATH_MAX];
    if (ashm_path(name, path, sizeof(path)) != 0)
        return -1;

    return unlink(path);
}

#undef _ASHM
#endif /* __ANDROID__ */

/*[clinic input]
module _posixshmem
[clinic start generated code]*/
/*[clinic end generated code: output=da39a3ee5e6b4b0d input=a416734e49164bf8]*/

/*
 *
 * Module-level functions & meta stuff
 *
 */

#ifdef HAVE_SHM_OPEN
/*[clinic input]
_posixshmem.shm_open -> int
    path: unicode
    flags: int
    mode: int = 0o777

# "shm_open(path, flags, mode=0o777)\n\n\

Open a shared memory object.  Returns a file descriptor (integer).

[clinic start generated code]*/

static int
_posixshmem_shm_open_impl(PyObject *module, PyObject *path, int flags,
                          int mode)
/*[clinic end generated code: output=8d110171a4fa20df input=e83b58fa802fac25]*/
{
    int fd;
    int async_err = 0;
    Py_ssize_t name_size;
    const char *name = PyUnicode_AsUTF8AndSize(path, &name_size);
    if (name == NULL) {
        return -1;
    }
    if (strlen(name) != (size_t)name_size) {
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        return -1;
    }
    do {
        Py_BEGIN_ALLOW_THREADS
        fd = shm_open(name, flags, mode);
        Py_END_ALLOW_THREADS
    } while (fd < 0 && errno == EINTR && !(async_err = PyErr_CheckSignals()));

    if (fd < 0) {
        if (!async_err)
            PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, path);
        return -1;
    }

    return fd;
}
#endif /* HAVE_SHM_OPEN */

#ifdef HAVE_SHM_UNLINK
/*[clinic input]
_posixshmem.shm_unlink
    path: unicode
    /

Remove a shared memory object (similar to unlink()).

Remove a shared memory object name, and, once all processes  have  unmapped
the object, de-allocates and destroys the contents of the associated memory
region.

[clinic start generated code]*/

static PyObject *
_posixshmem_shm_unlink_impl(PyObject *module, PyObject *path)
/*[clinic end generated code: output=42f8b23d134b9ff5 input=298369d013dcad63]*/
{
    int rv;
    int async_err = 0;
    Py_ssize_t name_size;
    const char *name = PyUnicode_AsUTF8AndSize(path, &name_size);
    if (name == NULL) {
        return NULL;
    }
    if (strlen(name) != (size_t)name_size) {
        PyErr_SetString(PyExc_ValueError, "embedded null character");
        return NULL;
    }
    do {
        Py_BEGIN_ALLOW_THREADS
        rv = shm_unlink(name);
        Py_END_ALLOW_THREADS
    } while (rv < 0 && errno == EINTR && !(async_err = PyErr_CheckSignals()));

    if (rv < 0) {
        if (!async_err)
            PyErr_SetFromErrnoWithFilenameObject(PyExc_OSError, path);
        return NULL;
    }

    Py_RETURN_NONE;
}
#endif /* HAVE_SHM_UNLINK */

#include "clinic/posixshmem.c.h"

static PyMethodDef module_methods[ ] = {
    _POSIXSHMEM_SHM_OPEN_METHODDEF
    _POSIXSHMEM_SHM_UNLINK_METHODDEF
    {NULL} /* Sentinel */
};


static PyModuleDef_Slot module_slots[] = {
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
    {Py_mod_gil, Py_MOD_GIL_NOT_USED},
    {0, NULL}
};


static struct PyModuleDef _posixshmemmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_posixshmem",
    .m_doc = "POSIX shared memory module",
    .m_size = 0,
    .m_methods = module_methods,
    .m_slots = module_slots,
};

/* Module init function */
PyMODINIT_FUNC
PyInit__posixshmem(void)
{
    return PyModuleDef_Init(&_posixshmemmodule);
}
