/* --------------------------------------------------------------------- 
 * Definition of class Memcheck                        memcheck_object.h
 * Memcheck-specific options / flags / fns
 * ---------------------------------------------------------------------
 * This file is part of Valkyrie, a front-end for Valgrind
 * Copyright (c) 2000-2005, OpenWorks LLP <info@open-works.co.uk>
 * This program is released under the terms of the GNU GPL v.2
 * See the file LICENSE.GPL for the full license details.
 */

#ifndef __MEMCHECK_OBJECT_H
#define __MEMCHECK_OBJECT_H


#include "tool_object.h"
#include "memcheck_view.h"
#include "memcheck_options_page.h"
#include "vk_logpoller.h"

#include "vglogreader.h"
#include "vk_process.h"


/* class Memcheck ------------------------------------------------------ */
class Memcheck : public ToolObject
{
   Q_OBJECT

public:
   Memcheck( int objId );
   ~Memcheck();

   /* returns the ToolView window (memcheckView) for this tool */
   ToolView* createView( QWidget* parent );
   /* called by MainWin::closeToolView() */
   bool isDone();

   bool start( VkRunState::State rm, QStringList vgflags );
   bool stop();

   int checkOptArg( int optid, const char* argval, bool use_gui=false );

   enum mcOpts { 
      LEAK_CHECK,
      LEAK_RES,
      SHOW_REACH,
      PARTIAL,
      FREELIST,
      GCC_296,
      ALIGNMENT,
      LAST_CMD_OPT  = ALIGNMENT
   };

   OptionsPage* createOptionsPage( OptionsWindow* parent ) {
      return (OptionsPage*)new MemcheckOptionsPage( parent, this );
   }

   /* returns a list of non-default flags to pass to valgrind */
   QStringList modifiedVgFlags();

public slots:
   bool fileSaveDialog( QString fname=QString() );

private:
   /* overriding to avoid casting everywhere */
   MemcheckView* view() { return (MemcheckView*)m_view; }

   void statusMsg(  QString hdr, QString msg );
   bool queryFileSave();
   bool saveParsedOutput( QString& fname );

   bool runValgrind( QStringList vgflags );  // RM_Valgrind
   bool parseLogFile();                      // RM_Tool0
   bool mergeLogFiles();                     // RM_Tool1
   bool runProcess( QStringList flags );

   /* TODO: put in VKProcess */
   void vgProcTerminate();

private slots:
   void processDone();
   void readVgLog();

private:
   QString      m_saveFname;
   VgLogReader* m_vgreader;
   VKProcess*   m_vgproc;
   VkLogPoller* m_logpoller;
};


#endif