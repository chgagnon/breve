#include "kernel.h"

#ifdef HAVE_LIBGSL

#include <gsl/gsl_matrix_float.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_blas.h>

#include "breveFunctionsImage.h"

#define GSL_MATRIX_POINTER(p)	((gsl_matrix_float*)(BRPOINTER(p)))

class brMatrix {
	public:
		float *_data;
		unsigned int _size;
};

class brMatrix3D: public brMatrix {
	public:
		brMatrix3D(int x, int y, int z) {
			_sizex = x;
			_sizey = y;
			_sizez = z;
			_size = x * y * z;
			_data = new float[ _size];
		}

		~brMatrix3D() {
			delete[] _data;
		}

		inline float get(int x, int y, int z) {
			return _data[ (x * _sizey * _sizez) + (y * _sizez) + z ];
		}

		inline void set(int x, int y, int z, float value) {
			_data[ (x * _sizey * _sizez) + (y * _sizez) + z ] = value;
		}

		unsigned int _sizex, _sizey, _sizez;
};

class brMatrix2D: public brMatrix {
	public:
		brMatrix2D(int x, int y) {
			_sizex = x;
			_sizey = y;
			_size = x * y;
			_data = new float[ _size];
		}

		~brMatrix2D() {
			delete[] _data;
		}

		unsigned int _sizex, _sizey;
};

int brIMatrixNew(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m;

	GSL_MATRIX_POINTER(target) = m = gsl_matrix_float_alloc(BRINT(&args[0]), BRINT(&args[1]));

	new brMatrix2D(BRINT(&args[0]), BRINT(&args[1]));

	if(!m) {
		slMessage(DEBUG_ALL, "Could not create matrix: gsl_matrix_float_alloc failed\n");
		return EC_ERROR;
	}

	gsl_matrix_float_set_zero(m);

	return EC_OK;
}

int brIMatrixFree(brEval args[], brEval *target, brInstance *i) {
	if(GSL_MATRIX_POINTER(&args[0])) gsl_matrix_float_free(GSL_MATRIX_POINTER(&args[0]));
	return EC_OK;
}

int brIMatrixGet(brEval args[], brEval *target, brInstance *i) {
	BRDOUBLE(target) = (double)gsl_matrix_float_get(GSL_MATRIX_POINTER(&args[0]), BRINT(&args[1]), BRINT(&args[2]));
	return EC_OK;
}

int brIMatrixSet(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float_set(GSL_MATRIX_POINTER(&args[0]), BRINT(&args[1]), BRINT(&args[2]), BRDOUBLE(&args[3]));
	return EC_OK;
}

int brIMatrixCopy(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	gsl_matrix_float *n = GSL_MATRIX_POINTER(&args[1]);

	gsl_vector_float_view mv = gsl_vector_float_view_array(m->data, m->tda * m->size1);
	gsl_vector_float_view nv = gsl_vector_float_view_array(n->data, n->tda * n->size2);
	
	gsl_blas_scopy((gsl_vector_float*)&mv, (gsl_vector_float*)&nv);

	return EC_OK;
}

int brIMatrixGetAbsoluteSum(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	gsl_vector_float_view mv = gsl_vector_float_view_array(m->data, m->tda * m->size1);

	BRDOUBLE(target) = gsl_blas_sasum((gsl_vector_float*)&mv);

	return EC_OK;
}

int brIMatrixAddScaled(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	gsl_matrix_float *n = GSL_MATRIX_POINTER(&args[1]);
	float scale = BRDOUBLE(&args[2]);

	gsl_vector_float_view mv = gsl_vector_float_view_array(m->data, m->tda * m->size1);
	gsl_vector_float_view nv = gsl_vector_float_view_array(n->data, n->tda * n->size2);
	
	gsl_blas_saxpy(scale, (gsl_vector_float*)&nv, (gsl_vector_float*)&mv);

	return EC_OK;
}

int brIMatrixAddScalar(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	float c = (float)BRDOUBLE(&args[1]);

	gsl_matrix_float_add_constant(m, c);

	return EC_OK;
}

int brIMatrixMulElements(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	gsl_matrix_float *n = GSL_MATRIX_POINTER(&args[1]);

	gsl_matrix_float_mul_elements(m, n);

	return EC_OK;
}

int brIMatrixBlasMul(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	gsl_matrix_float *n = GSL_MATRIX_POINTER(&args[1]);
	gsl_matrix_float *o = GSL_MATRIX_POINTER(&args[2]);

	gsl_blas_sgemm(CblasNoTrans, CblasNoTrans, 1, m, n, 0, o);

	return EC_OK;
}

int brIMatrixScale(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	double scale = BRDOUBLE(&args[1]);

	gsl_matrix_float_scale(m, scale);

	return EC_OK;
}

int brIMatrixDiffuse(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	gsl_matrix_float *n = GSL_MATRIX_POINTER(&args[1]);
	double scale = BRDOUBLE(&args[2]);

	unsigned int x, y;
	int xp, xm, yp, ym;

	for(y=0;x<m->size1;y++) {
		for(x=0;x<m->size2;x++) {
			xp = (x+1) % m->size2; yp = (y+1) % m->size1;
			xm = (x-1) % m->size2; ym = (y-1) % m->size1;

			n->data[m->tda * y + x] = scale * ((-4.0 * m->data[m->tda * y + x]) +
				m->data[m->tda * y + xm] + m->data[m->tda * y + xp] + 
			    m->data[m->tda * ym + x] + m->data[m->tda * yp + x]);
		}
	}

	return EC_OK;
}

int brIMatrixDiffusePeriodic(brEval args[], brEval *target, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	gsl_matrix_float *n = GSL_MATRIX_POINTER(&args[1]);
	double scale = BRDOUBLE(&args[2]);
	unsigned int x, y;
	int xp, xm, yp, ym;

	for(y=0;y<m->size1;y++) {
		for(x=0;x<m->size2;x++) {
			xp = x+1; yp = y+1;
			xm = x-1; ym = y-1;

			if(xm < 0) xm += m->size2;
			else if(xp >= (int)m->size2) xp -= m->size2;

			if(ym < 0) ym += m->size2;
			else if(yp >= (int)m->size1) yp -= m->size1;

			n->data[m->tda * y + x] = 
				scale * (-4.0*m->data[m->tda * y + x] +
				m->data[m->tda * y + xm] + m->data[m->tda * y + xp] + 
			    m->data[m->tda * ym + x] + m->data[m->tda * yp + x]);
		}
	}

	return EC_OK;
}

int brIMatrixCopyToImage(brEval args[], brEval *result, brInstance *i) {
	gsl_matrix_float *m = GSL_MATRIX_POINTER(&args[0]);
	brImageData *d = (brImageData*)BRPOINTER(&args[1]);
	int offset = BRINT(&args[2]);
	double scale = BRDOUBLE(&args[3]);
	int r;

	unsigned char *pdata;
	float *mdata;

	int x, y, xmax, ymax;

	xmax = m->size1;
	ymax = m->size2;

	if(xmax > d->x) xmax = d->x;
	if(ymax > d->y) ymax = d->y;

	mdata = m->data;
	pdata = d->data + offset;

	for(y=0;y<ymax;y++) {
	 	for(x=0;x<xmax;x++) {
			r = (int)(*mdata * scale * 255.0);
			if(r > 255) *pdata = 255;
			else *pdata = r;
			pdata += 4;
			mdata++;
	 	}

		mdata += m->tda - xmax;
	}

	if(d->textureNumber == -1) d->textureNumber = slTextureNew(i->engine->camera);

	slUpdateTexture(i->engine->camera, d->textureNumber, d->data, d->x, d->y, GL_RGBA);

	return EC_OK;
}
#endif

void breveInitMatrixFunctions(brNamespace *n) {
#ifdef HAVE_LIBGSL
	brNewBreveCall(n, "matrixNew", brIMatrixNew, AT_POINTER, AT_INT, AT_INT, 0);
	brNewBreveCall(n, "matrixFree", brIMatrixFree, AT_NULL, AT_POINTER, 0);
	brNewBreveCall(n, "matrixGet", brIMatrixGet, AT_DOUBLE, AT_POINTER, AT_INT, AT_INT, 0);
	brNewBreveCall(n, "matrixSet", brIMatrixSet, AT_NULL, AT_POINTER, AT_INT, AT_INT, AT_DOUBLE, 0);
	brNewBreveCall(n, "matrixScale", brIMatrixScale, AT_NULL, AT_POINTER, AT_DOUBLE, 0);
	brNewBreveCall(n, "matrixGetAbsoluteSum", brIMatrixGetAbsoluteSum, AT_DOUBLE, AT_POINTER, 0);
	brNewBreveCall(n, "matrixAddScaled", brIMatrixAddScaled, AT_NULL, AT_POINTER, AT_POINTER, AT_DOUBLE, 0);
	brNewBreveCall(n, "matrixAddScalar", brIMatrixAddScalar, AT_NULL, AT_POINTER, AT_DOUBLE, 0);
	brNewBreveCall(n, "matrixCopy", brIMatrixCopy, AT_NULL, AT_POINTER, AT_POINTER, 0);
	brNewBreveCall(n, "matrixDiffusePeriodic", brIMatrixDiffusePeriodic, AT_NULL, AT_POINTER, AT_POINTER, AT_DOUBLE, 0);
	brNewBreveCall(n, "matrixDiffuse", brIMatrixDiffuse, AT_NULL, AT_POINTER, AT_POINTER, AT_DOUBLE, 0);
	brNewBreveCall(n, "matrixMulElements", brIMatrixMulElements, AT_NULL, AT_POINTER, AT_POINTER, 0);
	brNewBreveCall(n, "matrixBlasMul", brIMatrixBlasMul, AT_NULL, AT_POINTER, AT_POINTER, AT_POINTER, 0);
	brNewBreveCall(n, "matrixCopyToImage", brIMatrixCopyToImage, AT_NULL, AT_POINTER, AT_POINTER, AT_INT, AT_DOUBLE, 0);
#endif
}