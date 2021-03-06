/*****************************************************************************
 *                                                                           *
 * The breve Simulation Environment                                          *
 * Copyright (C) 2000, 2001, 2002, 2003 Jonathan Klein                       *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License as published by      *
 * the Free Software Foundation; either version 2 of the License, or         *
 * (at your option) any later version.                                       *
 *                                                                           *
 * This program is distributed in the hope that it will be useful,           *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of            *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
 * GNU General Public License for more details.                              *
 *                                                                           *
 * You should have received a copy of the GNU General Public License         *
 * along with this program; if not, write to the Free Software               *
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA *
 *****************************************************************************/

#include "steve.h"
#include "expression.h"
#include "evaluation.h"

// The only eval-exp that requires the 4th argument is a method call object lookup,
// which could potentially be a 'self' or 'super'.  Those two expressions will never
// have the RTC block created, so it's safe to ignore the argument if there's a block

#define stExpEval4( s, i, r, t ) \
    ( ( ( s ) && ( s )->block ) ? ( s )->block->calls.stRtcEval3( ( s ), ( i ), ( r ) ) : stExpEval( ( s ), ( i ), ( r ), ( t ) ) )

#define stExpEval3( s, i, r ) \
    ( ( ( s ) && ( s )->block ) ?  ( s )->block->calls.stRtcEval3( ( s ), ( i ), ( r ) ) : stExpEval( ( s ), ( i ), ( r ), NULL ) )

/*!
 * This is the heart of steve language evaluation.  it takes parse trees and 
 * executes them.  The most basic entity is the stExp which is a wrapper 
 * around all the other expression types such as literal numbers and strings,
 * binary expressions, method calls, etc.  see expression.c and
 * steveparse.y which show what these expressions look like and how
 * the parse trees are created.
 */

/*
 * ERROR REPORTING RULES:
 * - call stEvalError if you originate the error (return EC_ERROR for the first time)
 * - otherwise call slMessage for a regular debug output
 */

#define CODE_BLOCK_SIZE	128

/*!
	\brief Creates a new RTC block of size CODE_BLOCK_SIZE (cache line size)

	Allocate a new RtcCodeBlock and then allocate the memory for it
*/

stRtcCodeBlock *stNewRtcBlock() {
	stRtcCodeBlock	*block = ( stRtcCodeBlock * )malloc( CODE_BLOCK_SIZE );

	block->length		= CODE_BLOCK_SIZE;
	block->calls.ptr	= block->code;

	return block;
}

/*!
	\brief update a RtcCodeBlock with more code of size CODE_BLOCK_SIZE (cache line size)

	Update a RtcCodeBlock with more code using realloc of size CODE_BLOCK_SIZE (cache line size)
*/

inline void stUpdateBlockSize( stRtcCodeBlock *block ) {
	block->length += CODE_BLOCK_SIZE;
	block = ( stRtcCodeBlock* )realloc( block, block->length );
}

/*!
	\brief flush instruction and data cache

	Flush instruction and data cache
*/

inline void stFlushBlock( stRtcCodeBlock *block ) {
}

/*!
	\brief Get space from the current block (or add additional space)
*/

inline unsigned int *stAllocInstSpace( stRtcCodeBlock *block, unsigned int word_count ) {
	if (( block->calls.ptr + word_count ) > ( block->code + block->length ) ) {
		stUpdateBlockSize( block );

		word_count -= CODE_BLOCK_SIZE;

		if ( word_count > 0 )
			return stAllocInstSpace( block, word_count );
	}

	return block->calls.ptr;
}

/*!
	\brief Returns a pointer for an evaluation in steve.

	For an array access, list access or regular load, this function gives a
	pointer to the desired data.  This lets us have a unified approach for
	variable lookups.
*/

inline int stPointerForExp( stExp *expression, stRunInstance *i, void **pointer, int *type ) {
	switch ( expression->type ) {

		case ET_LOAD:
			return stEvalLoadPointer(( stLoadExp * )expression, i, pointer, type );

		case ET_ARRAY_INDEX:
			return stEvalArrayIndexPointer(( stArrayIndexExp * )expression, i, pointer, type );

		case ET_LIST_INDEX:
			return stEvalListIndexPointer(( stListIndexExp * )expression, i, pointer, type );

		default:
			slMessage( DEBUG_ALL, "Invalid value for left-hand side of assignment expression" );

			return EC_ERROR;
	}
}

typedef struct stRtcVar_t {
	unsigned char		reg;
	unsigned char		type;
	unsigned short		stack_index;

	struct stRtcVar_t	*next;
}

stRtcVar;

typedef struct stRtcFunc_t {
	unsigned int	gp_mask;
	unsigned int	fp_mask;
	unsigned int	vp_mask;
	unsigned short	stack_size;
	stRtcVar		*gp_regs[32];
	stRtcVar		*fp_regs[32];
	stRtcVar		*vp_regs[32];
	stRtcVar		*gp_reg_list;
	stRtcVar		*fp_reg_list;
	stRtcVar		*vp_reg_list;
}

stRtcFunc;

enum stRtcRegFile {
    GPREG,
    FPREG,
    VPREG,
    UNKNOWN_REG = 0xff
};

void stRtcInitVar( stRtcVar *var ) {
	var->reg			= 0xff;
	var->type			= UNKNOWN_REG;
	var->stack_index	= 0xffff;
	var->next			= NULL;
}

void stRtcInitFuncXXX( stRtcFunc *func ) {
	unsigned int	i;

	func->gp_mask = 0;
	func->fp_mask = 0;
	func->vp_mask = 0;
	func->stack_size = 0;

	for ( i = 0; i < 32; i++ ) {
		func->gp_regs[i] = 0;
		func->fp_regs[i] = 0;
		func->vp_regs[i] = 0;
	}

	func->gp_reg_list = 0;

	func->fp_reg_list = 0;
	func->vp_reg_list = 0;
}

void stRtcAllocGPRegXXX( stRtcCodeBlock *bloc, stRtcFunc *func, stRtcVar *var ) {
	unsigned int	i, bit;
	stRtcVar		*temp;

	for ( i = 2; i < 32; i++ ) {
		bit = 1 << i;

		if ( !( func->gp_mask & bit ) ) {
			var->reg			= i;
			var->type			= GPREG;
			var->stack_index	= func->stack_size;
			var->next			= func->gp_reg_list;

			func->gp_mask		|= bit;
			func->stack_size	+= 4;
			func->gp_regs[i]	= var;
			func->gp_reg_list	= var;

			return;
		}
	}

	temp = func->gp_reg_list;

	while ( temp->next ) {
		temp = temp->next;
	}

	// save temp to stack
	// set reg to temp->reg
	// set temp->reg to 0xff
	// move reg to head
}

void stLoadGPRegXXX( stRtcCodeBlock *block, stRtcFunc *func, stRtcVar *var ) {
	if ( var->reg != 0xff )
		return;

	// load reg from stack
	// move reg to head
}

void stRtcAllocFPRegXXX( stRtcCodeBlock *bloc, stRtcFunc *func, stRtcVar *var ) {
	unsigned int	i, bit;
	stRtcVar		*temp;

	for ( i = 1; i < 32; i++ ) {
		bit = 1 << i;

		if ( !( func->fp_mask & bit ) ) {
			var->reg			= i;
			var->type			= FPREG;
			var->stack_index	= func->stack_size;
			var->next			= func->fp_reg_list;

			func->fp_mask		|= bit;
			func->stack_size	+= 4;
			func->fp_regs[i]	= var;
			func->fp_reg_list	= var;

			return;
		}
	}

	temp = func->fp_reg_list;

	while ( temp->next ) {
		temp = temp->next;
	}

	// save temp to stack
	// set reg to temp->reg
	// set temp->reg to 0xff
	// move reg to head
}

/*!
	\brief Load a pointer in steve.
*/

RTC_INLINE int stEvalLoadPointer( stLoadExp *e, stRunInstance *i, void **pointer, int *type ) {
	*type = e->loadType;

	if ( e->local ) *pointer = &i->instance->type->steveData->stack[e->offset];
	else *pointer = &i->instance->variables[e->offset];

	return EC_OK;
}

/*!
	\brief Convert an evaluation an int in steve.
*/

inline int stToInt( brEval *e, brEval *t, stRunInstance *i ) {
	char *str;
	int result = 0;

	switch ( e->type() ) {

		case AT_INT:
			result = BRINT( e );

			break;

		case AT_DOUBLE:
			result = ( int )BRDOUBLE( e );

			break;

		case AT_STRING:
			str = BRSTRING( e );

			if ( str ) result = atoi( str );
			else result = 0;

			break;

		case AT_LIST:
			result = BRLIST( e )->_vector.size();

			break;

		case AT_VECTOR:
			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"vector\" to type \"int\"" );

			return EC_ERROR;

			break;

		case AT_INSTANCE:
			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"object\" to type \"int\"" );

			return EC_ERROR;

			break;

		case AT_POINTER:
			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"pointer\" to type \"int\"" );

			return EC_ERROR;

			break;

		case AT_NULL:
			stEvalError( i->instance, EE_CONVERT, "cannot convert NULL expression to type \"int\"" );

			return EC_ERROR;

			break;

		case AT_HASH:
			stEvalError( i->instance, EE_CONVERT, "cannot convert hash expression to type \"int\"" );

			return EC_ERROR;

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown atomic type (%d) in stToInt", e->type() );

			return EC_ERROR;

			break;
	}

	t->set( result );

	return EC_OK;
}

/*!
	\brief Convert an evaluation a double in steve.
*/

int stToDouble( brEval *e, brEval *t, stRunInstance *i ) {
	char *str;
	brEvalListHead *theList;
	double resultCode;

	switch ( e->type() ) {

		case AT_DOUBLE:
			resultCode = BRDOUBLE( e );

			break;

		case AT_INT:
			resultCode = ( double )BRINT( e );

			break;

		case AT_STRING:
			str = BRSTRING( e );

			if ( str ) resultCode = atof( BRSTRING( e ) );
			else resultCode = 0.0;

			break;

		case AT_LIST:
			theList = BRLIST( e );

			resultCode = ( double )theList->_vector.size();

			break;

		case AT_VECTOR:
			stEvalError( i->instance, EE_CONVERT, "cannot convert vector expression to type \"double\"" );

			return EC_ERROR;

			break;

		case AT_NULL:
			stEvalError( i->instance, EE_CONVERT, "cannot convert NULL expression to type \"double\"" );

			return EC_ERROR;

			break;

		case AT_INSTANCE:
			stEvalError( i->instance, EE_CONVERT, "cannot convert object expression to type \"double\"" );

			return EC_ERROR;

			break;

		case AT_HASH:
			stEvalError( i->instance, EE_CONVERT, "cannot convert hash expression to type \"double\"" );

			return EC_ERROR;

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown atomic type (%d) in stToDouble", e->type() );

			return EC_ERROR;

			break;
	}

	e->set( resultCode );

	return EC_OK;
}

/*!
	\brief Attempt to convert between arbitrary types in steve.
*/

inline int stToType( brEval *e, int type, brEval *t, stRunInstance *i ) {
	if ( e->type() == type ) return EC_OK;

	switch ( type ) {

		case AT_DOUBLE:
			return stToDouble( e, t, i );

			break;

		case AT_INT:
			return stToInt( e, t, i );

			break;

		case AT_STRING:
			t->set( brFormatEvaluation( e, i->instance->breveInstance ) );

			return EC_OK;

			break;

		case AT_VECTOR:
			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"%s\" to type \"vector\"", brAtomicTypeStrings[( int )e->type()] );

			return EC_ERROR;

			break;

		case AT_INSTANCE:
			/* the only legal conversion to object is when the int interpretation is 0--a NULL object */

			if (( e->type() == AT_DOUBLE && BRDOUBLE( t ) == 0.0 ) || ( e->type() == AT_INT && BRINT( t ) == 0 ) ) {

				t->set(( brInstance* )NULL );

				return EC_OK;
			}

			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"%s\" to type \"object\"", brAtomicTypeStrings[( int )e->type()] );

			return EC_ERROR;
			break;

		case AT_LIST:
			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"%s\" to type \"list\"", brAtomicTypeStrings[( int )e->type()] );

			return EC_ERROR;

			break;

		case AT_MATRIX:
			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"%s\" to type \"matrix\"", brAtomicTypeStrings[( int )e->type()] );

			return EC_ERROR;

			break;

		case AT_HASH:
			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"%s\" to type \"hash\"", brAtomicTypeStrings[( int )e->type()] );

			return EC_ERROR;

			break;

		case AT_POINTER:
			if ( e->type() == AT_INT && BRINT( e ) == 0 ) {

				t->set(( void* )NULL );

				return EC_OK;
			}

			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"%s\" to type \"pointer\"", brAtomicTypeStrings[( int )e->type()] );

			return EC_ERROR;
			break;

		case AT_DATA:
			stEvalError( i->instance, EE_CONVERT, "cannot convert type \"%s\" to type \"data\"", brAtomicTypeStrings[( int )e->type()] );

			return EC_ERROR;

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "INTERNAL ERROR: unknown type in stToType", e->type() );

			return EC_ERROR;

			break;
	}

	return EC_OK;
}

static int stEvalLoadInt( stLoadExp *e, stRunInstance *i, brEval *target ) {
	void *pointer;

	if ( e->local ) pointer = &i->instance->type->steveData->stack[e->offset];
	else pointer = &i->instance->variables[e->offset];

	target->set( *( int* )pointer );

	return EC_OK;
}

static int stEvalLoadDouble( stLoadExp *e, stRunInstance *i, brEval *target ) {
	void *pointer;

	if ( e->local ) pointer = &i->instance->type->steveData->stack[e->offset];
	else pointer = &i->instance->variables[e->offset];

	target->set( *( double* )pointer );

	return EC_OK;
}

static int stEvalLoadIndirect( stLoadExp *e, stRunInstance *i, brEval *target ) {
	void *pointer;

	if ( e->local ) pointer = &i->instance->type->steveData->stack[e->offset];
	else pointer = &i->instance->variables[e->offset];

	if ( e->loadType == AT_INSTANCE ) {

		target->set( *( brInstance** )pointer );
	} else if ( e->loadType == AT_DATA ) {

		target->set( *( brData** )pointer );
	} else if ( e->loadType == AT_POINTER ) {

		target->set( *( void** )pointer );
	}

	return EC_OK;
}

static int stEvalLoadVector( stLoadExp *e, stRunInstance *i, brEval *target ) {
	void *pointer;

	if ( e->local ) pointer = &i->instance->type->steveData->stack[e->offset];
	else pointer = &i->instance->variables[e->offset];

	target->set( *( slVector* )pointer );

	return EC_OK;
}

static int stEvalLoadList( stLoadExp *e, stRunInstance *i, brEval *target ) {
	void *pointer;

	if ( e->local ) pointer = &i->instance->type->steveData->stack[e->offset];
	else pointer = &i->instance->variables[e->offset];

	if ( !*( brEvalListHead ** )pointer ) {
		// if there is nothing here (uninitialized list), then we create an empty one.
		// we retain it, since it's already stored as a variable.

		*( brEvalListHead ** )pointer = new brEvalListHead();
		stGCRetainPointer( *( void ** )pointer, AT_LIST );
	}

	target->set( *( brEvalListHead** )pointer );

	return EC_OK;
}

static int stEvalLoadHash( stLoadExp *e, stRunInstance *i, brEval *target ) {
	void *pointer;

	if ( e->local ) pointer = &i->instance->type->steveData->stack[e->offset];
	else pointer = &i->instance->variables[e->offset];

	if ( !*( brEvalHash ** )pointer ) {
		// if there is nothing here (uninitialized list), then we create an empty one.
		// we retain it, since it's already stored as a variable.

		*( brEvalHash ** )pointer = new brEvalHash();
		stGCRetainPointer( *( void ** )pointer, AT_HASH );
	}

	target->set( *( brEvalHash ** )pointer );

	return EC_OK;
}

static int stEvalLoadString( stLoadExp *e, stRunInstance *i, brEval *target ) {
	void *pointer;

	if ( e->local )
		pointer = &i->instance->type->steveData->stack[e->offset];
	else
		pointer = &i->instance->variables[e->offset];

	if ( !( *( char ** )pointer ) )
		*( char ** )pointer = slStrdup( "" );

	target->set( *( char ** )pointer );

	return EC_OK;
}

static int stEvalLoadMatrix( stLoadExp *e, stRunInstance *i, brEval *target ) {
	void *pointer;
	slMatrix m;

	if ( e->local )
		pointer = &i->instance->type->steveData->stack[e->offset];
	else
		pointer = &i->instance->variables[e->offset];

	slMatrixCopy(( double ** )pointer, m );

	target->set( m );

	return EC_OK;
}

int stLoadVariable( void *variable, unsigned char type, brEval *e, stRunInstance *i ) {

	slVector v;
	slMatrix m;
	// figure out which type we're dealing with so we interpret the pointer correctly

	switch ( type ) {

		case AT_INT:
			e->set( *( int* )variable );

			break;

		case AT_DOUBLE:
			e->set( *( double* )variable );

			break;

		case AT_POINTER:
			e->set( *( void** )variable );

			break;

		case AT_INSTANCE:
			e->set( *( brInstance** )variable );

			break;

		case AT_DATA:
			e->set( *( brData** )variable );

			break;

		case AT_VECTOR:
			slVectorCopy(( slVector * )variable, &v );

			e->set( v );

			break;

		case AT_LIST:
			// if there is nothing here (uninitialized list), then we create an empty list.
			// we retain it, since it's now stored as a variable.

			if ( !*( brEvalListHead ** )variable ) {
				*( brEvalListHead ** )variable = new brEvalListHead();
				stGCRetainPointer( *( void ** )variable, AT_LIST );
			}

			e->set( *( brEvalListHead ** )variable );

			break;

		case AT_HASH:
			if ( !*( brEvalHash ** )variable ) {
				// if there is nothing here (uninitialized hash), then we create an empty hash.
				// we retain it, since it's stored as a variable.

				*( brEvalHash ** )variable = new brEvalHash();
				stGCRetainPointer( *( void ** )variable, AT_HASH );
			}

			e->set( *( brEvalHash ** )variable );

			break;

		case AT_STRING:
			if ( !( *( char** )variable ) )
				*( char ** )variable = slStrdup( "" );

			e->set( *( char ** )variable );

			break;

		case AT_MATRIX:
			slMatrixCopy(( double** )variable, m );

			e->set( m );

			break;

		case AT_ARRAY:
			stEvalError( i->instance, EE_ARRAY, "Array variables cannot be loaded like normal expressions" );

			return EC_ERROR;

			break;

		default:
			slMessage( DEBUG_ALL, "Unknown atomic type in load: %d\n", e->type() );

			return EC_ERROR;

			break;
	}

	return EC_OK;
}

/*!
	\brief Sets a variable in steve.
*/

int stSetVariable( void *variable, unsigned char type, stObject *otype, brEval *e, stRunInstance *i ) {
	stInstance *instance;
	char *newstr;
	int resultCode;

	if ( type == AT_INSTANCE && otype && e->type() == AT_INSTANCE && BRINSTANCE( e ) ) {
		// if they specified an object type in the code, make sure
		// that this is a valid cast.  this requires a lookup each
		// time we're here, so it could really be improved.

		instance = ( stInstance * )BRINSTANCE( e )->userData;

		if ( !stIsSubclassOf( instance->type, otype ) ) {
			stEvalError( i->instance, EE_TYPE, "Cannot assign instance of class \"%s\" to variable of class \"%s\"\n", instance->type->name.c_str(), otype->name.c_str() );
			return EC_ERROR;
		}
	}

	if ( ( resultCode = stToType( e, type, e, i ) ) != EC_OK ) {
		slMessage( DEBUG_ALL, "error in assignment\n" );
		return resultCode;
	}

#ifdef MULTITHREAD
	if ( i ) pthread_mutex_lock( &i->lock );

#endif

	switch ( type ) {

		case AT_INT:
			*( int * )variable = BRINT( e );

			break;

		case AT_DOUBLE:

#if 0
			if( isnan( BRDOUBLE( e ) ) ) {
				slMessage( DEBUG_ALL, "Warning: NaN value assignment\n" );
			}
#endif


			*( double * )variable = BRDOUBLE( e );

			break;

		case AT_VECTOR:
			slVectorCopy( &BRVECTOR( e ), ( slVector * )variable );

			break;

		case AT_MATRIX:
			slMatrixCopy( BRMATRIX( e ), ( double ** )variable );

			break;

		case AT_INSTANCE:
			*( brInstance ** )variable = BRINSTANCE( e );

			break;

		case AT_DATA:
			// overwriting an old data variable?

			if ( *( brData ** )variable != BRDATA( e ) )
				stGCUnretainAndCollectPointer( *( void ** )variable, AT_DATA );

			// retain and set the new data

			if ( BRDATA( e ) )
				*( brData ** )variable = BRDATA( e );

			break;

		case AT_POINTER:
			*( void ** )variable = BRPOINTER( e );

			break;

		case AT_STRING:
			// overwriting an old string?
			if ( *( char ** )variable )
				slFree( *( void ** )variable );

			newstr = slStrdup( BRSTRING( e ) );

			*( char ** )variable = newstr;

			break;

		case AT_LIST:
			if ( !BRLIST( e ) ) {
				// if this is an empty list, just allocate some space,
				// and mark it.  it will be retained later.

				e->set( new brEvalListHead() );
			} else if ( *( brEvalListHead ** )variable != BRLIST( e ) ) {
				// overwriting an old list at this location

				stGCUnretainAndCollectPointer( *( void ** )variable, AT_LIST );
			}

			*( brEvalListHead ** )variable = BRLIST( e );

			break;

		case AT_HASH:
			// if this is an empty hash, just allocate some space...

			if ( !BRHASH( e ) ) {

				e->set( new brEvalHash() );
			} else if ( *( brEvalHash ** )variable != BRHASH( e ) ) {
				// overwriting an old hash at this location
				stGCUnretainAndCollectPointer( *( void ** )variable, AT_HASH );
			}

			*( brEvalHash ** )variable = BRHASH( e );

			break;

		case AT_ARRAY:
			stEvalError( i->instance, EE_ARRAY, "Array variables cannot be assigned like simple variables" );

#ifdef MULTITHREAD
			if ( i ) pthread_mutex_unlock( &i->lock );

#endif
			return EC_ERROR;

		default:
			slMessage( DEBUG_ALL, "INTERNAL ERROR: unknown variable type %d in set\n", type );

#ifdef MULTITHREAD
			if ( i ) pthread_mutex_unlock( &i->lock );

#endif
			return EC_ERROR;

			break;
	}

	stGCRetain( e );

#ifdef MULTITHREAD

	if ( i ) pthread_mutex_unlock( &i->lock );

#endif

	return EC_OK;
}

/*!
	\brief Computes the "truth" of an expression in steve.

	Takes any kind of expression and decides whether it is "true" in
	the context of boolean expressions.  It yields an int with a value
	of either 1 or 0.
*/

RTC_INLINE int stEvalTruth( brEval *e, brEval *t, stRunInstance *i ) {
	// e may equal t, so put the type in a local variable before changing it...

	char *str;
	int type = e->type();

	double zero[3][3];

	switch ( type ) {

		case AT_INT:
			t->set(( BRINT( e ) != 0 ) );

			return EC_OK;

			break;

		case AT_DOUBLE:
			t->set(( BRDOUBLE( e ) != 0.0 ) );

			return EC_OK;

			break;

		case AT_STRING:
			str = BRSTRING( e );

			t->set(( str && strlen( str ) > 0 ) );

			return EC_OK;

			break;

		case AT_INSTANCE:
			if ( BRINSTANCE( e ) && ( BRINSTANCE( e ) )->status != AS_ACTIVE )
				t->set( 0 );
			else
				t->set( BRINSTANCE( e ) != NULL );

			return EC_OK;

			break;

		case AT_POINTER:
		case AT_HASH:
		case AT_DATA:
			t->set( BRPOINTER( e ) != NULL );
			return EC_OK;
			break;

		case AT_LIST:
			t->set(( BRPOINTER( e ) != NULL && ( BRLIST( e ) )->_vector.size() != 0 ) );
			return EC_OK;
			break;

		case AT_VECTOR:
			t->set( !( slVectorIsZero( &BRVECTOR( e ) ) ) );
			return EC_OK;
			break;

		case AT_MATRIX:
			slMatrixZero( zero );
			t->set( !slMatrixCompare( BRMATRIX( e ), zero ) );
			break;

		case AT_NULL:
			t->set( 0 );
			return EC_OK;
			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown type %d in stEvalTruth", type );

			return EC_ERROR;

			break;
	}

	return EC_OK;
}

/*!
	\brief Evaluates a free expression in steve.
*/

RTC_INLINE int stEvalFree( stFreeExp *s, stRunInstance *i, brEval *t ) {
	brEval result;
	int resultCode;
	int finished = 0;

	resultCode = stExpEval3( s->expression, i, &result );

	if ( resultCode != EC_OK ) return EC_ERROR;

	if ( result.type() == AT_INSTANCE ) {
		if ( !BRINSTANCE( &result ) ) {
			slMessage( DEBUG_ALL, "warning: attempt to free uninitialized object\n" );
			return EC_OK;
		}

		// if we're freeing ourself (the calling instance) then we will return EC_STOP

		if ( BRINSTANCE( &result )->userData == i->instance )
			finished = 1;

		if ( BRINSTANCE( &result )->status == AS_ACTIVE )
			brInstanceRelease( BRINSTANCE( &result ) );
	} else if ( result.type() == AT_LIST ) {
		std::vector< brEval >::iterator li;

		for ( li = BRLIST( &result )->_vector.begin(); li != BRLIST( &result )->_vector.end(); li++ ) {
			brEval *listeval = &(*li);

			if ( listeval->type()  == AT_INSTANCE ) {
				if ( !BRINSTANCE( listeval ) )
					slMessage( DEBUG_ALL, "warning: attempt to free uninitialized object\n" );
				else {
					// if we're freeing ourself (the calling instance) then we will return EC_STOP

					if ( BRINSTANCE( listeval )->userData == i->instance ) finished = 1;

					if ( BRINSTANCE( listeval )->status == AS_ACTIVE ) brInstanceRelease( BRINSTANCE( listeval ) );
				}
			}
		}
	}

	if ( finished )
		return EC_STOP;

	return EC_OK;
}

/*!
	\brief Evaluates an array of expressions in steve.
*/

RTC_INLINE int stEvalCodeArray( stCodeArrayExp *a, stRunInstance *i, brEval *result ) {
	return stEvalExpVector( &a->expressions, i, result );
}

int stEvalExpVector( std::vector< stExp* > *a, stRunInstance *i, brEval *result ) {
	unsigned int count;
	int resultCode;

	if ( !i )
		return EC_ERROR;

	if ( i->instance->status != AS_ACTIVE )
		return EC_OK;

	if ( !result )
		return EC_ERROR;

	for ( count = 0; count < ( *a ).size(); ++count ) {
		stExp *expression = ( *a )[count];

		if ( expression ) {
			resultCode = stExpEval3( expression, i, result );

			if ( resultCode == EC_ERROR ) {
				brErrorInfo *error = brEngineGetErrorInfo( i->type->engine );

				error->line = expression->line;

				if ( error->file )
					slFree( error->file );

				error->file = slStrdup( expression->file );

				slMessage( DEBUG_ALL, "... error in file \"%s\" at line %d\n", expression->file, expression->line );

				return EC_ERROR_HANDLED;
			} else if ( resultCode == EC_ERROR_HANDLED ) {
				return EC_ERROR_HANDLED;
			} else if ( resultCode == EC_STOP ) {
				// an EC_STOP indicates a return value or other interruption
				return EC_STOP;
			}
		}
	}

	return EC_OK;
}

/*!
	\brief Evaluates a hardcoded list expression in steve.

	list is a list of stExp expressions, each of which is evaluated
	and added to the list.
*/

inline int brEvalListExp( stListExp *le, stRunInstance *i, brEval *result ) {
	brEval index;
	int resultCode;
	unsigned int n;

	result->set( new brEvalListHead() );

	for ( n = 0; n < le->expressions.size(); n++ ) {
		resultCode = stExpEval3( le->expressions[n], i, &index );

		if ( resultCode != EC_OK ) return resultCode;

		brEvalListInsert( BRLIST( result ), BRLIST( result )->_vector.size(), &index );
	}

	return EC_OK;
}

/**
 * \brief Initiates evaluation of a method call expression in steve.
 * This function doesn't do the actual method calling--it evaluates the
 * calling object or object-list, and then calls \ref stRealEvalMethodCall
 * for each object.
 */

RTC_INLINE int stEvalMethodCall( stMethodExp *mexp, stRunInstance *i, brEval *t ) {
	stRunInstance ri;
	brEval obj;
	int r;

	ri.type = NULL;
	r = stExpEval4( mexp->objectExp, i, &obj, &ri.type );

	if ( r != EC_OK )
		return r;

	if ( obj.type() == AT_INSTANCE ) {
		if ( !BRINSTANCE( &obj ) || BRINSTANCE( &obj )->status != AS_ACTIVE ) {
			stEvalError( i->instance, EE_NULL_INSTANCE, "method \"%s\" called with uninitialized object", mexp->methodName.c_str() );
			return EC_ERROR;
		}

		// if the new instance is not a steve object, it's a foreign method call

		if ( BRINSTANCE( &obj )->object->type->_typeSignature != STEVE_TYPE_SIGNATURE )
			return stEvalForeignMethodCall( mexp, BRINSTANCE( &obj ), i, t );

		ri.instance = ( stInstance * )BRINSTANCE( &obj )->userData;

		if ( !ri.type )
			ri.type = ri.instance->type;

		r = stRealEvalMethodCall( mexp, &ri, i, t );

		return r;
	}

	if ( obj.type() == AT_LIST ) {
		std::vector< brEval >::iterator li;
		brEvalListHead *l = BRLIST( &obj );

		for ( li = l->_vector.begin(); li != l->_vector.end(); li++ ) {
			brEval *listeval = &(*li);

			if ( listeval -> type() != AT_INSTANCE ) {
				stEvalError( i->instance, EE_NULL_INSTANCE, "method \"%s\" called with list containing non-instance value", mexp->methodName.c_str() );
				return EC_ERROR;
			}

			if( !BRINSTANCE( listeval ) || BRINSTANCE( listeval )->status != AS_ACTIVE ) {
				stEvalError( i->instance, EE_NULL_INSTANCE, "method \"%s\" called with uninitialized object", mexp->methodName.c_str() );
				return EC_ERROR;
			}

			// if the new instance is not a steve object, it's a foreign method call

			if ( BRINSTANCE( listeval )->object->type->_typeSignature != STEVE_TYPE_SIGNATURE )
				return stEvalForeignMethodCall( mexp, BRINSTANCE( listeval ), i, t );

			ri.instance = ( stInstance * )BRINSTANCE( listeval )->userData;

			ri.type = ri.instance->type;

			r = stRealEvalMethodCall( mexp, &ri, i, t );

			if( r != EC_OK ) 
				return r;
		}

		return r;
	}

	stEvalError( i->instance, EE_TYPE, "method \"%s\" called for an expression that is neither an object nor a list", mexp->methodName.c_str() );

	return EC_ERROR;
}

int stEvalForeignMethodCall( stMethodExp *mexp, brInstance *caller, stRunInstance *i, brEval *t ) {
	brEval args[ mexp->arguments.size() ];
	const brEval *argps[ mexp->arguments.size() ];
	unsigned int n;
	int resultCode;

	for ( n = 0; n < mexp->arguments.size(); ++n ) {
		stKeyword *k = mexp->arguments[n];

		argps[n] = &args[n];

		resultCode = stExpEval3( k->value, i, &args[ n ] );

		if ( resultCode != EC_OK )
			return resultCode;
	}

	if( brMethodCallByNameWithArgs( caller, mexp->methodName.c_str(), argps, mexp->arguments.size(), t ) != EC_OK ) {
		stEvalError( i->instance, EE_UNKNOWN_METHOD, "Error calling method \"%s\" for foreign object %p\n", mexp->methodName.c_str(), caller );
		return EC_ERROR;
	}

	return EC_OK;
}

/*!
	\brief Evaluates a method call expression in steve.
*/

inline int stRealEvalMethodCall( stMethodExp *mexp, stRunInstance *target, stRunInstance *caller, brEval *t ) {
	stKeywordEntry *keyEntry;
	stKeyword *key, *tmpkey;
	unsigned int n, m, argCount;
	int resultCode;

	if ( !target->instance ) {
		stEvalError( caller->instance, EE_NULL_INSTANCE, "method \"%s\" called with uninitialized object", mexp->methodName.c_str() );
		return EC_ERROR;
	}

	if ( target->instance->status != AS_ACTIVE ) {
		stEvalError( caller->instance, EE_FREED_INSTANCE, "method \"%s\" called with freed object (%p)", mexp->methodName.c_str(), target->instance );
		return EC_ERROR;
	}

	// When we look up a method, we can remember its address and argument ordering for next time.
	// We'll keep method cache data for each individual object type we come across.

	stMethodExpCache *cachedata = &mexp -> _cache[ target->type ];

	if ( cachedata -> _method == NULL ) { 
		stObject *newType;

		cachedata -> _method = stFindInstanceMethodWithMinArgs( target->type, mexp->methodName.c_str(), mexp->arguments.size(), &newType );

		if ( !cachedata -> _method ) {
			// for backwards compatibility, we'll make missing iterate methods be a no-op instead of an error.

			if( mexp->methodName == "iterate" ) 
				return EC_OK;

			// can't find the method!

			const char *kstring = mexp -> arguments.size() == 1 ? "keyword" : "keywords";

			target->type = target->instance->type;

			stEvalError( target->instance, EE_UNKNOWN_METHOD, "object type \"%s\" does not respond to method \"%s\" with %d %s", target->type->name.c_str(), mexp->methodName.c_str(), mexp->arguments.size(), kstring );

			return EC_ERROR;
		}

		cachedata -> _baseObjectCache = target -> type;

		argCount = mexp->arguments.size();

		// we have to lookup the order of the keywords.  initialize them to -1 first.

		for ( n = 0; n < argCount; ++n )
			mexp->arguments[n]->position = -1;

		for ( n = 0; n < cachedata -> _method -> keywords.size(); ++n ) {
			// look for the method's Nth keyword

			key = NULL;
			keyEntry = cachedata -> _method -> keywords[ n ];

			for ( m = 0; m < argCount; m++ ) {
				// go through all the arguments, checking what was passed in.

				tmpkey = mexp -> arguments[ m ];

				if ( tmpkey->position == -1 && tmpkey->word == keyEntry->keyword ) {
					tmpkey->position = n;
					key = tmpkey;
					m = argCount;
				}
			}

			if ( !key && keyEntry->defaultKey )
				key = keyEntry->defaultKey;

			if ( !key ) {
				stEvalError( target->instance, EE_MISSING_KEYWORD, "Call to method %s of class %s missing keyword \"%s\"", cachedata -> _method -> name.c_str(), target->type->name.c_str(), keyEntry->keyword.c_str() );
				cachedata -> _method = NULL;
				return EC_ERROR;
			}

			cachedata -> _positionedArguments.push_back( key );
		}

		for ( n = 0; n < argCount; ++n ) {
			if ( mexp->arguments[n]->position == -1 ) {
				tmpkey = mexp->arguments[n];

				stEvalError( target->instance, EE_UNKNOWN_KEYWORD, "Unknown keyword \"%s\" in call to method \"%s\"", tmpkey->word.c_str(), cachedata -> _method -> name.c_str() );
				cachedata -> _method = NULL;
				return EC_ERROR;
			}
		}

	} 

	target -> type = cachedata -> _baseObjectCache;

	if ( cachedata -> _method -> inlined ) {
		// The method is inlined if it has no local variables, and no arguments

		int resultCode = stEvalExpVector( &cachedata -> _method -> code, target, t );

		if ( resultCode == EC_STOP ) 
			return EC_OK;

		return resultCode;
	}

	// we don't want to reuse the same argps in the case of a 
	// recursive function so we create some local storage for them.

	brEval args[ cachedata -> _method -> keywords.size() ];

	const brEval *argps[ cachedata -> _method -> keywords.size() ];

	for ( n = 0; n < cachedata -> _method -> keywords.size(); ++n ) {
		argps[n] = &args[n];

		key = cachedata -> _positionedArguments[ n ];

		if ( !key ) {
			slMessage( DEBUG_ALL, "Missing keyword for method \"%s\"\n", cachedata -> _method -> name.c_str() );
			return EC_ERROR;
		}

		// evaluate the key into the eval...

		resultCode = stExpEval3( key->value, caller, &args[ n ] );

		if ( resultCode != EC_OK ) {
			if ( resultCode != EC_ERROR_HANDLED ) 
				slMessage( DEBUG_ALL, "Error evaluating keyword \"%s\" for method \"%s\"\n", key->word.c_str(), cachedata -> _method -> name.c_str() );

			return resultCode;
		}
	}

	return stCallMethod( caller, target, cachedata -> _method, argps, cachedata -> _method -> keywords.size(), t );
}

/*!
	\brief Evaluates a while expression in steve.
*/

RTC_INLINE int stEvalWhile( stWhileExp *w, stRunInstance *i, brEval *result ) {
	brEval condition, conditionExp;
	int r;

	if (( r = stExpEval3( w->cond, i, &conditionExp ) ) != EC_OK ||
	        ( r = stEvalTruth( &conditionExp, &condition, i ) ) != EC_OK )
		return r;

	while ( BRINT( &condition ) ) {
		if (( r = stExpEval3( w->code, i, result ) ) != EC_OK ||
		        ( r = stExpEval3( w->cond, i, &conditionExp ) ) != EC_OK ||
		        ( r = stEvalTruth( &conditionExp, &condition, i ) ) != EC_OK )
			return r;
	}

	return EC_OK;
}

RTC_INLINE int stEvalFor( stForExp *w, stRunInstance *i, brEval *result ) {
	brEval condition, conditionExp;
	brEval assignment, iteration;
	int r;

	if (( r = stExpEval3( w->assignment, i, &assignment ) ) != EC_OK ||
	        ( r = stExpEval3( w->condition, i, &conditionExp ) ) != EC_OK ||
	        ( r = stEvalTruth( &conditionExp, &condition, i ) ) != EC_OK )
		return r;

	while ( BRINT( &condition ) ) {
		if (( w->code && ( r = stExpEval3( w->code, i, result ) ) != EC_OK ) ||
		        ( r = stExpEval3( w->iteration, i, &iteration ) ) != EC_OK ||
		        ( r = stExpEval3( w->condition, i, &conditionExp ) ) != EC_OK ||
		        ( r = stEvalTruth( &conditionExp, &condition, i ) ) != EC_OK )
			return r;
	}

	return EC_OK;
}

RTC_INLINE int stEvalForeach( stForeachExp *w, stRunInstance *i, brEval *result ) {
	brEval list;
	void *iterationPointer;
	int resultCode;
	std::vector< brEval* >::iterator ei;

	stAssignExp *assignExp = w->assignment;

	if ( assignExp->_local )
		iterationPointer = &i->instance->type->steveData->stack[assignExp->_offset];
	else
		iterationPointer = &i->instance->variables[assignExp->_offset];

	stExpEval3( w->list, i, &list );

	if ( list.type() != AT_LIST ) {
		stEvalError( i->instance, EE_TYPE, "expected type \"list\" in foreach evaluation" );
		return EC_ERROR;
	}

	// for( ei = BRLIST( &list )->_vector.begin(); ei != BRLIST( &list )->_vector.end(); ei++ ) {
	for ( unsigned int n = 0; n < BRLIST( &list )->_vector.size(); n++ ) {
		brEval eval;

		if (( resultCode = brEvalCopy( &BRLIST( &list )->_vector[ n ], &eval ) ) != EC_OK )
			return resultCode;

		if ( assignExp->_objectName.size() && !assignExp->_objectType ) {
			brObject *object = brObjectFind( i->instance->type->engine, assignExp->_objectName.c_str() );

			if ( object )
				assignExp->_objectType = ( stObject* )object->userData;
			else
				assignExp->_objectType = NULL;
		}

		if (( resultCode = stSetVariable( iterationPointer, assignExp->_assignType, assignExp->_objectType, &eval, i ) ) != EC_OK )
			return resultCode;

		resultCode = stExpEval3( w->code, i, result );

		if ( resultCode != EC_OK ) return resultCode;
	}

	return EC_OK;
}

RTC_INLINE int stEvalIf( stIfExp *w, stRunInstance *i, brEval *result ) {
	brEval condition, conditionExp;
	int resultCode;

	if (( resultCode = stExpEval3( w->cond, i, &conditionExp ) ) != EC_OK ||
	        ( resultCode = stEvalTruth( &conditionExp, &condition, i ) ) != EC_OK )
		return resultCode;

	if ( BRINT( &condition ) ) {
		if ( w->trueCode )
			return stExpEval3( w->trueCode, i, result );
		else
			return EC_OK;
	} else {
		if ( w->falseCode )
			return stExpEval3( w->falseCode, i, result );
		else
			return EC_OK;
	}
}

RTC_INLINE int stEvalListInsert( stListInsertExp *w, stRunInstance *i, brEval *result ) {
	brEval pushEval, index;
	int resultCode;

	if (( resultCode = stExpEval3( w->listExp, i, result ) ) != EC_OK ||
	        ( resultCode = stExpEval3( w->exp, i, &pushEval ) ) != EC_OK )
		return resultCode;

	if ( result->type() != AT_LIST ) {
		stEvalError( i->instance, EE_TYPE, "expected type \"list\" in \"push\" evaluation" );
		return EC_ERROR;
	}

	if ( !BRLIST( result ) ) {
		stEvalError( i->instance, EE_INTERNAL, "attempt to insert value into uninitialized list\n" );
		return EC_ERROR;
	}

	if ( w->index ) {
		resultCode = stExpEval3( w->index, i, &index );

		if ( resultCode != EC_OK )
			return resultCode;
	} else {
		index.set(( int )BRLIST( result )->_vector.size() );
	}

	brEvalListInsert( BRLIST( result ), BRINT( &index ), &pushEval );

	return EC_OK;
}

RTC_INLINE int stEvalListRemove( stListRemoveExp *l, stRunInstance *i, brEval *result ) {
	brEval listEval, index;
	int resultCode;

	resultCode = stExpEval3( l->listExp, i, &listEval );

	if ( resultCode != EC_OK )
		return resultCode;

	int type = listEval.type();

	if ( type != AT_LIST && type != AT_HASH ) {
		stEvalError( i->instance, EE_TYPE, "expected type \"list\" or \"hash\" during list remove evaluation" );
		return EC_ERROR;
	}

	if ( !BRLIST( &listEval ) ) return EC_OK;

	if ( l->index ) {
		resultCode = stExpEval3( l->index, i, &index );

		if ( resultCode != EC_OK )
			return resultCode;
	} else {
		index.set(( int )BRLIST( &listEval )->_vector.size() - 1 );
	}

	if( type == AT_LIST ) 
		brEvalListRemove( BRLIST( &listEval ), BRINT( &index ), result );
	else if( type == AT_HASH )
		brEvalHashLookup( BRHASH( &listEval ), &index, result, true );

	return EC_OK;
}

RTC_INLINE int stEvalCopyList( stCopyListExp *l, stRunInstance *i, brEval *result ) {
	brEval listEval;
	int resultCode;

	if (( resultCode = stExpEval3( l->expression, i, &listEval ) ) != EC_OK ) return resultCode;

	if ( listEval.type() != AT_LIST ) {
		stEvalError( i->instance, EE_TYPE, "expected type \"list\" during \"copylist\" evaluation" );
		return EC_ERROR;
	}

	result->set( brEvalListDeepCopy( BRLIST( &listEval ) ) );

	return EC_OK;
}

RTC_INLINE int stEvalAll( stAllExp *e, stRunInstance *i, brEval *result ) {
	brEval instance;

	std::set< stInstance*, stInstanceCompare>::iterator ii;

	if ( !e->object ) {
		brObject *type = brObjectFind( i->instance->type->engine, e->name.c_str() );

		if ( !type ) {
			stEvalError( i->instance, EE_UNKNOWN_OBJECT, "unknown object type \"%s\" in \"all\" expression\n", e->name.c_str() );
			return EC_ERROR;
		}

		e->object = ( stObject * )type->userData;
	}

	result->set( new brEvalListHead() );

	brEvalListHead *listhead = BRLIST( result );

	for ( ii = e->object->allInstances.begin(); ii != e->object->allInstances.end(); ii++ ) {
		stInstance *i = *ii;

		instance.set( i->breveInstance );

		brEvalListInsert( listhead, listhead->_vector.size(), &instance );
	}

	return EC_OK;
}

RTC_INLINE int stEvalSort( stSortExp *w, stRunInstance *i, brEval *result ) {
	brEval listEval;
	stMethod *method;
	int resultCode;

	resultCode = stExpEval3( w->listExp, i, &listEval );

	if ( resultCode != EC_OK )
		return resultCode;

	method = stFindInstanceMethod( i->instance->type, w->methodName.c_str(), 2, NULL );

	if ( !method ) {
		stEvalError( i->instance, EE_UNKNOWN_METHOD, "object type \"%s\" does not respond to method \"%s\"", i->instance->type->name.c_str(), w->methodName.c_str() );
		return EC_ERROR;
	}

	resultCode = stSortEvalList( BRLIST( &listEval ), i->instance, method );

	return resultCode;
}

RTC_INLINE int stEvalListIndexPointer( stListIndexExp *l, stRunInstance *i, void **pointer, int *type ) {
	brEval list, index;
	brEval *result;
	int resultCode;

	if (( resultCode = stExpEval3( l->listExp, i, &list ) ) != EC_OK ||
	        ( resultCode = stExpEval3( l->indexExp, i, &index ) ) != EC_OK )
		return resultCode;

	if ( index.type() != AT_INT && stToInt( &index, &index, i ) == EC_ERROR ) {
		stEvalError( i->instance, EE_TYPE, "expected type \"int\" in list element evaluation (index component)" );
		return EC_ERROR;
	}

	if( list.type() == AT_MATRIX ) {
		stPointerForExp( (stLoadExp*)l->listExp, i, pointer, type );

		*type = AT_VECTOR;

		switch( BRINT( &index ) ) {
			

			case 0:
				return EC_OK;
				break;
			case 1:
				*pointer = ( ( double* )*pointer ) + 3;
				return EC_OK;
				break;
			case 2:
				*pointer = ( ( double* )*pointer ) + 6;
				return EC_OK;
				break;
			default:
				stEvalError( i->instance, EE_BOUNDS, "matrix index \"%d\" out of bounds", BRINT( &index ) );
				return EC_ERROR;
				break;
		}
	}

	if ( list.type() != AT_LIST )
		return EC_ERROR;

	if ( stDoEvalListIndexPointer( BRLIST( &list ), BRINT( &index ), &result ) ) {
		stEvalError( i->instance, EE_BOUNDS, "list index \"%d\" out of bounds", BRINT( &index ) );
		return EC_ERROR;
	}

	*pointer = &result->getVector();

	*type = result->type();

	return EC_OK;
}

RTC_INLINE int stEvalIndexLookup( stListIndexExp *l, stRunInstance *i, brEval *t ) {
	brEval list, index;
	int resultCode;

	if (( resultCode = stExpEval3( l->listExp, i, &list ) ) != EC_OK ||
	        ( resultCode = stExpEval3( l->indexExp, i, &index ) ) != EC_OK )
		return resultCode;

	if ( list.type() == AT_LIST ) {
		if ( index.type() != AT_INT && stToInt( &index, &index, i ) == EC_ERROR ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"int\" in list index" );
			return EC_ERROR;
		}

		if ( stDoEvalListIndex( BRLIST( &list ), BRINT( &index ), t ) ) {
			stEvalError( i->instance, EE_BOUNDS, "list index \"%d\" out of bounds", BRINT( &index ) );
			return EC_ERROR;
		}
	} else if( list.type() == AT_INSTANCE ) {
		if( !BRINSTANCE( &list ) ) {
			stEvalError( i -> instance, EE_NULL_INSTANCE, "uninitialized instance in index lookup" );
			return EC_ERROR;
		}
        
		stInstance *lookupInstance = ( stInstance * )BRINSTANCE( &list )->userData;
        
		// Find the variable for this instance

		if( index.type() != AT_STRING ) {
			stEvalError( i -> instance, EE_TYPE, "expected type \"string\" in instance lookup" );
			return EC_ERROR;
		}
        
		stVar *var = stObjectLookupVariable( lookupInstance -> type, BRSTRING( &index ) );
        
		if( !var ) {
			stEvalError( i->instance, EE_TYPE, "Cannot locate variable \"%s\" for object", BRSTRING( &index ) );
			return EC_ERROR;
		}
        
		// Make a pointer to the variable by looking inside the instance by the proper offset
        
		void *pointer = &lookupInstance->variables[ var -> offset ];
        
		return stLoadVariable( pointer, var -> type -> _type, t, i );
	} else if ( list.type() == AT_VECTOR ) {
		if ( index.type() != AT_INT && stToInt( &index, &index, i ) == EC_ERROR ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"int\" in vector index" );
			return EC_ERROR;
		}

		switch( BRINT( &index ) ) {
			case 0:
				t->set( BRVECTOR( &list ).x );
				break;
			case 1:
				t->set( BRVECTOR( &list ).y );
				break;
			case 2:
				t->set( BRVECTOR( &list ).z );
				break;

			default:
				stEvalError( i->instance, EE_BOUNDS, "vector index \"%d\" out of bounds", BRINT( &index ) );
				return EC_ERROR;
		}
	} else if ( list.type() == AT_MATRIX ) {
		if ( index.type() != AT_INT && stToInt( &index, &index, i ) == EC_ERROR ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"int\" in matrix index" );
			return EC_ERROR;
		}

		slMatrix &m = BRMATRIX( &list );
		slVector result;

		switch( BRINT( &index ) ) {

			case 0:
				slVectorSet( &result, m[ 0 ][ 0 ], m[ 0 ][ 1 ], m[ 0 ][ 2 ] );
				break;
			case 1:
				slVectorSet( &result, m[ 1 ][ 0 ], m[ 1 ][ 1 ], m[ 1 ][ 2 ] );
				break;
			case 2:
				slVectorSet( &result, m[ 2 ][ 0 ], m[ 2 ][ 1 ], m[ 2 ][ 2 ] );
				break;

			default:
				stEvalError( i->instance, EE_BOUNDS, "matrix index \"%d\" out of bounds", BRINT( &index ) );
				return EC_ERROR;

		}

		t->set( result );

	} else if ( list.type() == AT_HASH ) {
		brEvalHashLookup( BRHASH( &list ), &index, t );
	} else if ( list.type() == AT_STRING ) {
		char *newstring, *oldstring;

		oldstring = BRSTRING( &list );

		if (( int )strlen( oldstring ) <= BRINT( &index ) ) {
			stEvalError( i->instance, EE_BOUNDS, "string index \"%d\" out of bounds", BRINT( &index ) );
			return EC_ERROR;
		}

		newstring = ( char * )slMalloc( 2 );

		newstring[0] = oldstring[BRINT( &index )];
		newstring[1] = '\0';

		t->set( newstring );

		slFree( newstring );
	} else {
		stEvalError( i->instance, EE_TYPE, "expected type \"list\" or \"hash\" in lookup expression" );
		return EC_ERROR;
	}

	return EC_OK;
}

RTC_INLINE int stEvalIndexAssign( stListIndexAssignExp *l, stRunInstance *i, brEval *t ) {
	brEval expression, index;
	int resultCode;

	if (( resultCode = stExpEval3( l->listExp, i, &expression ) ) != EC_OK ||
	        ( resultCode = stExpEval3( l->indexExp, i, &index ) ) != EC_OK ||
	        ( resultCode = stExpEval3( l->assignment, i, t ) ) != EC_OK )
		return resultCode;

	if ( expression.type() == AT_LIST ) {
		if ( index.type() != AT_INT && stToInt( &index, &index, i ) == EC_ERROR ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"int\" for list index" );
			return EC_ERROR;
		}

		if ( stEvalListIndexAssign( BRLIST( &expression ), BRINT( &index ), t, i ) ) {
			stEvalError( i->instance, EE_BOUNDS, "list index \"%d\" out of bounds", BRINT( &index ) );
			return EC_ERROR;
		}
	} else if ( expression.type() == AT_HASH ) {

		brEvalHashStore( BRHASH( &expression ), &index, t );
	} else if ( expression.type() == AT_VECTOR ) {
		int type;
		slVector *vec;

		resultCode = stPointerForExp( l->listExp, i, ( void ** ) &vec, &type );

		if( !vec || type != AT_VECTOR ) {
			stEvalError( i->instance, EE_TYPE, "cannot assign value to vector index" );
			return EC_ERROR;
		}

		if ( index.type() != AT_INT && stToInt( &index, &index, i ) == EC_ERROR ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"int\" in vector index" );
			return EC_ERROR;
		}
		if ( t->type() != AT_DOUBLE && stToDouble( t, t, i ) == EC_ERROR ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"double\" in vector index assignment" );
			return EC_ERROR;
		}

		switch( BRINT( &index ) ) {
			case 0:
				vec->x = BRDOUBLE( t );
				break;
			case 1:
				vec->y = BRDOUBLE( t );
				break;
			case 2:
				vec->z = BRDOUBLE( t );
				break;

			default:
				stEvalError( i->instance, EE_BOUNDS, "vector index \"%d\" out of bounds", BRINT( &index ) );
				return EC_ERROR;
		}

		t->set( *vec );

	} else if ( expression.type() == AT_MATRIX ) {
		int type;
		slMatrix *mat;

		resultCode = stPointerForExp( l->listExp, i, ( void ** ) &mat, &type );

		if ( index.type() != AT_INT && stToInt( &index, &index, i ) == EC_ERROR ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"int\" in matrix index" );
			return EC_ERROR;
		}
		if ( t->type() != AT_VECTOR ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"vector\" in matrix index assignment" );
			return EC_ERROR;
		}

		slVector &result = BRVECTOR( t );

		switch( BRINT( &index ) ) {

			case 0:
				(*mat)[ 0 ][ 0 ] = result.x; (*mat)[ 0 ][ 1 ] = result.y; (*mat)[ 0 ][ 2 ] = result.z;
				break;
			case 1:
				(*mat)[ 1 ][ 0 ] = result.x; (*mat)[ 1 ][ 1 ] = result.y; (*mat)[ 1 ][ 2 ] = result.z;
				break;
			case 2:
				(*mat)[ 2 ][ 0 ] = result.x; (*mat)[ 2 ][ 1 ] = result.y; (*mat)[ 2 ][ 2 ] = result.z;
				break;

			default:
				stEvalError( i->instance, EE_BOUNDS, "matrix index \"%d\" out of bounds", BRINT( &index ) );
				return EC_ERROR;

		}

	} else if ( expression.type() == AT_STRING ) {
		char **stringptr, *newstring, *oldstring, *substring;
		unsigned int n;
		int type;

		n = BRINT( &index );

		resultCode = stPointerForExp( l->listExp, i, ( void ** ) & stringptr, &type );

		oldstring = *stringptr;

		if ( !oldstring || n < 0 || n > strlen( oldstring ) + 1 ) {
			stEvalError( i->instance, EE_BOUNDS, "string index \"%d\" out of bounds", BRINT( &index ) );
			return EC_ERROR;
		}

		if ( t->type() != AT_STRING ) {
			stEvalError( i->instance, EE_TYPE, "expected type \"string\" for string index assignment", BRINT( &index ) );
			return EC_ERROR;
		}

		substring = BRSTRING( t );

		newstring = ( char * )slMalloc( strlen( oldstring ) + strlen( substring ) + 1 );

		strncpy( newstring, oldstring, n );
		strcpy( &newstring[n], substring );

		if ( n != strlen( oldstring ) ) {
			strcpy( &newstring[n + strlen( substring )], &oldstring[n + 1] );
			newstring[strlen( oldstring ) + strlen( substring ) - 1] = 0;
		} else
			newstring[strlen( oldstring ) + strlen( substring )] = 0;

		t->set( *stringptr );

		if ( *stringptr )
			slFree( *stringptr );

		*stringptr = newstring;

		t->set( slStrdup( newstring ) );
	} else if ( expression.type() == AT_INSTANCE ) {
		// If the index isn't a string, we can't lookup the instance variable
        
		if( index.type() != AT_STRING ) {
			stEvalError( i->instance, EE_TYPE, "Instance index value must be a string" );
			return EC_ERROR;
		}
        
		// Get the steve instance for the expression they're indexxing
        
		stInstance *assignInstance = ( stInstance * )BRINSTANCE( &expression )->userData;
        
		// Find the variable for this instance
        
		stVar *theVariable = stObjectLookupVariable( assignInstance -> type, BRSTRING( &index ) );
        
		if( !theVariable ) {
			stEvalError( i->instance, EE_TYPE, "Cannot locate variable \"%s\" for object", BRSTRING( &index ) );
			return EC_ERROR;
		}
        
		// Make a pointer to the variable by looking inside the instance by the proper offset

		void *pointer = &assignInstance -> variables[ theVariable -> offset ];
        
		// Assign the variable
        
		resultCode = stSetVariable( pointer, theVariable->type->_type, assignInstance->type, t, i );
	} else {
		stEvalError( i->instance, EE_TYPE, "expected type \"list\" or \"hash\" in index assignment" );
		return EC_ERROR;
	}

	return EC_OK;
}

RTC_INLINE int stEvalPrint( stPrintExp *exp, stRunInstance *i, brEval *t ) {
	brEval arg;
	unsigned int n;
	int resultCode;

	for ( n = 0; n < exp->expressions.size(); ++n ) {
		resultCode = stExpEval3( exp->expressions[n], i, &arg );

		if ( resultCode != EC_OK )
			return resultCode;

		resultCode = stPrintEvaluation( &arg, i );

		if ( resultCode != EC_OK )
			return resultCode;

		if ( n != exp->expressions.size() - 1 )
			slMessage( NORMAL_OUTPUT, " " );
	}

	if ( exp->newline )
		slMessage( NORMAL_OUTPUT, "\n" );

	return EC_OK;
}

RTC_INLINE int stEvalVectorElementExp( stVectorElementExp *s, stRunInstance *i, brEval *result ) {
	int resultCode;

	resultCode = stExpEval3( s->exp, i, result );

	if ( resultCode != EC_OK )
		return EC_ERROR;

	if ( result->type() != AT_VECTOR ) {
		stEvalError( i->instance, EE_TYPE, "expected type \"vector\" in vector element evaluation" );
		return EC_ERROR;
	}

	switch ( s->element ) {

		case VE_X:
			result->set( BRVECTOR( result ).x );

			return EC_OK;

			break;

		case VE_Y:
			result->set( BRVECTOR( result ).y );

			return EC_OK;

			break;

		case VE_Z:
			result->set( BRVECTOR( result ).z );

			return EC_OK;

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown vector element (%d) in stEvalVectorElementExp", s->element );

			return EC_ERROR;

			break;
	}

	return EC_OK;
}

RTC_INLINE int stEvalVectorElementAssignExp( stVectorElementAssignExp *s, stRunInstance *i, brEval *result ) {
	slVector *vector;
	int resultCode;
	int type = AT_NULL;

	resultCode = stPointerForExp( s->exp, i, ( void ** ) & vector, &type );

	if ( resultCode != EC_OK )
		return EC_ERROR;

	if ( type != AT_VECTOR ) {
		stEvalError( i->instance, EE_TYPE, "expected vector expression in vector element assign" );
		return EC_ERROR;
	}

	resultCode = stExpEval3( s->assignExp, i, result );

	if ( resultCode != EC_OK ) return EC_ERROR;

	resultCode = stToDouble( result, result, i );

	if ( resultCode != EC_OK ) return EC_ERROR;

	switch ( s->element ) {

		case VE_X:
			vector->x = BRDOUBLE( result );

			break;

		case VE_Y:
			vector->y = BRDOUBLE( result );

			break;

		case VE_Z:
			vector->z = BRDOUBLE( result );

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown vector element (%d) in stEvalVectorElementAssignExp", s->element );

			break;
	}

	return EC_OK;
}

/*!
	\brief Calls a C-style internal function in steve.
*/

RTC_INLINE int stEvalCallFunc( stCCallExp *c, stRunInstance *i, brEval *result ) {
	brEval evals[ c->_function->_argCount ];

	int n, resultCode;

	for ( n = 0; n < c->_function->_argCount; ++n ) {
		resultCode = stExpEval3( c->_arguments[n], i, &evals[ n ] );

		if ( resultCode != EC_OK )
			goto cleanup;

		if( evals[ n ].checkNaNInf() ) {
			slMessage( DEBUG_ALL, "warning: NaN/Inf passed into internal function %s as argument %d at line %d of \"%s\"\n", c->_function->_name.c_str(), n , c->line, c->file );
		}

		// if the types don't match, try to convert them 

		if ( evals[n].type() != c->_function->_argTypes[n] && c->_function->_argTypes[n] != AT_UNDEFINED ) {
			resultCode = stToType( &evals[n], c->_function->_argTypes[n], &evals[n], i );
		
			if ( resultCode != EC_OK ) {
				stEvalError( i->instance, EE_TYPE, "expected type \"%s\" for argument #%d to internal method \"%s\", got type \"%s\"", brAtomicTypeStrings[c->_function->_argTypes[n]], n + 1, c->_function->_name.c_str(), brAtomicTypeStrings[( int )evals[n].type()] );
				goto cleanup;
			}
		}

		// if ( c->_function->_argTypes[n] == AT_POINTER && BRPOINTER( &evals[n] ) == NULL ) {
		// 	stEvalError( i->instance, EE_TYPE, "NULL pointer passed as argument %d to internal function %s", n, c->_function->_name.c_str() );
		// 	resultCode = EC_ERROR;
		//	goto cleanup;
		//}
	}

	result->clear();

#ifdef MULTITHREAD

	pthread_mutex_lock( &( i->instance->lock ) );

#endif

	try {
		resultCode = c->_function->_call( evals, result, i->instance->breveInstance );

		if( resultCode != EC_OK ) 
			throw slException( "unknown error" );

		if ( c->_function->_returnType != AT_UNDEFINED && c->_function->_returnType != result->type() ) {
			slMessage( DEBUG_ALL, "Warning: internal function \"%s\" does not correctly set an output value.  If this is a plugin function, see the updated documentation on plugins for more details.\n", c->_function->_name.c_str() );
		}

	} catch ( slException &error ) {
		stEvalError( i->instance, EE_INTERNAL, "an error occurred executing the internal function \"%s\": %s", c->_function->_name.c_str(), error._message.c_str() );

		resultCode = EC_ERROR;
		goto cleanup;
	}

#ifdef MULTITHREAD
	pthread_mutex_unlock( &( i->instance->lock ) );
#endif

	if( result->checkNaNInf() ) {
		slMessage( DEBUG_ALL, "warning: NaN/Inf returned from internal function %s at line %d of \"%s\"\n", c->_function->_name.c_str(), c->line, c->file );
	}

cleanup:
	return resultCode;
}

/*!
	\brief Gives the pointer for an array-load expression in steve.
*/

RTC_INLINE int stEvalArrayIndexPointer( stArrayIndexExp *a, stRunInstance *i, void **pointer, int *type ) {
	brEval indexExp;
	int r;
	int offset;

	r = stExpEval3( a->index, i, &indexExp );

	*type = a->loadType;

	if ( r != EC_OK ) {
		slMessage( DEBUG_ALL, "Error evaluating index of array expression.\n" );
		return r;
	}

	if ( indexExp.type() != AT_INT && !( r = stToInt( &indexExp, &indexExp, i ) ) ) {
		slMessage( DEBUG_ALL, "Array index must be of type \"int\"\n" );
		return r;
	}

	offset = BRINT( &indexExp );

	if ( offset >= a->maxIndex || offset < 0 ) {
		stEvalError( i->instance, EE_BOUNDS, "array index \"%d\" out of bounds", BRINT( &indexExp ) );
		return EC_ERROR;
	}

	if ( a->local )
		*pointer = &i->instance->type->steveData->stack[a->offset];
	else
		*pointer = &i->instance->variables[a->offset];

	*pointer = ( char * ) * pointer + a->typeSize * offset;

	return EC_OK;
}

RTC_INLINE int stEvalArrayIndex( stArrayIndexExp *a, stRunInstance *i, brEval *result ) {
	void *pointer = NULL;
	int r, type;

	r = stEvalArrayIndexPointer( a, i, &pointer, &type );

	if ( r != EC_OK ) return r;

	return stLoadVariable( pointer, a->loadType, result, i );
}

RTC_INLINE int stEvalArrayIndexAssign( stArrayIndexAssignExp *a, stRunInstance *i, brEval *rvalue ) {
	brEval indexExp;
	char *pointer;
	int r;

	r = stExpEval3( a->index, i, &indexExp );

	if ( r != EC_OK ) {
		slMessage( DEBUG_ALL, "Error evaluating index of array expression.\n" );
		return r;
	}

	r =  stExpEval3( a->rvalue, i, rvalue );

	if ( r != EC_OK ) return r;

	if ( indexExp.type() != AT_INT && !( r = stToInt( &indexExp, &indexExp, i ) ) ) {
		slMessage( DEBUG_ALL, "Array index must be of type \"int\".\n" );
		return r;
	}

	if ( BRINT( &indexExp ) >= a->maxIndex || BRINT( &indexExp ) < 0 ) {
		stEvalError( i->instance, EE_BOUNDS, "array index \"%d\" out of bounds", BRINT( &indexExp ) );
		return EC_ERROR;
	}

	if ( a->local )
		pointer = &i->instance->type->steveData->stack[a->offset];
	else
		pointer = &i->instance->variables[a->offset];

	pointer += a->typeSize * BRINT( &indexExp );

	return stSetVariable( pointer, a->assignType, NULL, rvalue, i );
}

RTC_INLINE int stEvalAssignment( stAssignExp *a, stRunInstance *i, brEval *t ) {
	char *pointer;
	int resultCode;

	resultCode = stExpEval3( a->_rvalue, i, t );

	if ( resultCode != EC_OK ) return resultCode;

	if ( a->_local )
		pointer = &i->instance->type->steveData->stack[a->_offset];
	else
		pointer = &i->instance->variables[a->_offset];

	if ( a->_objectName.size() && !a->_objectType ) {
		brObject *object = brObjectFind( i->instance->type->engine, a->_objectName.c_str() );

		if ( object )
			a->_objectType = ( stObject * )object->userData;
	}

	resultCode = stSetVariable( pointer, a->_assignType, a->_objectType, t, i );

	return resultCode;
}

RTC_INLINE int stEvalUnaryExp( stUnaryExp *b, stRunInstance *i, brEval *result ) {
	brEval truth;
	char *str;
	int resultCode;

	// This particular code is not very robust. 

	resultCode = stExpEval3( b->expression, i, result );

	if ( resultCode != EC_OK )
		return resultCode;

	if ( b->op == UT_NOT ) {
		stEvalTruth( result, &truth, i );

		result->set( !BRINT( &truth ) );

		return EC_OK;
	}

	// if it's a string, switch to a number before continuing.

	if ( result->type() == AT_STRING ) {
		str = BRSTRING( result );

		if (( resultCode = stToDouble( result, result, i ) ) != EC_OK )
			return resultCode;
	}

	if ( result->type() == AT_LIST ) {
		switch ( b->op ) {

			case UT_MINUS:
				stEvalError( i->instance, EE_TYPE, "type \"list\" unexpected during evaluation of unary operator \"-\"" );

				return EC_ERROR;

				break;
		}
	}

	if ( result->type() == AT_VECTOR ) {
		slVector v;

		switch ( b->op ) {

			case UT_MINUS:
				slVectorMul( &BRVECTOR( result ), -1, &v );

				result->set( v );

				return EC_OK;

				break;

			default:
				stEvalError( i->instance, EE_INTERNAL, "unknown unary operator (%d) in stEvalUnaryExp", b->op );

				return EC_ERROR;

				break;
		}
	}

	if ( result->type() == AT_INT ) {
		switch ( b->op ) {

			case UT_MINUS:
				result->set( BRINT( result ) * -1 );

				return EC_OK;

				break;

			default:
				stEvalError( i->instance, EE_INTERNAL, "unknown unary operator (%d) in stEvalUnaryExp", b->op );

				return EC_ERROR;

				break;
		}
	}

	if ( result->type() == AT_DOUBLE ) {
		switch ( b->op ) {

			case UT_MINUS:
				result->set( BRDOUBLE( result ) * -1 );

				return EC_OK;

				break;

			default:
				stEvalError( i->instance, EE_INTERNAL, "unknown unary operator (%d) in stEvalUnaryExp", b->type );

				return EC_ERROR;

				break;
		}
	}

	if ( result->type() == AT_INSTANCE ) {
		switch ( b->op ) {

			case UT_MINUS:
				stEvalError( i->instance, EE_TYPE, "type \"object\" unexpected during evaluation of unary operator \"-\"" );

				return EC_ERROR;

				break;

			default:
				stEvalError( i->instance, EE_INTERNAL, "unknown unary operator (%d) in stEvalUnaryExp", b->op );

				return EC_ERROR;

				break;
		}
	}

	if ( result->type() == AT_POINTER ) {
		switch ( b->op ) {

			case UT_MINUS:
				stEvalError( i->instance, EE_TYPE, "type \"pointer\" unexpected during evaluation of unary operator \"-\"" );

				return EC_ERROR;

				break;

			default:
				stEvalError( i->instance, EE_INTERNAL, "unknown unary operator (%d) in stEvalUnaryExp", b->op );

				return EC_ERROR;

				break;
		}
	}

	stEvalError( i->instance, EE_INTERNAL, "unknown expression type (%d) in stEvalUnaryExp", result->type() );

	return EC_ERROR;
}

RTC_INLINE int stEvalBinaryStringExp( char op, brEval *l, brEval *r, brEval *result, stRunInstance *i ) {
	char *sl, *sr;

	sl = BRSTRING( l );
	sr = BRSTRING( r );

	switch ( op ) {
		case BT_ADD:
			{
				std::string r = std::string( sl ) + std::string( sr );
				result->set( r.c_str() );
			}
			break;

		case BT_EQ:
			result->set( !strcmp( sl, sr ) );

			break;

		case BT_NE:
			result->set( strcmp( sl, sr ) );

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown string operator (%d) in stEvalBinaryStringExp", op );

			return EC_ERROR;
	}

	return EC_OK;
}

RTC_INLINE int stEvalBinaryMatrixExp( char op, brEval *l, brEval *r, brEval *t, stRunInstance *i ) {
	double temp[3][3];
	slMatrix result;

	if ( l->type() == AT_MATRIX && r->type() == AT_MATRIX ) {
		switch ( op ) {

			case BT_MUL:
				slMatrixMulMatrix( BRMATRIX( l ), BRMATRIX( r ), result );

				t->set( result );

				break;

			case BT_DIV:
				slMatrixInvert( BRMATRIX( r ), temp );

				slMatrixMulMatrix( BRMATRIX( l ), temp, result );

				t->set( result );

				break;

			case BT_ADD:
				slMatrixAdd( BRMATRIX( l ), BRMATRIX( r ), result );

				t->set( result );

				break;

			case BT_SUB:
				slMatrixSub( BRMATRIX( l ), BRMATRIX( r ), result );

				t->set( result );

				break;

			case BT_EQ:
				t->set( slMatrixCompare( BRMATRIX( l ), result ) );

				t->set( result );

				break;

			case BT_NE:
				t->set( !slMatrixCompare( BRMATRIX( l ), result ) );

				t->set( result );

				break;

			default:
				stEvalError( i->instance, EE_INTERNAL, "invalid matrix binary operator (%d)", op );

				return EC_ERROR;
		}

		return EC_OK;
	}

	/* transforming a vector... */

	if ( l->type() == AT_MATRIX && r->type() == AT_VECTOR ) {
		slVector v;

		switch ( op ) {

			case BT_MUL:
				slVectorXform( BRMATRIX( l ), &BRVECTOR( r ), &v );

				t->set( v );

				break;

			default:
				break;

				return EC_ERROR;
		}

		return EC_OK;
	}

	/* inverse transform */

	if ( l->type() == AT_VECTOR && r->type() == AT_MATRIX ) {
		slVector v;

		switch ( op ) {

			case BT_MUL:
				slVectorInvXform( BRMATRIX( l ), &BRVECTOR( r ), &v );

				t->set( v );

				break;

			default:
				break;

				return EC_ERROR;
		}

		return EC_OK;
	}

	if ( l->type() == AT_MATRIX ) {
		if ( stToDouble( r, r, i ) != EC_OK ) return EC_ERROR;

		switch ( op ) {

			case BT_MUL:
				slMatrixMulScalar( BRMATRIX( l ), BRDOUBLE( r ), result );

				t->set( result );

				break;

			case BT_DIV:
				slMatrixMulScalar( BRMATRIX( l ), 1.0 / BRDOUBLE( r ), result );

				t->set( result );

				break;

			default:
				return EC_ERROR;

				break;
		}

		return EC_OK;
	}

	if ( r->type() == AT_MATRIX ) {
		if ( stToDouble( l, l, i ) != EC_OK ) return EC_ERROR;

		switch ( op ) {

			case BT_MUL:
				slMatrixMulScalar( BRMATRIX( r ), BRDOUBLE( l ), result );

				t->set( result );

				break;

			case BT_DIV:
				slMatrixInvert( BRMATRIX( r ), temp );

				slMatrixMulScalar( temp, BRDOUBLE( l ), result );

				t->set( result );

				break;

			default:
				return EC_ERROR;

				break;
		}

		return EC_OK;
	}

	return EC_ERROR;
}

RTC_INLINE int stEvalBinaryVectorExp( char op, brEval *l, brEval *r, brEval *t, stRunInstance *i ) {
	brEval *temp;
	slVector v;

	if ( op == BT_MUL || op == BT_DIV ) {
		if ( l->type() == AT_VECTOR && r->type() != AT_DOUBLE && stToDouble( r, r, i ) == EC_ERROR ) {
			if ( op == BT_MUL ) stEvalError( i->instance, EE_TYPE, "expected type \"double\" for right expression in vector multiplication evaluation" );
			else stEvalError( i->instance, EE_TYPE, "expected type \"vector\" for left expression in vector division evaluation" );

			return EC_ERROR;
		}

		if ( r->type() == AT_VECTOR && l->type() != AT_DOUBLE && stToDouble( l, l, i ) == EC_ERROR ) {
			if ( op == BT_MUL ) stEvalError( i->instance, EE_TYPE, "expected type \"vector\" for left expression in vector multiplication evaluation" );
			else stEvalError( i->instance, EE_TYPE, "expected type \"vector\" for left expression in vector division evaluation" );

			return EC_ERROR;
		}

		// so now we know that we have the right types, but we want the
		// number to be the right argument, so switch them if necessary...

		if ( r->type() == AT_VECTOR ) {
			temp = r;
			r = l;
			l = temp;
		}

	} else if ( l->type() != AT_VECTOR || r->type() != AT_VECTOR ) {
		stEvalError( i->instance, EE_TYPE, "expected type \"vector\" for vector binary operation evaluation" );
		return EC_ERROR;
	}

	switch ( op ) {

		case BT_ADD:
			slVectorAdd( &BRVECTOR( l ), &BRVECTOR( r ), &v );

			t->set( v );

			return EC_OK;

			break;

		case BT_SUB:
			slVectorSub( &BRVECTOR( l ), &BRVECTOR( r ), &v );

			t->set( v );

			return EC_OK;

			break;

		case BT_MUL:
			slVectorMul( &BRVECTOR( l ), BRDOUBLE( r ), &v );

			t->set( v );

			return EC_OK;

			break;

		case BT_DIV:
			if ( BRDOUBLE( r ) == 0.0 ) {
				stEvalError( i->instance, EE_MATH, "division by zero error in vector division" );
				return EC_ERROR;
			}

			slVectorMul( &BRVECTOR( l ), 1.0 / BRDOUBLE( r ), &v );

			t->set( v );

			return EC_OK;

			break;

		case BT_EQ:
			if ( BRVECTOR( l ).x == BRVECTOR( r ).x &&
			        BRVECTOR( l ).y == BRVECTOR( r ).y &&
			        BRVECTOR( l ).z == BRVECTOR( r ).z ) t->set( 1 );
			else t->set( 0 );

			return EC_OK;

			break;

		case BT_NE:
			if ( BRVECTOR( l ).x == BRVECTOR( r ).x &&
			        BRVECTOR( l ).y == BRVECTOR( r ).y &&
			        BRVECTOR( l ).z == BRVECTOR( r ).z ) t->set( 0 );
			else t->set( 1 );

			return EC_OK;

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown binary expression operator (%d) in stEvalBinaryVectorExp", op );

			return EC_ERROR;

			break;
	}
}

RTC_INLINE int stEvalBinaryDoubleExp( char op, brEval *l, brEval *r, brEval *t, stRunInstance *i ) {
	int c;

	if ( l->type() != AT_DOUBLE ) if (( c = stToDouble( l, l, i ) ) != EC_OK ) return c;
	if ( r->type() != AT_DOUBLE ) if (( c = stToDouble( r, r, i ) ) != EC_OK ) return c;

	switch ( op ) {

		case BT_ADD:
			t->set( BRDOUBLE( l ) + BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_SUB:
			t->set( BRDOUBLE( l ) - BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_MUL:
			t->set( BRDOUBLE( l ) * BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_DIV:
			if ( BRDOUBLE( r ) == 0.0 ) {
				stEvalError( i->instance, EE_MATH, "Division by zero error" );
				return EC_ERROR;
			}

			t->set( BRDOUBLE( l ) / BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_EQ:
			t->set( BRDOUBLE( l ) == BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_NE:
			t->set( BRDOUBLE( l ) != BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_GT:
			t->set( BRDOUBLE( l ) > BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_GE:
			t->set( BRDOUBLE( l ) >= BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_LT:
			t->set( BRDOUBLE( l ) < BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_LE:
			t->set( BRDOUBLE( l ) <= BRDOUBLE( r ) );

			return EC_OK;

			break;

		case BT_MOD:
			if ( BRDOUBLE( r ) == 0.0 ) {
				stEvalError( i->instance, EE_MATH, "Modulus by zero error" );
				return EC_ERROR;
			}

			t->set( fmod( BRDOUBLE( l ), BRDOUBLE( r ) ) );

			return EC_OK;

			break;

		case BT_POW:
			t->set( pow( BRDOUBLE( l ), BRDOUBLE( r ) ) );

			return EC_OK;

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown binary expression operator (%d) in stEvalBinaryDoubleExp", op );

			return EC_ERROR;

			break;
	}
}

RTC_INLINE int stEvalBinaryIntExp( char op, brEval *l, brEval *r, brEval *t, stRunInstance *i ) {
	switch ( op ) {

		case BT_ADD:
			t->set( BRINT( l ) + BRINT( r ) );

			return EC_OK;

			break;

		case BT_SUB:
			t->set( BRINT( l ) - BRINT( r ) );

			return EC_OK;

			break;

		case BT_MUL:
			t->set( BRINT( l ) * BRINT( r ) );

			return EC_OK;

			break;

		case BT_DIV:
			if ( BRINT( r ) == 0 ) {
				stEvalError( i->instance, EE_MATH, "division by zero error" );
				return EC_ERROR;
			}

			t->set( BRINT( l ) / BRINT( r ) );

			return EC_OK;

			break;

		case BT_POW:
			t->set(( int )pow( BRINT( l ), BRINT( r ) ) );

			return EC_OK;

			break;

		case BT_MOD:
			if ( BRINT( r ) == 0 ) {
				stEvalError( i->instance, EE_MATH, "modulus by zero error" );
				return EC_ERROR;
			}

			t->set( BRINT( l ) % BRINT( r ) );

			return EC_OK;

			break;

		case BT_EQ:
			t->set( BRINT( l ) == BRINT( r ) );

			return EC_OK;

			break;

		case BT_NE:
			t->set( BRINT( l ) != BRINT( r ) );

			return EC_OK;

			break;

		case BT_GT:
			t->set( BRINT( l ) > BRINT( r ) );

			return EC_OK;

			break;

		case BT_GE:
			t->set( BRINT( l ) >= BRINT( r ) );

			return EC_OK;

			break;

		case BT_LT:
			t->set( BRINT( l ) < BRINT( r ) );

			return EC_OK;

			break;

		case BT_LE:
			t->set( BRINT( l ) <= BRINT( r ) );

			return EC_OK;

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown binary expression operator (%d) in stEvalBinaryIntExp", op );

			return EC_ERROR;

			break;
	}
}

RTC_INLINE int stEvalBinaryExp( stBinaryExp *b, stRunInstance *i, brEval *result ) {
	brEval tl, tr;
	brEval truthResult;
	int c;

	if ( b->op == BT_LAND ) {

		result->set( 1 );

		/* LAND is treated as a short circuit op, do not eval both sides */

		if (( c = stExpEval3( b->left, i, &tl ) ) != EC_OK ) return c;

		if (( c = stEvalTruth( &tl, &truthResult, i ) ) != EC_OK ) return c;

		if ( !BRINT( &truthResult ) ) {
			/* short circuit -- we know that the left is false, so return */

			result->set( 0 );

			return EC_OK;
		}

		if (( c = stExpEval3( b->right, i, &tr ) ) != EC_OK ) return c;

		if (( c = stEvalTruth( &tr, &truthResult, i ) ) != EC_OK ) return c;

		if ( !BRINT( &truthResult ) ) result->set( 0 );

		return EC_OK;
	}

	if ( b->op == BT_LOR ) {

		result->set( 0 );

		/* LOR is treated as a short circuit op, do not eval both sides */

		if (( c =  stExpEval3( b->left, i, &tl ) ) != EC_OK ) return c;

		if (( c = stEvalTruth( &tl, &truthResult, i ) ) != EC_OK ) return c;

		if ( BRINT( &truthResult ) ) {
			/* short circuit -- we know that the left is true, so return */

			result->set( 1 );

			return EC_OK;
		}

		if (( c = stExpEval3( b->right, i, &tr ) ) != EC_OK ) return c;

		if (( c = stEvalTruth( &tr, &truthResult, i ) ) != EC_OK ) return c;

		if ( BRINT( &truthResult ) ) result->set( 1 );

		return EC_OK;
	}

	if (( c =  stExpEval3( b->left, i, &tl ) ) != EC_OK ) return c;

	if (( c =  stExpEval3( b->right, i, &tr ) ) != EC_OK ) return c;

	return stEvalBinaryExpWithEvals( i, b->op, &tl, &tr, result );
}

RTC_INLINE int stEvalBinaryExpWithEvals( stRunInstance *i, unsigned char op, brEval *tl, brEval *tr, brEval *result ) {
	int c;

	int t1 = tl -> type();
	int t2 = tr -> type();

	if ( t1 == AT_INT && t2 == AT_INT ) 
		return stEvalBinaryIntExp( op, tl, tr, result, i );

	// if either expression is a matrix... 

	if ( t1 == AT_MATRIX || t2 == AT_MATRIX ) 
		return stEvalBinaryMatrixExp( op, tl, tr, result, i );

	// if either expression is a vector... 

	if ( t1 == AT_VECTOR || t2 == AT_VECTOR ) 
		return stEvalBinaryVectorExp( op, tl, tr, result, i );

	// if we have two strings and they're testing for equality or inequality 
	// we do a string compare--otherwise we'll convert to doubles and handle 
	// the expression that way 

	if ( t2 == AT_STRING && t1 == AT_STRING && ( op == BT_EQ || op == BT_NE || op == BT_ADD ) )
		return stEvalBinaryStringExp( op, tl, tr, result, i );

	if ( t2 == AT_LIST && t1 == AT_LIST && ( op == BT_EQ || op == BT_NE ) )
		return stEvalBinaryEvalListExp( op, tl, tr, result, i );

	if ( t2 == AT_STRING ) {
		if (( c = stToDouble( tr, tr, i ) ) != EC_OK ) return c;
		t2 = AT_DOUBLE;
	} else if ( t2 == AT_LIST ) {
		if (( c = stToInt( tr, tr, i ) ) != EC_OK ) return c;
		t2 = AT_INT;
	}

	if ( t1 == AT_STRING ) {
		if (( c = stToDouble( tl, tl, i ) ) != EC_OK ) return c;
		t1 = AT_DOUBLE;
	} else if ( t1 == AT_LIST ) {
		if (( c = stToInt( tl, tl, i ) ) != EC_OK ) return c;
		t1 = AT_INT;
	}

	if ( t1 == AT_DOUBLE || t2 == AT_DOUBLE ) 
		return stEvalBinaryDoubleExp( op, tl, tr, result, i );

	return stEvalBinaryIntExp( op, tl, tr, result, i );
}

RTC_INLINE int stEvalRandExp( stRandomExp *r, stRunInstance *i, brEval *result ) {
	slVector v;

	stExpEval3( r->expression, i, result );

	switch ( result->type() ) {

		case AT_INT:
			if ( BRINT( result ) < 0 ) result->set( 0 );
			else result->set(( int )( random() % ( BRINT( result ) + 1 ) ) );

			return EC_OK;

			break;

		case AT_DOUBLE:
			result->set( slRandomDouble() * BRDOUBLE( result ) );
			return EC_OK;

			break;

		case AT_VECTOR:
			v.x = slRandomDouble() * BRVECTOR( result ).x;
			v.y = slRandomDouble() * BRVECTOR( result ).y;
			v.z = slRandomDouble() * BRVECTOR( result ).z;

			result->set( v );

			return EC_OK;

			break;

		default:
			stEvalError( i->instance, EE_TYPE, "expected type \"int\", \"double\" or \"vector\" in evaluation of \"random\"" );
			return EC_ERROR;
	}

	return EC_OK;
}

/* turn a vector stExp tree into a real vector */

RTC_INLINE int stEvalVectorExp( stVectorExp *ve, stRunInstance *i, brEval *result ) {
	brEval tx, ty, tz;
	int resultCode;
	slVector v;

	stExpEval3( ve->_x, i, &tx );
	stExpEval3( ve->_y, i, &ty );
	stExpEval3( ve->_z, i, &tz );

	if ( tx.type() != AT_DOUBLE ) {
		if (( resultCode = stToDouble( &tx, &tx, i ) ) != EC_OK ) return resultCode;
	}

	if ( ty.type() != AT_DOUBLE ) {
		if (( resultCode = stToDouble( &ty, &ty, i ) ) != EC_OK ) return resultCode;
	}

	if ( tz.type() != AT_DOUBLE ) {
		if (( resultCode = stToDouble( &tz, &tz, i ) ) != EC_OK ) return resultCode;
	}

	v.x = BRDOUBLE( &tx );

	v.y = BRDOUBLE( &ty );
	v.z = BRDOUBLE( &tz );

	result->set( v );

	return EC_OK;
}

RTC_INLINE int stEvalMatrixExp( stMatrixExp *v, stRunInstance *i, brEval *result ) {
	int n, resultCode;
	brEval t;
	slMatrix m;

	for ( n = 0;n < 9;n++ ) {
		stExpEval3( v->expressions[n], i, &t );

		if ( t.type() != AT_DOUBLE )
			if (( resultCode = stToDouble( &t, &t, i ) ) != EC_OK ) return resultCode;

		m[n / 3][n % 3] = BRDOUBLE( &t );
	}

	result->set( m );

	return EC_OK;
}

/*!
	\brief Calls a method once the method has been found and the arguments have been processed.
*/

int stCallMethod( stRunInstance *caller, stRunInstance *target, stMethod *method, const brEval **args, int argcount, brEval *result ) {
	int n;
	stKeywordEntry *keyEntry;
	stStackRecord record;

	char *savedStackPointer, *newStStack;
	int resultCode;

	stSteveData *steveData;

	if ( target->instance->status != AS_ACTIVE ) {
		stEvalError( target->instance, EE_FREED_INSTANCE, "method \"%s\" being called with freed instance of class \"%s\" (%p)", method->name.c_str(), target->instance->type->name.c_str(), target->instance );
		return EC_ERROR;
	}

	steveData = target->instance->type->steveData;

	savedStackPointer = steveData->stack;

	// if there is no current stackpointer (outermost frame), start at the end of the stack

	if ( savedStackPointer == NULL ) 
		steveData->stack = &steveData->stackBase[ ST_STACK_SIZE ];

	// step down the stack enough to make room for the current calling method

	newStStack = steveData->stack - method->stackOffset;

	if ( newStStack < steveData->stackBase ) {
		slMessage( DEBUG_ALL, "Stack overflow in class \"%s\" method \"%s\"\n", target->instance->type->name.c_str(), method->name.c_str() );
		return EC_ERROR;
	}

	memset( newStStack, 0, method->stackOffset );

	// go through the arguments and place them on the stack.

	for ( n = 0;n < argcount;n++ ) {
		keyEntry = method->keywords[n];

		if ( keyEntry->var->type->_objectName.size() && !keyEntry->var->type->_objectType ) {
			brObject *o = brObjectFind( target->instance->type->engine, keyEntry->var->type->_objectName.c_str() );

			if ( o ) keyEntry->var->type->_objectType = ( stObject* )o->userData;
		}

		resultCode = stSetVariable( &newStStack[keyEntry->var->offset], keyEntry->var->type->_type, keyEntry->var->type->_objectType, (brEval*) args[ n ], caller );

		if ( resultCode != EC_OK ) {
			slMessage( DEBUG_ALL, "Error evaluating keyword \"%s\" for method \"%s\"\n", keyEntry->keyword.c_str(), method->name.c_str() );
			return resultCode;
		}
	}

	record.instance = target->instance;

	record.method = method;
	record.previousStackRecord = steveData->stackRecord;
	steveData->stackRecord = &record;

	// prepare for the actual method call

	steveData->stack = newStStack;

	resultCode = stEvalExpVector( &method->code, target, result );

	if ( resultCode == EC_STOP ) 
		resultCode = EC_OK;



	// unretain the local variables

	std::vector< stVar* >::iterator vi;

	for ( vi = method->variables.begin(); vi != method->variables.end(); vi++ ) {
		brEval e;

		if ( stGCNEEDSCOLLECT( ( *vi )->type->_type ) && (
				( *vi )->type->_type != AT_STRING || *( void** )&newStStack[( *vi )->offset] != BRSTRING( result ) ) )
			stGCUnretainAndCollectPointer( *( void** )&newStStack[( *vi )->offset], ( *vi )->type->_type );
	}

	// unretain the input arguments

	std::vector< stKeywordEntry* >::iterator ki;

	for ( ki = method->keywords.begin(); ki != method->keywords.end(); ki++ ) {
		if( stGCNEEDSCOLLECT( ( *ki )->var->type->_type ) ) 
			stGCUnretainAndCollectPointer( *( void** )&newStStack[( *ki )->var->offset ], ( *ki )->var->type->_type );
	}



	// restore the previous stack and stack records

	steveData->stack       = savedStackPointer;
	steveData->stackRecord = record.previousStackRecord;

	return resultCode;
}

int stCallMethodByName( stRunInstance *target, char *name, brEval *resultCode ) {
	return stCallMethodByNameWithArgs( target, name, NULL, 0, resultCode );
}

int stCallMethodByNameWithArgs( stRunInstance *target, char *name, const brEval **args, int argcount, brEval *resultCode ) {
	stMethod *method;
	stRunInstance ri;

	ri.instance = target->instance;
	ri.type = ri.instance->type;

	method = stFindInstanceMethod( target->instance->type, name, argcount, &ri.type );

	if ( !method ) {
		stEvalError( target->instance, EE_UNKNOWN_METHOD, "object type \"%s\" does not respond to method \"%s\"", target->instance->type->name.c_str(), name );
		return EC_ERROR;
	}

	return stCallMethod( NULL, &ri, method, args, argcount, resultCode );
}

void stStackTrace( stSteveData *d ) {
	stStackRecord *r = d->stackRecord;

	slMessage( DEBUG_ALL, "breve engine tack trace:\n" );

	stStackTraceFrame( r );
}

int stStackTraceFrame( stStackRecord *r ) {
	int n;

	if ( !r ) return 0;

	n = stStackTraceFrame( r->previousStackRecord );

	slMessage( DEBUG_ALL, "%d) %s (%p) %s (line %d of \"%s\")\n", n, r->instance->type->name.c_str(), r->instance, r->method->name.c_str(), r->method->lineno, r->method->filename.c_str() );

	return n + 1;
}

int stPrintEvaluation( brEval *e, stRunInstance *i ) {
	char *evalString;

	evalString = brFormatEvaluation( e, i->instance->breveInstance );

	slMessage( NORMAL_OUTPUT, "%s", evalString );

	slFree( evalString );

	return EC_OK;
}

/*!
	\brief Evaluates a new instance expression in steve.

	Produces either a single object or a list.
*/

RTC_INLINE int stEvalNewInstance( stInstanceExp *ie, stRunInstance *i, brEval *t ) {
	brObject *object;
	brEvalListHead *list = NULL;
	brEval listItem, count;
	int n;

	object = brObjectFindWithPreferredType( i->instance->type->engine, ie->name.c_str(), STEVE_TYPE_SIGNATURE );

	if ( !object ) {
		stEvalError( i->instance, EE_UNKNOWN_OBJECT, "unknown object type \"%s\" during new instance evaluation", ie->name.c_str() );
		return EC_ERROR;
	}

	if( object -> type -> _typeSignature != STEVE_TYPE_SIGNATURE ) {
		// Warn the user in case this happens unexpectedly.
		slMessage( DEBUG_ALL, "Could not locate steve object \"%s\", creating bridge instance to other frontend language\n", ie->name.c_str() );
	}

	stExpEval3( ie->count, i, &count );

	if ( count.type() != AT_INT ) {
		stEvalError( i->instance, EE_TYPE, "expected integer count for \"new\" expression" );
		return EC_ERROR;
	}

	if ( BRINT( &count ) == 1 ) {

		t->set( brObjectInstantiate( i->instance->type->engine, object, NULL, 0, false ) );

		if ( BRINSTANCE( t ) == NULL ) {
			stEvalError( i->instance, EE_UNKNOWN_OBJECT, "error creating instance of class %s", ie->name.c_str() );
			return EC_ERROR_HANDLED;
		}

		// As soon as we have created the object, we've passed ownership onto the 
		// return eval (t), so we decrement it

		stGCUnretain( t );

	} else {
		list = new brEvalListHead();

		for ( n = 0;n < BRINT( &count );n++ ) {
			brInstance *instance = brObjectInstantiate( i->instance->type->engine, object, NULL, 0, false );
			listItem.set( instance );

			if ( instance == NULL ) {
				stEvalError( i->instance, EE_UNKNOWN_OBJECT, "error creating instance of class %s", ie->name.c_str() );
				return EC_ERROR_HANDLED;
			}

			// Ownership is now listItem (and the list)

			stGCUnretain( &listItem );

			brEvalListInsert( list, list->_vector.size(), &listItem );
		}

		t->set( list );
	}


	return EC_OK;
}

int stExpEval( stExp *s, stRunInstance *i, brEval *result, stObject **tClass ) {
	brEval t;
	int resultCode = EC_OK;

	if ( tClass ) 
		*tClass = NULL;

	if ( !i ) {
		stEvalError( i->instance, EE_INTERNAL, "Expression evaluated with uninitialized instance" );
		return EC_ERROR;
	}

	if ( !s )
		return EC_OK;

	if ( s->debug == 1 )
		slDebug( "debug called from within steve evaluation\n" );

	if ( i->instance->status == AS_ACTIVE ) {
		if ( s->block )
			return s->block->calls.stRtcEval3( s, i, result );
	} else {
		// we don't allow execution with freed instances, unless it's
		// a return value, in which case it's okay.

		if ( s->type == ET_RETURN ) {
			resultCode = stExpEval3( s, i, result );
			resultCode = EC_STOP;
		}

		stEvalError( i->instance, EE_FREED_INSTANCE, "Expression evaluated using freed instance" );

		return EC_ERROR;
	}

	switch ( s->type ) {
		case ET_SUPER:
			if ( i->type->super ) {

				result->set( i->instance->breveInstance );

				if ( tClass ) 
					*tClass = i->type->super;
			} else {
				result->set( ( brInstance* )NULL );
			}

			break;

		case ET_SELF:
			result->set( i->instance->breveInstance );
			break;

		case ET_ST_EVAL:
			resultCode = EVAL_RTC_CALL_3( s, stEvalEvalExp, ( stEvalExp*)s, i, result );
			break;

		case ET_LOAD: {
				stLoadExp *e = ( stLoadExp * )s;

				switch ( e->loadType ) {

					case AT_INT:
						resultCode = EVAL_RTC_CALL_3( s, stEvalLoadInt, e, i, result );

						break;

					case AT_DOUBLE:
						resultCode = EVAL_RTC_CALL_3( s, stEvalLoadDouble, e, i, result );

						break;

					case AT_POINTER:
					case AT_INSTANCE:
					case AT_DATA:
						resultCode = EVAL_RTC_CALL_3( s, stEvalLoadIndirect, e, i, result );

						break;

					case AT_VECTOR:
						resultCode = EVAL_RTC_CALL_3( s, stEvalLoadVector, e, i, result );

						break;

					case AT_LIST:
						resultCode = EVAL_RTC_CALL_3( s, stEvalLoadList, e, i, result );

						break;

					case AT_HASH:
						resultCode = EVAL_RTC_CALL_3( s, stEvalLoadHash, e, i, result );

						break;

					case AT_STRING:
						resultCode = EVAL_RTC_CALL_3( s, stEvalLoadString, e, i, result );

						break;

					case AT_MATRIX:
						resultCode = EVAL_RTC_CALL_3( s, stEvalLoadMatrix, e, i, result );

						break;

					case AT_ARRAY:
						stEvalError( i->instance, EE_ARRAY, "Array variables cannot be loaded like normal expressions" );

						return EC_ERROR;

						break;

					default:
						slMessage( DEBUG_ALL, "Unknown atomic type in load: %d\n", e->type );

						return EC_ERROR;

						break;
				}
			}

			break;

		case ET_ASSIGN:
			resultCode = EVAL_RTC_CALL_3( s, stEvalAssignment, ( stAssignExp * )s, i, result );

			break;

		case ET_BINARY:
			resultCode = EVAL_RTC_CALL_3( s, stEvalBinaryExp, ( stBinaryExp * )s, i, result );

			break;

		case ET_METHOD:
			resultCode = EVAL_RTC_CALL_3( s, stEvalMethodCall, ( stMethodExp * )s, i, result );

			break;

		case ET_FUNC:
			resultCode = EVAL_RTC_CALL_3( s, stEvalCallFunc, ( stCCallExp * )s, i, result );

			break;

		case ET_RETURN:
			if ( !(( stReturnExp * )s )->expression ) {
				resultCode = EC_STOP;
			} else {
				resultCode = stExpEval3((( stReturnExp * )s )->expression, i, result );

				if ( resultCode == EC_OK )
					resultCode = EC_STOP;
			}

			break;

		case ET_ARRAY_INDEX:
			resultCode = EVAL_RTC_CALL_3( s, stEvalArrayIndex, ( stArrayIndexExp * )s, i, result );

			break;

		case ET_ARRAY_INDEX_ASSIGN:
			resultCode = EVAL_RTC_CALL_3( s, stEvalArrayIndexAssign, ( stArrayIndexAssignExp * )s, i, result );

			break;

		case ET_IF:
			resultCode = EVAL_RTC_CALL_3( s, stEvalIf, ( stIfExp * )s, i, result );

			break;

		case ET_VECTOR:
			resultCode = EVAL_RTC_CALL_3( s, stEvalVectorExp, ( stVectorExp * )s, i, result );

			break;

		case ET_MATRIX:
			resultCode = EVAL_RTC_CALL_3( s, stEvalMatrixExp, ( stMatrixExp * )s, i, result );

			break;

		case ET_UNARY:
			resultCode = EVAL_RTC_CALL_3( s, stEvalUnaryExp, ( stUnaryExp * )s, i, result );

			break;

		case ET_LIST:
			resultCode = brEvalListExp(( stListExp * )s, i, result );

			break;

		case ET_CODE_ARRAY:
			resultCode = EVAL_RTC_CALL_3( s, stEvalCodeArray, ( stCodeArrayExp * )s, i, result );

			break;

		case ET_LENGTH:
			resultCode = stExpEval3((( stLengthExp * )s )->expression, i, result );

			switch ( result->type() ) {

				case AT_VECTOR:
					result->set( slVectorLength( &BRVECTOR( result ) ) );

					resultCode = EC_OK;

					break;

				case AT_LIST:
					result->set(( int ) BRLIST( result )->_vector.size() );

					resultCode = EC_OK;

					break;

				case AT_INT:
					result->set( abs( BRINT( result ) ) );

					resultCode = EC_OK;

					break;

				case AT_DOUBLE:
					result->set( fabs( BRDOUBLE( result ) ) );

					resultCode = EC_OK;

					break;

				case AT_STRING:
					if ( !BRSTRING( result ) ) result->set( 0 );
					else result->set(( int )strlen( BRSTRING( result ) ) );

					break;

				default:
					stEvalError( i->instance, EE_TYPE, "Cannot give magnitude of %s expression", brAtomicTypeStrings[( int )result->type()] );

					return EC_ERROR;

					break;
			}

			break;

		case ET_WHILE:
			resultCode = EVAL_RTC_CALL_3( s, stEvalWhile, ( stWhileExp * )s, i, result );

			break;

		case ET_FOREACH:
			resultCode = EVAL_RTC_CALL_3( s, stEvalForeach, ( stForeachExp * )s, i, result );

			break;

		case ET_FOR:
			resultCode = EVAL_RTC_CALL_3( s, stEvalFor, ( stForExp * )s, i, result );

			break;

		case ET_INSERT:
			resultCode = EVAL_RTC_CALL_3( s, stEvalListInsert, ( stListInsertExp * )s, i, result );

			break;

		case ET_REMOVE:
			resultCode = EVAL_RTC_CALL_3( s, stEvalListRemove, ( stListRemoveExp * )s, i, result );

			break;

		case ET_COPYLIST:
			resultCode = EVAL_RTC_CALL_3( s, stEvalCopyList, ( stCopyListExp * )s, i, result );

			break;

		case ET_ALL:
			resultCode = EVAL_RTC_CALL_3( s, stEvalAll, ( stAllExp * )s, i, result );

			break;

		case ET_SORT:
			resultCode = EVAL_RTC_CALL_3( s, stEvalSort, ( stSortExp * )s, i, result );

			break;

		case ET_LIST_INDEX:
			resultCode = EVAL_RTC_CALL_3( s, stEvalIndexLookup, ( stListIndexExp * )s, i, result );

			break;

		case ET_LIST_INDEX_ASSIGN:
			resultCode = EVAL_RTC_CALL_3( s, stEvalIndexAssign, ( stListIndexAssignExp * )s, i, result );

			break;

		case ET_PRINT:
			resultCode = stEvalPrint(( stPrintExp * )s, i, &t );

			break;

		case ET_STRING:
			resultCode = stProcessString(( stStringExp * )s, i, result );

			break;

		case ET_INSTANCE:
			resultCode = EVAL_RTC_CALL_3( s, stEvalNewInstance, ( stInstanceExp * )s, i, result );

			break;

		case ET_VECTOR_ELEMENT:
			resultCode = EVAL_RTC_CALL_3( s, stEvalVectorElementExp, ( stVectorElementExp * )s, i, result );

			break;

		case ET_VECTOR_ELEMENT_ASSIGN:
			resultCode = EVAL_RTC_CALL_3( s, stEvalVectorElementAssignExp, ( stVectorElementAssignExp * )s, i, result );

			break;

		case ET_RANDOM:
			resultCode = EVAL_RTC_CALL_3( s, stEvalRandExp, ( stRandomExp * )s, i, result );

			break;

		case ET_DUPLICATE:
			// resultCode = EVAL_RTC_CALL_3(s, stExpEval3, ((stDuplicateExp *)s)->expression, i, result);
			resultCode = stExpEval3((( stDuplicateExp * )s )->expression, i, result );

			break;

		case ET_FREE:
			resultCode = EVAL_RTC_CALL_3( s, stEvalFree, ( stFreeExp * )s, i, result );

			break;

		case ET_COMMENT:
			return EC_OK;
			break;

		case ET_DIE:
			resultCode = stExpEval3((( stDieExp * )s )->expression, i, &t );

			stEvalError( i->instance, EE_USER, "execution stopped from within simulation: %s", BRSTRING( &t ) );

			return EC_ERROR;

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown expression type (%d) in stExpEval", s->type );

			resultCode = EC_ERROR;

			break;
	}

	return resultCode;
}

int stEvalEvalExp( stEvalExp *inEval, stRunInstance *inInstance, brEval *outResult ) {
	*outResult = inEval -> _eval;
	return EC_OK;
}

/*!
	\brief Gives a pointer to the eval-value of a list at a specified index.

	The index is 0-based.
*/


int stDoEvalListIndexPointer( brEvalListHead *l, int n, brEval **eval ) {
	if ( n > ( int )( l->_vector.size() - 1 ) || n < 0 ) {
		*eval = NULL;
		return -1;
	}

	*eval = brEvalListIndexLookup( l, n );

	return 0;
}

/*!
	\brief Assigns a value to a specified index of an eval list.

    The index is 0-based.  If the index is out of bounds, the brEval
	pointer is set to type AT_NULL and -1 is returned.  Upon success,
	0 is returned.
*/

int stDoEvalListIndex( brEvalListHead *l, int n, brEval *newLoc ) {
	brEval *eval;

	stDoEvalListIndexPointer( l, n, &eval );

	if ( !eval ) return -1;

	brEvalCopy( eval, newLoc );

	return 0;
}

int stEvalListIndexAssign( brEvalListHead *l, int n, brEval *newVal, stRunInstance *ri ) {
	brEval *eval;

	if ( n > ( int )l->_vector.size() || n < 0 )
		return -1;

	// if this is a new entry at the end, append it instead

	if ( n == ( int )l->_vector.size() || l->_vector.size() == 0 ) {
		if ( brEvalListInsert( l, l->_vector.size(), newVal ) > -1 )
			return 0;
		else
			return -1;
	}

	eval = brEvalListIndexLookup( l, n );

	brEvalCopy( newVal, eval );

	return 0;
}

RTC_INLINE int stEvalBinaryEvalListExp( char op, brEval *l, brEval *r, brEval *result, stRunInstance *i ) {
	brEvalListHead *h1, *h2;
	std::vector< brEval >::iterator l1, l2;
	int same;
	int ret;

	h1 = BRLIST( l );
	h2 = BRLIST( r );

	if ( h1->_vector.size() != h2->_vector.size() )
		same = 0;
	else {
		same = 1;

		l1 = h1->_vector.begin();
		l2 = h2->_vector.begin();

		while ( l1 != h1->_vector.end() && l2 != h2->_vector.end() && same ) {
			ret = stEvalBinaryExpWithEvals( i, BT_EQ, &(*l1), &(*l2), result );

			if ( ret != EC_OK || !BRINT( result ) )
				same = 0;

			l1++;

			l2++;
		}
	}

	switch ( op ) {

		case BT_EQ:
			result->set( same );

			break;

		case BT_NE:
			result->set( !same );

			break;

		default:
			stEvalError( i->instance, EE_INTERNAL, "unknown binary expression operator (%d) in stEvalBinaryDoubleExp", op );

			return EC_ERROR;
	}

	return EC_OK;
}

/*!
	\brief Triggers a run-time simulation error.

	A copy of \ref brEvalError that includes a steve stack trace.

	Takes an engine, a type (one of the \ref parseErrorMessageCodes), and
	a set of printf-style arguments (format string and data).

	Exactly how the error is handled depends on the simulation frontend,
	but this will typically cause a simulation to die.
*/

void stEvalError( stInstance *inInstance, int type, const char *proto, ... ) {
	va_list vp;
	brEngine *e = inInstance->type->engine;

	brErrorInfo *error = brEngineGetErrorInfo( e );
	char *m, localMessage[BR_ERROR_TEXT_SIZE];

	if ( !error->type ) {
		m = error->message;

		// if this is the first stEvalError, this is the primary error
		// print out all of the information

		error->type = type;

	} else
		m = localMessage;

	va_start( vp, proto );

	vsnprintf( m, BR_ERROR_TEXT_SIZE, proto, vp );

	va_end( vp );

	slMessage( DEBUG_ALL, m );

	slMessage( DEBUG_ALL, "\n" );

	slMessage( DEBUG_ALL, "Begin stack trace:\n" );
	stStackTrace( inInstance->type->steveData );
	slMessage( DEBUG_ALL, "End stack trace:\n" );
}

/*!
	\brief Prints an evaluation warning.
*/

void stEvalWarn( stExp *exp, const char *proto, ... ) {
	va_list vp;
	char localMessage[BR_ERROR_TEXT_SIZE];

	va_start( vp, proto );
	vsnprintf( localMessage, BR_ERROR_TEXT_SIZE, proto, vp );
	va_end( vp );
	slMessage( DEBUG_ALL, localMessage );
	slMessage( DEBUG_ALL, "... in file \"%s\" at line %d\n", exp->file, exp->line );
}
