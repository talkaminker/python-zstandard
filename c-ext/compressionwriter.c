/**
* Copyright (c) 2016-present, Gregory Szorc
* All rights reserved.
*
* This software may be modified and distributed under the terms
* of the BSD license. See the LICENSE file for details.
*/

#include "python-zstandard.h"

extern PyObject* ZstdError;

PyDoc_STRVAR(ZstdCompresssionWriter__doc__,
"""A context manager used for writing compressed output to a writer.\n"
);

static void ZstdCompressionWriter_dealloc(ZstdCompressionWriter* self) {
	Py_XDECREF(self->compressor);
	Py_XDECREF(self->writer);

	if (self->cstream) {
		ZSTD_freeCStream(self->cstream);
		self->cstream = NULL;
	}

	PyObject_Del(self);
}

static PyObject* ZstdCompressionWriter_enter(ZstdCompressionWriter* self) {
	if (self->entered) {
		PyErr_SetString(ZstdError, "cannot __enter__ multiple times");
		return NULL;
	}

	self->cstream = CStream_from_ZstdCompressor(self->compressor, self->sourceSize);
	if (!self->cstream) {
		return NULL;
	}

	self->entered = 1;

	Py_INCREF(self);
	return (PyObject*)self;
}

static PyObject* ZstdCompressionWriter_exit(ZstdCompressionWriter* self, PyObject* args) {
	PyObject* exc_type;
	PyObject* exc_value;
	PyObject* exc_tb;
	size_t zresult;

	ZSTD_outBuffer output;
	PyObject* res;

	if (!PyArg_ParseTuple(args, "OOO", &exc_type, &exc_value, &exc_tb)) {
		return NULL;
	}

	self->entered = 0;

	if (self->cstream && exc_type == Py_None && exc_value == Py_None &&
		exc_tb == Py_None) {

		output.dst = malloc(self->outSize);
		if (!output.dst) {
			return PyErr_NoMemory();
		}
		output.size = self->outSize;
		output.pos = 0;

		while (1) {
			zresult = ZSTD_endStream(self->cstream, &output);
			if (ZSTD_isError(zresult)) {
				PyErr_Format(ZstdError, "error ending compression stream: %s",
					ZSTD_getErrorName(zresult));
				free(output.dst);
				return NULL;
			}

			if (output.pos) {
#if PY_MAJOR_VERSION >= 3
				res = PyObject_CallMethod(self->writer, "write", "y#",
#else
				res = PyObject_CallMethod(self->writer, "write", "s#",
#endif
					output.dst, output.pos);
				Py_XDECREF(res);
			}

			if (!zresult) {
				break;
			}

			output.pos = 0;
		}

		free(output.dst);
		ZSTD_freeCStream(self->cstream);
		self->cstream = NULL;
	}

	Py_RETURN_FALSE;
}

static PyObject* ZstdCompressionWriter_memory_size(ZstdCompressionWriter* self) {
	if (!self->cstream) {
		PyErr_SetString(ZstdError, "cannot determine size of an inactive compressor; "
			"call when a context manager is active");
		return NULL;
	}

	return PyLong_FromSize_t(ZSTD_sizeof_CStream(self->cstream));
}

static PyObject* ZstdCompressionWriter_write(ZstdCompressionWriter* self, PyObject* args) {
	const char* source;
	Py_ssize_t sourceSize;
	size_t zresult;
	ZSTD_inBuffer input;
	ZSTD_outBuffer output;
	PyObject* res;

#if PY_MAJOR_VERSION >= 3
	if (!PyArg_ParseTuple(args, "y#", &source, &sourceSize)) {
#else
	if (!PyArg_ParseTuple(args, "s#", &source, &sourceSize)) {
#endif
		return NULL;
	}

	if (!self->entered) {
		PyErr_SetString(ZstdError, "compress must be called from an active context manager");
		return NULL;
	}

	output.dst = malloc(self->outSize);
	if (!output.dst) {
		return PyErr_NoMemory();
	}
	output.size = self->outSize;
	output.pos = 0;

	input.src = source;
	input.size = sourceSize;
	input.pos = 0;

	while ((ssize_t)input.pos < sourceSize) {
		Py_BEGIN_ALLOW_THREADS
		zresult = ZSTD_compressStream(self->cstream, &output, &input);
		Py_END_ALLOW_THREADS

		if (ZSTD_isError(zresult)) {
			free(output.dst);
			PyErr_Format(ZstdError, "zstd compress error: %s", ZSTD_getErrorName(zresult));
			return NULL;
		}

		/* Copy data from output buffer to writer. */
		if (output.pos) {
#if PY_MAJOR_VERSION >= 3
			res = PyObject_CallMethod(self->writer, "write", "y#",
#else
			res = PyObject_CallMethod(self->writer, "write", "s#",
#endif
				output.dst, output.pos);
			Py_XDECREF(res);
		}
		output.pos = 0;
	}

	free(output.dst);

	/* TODO return bytes written */
	Py_RETURN_NONE;
	}

static PyMethodDef ZstdCompressionWriter_methods[] = {
	{ "__enter__", (PyCFunction)ZstdCompressionWriter_enter, METH_NOARGS,
	PyDoc_STR("Enter a compression context.") },
	{ "__exit__", (PyCFunction)ZstdCompressionWriter_exit, METH_VARARGS,
	PyDoc_STR("Exit a compression context.") },
	{ "memory_size", (PyCFunction)ZstdCompressionWriter_memory_size, METH_NOARGS,
	PyDoc_STR("Obtain the memory size of the underlying compressor") },
	{ "write", (PyCFunction)ZstdCompressionWriter_write, METH_VARARGS,
	PyDoc_STR("Compress data") },
	{ NULL, NULL }
};

PyTypeObject ZstdCompressionWriterType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"zstd.ZstdCompressionWriter",  /* tp_name */
	sizeof(ZstdCompressionWriter),  /* tp_basicsize */
	0,                              /* tp_itemsize */
	(destructor)ZstdCompressionWriter_dealloc, /* tp_dealloc */
	0,                              /* tp_print */
	0,                              /* tp_getattr */
	0,                              /* tp_setattr */
	0,                              /* tp_compare */
	0,                              /* tp_repr */
	0,                              /* tp_as_number */
	0,                              /* tp_as_sequence */
	0,                              /* tp_as_mapping */
	0,                              /* tp_hash */
	0,                              /* tp_call */
	0,                              /* tp_str */
	0,                              /* tp_getattro */
	0,                              /* tp_setattro */
	0,                              /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
	ZstdCompresssionWriter__doc__,  /* tp_doc */
	0,                              /* tp_traverse */
	0,                              /* tp_clear */
	0,                              /* tp_richcompare */
	0,                              /* tp_weaklistoffset */
	0,                              /* tp_iter */
	0,                              /* tp_iternext */
	ZstdCompressionWriter_methods,  /* tp_methods */
	0,                              /* tp_members */
	0,                              /* tp_getset */
	0,                              /* tp_base */
	0,                              /* tp_dict */
	0,                              /* tp_descr_get */
	0,                              /* tp_descr_set */
	0,                              /* tp_dictoffset */
	0,                              /* tp_init */
	0,                              /* tp_alloc */
	PyType_GenericNew,              /* tp_new */
};

void compressionwriter_module_init(PyObject* mod) {
	Py_TYPE(&ZstdCompressionWriterType) = &PyType_Type;
	if (PyType_Ready(&ZstdCompressionWriterType) < 0) {
		return;
	}
}
