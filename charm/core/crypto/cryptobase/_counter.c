/*
 *  _counter.c: Fast counter for use with CTR-mode ciphers
 *
 * Written in 2008 by Dwayne C. Litzenberger <dlitz@dlitz.net>
 *
 * ===================================================================
 * The contents of this file are dedicated to the public domain.  To
 * the extent that dedication to the public domain is not available,
 * everyone is granted a worldwide, perpetual, royalty-free,
 * non-exclusive license to exercise all rights associated with the
 * contents of this file for any purpose whatsoever.
 * No rights are reserved.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * ===================================================================
 */

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "_counter.h"

#include "pycrypto_compat.h"

/* NB: This can be called multiple times for a given object, via the __init__ method.  Be careful. */
static int
CounterObject_init(PCT_CounterObject *self, PyObject *args, PyObject *kwargs)
{
    PyUnicodeObject *prefix=NULL, *suffix=NULL, *initval=NULL;
    int allow_wraparound = 0;
    int disable_shortcut = 0;
    Py_ssize_t size;

    static char *kwlist[] = {"prefix", "suffix", "initval", "allow_wraparound", "disable_shortcut", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "SSS|ii", kwlist, &prefix, &suffix, &initval, &allow_wraparound, &disable_shortcut))
        return -1;

    /* Check string size and set nbytes */
    size = PyUnicode_GET_SIZE(initval);
    if (size < 1) {
        PyErr_SetString(PyExc_ValueError, "initval length too small (must be >= 1 byte)");
        return -1;
    } else if (size > 0xffff) {
        PyErr_SetString(PyExc_ValueError, "initval length too large (must be <= 65535 bytes)");
        return -1;
    }
    self->nbytes = (uint16_t) size;

    /* Check prefix length */
    size = PyUnicode_GET_SIZE(prefix);
    assert(size >= 0);
    if (size > 0xffff) {
        PyErr_SetString(PyExc_ValueError, "prefix length too large (must be <= 65535 bytes)");
        return -1;
    }

    /* Check suffix length */
    size = PyUnicode_GET_SIZE(suffix);
    assert(size >= 0);
    if (size > 0xffff) {
        PyErr_SetString(PyExc_ValueError, "suffix length too large (must be <= 65535 bytes)");
        return -1;
    }

    /* Set prefix, being careful to properly discard any old reference */
    Py_CLEAR(self->prefix);
    Py_INCREF(prefix);
    self->prefix = prefix;

    /* Set prefix, being careful to properly discard any old reference */
    Py_CLEAR(self->suffix);
    Py_INCREF(suffix);
    self->suffix = suffix;

    /* Free old buffer (if any) */
    if (self->val) {
        PyMem_Free(self->val);
        self->val = self->p = NULL;
        self->buf_size = 0;
    }

    /* Allocate new buffer */
    /* buf_size won't overflow because the length of each string will always be <= 0xffff */
    self->buf_size = PyUnicode_GET_SIZE(prefix) + PyUnicode_GET_SIZE(suffix) + self->nbytes;
    self->val = self->p = PyMem_Malloc(self->buf_size);
    if (self->val == NULL) {
        self->buf_size = 0;
        return -1;
    }
    self->p = self->val + PyUnicode_GET_SIZE(prefix);

    /* Sanity-check pointers */
    assert(self->val <= self->p);
    assert(self->p + self->nbytes <= self->val + self->buf_size);
    assert(self->val + PyUnicode_GET_SIZE(self->prefix) == self->p);
    assert(PyUnicode_GET_SIZE(self->prefix) + self->nbytes + PyUnicode_GET_SIZE(self->suffix) == self->buf_size);

    /* Copy the prefix, suffix, and initial value into the buffer. */
    memcpy(self->val, PyUnicode_AS_STRING(prefix), PyUnicode_GET_SIZE(prefix));
    memcpy(self->p, PyUnicode_AS_STRING(initval), self->nbytes);
    memcpy(self->p + self->nbytes, PyUnicode_AS_STRING(suffix), PyUnicode_GET_SIZE(suffix));

    /* Set shortcut_disabled and allow_wraparound */
    self->shortcut_disabled = disable_shortcut;
    self->allow_wraparound = allow_wraparound;

    /* Clear the carry flag */
    self->carry = 0;

    return 0;
}

static void
CounterObject_dealloc(PCT_CounterObject *self)
{
    /* Free the buffer */
    if (self->val) {
        memset(self->val, 0, self->buf_size);   /* wipe the buffer before freeing it */
        PyMem_Free(self->val);
        self->val = self->p = NULL;
        self->buf_size = 0;
    }

    /* Deallocate the prefix and suffix, if they are present. */
    Py_CLEAR(self->prefix);
    Py_CLEAR(self->suffix);

    /* Free this object */
    PyObject_Del(self);
}

static inline PyObject *
_CounterObject_next_value(PCT_CounterObject *self, int little_endian)
{
    unsigned int i;
    int increment;
    uint8_t *p;
    PyObject *eight = NULL;
    PyObject *ch = NULL;
    PyObject *y = NULL;
    PyObject *x = NULL;

    if (self->carry && !self->allow_wraparound) {
        PyErr_SetString(PyExc_OverflowError,
                "counter wrapped without allow_wraparound");
        goto err_out;
    }

    eight = PyInt_FromLong(8);
    if (!eight)
        goto err_out;

    /* Make a new Python long integer */
    x = PyLong_FromUnsignedLong(0);
    if (!x)
        goto err_out;

    if (little_endian) {
        /* little endian */
        p = self->p + self->nbytes - 1;
        increment = -1;
    } else {
        /* big endian */
        p = self->p;
        increment = 1;
    }
    for (i = 0; i < self->nbytes; i++, p += increment) {
        /* Sanity check pointer */
        assert(self->p <= p);
        assert(p < self->p + self->nbytes);

        /* ch = ord(p) */
        Py_CLEAR(ch);   /* delete old ch */
        ch = PyInt_FromLong((long) *p);
        if (!ch)
            goto err_out;

        /* y = x << 8 */
        Py_CLEAR(y);    /* delete old y */
        y = PyNumber_Lshift(x, eight);
        if (!y)
            goto err_out;

        /* x = y | ch */
        Py_CLEAR(x);    /* delete old x */
        x = PyNumber_Or(y, ch);
    }

    Py_CLEAR(eight);
    Py_CLEAR(ch);
    Py_CLEAR(y);
    return x;

err_out:
    Py_CLEAR(eight);
    Py_CLEAR(ch);
    Py_CLEAR(y);
    Py_CLEAR(x);
    return NULL;
}

static PyObject *
CounterLEObject_next_value(PCT_CounterObject *self, PyObject *args)
{
    return _CounterObject_next_value(self, 1);
}

static PyObject *
CounterBEObject_next_value(PCT_CounterObject *self, PyObject *args)
{
    return _CounterObject_next_value(self, 0);
}

static void
CounterLEObject_increment(PCT_CounterObject *self)
{
    unsigned int i, tmp, carry;
    uint8_t *p;

    assert(sizeof(i) >= sizeof(self->nbytes));

    carry = 1;
    p = self->p;
    for (i = 0; i < self->nbytes; i++, p++) {
        /* Sanity check pointer */
        assert(self->p <= p);
        assert(p < self->p + self->nbytes);

        tmp = *p + carry;
        carry = tmp >> 8;   /* This will only ever be 0 or 1 */
        *p = tmp & 0xff;
    }
    self->carry = carry;
}

static void
CounterBEObject_increment(PCT_CounterObject *self)
{
    unsigned int i, tmp, carry;
    uint8_t *p;

    assert(sizeof(i) >= sizeof(self->nbytes));

    carry = 1;
    p = self->p + self->nbytes-1;
    for (i = 0; i < self->nbytes; i++, p--) {
        /* Sanity check pointer */
        assert(self->p <= p);
        assert(p < self->p + self->nbytes);

        tmp = *p + carry;
        carry = tmp >> 8;   /* This will only ever be 0 or 1 */
        *p = tmp & 0xff;
    }
    self->carry = carry;
}

static PyObject *
CounterObject_call(PCT_CounterObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *retval;

    if (self->carry && !self->allow_wraparound) {
        PyErr_SetString(PyExc_OverflowError,
                "counter wrapped without allow_wraparound");
        return NULL;
    }

    retval = (PyObject *)PyUnicode_FromStringAndSize((const char *)self->val, self->buf_size);

    self->inc_func(self);

    return retval;
}

static PyMethodDef CounterLEObject_methods[] = {
    {"next_value", (PyCFunction)CounterLEObject_next_value, METH_VARARGS,
        "Get the numerical value of next value of the counter."},

    {NULL} /* sentinel */
};

static PyMethodDef CounterBEObject_methods[] = {
    {"next_value", (PyCFunction)CounterBEObject_next_value, METH_VARARGS,
        "Get the numerical value of next value of the counter."},

    {NULL} /* sentinel */
};

/* Python 2.1 doesn't allow us to assign methods or attributes to an object,
 * so we hack it here. */
static PyObject *
CounterLEObject_getattr(PyObject *s, char *name)
{
    PCT_CounterObject *self = (PCT_CounterObject *)s;
    if (strcmp(name, "carry") == 0) {
        return PyInt_FromLong((long)self->carry);
    } else if (!self->shortcut_disabled && strcmp(name, "__PCT_CTR_SHORTCUT__") == 0) {
        /* Shortcut hack - See block_template.c */
        Py_INCREF(Py_True);
        return Py_True;
    }
    return Py_FindMethod(CounterLEObject_methods, (PyObject *)self, name);
}

static PyObject *
CounterBEObject_getattr(PyObject *s, char *name)
{
    PCT_CounterObject *self = (PCT_CounterObject *)s;
    if (strcmp(name, "carry") == 0) {
        return PyInt_FromLong((long)self->carry);
    } else if (!self->shortcut_disabled && strcmp(name, "__PCT_CTR_SHORTCUT__") == 0) {
        /* Shortcut hack - See block_template.c */
        Py_INCREF(Py_True);
        return Py_True;
    }

    return Py_FindMethod(CounterBEObject_methods, (PyObject *)self, name);
}

static PyTypeObject
my_CounterLEType = {
    PyObject_HEAD_INIT(NULL)
    0,                              /* ob_size */
	"_counter.CounterLE",           /* tp_name */
	sizeof(PCT_CounterObject),       /* tp_basicsize */
    0,                              /* tp_itemsize */
    (destructor)CounterObject_dealloc, /* tp_dealloc */
    0,                              /* tp_print */
    CounterLEObject_getattr,        /* tp_getattr */
    0,                              /* tp_setattr */
    0,                              /* tp_compare */
    0,                              /* tp_repr */
    0,                              /* tp_as_number */
    0,                              /* tp_as_sequence */
    0,                              /* tp_as_mapping */
    0,                              /* tp_hash */
    (ternaryfunc)CounterObject_call, /* tp_call */
    0,                              /* tp_str */
    0,                              /* tp_getattro */
    0,                              /* tp_setattro */
    0,                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,             /* tp_flags */
    "Counter (little endian)",      /* tp_doc */
};

static PyTypeObject
my_CounterBEType = {
    PyObject_HEAD_INIT(NULL)
    0,                              /* ob_size */
	"_counter.CounterBE",           /* tp_name */
	sizeof(PCT_CounterObject),       /* tp_basicsize */
    0,                              /* tp_itemsize */
    (destructor)CounterObject_dealloc, /* tp_dealloc */
    0,                              /* tp_print */
    CounterBEObject_getattr,        /* tp_getattr */
    0,                              /* tp_setattr */
    0,                              /* tp_compare */
    0,                              /* tp_repr */
    0,                              /* tp_as_number */
    0,                              /* tp_as_sequence */
    0,                              /* tp_as_mapping */
    0,                              /* tp_hash */
    (ternaryfunc)CounterObject_call, /* tp_call */
    0,                              /* tp_str */
    0,                              /* tp_getattro */
    0,                              /* tp_setattro */
    0,                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,             /* tp_flags */
    "Counter (big endian)",         /* tp_doc */
};

/*
 * Python 2.1 doesn't seem to allow a C equivalent of the __init__ method, so
 * we use the module-level functions newLE and newBE here.
 */
static PyObject *
CounterLE_new(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PCT_CounterObject *obj = NULL;

    /* Create the new object */
    obj = PyObject_New(PCT_CounterObject, &my_CounterLEType);
    if (obj == NULL) {
        return NULL;
    }

    /* Zero the custom portion of the structure */
    memset(&obj->prefix, 0, sizeof(PCT_CounterObject) - offsetof(PCT_CounterObject, prefix));

    /* Call the object's initializer.  Delete the object if this fails. */
    if (CounterObject_init(obj, args, kwargs) != 0) {
        return NULL;
    }

    /* Set the inc_func pointer */
    obj->inc_func = (void (*)(void *))CounterLEObject_increment;

    /* Return the object */
    return (PyObject *)obj;
}

static PyObject *
CounterBE_new(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PCT_CounterObject *obj = NULL;

    /* Create the new object */
    obj = PyObject_New(PCT_CounterObject, &my_CounterBEType);
    if (obj == NULL) {
        return NULL;
    }

    /* Zero the custom portion of the structure */
    memset(&obj->prefix, 0, sizeof(PCT_CounterObject) - offsetof(PCT_CounterObject, prefix));

    /* Call the object's initializer.  Delete the object if this fails. */
    if (CounterObject_init(obj, args, kwargs) != 0) {
        return NULL;
    }

    /* Set the inc_func pointer */
    obj->inc_func = (void (*)(void *))CounterBEObject_increment;

    /* Return the object */
    return (PyObject *)obj;
}

/*
 * Module-level method table and module initialization function
 */

static PyMethodDef module_methods[] = {
    {"_newLE", (PyCFunction) CounterLE_new, METH_VARARGS|METH_KEYWORDS, NULL},
    {"_newBE", (PyCFunction) CounterBE_new, METH_VARARGS|METH_KEYWORDS, NULL},
    {NULL, NULL, 0, NULL}   /* end-of-list sentinel value */
};


PyMODINIT_FUNC
init_counter(void)
{
    PyObject *m;

    /* TODO - Is the error handling here correct? */

    /* Initialize the module */
    m = Py_InitModule("_counter", module_methods);
    if (m == NULL)
        return;

    my_CounterLEType.ob_type = &PyType_Type;
    my_CounterBEType.ob_type = &PyType_Type;
}

/* vim:set ts=4 sw=4 sts=4 expandtab: */
