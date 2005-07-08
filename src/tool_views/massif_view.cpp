/* ---------------------------------------------------------------------
 * Implementation of MassifView                          massif_view.cpp
 * Massif's personal window
 * ---------------------------------------------------------------------
 * This file is part of Valkyrie, a front-end for Valgrind
 * Copyright (c) 2000-2005, Donna Robinson <donna@valgrind.org>
 * This program is released under the terms of the GNU GPL v.2
 * See the file LICENSE.GPL for the full license details.
 */

#include "massif_view.h"
#include "massif_object.h"

#include <qcursor.h>
#include <qlayout.h>
#include <qlabel.h>


/* class MassifView ---------------------------------------------------- */
MassifView::~MassifView() 
{ }


MassifView::MassifView( QMainWindow* mwin, QWidget* parent, Massif* ms )
  : ToolView( parent, ms->name(), ms->id() )
{
  massif = ms;

  mwin = mwin;// stop compiler complaining 

  QVBoxLayout* vLayout = new QVBoxLayout( this );

  /* create the listview */
  QLabel* lbl = new QLabel( "Massif", this, "massif label" );
  vLayout->addWidget( lbl );
}


/* called by massif: set state for buttons; set cursor state */
void MassifView::setState( bool run )
{ 
  if ( run ) {       /* startup */
    setCursor( QCursor(Qt::WaitCursor) );
  } else {           /* finished */
    unsetCursor();
  }
}

/* slot: connected to MainWindow::toggleToolbarLabels(). 
   called when user toggles 'show-butt-text' in Options page */
void MassifView::toggleToolbarLabels( bool /*state*/ )
{

}

