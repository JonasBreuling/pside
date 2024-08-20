#include <Python.h>
#define NPY_NO_DEPRECATED_API NPY_1_9_API_VERSION
#include "numpy/arrayobject.h"

#ifdef HAVE_BLAS_ILP64
#define F_INT npy_int64
#define F_INT_NPY NPY_INT64
#else
#define F_INT int
#define F_INT_NPY NPY_INT
#endif

typedef struct _pside_globals {
    PyObject *python_function;
    PyObject *t_sol;
    PyObject *y_sol;
    PyObject *yp_sol;
    int nt;
} pside_params;

static pside_params global_params = {NULL, NULL, NULL, NULL, 0};

#if defined(UPPERCASE_FORTRAN)
    #if defined(NO_APPEND_FORTRAN)
        /* nothing to do here */
    #else
        #define PSIDE  PSIDE_
    #endif
#else
    #if defined(NO_APPEND_FORTRAN)
        #define PSIDE  pside
    #else
        #define PSIDE  pside_
    #endif
#endif

typedef void pside_f_t(F_INT *neqn, double *t, double *y, double *ydot, double *f, F_INT *ierr, double *rpar, F_INT *ipar);
typedef void pside_J_t(F_INT ldj, F_INT neqn, F_INT nlj, F_INT nuj, double *t, double *y, double *ydot, double *J, double *rpar, F_INT *ipar);
typedef void pside_M_t(F_INT lmj, F_INT neqn, F_INT nlm, F_INT num, double *t, double *y, double *ydot, double *M, double *rpar, F_INT *ipar);
typedef void pside_solout_t(F_INT *iter, F_INT *neqn, double *t, double *y, double *ydot);

void PSIDE(F_INT *neq, double *y, double *yp, pside_f_t *f, 
           F_INT *jnum /*should be boolean*/, F_INT *nlj, F_INT *nuj, pside_J_t *J, 
           F_INT *mnum /*should be boolean*/, F_INT *nlm, F_INT *num, pside_M_t *M, 
           double *t, double *tend, double *rtol, double *atol, F_INT *IND,
           F_INT *lrwork, double *rwork, F_INT *liwork, F_INT *iwork, 
           double *rpar, F_INT *ipar, F_INT *idid, pside_solout_t *solout);

void pside_f(F_INT *neqn, double *t, double *y, double *yp, double *f, F_INT *ierr, double *rpar, F_INT *ipar)
{
    PyObject *y_object = NULL;
    PyObject *yp_object = NULL;
    PyObject *result = NULL;
    PyObject *arglist = NULL;
    PyArrayObject *result_array = NULL;

    npy_intp dims[1];
    dims[0] = *neqn;

    /* Build numpy arrays from y and yp. */
    y_object = PyArray_SimpleNewFromData(1, dims, NPY_DOUBLE, y);
    if (y_object == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyArray_SimpleNewFromData(*neqn, global_params.dims, NPY_DOUBLE, y) failed.");
        goto fail;
    }
    yp_object = PyArray_SimpleNewFromData(1, dims, NPY_DOUBLE, yp);
    if (yp_object == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyArray_SimpleNewFromData(*neqn, global_params.dims, NPY_DOUBLE, yp) failed.");
        goto fail;
    }

    /* Build argument list. */
    arglist = Py_BuildValue(
        "dOO",
        *t,
        y_object,
        yp_object
    );
    if (arglist == NULL) {
        PyErr_SetString(PyExc_ValueError, "Py_BuildValue failed.");
        goto fail;
    }

    /* Call the Python function. */
    result = PyObject_CallObject(global_params.python_function, arglist);
    if (result == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyObject_CallObject(global_params.python_function, arglist) failed.");
        goto fail;
    }

    /* Build numpy array from result and copy to f. */
    result_array = (PyArrayObject *) PyArray_ContiguousFromObject(result, NPY_DOUBLE, 0, 0);
    if (result_array == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyArray_ContiguousFromObject(result, NPY_DOUBLE, 0, 0) failed.");
        goto fail;
    }

    /* Copy data from the result array to your C array */
    memcpy(f, PyArray_DATA(result_array), PyArray_NBYTES(result_array));

    fail:
        Py_XDECREF(y_object);
        Py_XDECREF(yp_object);
        Py_XDECREF(result);
        Py_XDECREF(arglist);
        Py_XDECREF(result_array);
        return;
}


void pside_J(F_INT ldj, F_INT neqn, F_INT nlj, F_INT nuj, 
             double *t, double *y, double *ydot, double *J, 
             double *rpar, F_INT *ipar){}

void pside_M(F_INT lmj, F_INT neqn, F_INT nlm, F_INT num, 
             double *t, double *y, double *ydot, double *M, 
             double *rpar, F_INT *ipar){}

void pside_solout(F_INT *iter, F_INT *neqn, double *t, double *y, double *yp)
{
    global_params.nt += 1;

    npy_intp dims[1] = {*neqn};
    PyObject *y_array = PyArray_SimpleNew(1, dims, NPY_DOUBLE);
    PyObject *yp_array = PyArray_SimpleNew(1, dims, NPY_DOUBLE);

    memcpy(PyArray_DATA(y_array), y, PyArray_NBYTES(y_array));
    memcpy(PyArray_DATA(yp_array), yp, PyArray_NBYTES(yp_array));

    PyList_Append(global_params.t_sol, PyFloat_FromDouble(*t));
    PyList_Append(global_params.y_sol, y_array);
    PyList_Append(global_params.yp_sol, yp_array);

    Py_XDECREF(y_array);
    Py_XDECREF(yp_array);
}

static PyObject* integrate(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *f_obj = NULL;
    PyObject *t_span_obj = NULL;
    PyObject *y0_obj = NULL;
    PyObject *yp0_obj = NULL;
    PyArrayObject *y_array = NULL;
    PyArrayObject *yp_array = NULL;

    double rtol = 1.0e-3;
    double atol = 1.0e-6;
    double t0, t1;
    double *y, *yp;

    int neqn;
    int jnum = 1; 
    int mnum = 1; 

    int IND;
    int lrwork;
    int liwork;
    double *rwork;
    int *iwork;

    double *rpar;
    int *ipar;
    int idid;

    // parse inputs
    static char *kwlist[] = {"f", "t_span", "y0", "yp0", // mandatory arguments
                             "rtol", "atol", NULL}; // optional arguments and NULL termination
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOOO|dd", kwlist, 
                                     &f_obj, &t_span_obj, &y0_obj, &yp0_obj, // positional arguments
                                     &rtol, &atol)) // optional arguments
        return NULL;

    if (!PyCallable_Check(f_obj)) {
        PyErr_SetString(PyExc_ValueError, "`f` must be a callable function.");
    }

    PyArg_ParseTuple(t_span_obj, "dd", &t0, &t1);
    if (!(t1 > t0)) {
        PyErr_SetString(PyExc_ValueError, "`t1` must larger than `t0`.");
    }

    // initial conditions
    y_array = (PyArrayObject *) PyArray_ContiguousFromObject(y0_obj, NPY_DOUBLE, 0, 0);
    if (y_array == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyArray_ContiguousFromObject(y0_obj, NPY_DOUBLE, 0, 0) failed");
        goto fail;
    }
    if (PyArray_NDIM(y_array) > 1) {
        PyErr_SetString(PyExc_ValueError, "Initial condition y0 must be one-dimensional.");
        goto fail;
    }
    y = (double *) PyArray_DATA(y_array);
    neqn = PyArray_Size((PyObject *) y_array);

    yp_array = (PyArrayObject *) PyArray_ContiguousFromObject(yp0_obj, NPY_DOUBLE, 0, 0);
    if (yp_array == NULL) {
        PyErr_SetString(PyExc_ValueError, "PyArray_ContiguousFromObject(yp0_obj, NPY_DOUBLE, 0, 0) failed");
        goto fail;
    }
    if (PyArray_NDIM(yp_array) > 1) {
        PyErr_SetString(PyExc_ValueError, "Initial condition yp0 must be one-dimensional.");
        goto fail;
    }
    yp = (double *) PyArray_DATA(yp_array);
    if (!(neqn == PyArray_Size((PyObject *) yp_array))) {
        PyErr_SetString(PyExc_ValueError, "Size of y0 and yp0 have to coincide.");
        goto fail;
    }

    /* Initialize iwork and rwork. */
    lrwork = 20 + 27 * neqn + 6 * pow(neqn, 2);
    liwork = 20 + 4 * neqn;

    rwork = malloc(lrwork * sizeof(double));
    iwork = malloc(liwork * sizeof(int));

    for (int i=0; i<20; i++) {
        iwork[i] = 0;
        rwork[i] = 0.0;
    }
    /* Set global_params. */
    global_params.python_function = f_obj;
    global_params.t_sol = PyList_New(0);
    global_params.y_sol = PyList_New(0);
    global_params.yp_sol = PyList_New(0);

    // call pside solver
    PSIDE(&neqn, y, yp, pside_f, 
        &jnum, &neqn, &neqn, pside_J, 
        &mnum, &neqn, &neqn, pside_M, 
        &t0, &t1, &rtol, &atol, &IND,
        &lrwork, rwork, &liwork, iwork, 
        rpar, ipar, &idid, pside_solout);

    // // store initial data
    // int i = 0;
    // memcpy(PyArray_GETPTR2(y_sol_array, i, 0), y, PyArray_NBYTES(y_array));
    // memcpy(PyArray_GETPTR2(yp_sol_array, i, 0), yp, PyArray_NBYTES(yp_array));

    // // loop over all other steps
    // for (i=1; i<nt; i++) {
    //     t1 = t_eval[i];

    //     PSIDE(&neqn, y, yp, pside_f, 
    //         &jnum, &neqn, &neqn, pside_J, 
    //         &mnum, &neqn, &neqn, pside_M, 
    //         &t, &t1, &rtol, &atol, &IND,
    //         &lrwork, rwork, &liwork, iwork, 
    //         rpar, ipar, &idid, pside_solout);

    //     // store data in ith column of y_sol_array/ yp_sol_array
    //     memcpy(PyArray_GETPTR2(y_sol_array, i, 0), y, PyArray_NBYTES(y_array));
    //     memcpy(PyArray_GETPTR2(yp_sol_array, i, 0), yp, PyArray_NBYTES(yp_array));
    // }

    free(rwork);
    free(iwork);

    Py_XDECREF(f_obj);
    Py_XDECREF(y0_obj);
    Py_XDECREF(yp0_obj);

    return Py_BuildValue(
        "{s:N,s:N,s:N,s:N,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}",
        "success", Py_True,
        "t", PyArray_Return(PyArray_FromAny(global_params.t_sol,    // Input object
                                         NULL,                      // Desired data type (None means let NumPy decide)
                                         0,                         // Minimum number of dimensions
                                         0,                         // Maximum number of dimensions
                                         NPY_ARRAY_DEFAULT,         // Flags
                                         NULL)                      // Array description (NULL means default)
                            ),
        "y", PyArray_Return(PyArray_Concatenate(global_params.y_sol, 0)),
        "yp", PyArray_Return(PyArray_Concatenate(global_params.yp_sol, 0)),
        // "yp", PyArray_Return(PyArray_ConcatenateInto(global_params.yp_sol, 0, NULL, NULL, NPY_UNSAFE_CASTING)),
        // "yp", PyArray_Return(PyArray_Reshape((PyArrayObject *)PyArray_Concatenate(global_params.yp_sol, 0), dims_y)),
        "ncalls", iwork[9], // IWORK(10) number of successive PSIDE calls
        "nf", iwork[10], // IWORK(11) number of function evaluations
        "njac", iwork[11], // IWORK(12) number of jacobian evaluations
        "nlu", iwork[12], // IWORK(13) number of LU-decompositions
        "nsolve", iwork[13], // IWORK(14) number of forward/backward solves
        "nsteps", iwork[14], // IWORK(15) total number of steps
        "nrejerror", iwork[15], // IWORK(16) rejected steps due to error control
        "nrejnewton", iwork[16], // IWORK(17) rejected steps due to Newton failure
        "nrejgroth", iwork[17] // IWORK(18) rejected steps due to excessive growth of solution
    );

    fail:
        Py_XDECREF(f_obj);
        Py_XDECREF(t_span_obj);
        Py_XDECREF(y0_obj);
        Py_XDECREF(yp0_obj);
        Py_XDECREF(y_array);
        Py_XDECREF(yp_array);
        return NULL;
}

static PyMethodDef methods[] = {
    {"integrate", (PyCFunction)integrate, METH_VARARGS | METH_KEYWORDS, NULL},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "_psidemodule",
    NULL,
    -1,
    methods,
};

PyMODINIT_FUNC PyInit__psidemodule(void)
{
    import_array();
    return PyModule_Create(&module);
}