#if defined(__GNUG__) && !defined(__APPLE__)
#pragma implementation "BreveInterface.h"
#endif

#include "wx/wxprec.h"  

#ifdef __BORLANDC__
#pragma hdrstop
#endif

#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include "BreveInterface.h"
#include "BreveRender.h"
#include "BreveCanvas.h"
#include "Main.h"
#include "steve.h"
#include "camera.h"
#include "gldraw.h"
#include "SimInstance.h"
#include "BDialog.h"

BreveInterface::BreveInterface(char * simfile, wxString simdir, char * text) {
	this->simulationfile = simfile;
	this->text = text;

	next = NULL;

	_simdir = simdir;
	_engine = NULL;
	_simmenu = NULL;

	slSetMessageCallbackFunction( ::messageCallback );
}

void BreveInterface::GenerateEngine() {
	char buf[ 2048 ];
	wxString str;
	int i = 0;

	_engine = brEngineNew();

	// Setup file paths & message callback before initing the frontoff language

#ifdef WINDOWS
	strncpy( buf, app->GetBreveDir() + "lib" + FILE_SEP_PATH + "python2.3" + FILE_SEP_PATH, 2047 );
	brAddSearchPath( _engine, buf );
#endif

	strncpy( buf, app->GetBreveDir(), 2047 );
	brAddSearchPath( _engine, buf );

	strncpy( buf, app->GetBreveDir() + "plugins" + FILE_SEP_PATH, 2047 );
	brAddSearchPath( _engine, buf );

	strncpy( buf, app->GetLocalDir(), 2047 );
	brAddSearchPath( _engine, buf );


	_steveData = (stSteveData*)brInitFrontendLanguages( _engine );

	_simmenu = new wxMenu;
	paused = 1;
	valid = 1;
	x = 400;
	y = 300;

	_engine->getLoadname = ::getLoadname;
	_engine->getSavename = ::getSavename;
	_engine->soundCallback = soundCallback;
	_engine->pauseCallback = pauseCallback;
	_engine->dialogCallback = ::dialogCallback;
	_engine->interfaceTypeCallback = interfaceVersionCallback;
	_engine->newWindowCallback = newWindowCallback;
	_engine->freeWindowCallback = freeWindowCallback;
	_engine->renderWindowCallback = renderWindowCallback;
	brEngineSetUpdateMenuCallback(_engine, ::menuCallback);

	for (i = 0; i < app->GetSearchPathArray()->Count(); i++) {
		strncpy(buf, app->GetSearchPathArray()->Item(i), 2047);
		buf[2047] = '\0';
		brAddSearchPath(_engine, (char*)&buf);
	}

	if ( !_simdir.IsEmpty() ) {
		brAddSearchPath(_engine, _simdir.c_str() );
	}

	UpdateLog();
}

BreveInterface::~BreveInterface()
{
	if ( _engine != NULL ) {
		brEngineFree(_engine);
		_engine = NULL;
	}

	delete _simmenu;

	free(simulationfile);

	if (text != NULL)
		free(text);
}

void BreveInterface::Pause( int pause ) {

	if( !_engine )
		return;

	if ( pause == paused )
		return;

	paused = !paused;

	if ( paused )
		brPauseTimer( _engine );
	else
		brUnpauseTimer( _engine );

	UpdateLog();
}

bool BreveInterface::Initialize() {
	GenerateEngine();

	_engine -> camera -> initGL();

	if ( brLoadSimulation( _engine, text, simulationfile ) != EC_OK ) {
		reportError();
		slFree( text );
		text = NULL;
		Abort();
	
		return 0;
	}

	slFree( text );
	text = NULL;

	ResizeView( x, y );

	return 1;
}

void BreveInterface::ResizeView( int inX, int inY ) {
	x = inX;
	y = inY;
	if( _engine )
		_engine->camera->setBounds( x, y );
}

void BreveInterface::Iterate() {
	if ( _engine == NULL )
		return;

	if ( brEngineIterate( _engine ) != EC_OK) {
		reportError();
		Abort();
	}

	UpdateLog();

	usleep( gBreverender->GetSleepMS() * 1000 );
}

void BreveInterface::UpdateLog() {
	if( mQueuedMessage.length() > 0 ) {
		gBreverender->AppendLog( mQueuedMessage.c_str() );
		mQueuedMessage = "";
	}
}

void BreveInterface::Render() {
	if ( _engine == NULL )
		return;

	brEngineLock(_engine);
	brEngineRenderWorld( _engine, gBreverender->MouseDown() );
	brEngineUnlock(_engine);
}

void BreveInterface::Abort() {
	if ( valid == 0 ) 
		return;

	gBreverender->ResetSim();
	UpdateLog();

	valid = 0;
}

char * getLoadname() {
	return gBreverender->GetSimulation()->GetInterface()->getLoadname();
}

char * getSavename() {
	return gBreverender->GetSimulation()->GetInterface()->getSavename();
}

char * BreveInterface::getLoadname() {
	wxTextEntryDialog d(gBreverender, "Loadname required.", "Please enter filename to load:");
	char * buf;

	buf = (char*)slMalloc(1024);

	if (d.ShowModal() == wxID_OK)
	{
	strncpy(buf, d.GetValue(), 1023);

	if (d.GetValue().Length() > 1023)
		buf[1023] = '\0';
	}
	else
	buf[0] = '\0';

	return buf;
}

char * BreveInterface::getSavename()
{
	wxTextEntryDialog d(gBreverender, "Savename required.", "Please enter filename to save:");
	char * buf;

	buf = (char*)slMalloc(1024);

	if (d.ShowModal() == wxID_OK)
	{
	strncpy(buf, d.GetValue(), 1023);

	if (d.GetValue().Length() > 1023)
		buf[1023] = '\0';
	}
	else
	buf[0] = '\0';

	return buf;
}

void BreveInterface::reportError() {
	if ( !_engine )
		return;

	brErrorInfo *error = brEngineGetErrorInfo( _engine );

	char errorMessage[ 10240 ];

	snprintf( errorMessage, 10239, "Error at line %d of \"%s\":\n\n%s\n\nSee log window for details", error->line, error->file, error->message );

	gBreverender->queMsg( errorMessage );
}

void BreveInterface::messageCallback( const char *text ) {
	mQueuedMessage += text;
}

void messageCallback( const char *text ) {
	if ( gBreverender->GetSimulation() == NULL )
		return;

	gBreverender->GetSimulation()->GetInterface()->messageCallback( text );
}

int soundCallback() {
	wxBell();
	return 0;
}

int pauseCallback() {
	if ( gBreverender->GetSimulation() == NULL )
	return 0;

	if ( gBreverender->GetSimulation()->GetInterface()->Paused() )
	return 0;

	gBreverender->OnRenderRunClick(*((wxCommandEvent*)NULL));

	return 0;
}

void *newWindowCallback(char *name, void *graph)
{
	printf("newWindowCallback stub\n\r");
}

void freeWindowCallback(void *w)
{
	printf("freeWindowCallback stub\n\r");
}

void renderWindowCallback(void *w)
{
	printf("renderWindowCallback\n\r");
}

void graphDisplay()
{
	printf("graphDisplay stub\n\r");
}

void menuCallback(brInstance *i) {
	gBreverender->GetSimulation()->GetInterface()->menuCallback(i);
}

void BreveInterface::menuCallback(brInstance *binterface) {
	brMenuEntry * e;
	int i = 0;

	if(!binterface->engine || binterface != brEngineGetController(binterface->engine))
		return;

	gBreverender->SetMenu( 0 );

	if( _simmenu ) 
		delete _simmenu;

	_simmenu = new wxMenu;

	for (i = 0; i < binterface->_menus.size(); i++) {
		e = (brMenuEntry*)binterface->_menus[ i ];

		if (e->title[0] == '\0') {
			_simmenu->AppendSeparator();
			continue;
		}

		_simmenu->Append(BREVE_SIMMENU + i, e->title, "", wxITEM_CHECK);

		if ( !e->enabled )
			_simmenu->Enable(BREVE_SIMMENU + i, FALSE);

		if ( e->checked )
			_simmenu->Check(BREVE_SIMMENU + i, TRUE);
	}

	gBreverender->SetMenu( 1 );
}

int BreveInterface::dialogCallback(char *title, char *message, char *b1, char *b2)
{
	BDialog d(gBreverender, title, message, b1, b2);

	return !d.ShowModal();
}

int dialogCallback(char *title, char *message, char *b1, char *b2)
{
	return gBreverender->GetSimulation()->GetInterface()->dialogCallback(title, message, b1, b2);
}

char *interfaceVersionCallback()
{
	return "wxwidgets/2.0";
}

void BreveInterface::RunMenu(int id, brInstance *n) {
	int i;

	if (brMenuCallback(_engine, n, id) != EC_OK) {
		reportError();
		Abort();
	}
}

void BreveInterface::ExecuteCommand(wxString str) {
	wxString nstr;

	nstr << "> " << str << "\n";

	gBreverender->AppendLog(nstr);

	gBreverender->queCmd(str);
}

void BreveInterface::RunCommand(char * str) {
	stRunSingleStatement( _steveData, _engine, str);
}
