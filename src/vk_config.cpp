/* --------------------------------------------------------------------- 
 * Implementation of VkConfig                              vk_config.cpp
 * Configuration file parser
 * ---------------------------------------------------------------------
 * This file is part of Valkyrie, a front-end for Valgrind
 * Copyright (c) 2000-2005, OpenWorks LLP <info@open-works.co.uk>
 * This program is released under the terms of the GNU GPL v.2
 * See the file LICENSE.GPL for the full license details.
 */

#include "vk_config.h"
#include "vk_include.h"
#include "vk_utils.h"       /* VK_DEBUG */
#include "vk_messages.h"

#include "valkyrie_object.h"
#include "valgrind_object.h"
#include "memcheck_object.h"
#include "cachegrind_object.h"
#include "massif_object.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>         /* close, access */

#include <qdatetime.h>
#include <qdir.h>
#include <qfiledialog.h>


VkConfig::~VkConfig()
{ 
  sync();
  vkObjectList.setAutoDelete( true );
  vkObjectList.clear();
}


/* set mDirty to false in order to stop config from writing any
   entries-set-so-far to disk.  called by main() after
   parseOptions() returns false. --------------------------------------- */
void VkConfig::dontSync() 
{ mDirty = false; }


/* write-sync is only necessary if there are dirty entries ------------- */
void VkConfig::sync()
{
  if ( mDirty ) {
    writebackConfig();
    mDirty = false;
  }
}


/* class VkConfig ------------------------------------------------------ */
VkConfig::VkConfig( bool *ok ) : QObject( 0, "vkConfig" )
{
  mEntryMap.clear();
  sep       = ',';         /* separator for lists of strings */
  mDirty    = false;
  newConfigFile = false;   /* set to true in mkConfigFile() */

  mPackagePath = PREFIX;
  vkdocPath    = mPackagePath + VK_DOC_PATH;
  vgdocPath    = mPackagePath + VG_DOC_PATH;
  imgPath      = mPackagePath + VK_ICONS_PATH;

  vk_name      = Vk_Name;
  vk_Name      = VK_NAME;
  vk_version   = VK_VERSION;
  vk_copyright = VK_COPYRIGHT;
  vk_author    = VK_AUTHOR;
  vk_email     = VK_EMAIL;
  vg_copyright = VG_COPYRIGHT;

  rcPath.sprintf( "%s/.%s-%s", 
                  QDir::homeDirPath().latin1(), vkname(), vkVersion() );
  rcFileName.sprintf( "%s/%src", rcPath.latin1(), vkname() );
  dbasePath = rcPath + VK_DBASE_DIR;
  logsPath  = rcPath + VK_LOGS_DIR;
  suppPath  = rcPath + VK_SUPPS_DIR;

  if ( !checkDirs() ) {     /* we always do this on startup */
    *ok = false;
    return;
  }
  rcPath    += "/";

  /* The various valgrind, valkyrie, and tool objects ------------------
     Initialise these first, so that if ~/.PACKAGE/PACKAGErc does not
     exist, vkConfig can write the various options to the config file. */
  initVkObjects();

  int num_tries = 0;
 retry:
  if ( num_tries > 1 )
    return;

  RetVal rval = checkAccess();
  switch ( rval ) {

  case Okay:
    *ok = true;
    if ( parseFile() == Fail ) {
      *ok = false;
    } break;

  case BadRcVersion:
    vkInfo( 0, "Configuration",
            "<p>The configuration file '%s' version is invalid.</p> "
            "<p>Removing and re-creating it now ... ... </p>", 
            rcFileName.latin1() );
    mkConfigFile( true );
    num_tries++;
    goto retry;      /* try again */
    break;

  case CreateRcFile:
    vkInfo( 0, "Configuration",
            "<p>The configuration file '%s' does not exist, "
            "and %s cannot run without this file.<br>"
            "Creating it now ... ... </p>", 
            rcFileName.latin1(), vkName() );
    mkConfigFile();
    num_tries++;
    goto retry;      /* try again */
    break;

  case NoPerms:
    vkFatal( 0, "Configuration",
             "<p>You do not have read/write permissions set"
             "on the directory %s</p>", rcPath.latin1() );
    break;

  case BadFilename:
  case NoDir:
  case BadRcFile:
  case Fail:
    vkFatal( 0, "Config Creation Failed",
             "<p>Initialisation of Config failed.</p>" );
    break;
  }

  /* If we've just created valkyrierc, write the vg-exec-path and
     vg-supp-dir paths found by 'configure' to valkyrierc. 
     These values can be over-ridden by the user via the Options dialog.*/
  if ( newConfigFile ) {
    updatePaths();
  }

}


/* misc. make-life-easier stuff ---------------------------------------- */

/* these fns return vars initialised from the #defines set in vk_include.h */
const char* VkConfig::vkname()      { return vk_name.data();      }
const char* VkConfig::vkName()      { return vk_Name.data();      }
const char* VkConfig::vkVersion()   { return vk_version.data();   }
const char* VkConfig::vkCopyright() { return vk_copyright.data(); }
const char* VkConfig::vkAuthor()    { return vk_author.data();    }
const char* VkConfig::vkEmail()     { return vk_email.data();     }
const char* VkConfig::vgCopyright() { return vg_copyright.data(); }

/* these fns return values held in private vars */
QString VkConfig::vkdocDir()  { return vkdocPath; }

QString VkConfig::vgdocDir()  { return vgdocPath; }
/* ~/.valkyrie-X.X.X/ */
QString VkConfig::rcDir()     { return rcPath;    }
/* ~/.valkyrie-X.X.X/dbase/ */
QString VkConfig::dbaseDir()  { return dbasePath; }
/* ~/.valkyrie-X.X.X/logs/ */
QString VkConfig::logsDir()   { return logsPath;  }
/* ~/.valkyrie-X.X.X/suppressions/ */
QString VkConfig::suppDir()   { return suppPath;  }


/* read functions ------------------------------------------------------ */
QString VkConfig::rdEntry( const QString &pKey, 
                           const QString &pGroup )
{
  QString aValue = QString::null;
  EntryKey entryKey( pGroup, pKey );

  EntryMapIterator aIt;
  aIt = mEntryMap.find( entryKey );
  if ( aIt != mEntryMap.end() ) {
    aValue = aIt.data().mValue;
  } else {
    /* end() points to the element which is one past the
       last element in the map; ergo, the key wasn't found. */
    VK_DEBUG( "VkConfig::rdEntry(): key not found.\n"
             "\tFile  = %s\n"
             "\tGroup = %s\n"
             "\tKey   = %s\n", 
             rcFileName.latin1(), pGroup.latin1(), pKey.latin1() );
  }

  return aValue;
}


int VkConfig::rdInt( const QString &pKey, const QString &pGroup )
{
  QString aValue = rdEntry( pKey, pGroup );
  if ( aValue.isNull() )
    return -1;

  bool ok;
  int aInt = aValue.toInt( &ok );
  return( ok ? aInt : -1 );
}


bool VkConfig::rdBool( const QString &pKey, const QString &pGroup )
{
  QString aValue = rdEntry( pKey, pGroup );

  if ( aValue == "true" || aValue == "on"   || 
       aValue == "yes"  || aValue == "1"    ||
       aValue == "T" )
    return true;

  return false;
}


/* guaranteed to return a valid font.  if an invalid font is
   found, the application's default font is used. */
QFont VkConfig::rdFont( const QString &pKey, 
                        const QString &pGroup/*=QString::null*/ )
{
  QFont aRetFont;

  QString pgroup = pGroup.isNull() ? "Fonts" : pGroup;
  QString aValue = rdEntry( pKey, pgroup/*"Fonts"*/ );
  if ( aValue.isNull() )
    return QFont();

  if ( aValue.contains( ',' ) > 5 ) {    // new format
    if ( !aRetFont.fromString( aValue ) )
      return QFont();
  }
  else { /* backward compatibility with older formats */
    /* find first part (font family) */
    int nIndex = aValue.find( ',' );
    if ( nIndex == -1 ) {
      return QFont();
    }
    aRetFont.setFamily( aValue.left( nIndex ) );
    
    /* find second part (point size) */
    int nOldIndex = nIndex;
    nIndex = aValue.find( ',', nOldIndex+1 );
    if ( nIndex == -1 ) {
      return QFont();
    }
    aRetFont.setPointSize( aValue.mid( nOldIndex+1,
                           nIndex-nOldIndex-1 ).toInt() );

    /* find third part (style hint) */
    nOldIndex = nIndex;
    nIndex = aValue.find( ',', nOldIndex+1 );

    if ( nIndex == -1 )
      return QFont();
    aRetFont.setStyleHint( (QFont::StyleHint)aValue.mid( nOldIndex+1, 
                                     nIndex-nOldIndex-1 ).toUInt() );
    
    /* find fourth part (char set) */
    nOldIndex = nIndex;
    nIndex = aValue.find( ',', nOldIndex+1 );

    if ( nIndex == -1 )
      return QFont();
    QString chStr;     /* never used ... */
    chStr = aValue.mid( nOldIndex+1,
                        nIndex-nOldIndex-1 );

    /* find fifth part (weight) */
    nOldIndex = nIndex;
    nIndex = aValue.find( ',', nOldIndex+1 );
    if ( nIndex == -1 )
      return QFont();
    aRetFont.setWeight( aValue.mid( nOldIndex+1,
                                    nIndex-nOldIndex-1 ).toUInt() );

    /* find sixth part (font bits) */
    uint nFontBits = aValue.right( aValue.length()-nIndex-1 ).toUInt();
    aRetFont.setItalic( nFontBits & 0x01 );
    aRetFont.setUnderline( nFontBits & 0x02 );
    aRetFont.setStrikeOut( nFontBits & 0x04 );
    aRetFont.setFixedPitch( nFontBits & 0x08 );
    aRetFont.setRawMode( nFontBits & 0x20 );
  }

  return aRetFont;
}


QColor VkConfig::rdColor( const QString &pKey )
{
  bool ok;
  QColor aRetColor;

  QString aValue = rdEntry( pKey, "Colors" );
  if ( !aValue.isEmpty() ) {
    int nRed = 0, nGreen = 0, nBlue = 0;

    /* find first part (red) */
    int nIndex = aValue.find( ',' );
    if ( nIndex == -1 )
      return QColor();
    nRed = aValue.left( nIndex ).toInt( &ok );

    /* find second part (green) */
    int nOldIndex = nIndex;
    nIndex = aValue.find( ',', nOldIndex+1 );
    if ( nIndex == -1 )
      return QColor();
    nGreen = aValue.mid( nOldIndex+1, nIndex-nOldIndex-1 ).toInt( &ok );

    /* find third part (blue) */
    nBlue = aValue.right( aValue.length()-nIndex-1 ).toInt( &ok );

    aRetColor.setRgb( nRed, nGreen, nBlue );
  }

  return aRetColor;
}



/* write functions ----------------------------------------------------- 
   the vkConfig object is dirty now */
void VkConfig::wrEntry( const QString &pValue,
                        const QString &pKey, const QString &pGroup )
{
  /* debug: check we aren't unwittingly inserting a new value. */
  EntryKey entryKey( pGroup, pKey );
  if ( !mEntryMap.contains( entryKey ) ) {
    VK_DEBUG( "Config::wrEntry(): key not found.\n"
             "\tFile  = %s\n"
             "\tGroup = %s\n"
             "\tKey   = %s\n"
             "\tValue = %s\n", 
             rcFileName.latin1(), pGroup.latin1(),
             pKey.latin1(), pValue.latin1() );
  }

  mDirty = true;
  /* set new value */
  EntryData entryData( pValue, true );
  /* rewrite the new value */
  insertData( entryKey, entryData );
}


/* special version of wrEntry: adds values to the existing entry,
   rather than replacing it */
void VkConfig::addEntry( const QString &pValue, 
                         const QString &pKey, const QString &pGroup )
{
  /* get hold of the current value(s) */
  QString curr_values = rdEntry( pKey, pGroup );

  /* concat curr_values with new value */
  if ( !curr_values.isEmpty() )
    curr_values += sep;
  curr_values += pValue;

  wrEntry( curr_values, pKey, pGroup );
}


void VkConfig::wrInt( const int pValue, const QString &pKey, 
                      const QString &pGroup )
{ wrEntry( QString::number( pValue ), pKey, pGroup ); }


void VkConfig::wrBool( const bool &pValue, const QString &pKey, 
                     const QString &pGroup )
{
  QString aValue = ( pValue == true ) ? "true" : "false";
  wrEntry( aValue, pKey, pGroup );
}


void VkConfig::wrFont( const QFont &pFont, const QString &pKey )
{ wrEntry( pFont.toString(), pKey, "Fonts" ); }


void VkConfig::wrColor( const QColor &pColor, const QString &pKey )
{
  QString aValue = "";
  if ( pColor.isValid() )
    aValue.sprintf( "%d,%d,%d", 
                    pColor.red(), pColor.green(), pColor.blue() );

  wrEntry( aValue, pKey, "Colors" );
}


/* private functions --------------------------------------------------- */
void VkConfig::insertData( const EntryKey &ekey, 
                           const EntryData &edata )
{
  EntryData &entry = mEntryMap[ekey];
  entry = edata;
}


/* If we've just created valkyrierc, or if we've moved machines, or
   changed some install paths, write the vg-exec-path and vg-supp-dir
   paths found by 'configure' to valkyrierc.  These values can be
   over-ridden by the user via the Options dialog.*/
void VkConfig::updatePaths()
{
  wrEntry( VG_EXEC_PATH, "vg-exec",  "valkyrie" );
  wrEntry( VG_SUPP_DIR,  "vg-supps-dir", "valkyrie" );

  /* find and store valgrind's suppressions files */
  QString def_supp   = "";
  QString supp_files = "";
  QDir supp_dir( VG_SUPP_DIR );
  /* see if we have any *.supp files in here - if so, grab 'em while
     the going's good */
  QStringList supp_list = supp_dir.entryList( "*.supp", QDir::Files );
  for ( unsigned int i=0; i<supp_list.count(); i++ ) {
    supp_files += supp_dir.absPath() + "/" + supp_list[i] + sep;
    /* the only selected one is the default suppression file */
    if ( supp_list[i] == rdEntry( "suppressions", "valgrind" ) )
      def_supp = supp_dir.absPath() + "/" + supp_list[i];
  }
  /* chop off the trailing ';' */
  supp_files.truncate( supp_files.length() - 1 );
  /* write the list of found .supp files */
  wrEntry( supp_files, "supps-all", "valgrind" );
  /* and hold onto these values, 'cos they are the install defaults */
  wrEntry( supp_files, "supps-def", "valgrind" );
  /* and write the default supp. file including path */
  wrEntry( def_supp, "suppressions", "valgrind" );

  /* write entries to disk immediately in case Something Bad happens,
     as we won't get another chance to do this */
  sync();
}


VkConfig::RetVal VkConfig::parseFile()
{
  QFile rFile( rcFileName );
  if ( !rFile.open( IO_ReadOnly ) ) {
    vkFatal( 0, "Parse Config File",
             "<p>Failed to open the file %s for reading.<br>"
             "%s cannot run without this file.</p>", 
             rcFileName.latin1(), vkName() );
    return Fail;
  } else {
    /* beam me up, scotty */
    parseConfigFile( rFile );
    rFile.close();
  }

  /* double-check that all the install paths are correct - if not,
     silently correct them.  we have to delete any entries held in
     [valgrind:suppressions] as they may contain invalid paths, and
     re-initialise with default suppressions */
  if ( rdEntry("vg-exec",      "valkyrie") != VG_EXEC_PATH || 
       rdEntry("vg-supps-dir", "valkyrie") != VG_SUPP_DIR ) {
    updatePaths();
  }

  return Okay;
}


void VkConfig::parseConfigFile( QFile &rFile, EntryMap *writeBackMap )
{
  if ( !rFile.isOpen() ) {
    VK_DEBUG( "parseConfigFile( %s )\n"
             "rFile '%s' is not open for parsing", 
             VK_STRLOC, rcFileName.latin1() );
    return;
  }

  QString line;
  QString aGroup;

  QTextStream stream( &rFile );
  stream.setEncoding( QTextStream::UnicodeUTF8 );
  while ( !stream.atEnd() ) {

    line = stream.readLine();
    if ( line.isEmpty() ) {         /* empty line... skip it   */
      continue;
    }
    if ( line[0] == QChar('#') ) {  /* comment line... skip it */
      continue;
    }
    
    int len = line.length();
    if ( line[0] == QChar('[') && line[len-1] == QChar(']') ) {
      /* found a group */
      line.setLength( len-1 );      /* chop off the ']' */
      aGroup = line.remove(0, 1);   /* ditto re the '[' */
    }
    else {
      /* found a key --> value pair */
      int pos = line.find('=');
      QString key   = line.left( pos );
      QString value = line.right( len-pos-1 );

      EntryKey entryKey( aGroup, key );
      EntryData entryData( value, false );

      if ( writeBackMap ) {
        /* insert into the temporary scratchpad map */
        writeBackMap->insert( entryKey, entryData );
      } else { 
        /* directly insert value into config object */
        insertData( entryKey, entryData );
      }

    }
  }
}


void VkConfig::writebackConfig()
{
  EntryMap tmpMap;

  /* read config file from disk, and fill the temporary structure 
     with entries from the file */
  QFile rcFile( rcFileName );
  if ( !rcFile.open(IO_ReadOnly) ) {
    VK_DEBUG( "writebackConfig( %s )\n"
             "failed to open rcfile: %s", 
             VK_STRLOC, rcFileName.latin1() );
  } else {
    parseConfigFile( rcFile, &tmpMap );
    rcFile.close();
  }

  /* augment this structure with the dirty entries from the
     config object */
  EntryMapIterator aIt;
  for ( aIt = mEntryMap.begin(); aIt != mEntryMap.end(); ++aIt) {
    const EntryData &dirtyEntry = aIt.data();
    if ( !dirtyEntry.mDirty ) 
      continue;
    /* put dirty entries from the config object into the
       temporary map, possibly replacing an existing entry */
    tmpMap.replace( aIt.key(), dirtyEntry );
  }

  /* The temporary map should now be full of ALL entries.
     Write it out to disk. */
  FILE *pStream = 0;
  int fd = open( rcFileName, O_WRONLY | O_TRUNC);
  if ( fd < 0 )
    return;
  pStream = fdopen( fd, "w");
  if ( !pStream ) {
    close(fd);
    return;
  }

  /* ### evil hack: this is the one flag we never want changed on a
     permanent basis: user must specify 'no' each run. */
  wrEntry( "yes", "gui", "valkyrie" );

  bool firstEntry = true;
  QString currGroup;
  for ( aIt = tmpMap.begin(); aIt != tmpMap.end(); ++aIt) {

    const EntryKey  &currKey   = aIt.key();
    const EntryData &currEntry = aIt.data();

    /* new group */
    if ( currGroup != currKey.mGroup ) {

      if ( firstEntry ) {
        QString hdr = QString("# %1 %2 Configuration File\n")
                             .arg(vkName()).arg(vkVersion());

        fprintf( pStream, hdr.latin1() );

        QDateTime dt = QDateTime::currentDateTime();
        hdr = "# " + dt.toString( "MMMM d hh:mm yyyy" ) + "\n";
        fprintf( pStream, hdr.latin1() );
      }

      currGroup = currKey.mGroup;
      fprintf( pStream, "\n[%s]\n", currGroup.latin1() );
    }
    firstEntry = false;

    /* group data */
    fprintf( pStream, "%s=%s\n",  
             currKey.mKey.latin1(), currEntry.mValue.latin1() );
  }

  fclose( pStream );
}


VkConfig::RetVal VkConfig::checkAccess() const
{
  /* 0. first things first .... */
  if ( rcFileName.isEmpty() ) {
    VK_DEBUG( "checkAccess( %s )\n"
             "rcFileName is empty", VK_STRLOC );
    return BadFilename;
  }

  /* 1. check the /rc/ directory actually exists */
  if ( 0 != access( rcPath, F_OK ) ) {
    VK_DEBUG("checkAccess( %s )\n" 
            "The directory '%s' does not exist", 
            VK_STRLOC, rcPath.latin1() );
    return NoDir;
  }

  /* 2. ... and that the user has read/write permissions set.  
     can we allow the write?  we can, if the program does not run
     SUID.  but if it runs SUID, we must check if the user would be
     allowed to write if it wasn't SUID. */
  if ( 0 != access( rcPath, R_OK & W_OK ) ) {
    return NoPerms;
  }

  /* 3. check the rcfile actually exists. If not, create it now */
  if ( 0 != access( rcFileName, F_OK ) ) {
    return CreateRcFile;
  }

  /* 4. if it already exists, can we read / write it? */
  if ( 0 != access( rcFileName, R_OK & W_OK ) ) {
    vkInfo( 0, "Configuration",
            "<p>The file %s seems to be corrupted, and "
            "valkyrie cannot run without this file.</p>"
            "<p>Re-creating it now ... .. </p>", 
            rcFileName.latin1() );
    return CreateRcFile;
  }

  /* 5. and finally, check the version no. for compatibility */
  QFile rcFile( rcFileName );
  if ( rcFile.open( IO_ReadOnly ) ) {
    QTextStream ts( &rcFile );
    QString line = ts.readLine();
    rcFile.close();
    /* get the version as a string */
    int i = 0;
    while ( !line[i].isDigit() ) i++;
    line = line.right( line.length() - i );
    i = 0;
    while ( line[i].isDigit() || line[i] == '.' )  i++;
    line = line.left( i );
    if ( line != vk_version.data() )
      return BadRcVersion;
  }

  return Okay;
}


/* Initialise pointer list of objects. */
void VkConfig::initVkObjects() 
{ 
  vkObjectList.append( new Valkyrie() );
  vkObjectList.append( new Valgrind() );
  vkObjectList.append( new Memcheck() );
  vkObjectList.append( new Cachegrind() );
  vkObjectList.append( new Massif() );
}

/* Returns a ptr to be tool currently set in [valgrind:tool] */
ToolObject* VkConfig::tool()
{
  QString name = rdEntry("tool", "valgrind");
  for ( VkObject* obj=vkObjectList.first(); obj; obj=vkObjectList.next() ) {
    if ( obj->isTool() && obj->name() == name )
      return (ToolObject*)obj;
  }
  vk_assert_never_reached();
  return NULL;
}

/* returns the name of the current tool in [valgrind:tool] */
QString VkConfig::toolName()
{ return rdEntry("tool", "valgrind"); }

/* returns the tool id of [valgrind:tool] */
int VkConfig::toolId()
{
  QString tool_name = rdEntry( "tool", "valgrind" );
  for ( VkObject* obj=vkObjectList.first(); obj; obj=vkObjectList.next() ) {
    if ( obj->isTool() && obj->name() == tool_name )
      return vkObjectId( obj );
  }
  vk_assert_never_reached();
  return -1;
}

/* returns the list of ToolObjects
   Note: toolList order doesn't match objectId */
ToolList VkConfig::toolList()
{
  ToolList tools;
  for ( VkObject* obj=vkObjectList.first(); obj; obj=vkObjectList.next() ) {
    if ( obj->isTool() )
      tools.append( (ToolObject*)obj );
  }
  return tools;
}

/* returns a ToolObject, based on its objectId */
ToolObject* VkConfig::vkToolObj( int tvid )
{
  VkObject* obj = vkObjectList.at( tvid );
  vk_assert( obj != 0 && obj->isTool() );
  return (ToolObject*)obj;
}

/* returns list of all objects */
VkObjectList VkConfig::vkObjList()
{ return vkObjectList; }

/* returns an object based on its objectId */
VkObject* VkConfig::vkObject( int tvid )
{
  VkObject* obj = vkObjectList.at( tvid );
  vk_assert( obj != 0 );
  return obj;
}

/* returns a vkObject based on its name */
VkObject* VkConfig::vkObject( const QString& obj_name )
{
  VkObject* obj;
  for ( obj = vkObjectList.first(); obj; obj = vkObjectList.next() ) {
    if ( obj->name() == obj_name )
      return obj;
  }

  vk_assert_never_reached();
  return NULL;
}

/* returns an object's id */
int VkConfig::vkObjectId( VkObject* obj )
{
  int id = vkObjectList.findRef( obj );
  vk_assert( id >= 0 );
  return id;
}


/* Create the default configuration file.  -----------------------------
   The first time valkyrie is started, vkConfig looks to see if this
   file is present in the user's home dir.  If not, it writes the
   relevant data to ~/.PACKAGE/PACKAGErc */
void VkConfig::mkConfigFile( bool rm )
{
  /* we might have to remove an old rc file */
  if ( rm ) {
    QFile rcF( rcFileName ); 
    if ( !rcF.remove() ) 
      VK_DEBUG("Failed to delete old version rcfile.");
  }

  QDateTime dt = QDateTime::currentDateTime();

  QString header = QString("# %1 %2 configuration file\n")
                          .arg(vkName()).arg(vkVersion());
  header += "# " + dt.toString( "MMMM d hh:mm yyyy" ) + "\n\n";


  char * window_colors = "[Colors]\n\
background=214,205,187\n\
base=255,255,255\n\
dkgray=128,128,128\n\
editColor=254,222,190\n\
highlight=147,40,40\n\
nullColor=239,227,211\n\
text=0,0,0\n\n";

  char * mainwin_size_pos = "[MainWin]\n\
height=600\n\
width=550\n\
x-pos=400\n\
y-pos=0\n\n";

  char * dbase = "[Database]\n\
user=root\n\
host=localhost\n\
pword=Poniarl7\n\
dbase=valkyrie\n\
logging=true\n\
logfile=\n\n";


  QFile outF( rcFileName ); 
  if ( outF.open( IO_WriteOnly ) ) { 
    QTextStream aStream( &outF ); 

    aStream << header << window_colors << mainwin_size_pos << dbase;

    /* a new tool might have been added, or other changes made, in
       which case this fn wouldn't contain the correct options=values
       if it were hard-wired in. Better safe than sorry: just get all
       tools that are present to spew their options/flags out to disk. */
    VkObject* vkobj;
    for ( vkobj = vkObjectList.first(); vkobj; vkobj = vkObjectList.next() ) {
      aStream << vkobj->configEntries();
    }

    outF.close();
  }

  newConfigFile = true;
}


/* ~/.PACKAGE-X.X.X is a sine qua non ---------------------------------- 
   checks to see if ~/valkyrie-X.X.X/ and its required sub-dirs are
   all present and correct.  If not, tries to create them.
  ~/valkyrie-X.X.X/ 
    - valkyrierc
    - dbase/
    - logs/
    - suppressions/ 
*/
bool VkConfig::checkDirs()
{
  QString msg = "";
  bool success = true;

  QDir vk_dir( rcPath );

  enum State { 
    CHECK_DIR=0, CHECK_SUB_DIRS, MK_TOP_DIR, MK_DB_DIR, 
    MK_LOG_DIR,  MK_SUPP_DIR, DONE, GIVE_UP };
  State state = CHECK_DIR;

  bool not_done = true;
  while ( not_done ) {

    switch ( state ) {

      /* normal startup checks ----------------------------------------- */
      case CHECK_DIR:        /* does ~/.PACKAGE-X.X.X/ exist ? */
        state = vk_dir.exists() ? CHECK_SUB_DIRS : MK_TOP_DIR;
        break;

      case CHECK_SUB_DIRS: { /* check sub-dirs */
        const QFileInfoList * files = vk_dir.entryInfoList();
        QFileInfoListIterator it( *files );
        QFileInfo * fi;
        while ( ( fi=it.current() ) != 0 ) {
          ++it;
          if ( fi->fileName() == "." || fi->fileName() == ".." ) ;
          else if ( fi->isFile() && fi->isReadable() &&
                    fi->fileName() == "valkyrierc" ) ;
          else if ( fi->isDir() && fi->isReadable() ) {
                 if ( fi->fileName() == "dbase" ) ;
            else if ( fi->fileName() == "logs" ) ;
            else if ( fi->fileName() == "suppressions" ) ;
            else { /* problem */
              state = GIVE_UP;
              break;
            }
          }
        }
        state = (state == GIVE_UP) ? state : DONE;
      } break;

      /* first time ever startup --------------------------------------- */
      case MK_TOP_DIR:       /* create '~/PACKAGE-X.X.X' */
        state = vk_dir.mkdir( rcPath ) ? MK_DB_DIR : GIVE_UP;
        break;

      case MK_DB_DIR:        /* create sub-dir '/dbase' */
        state = vk_dir.mkdir( dbasePath ) ? MK_LOG_DIR : GIVE_UP;
        break;

      case MK_LOG_DIR:       /* create sub-dir '/logs' */
        state = vk_dir.mkdir( logsPath ) ? MK_SUPP_DIR : GIVE_UP;
        break;

      /* last case statement MUST set 'state = DONE || GIVE_UP' */
      case MK_SUPP_DIR:      /* create sub-dir '/suppressions' */
        state = vk_dir.mkdir( suppPath ) ? DONE : GIVE_UP;
        if ( state == DONE )
        break;

      case GIVE_UP:
        not_done = false;
        success = false;
        msg.sprintf( "<p>There is a problem with '%s'.<br>"
                     "Either some files or sub-directories do not exist, "
                     "or the permissions are not set correctly."
                     "<p>Please check and retry.</p>", rcPath.latin1() );
        break;

      case DONE:
        not_done = false;
        break;

    }  /* end switch ( state ) */
  }    /* end while (1) */

  if ( !msg.isEmpty() ) {
    vkError( 0, "Directory Error", msg.latin1() );
  }

  return success;
}
