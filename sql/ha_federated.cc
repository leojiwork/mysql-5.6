/* Copyright (C) 2004 MySQL AB

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/*

  MySQL Federated Storage Engine

  ha_federated.cc - MySQL Federated Storage Engine
  Patrick Galbraith and Brian Aker, 2004

  This is a handler which uses a remote database as the data file, as 
  opposed to a handler like MyISAM, which uses .MYD files locally.

  How this handler works
  ----------------------------------
  Normal database files are local and as such: You create a table called 
  'users', a file such as 'users.MYD' is created. A handler reads, inserts, 
  deletes, updates data in this file. The data is stored in particular format,
  so to read, that data has to be parsed into fields, to write, fields have to
  be stored in this format to write to this data file. 

  With MySQL Federated storage engine, there will be no local files for each
  table's data (such as .MYD). A remote database will store the data that would
  normally be in this file. This will necessitate the use of MySQL client API 
  to read, delete, update, insert this data. The data will have to be retrieve
  via an SQL call "SELECT * FROM users". Then, to read this data, it will have
  to be retrieved via mysql_fetch_row one row at a time, then converted from 
  the  column in this select into the format that the handler expects.

  The create table will simply create the .frm file, and within the 
  "CREATE TABLE" SQL, there SHALL be any of the following : 

  comment=username:password@hostname:port/database/tablename
  comment=username@hostname/database/tablename
  comment=username:password@hostname/database/tablename
  comment=username:password@hostname/database/tablename

  This string is necessary for the handler to be able to connect to the remote
  server.

  The basic flow is this:

  SQL calls issues locally ->
  mysql handler API (data in handler format) -> 
  mysql client API (data converted to SQL calls) -> 
  remote database -> mysql client API -> 
  convert result sets (if any) to handler format ->
  handler API -> results or rows affected to local

  What this handler does and doesn't support
  ------------------------------------------
  * There will not be support for transactions
  * There is no way for the handler to know if the database on the remote end
    has changed. The reason for this is that this database has to work like a
    data file that would never be written to by anything other than the 
    database. The integrity of the data in the local table could be breached 
    if there was any change to the remote database.
  * Support for SELECT, INSERT, UPDATE , DELETE, indexes
  * No ALTER TABLE, DROP TABLE or any other Data Definition Language calls.
  * Prepared statements will not be used in the first implementation, it 
    remains to to be seen whether the limited subset of the client API for the
    server supports this
  * This uses SELECT, INSERT, UPDATE, DELETE and not HANDLER for its 
    implementation 
  * Tables must be created on the remote server prior to any action on those
    tables via the handler, first version.
  * This will not work with the query cache

   Method calls

   A two column table, with one record:

   (SELECT)

   "SELECT * FROM foo" 
    ha_federated::external_lock
    ha_federated::info
    ha_federated::scan_time:
    ha_federated::rnd_init: share->select_query SELECT * FROM foo
    ha_federated::extra

    <for every row of data retrieved>
    ha_federated::rnd_next
    ha_federated::convert_row_to_internal_format
    ha_federated::rnd_next
    </for every row of data retrieved>

    ha_federated::rnd_end
    ha_federated::extra
    ha_federated::external_lock
    ha_federated::reset

    (INSERT)

    "INSERT INTO foo (id, ts) VALUES (2, now());"

    ha_federated::external_lock
    ha_federated::write_row

    <for every field/column>
    ha_federated::quote_data
    ha_federated::quote_data
    </for every field/column>

    ha_federated::external_lock
    ha_federated::reset

    (UPDATE)

    "UPDATE foo SET ts = now() WHERE id = 1;"

    ha_federated::index_init
    ha_federated::index_read
    ha_federated::index_read_idx
    ha_federated::quote_data
    ha_federated::rnd_next
    ha_federated::convert_row_to_internal_format
    ha_federated::update_row
    
    <quote 3 cols, new and old data>
    <ha_federated::quote_data
    <ha_federated::quote_data
    <ha_federated::quote_data
    <ha_federated::quote_data
    <ha_federated::quote_data
    <ha_federated::quote_data
    </quote 3 cols, new and old data>
    
    ha_federated::extra
    ha_federated::extra
    ha_federated::extra
    ha_federated::external_lock
    ha_federated::reset


    How do I use this handler?
    --------------------------
    First of all, you need to build this storage engine:
    
      ./configure --with-federated-storage-engine
      make

    Next, to use this handler, it's very simple. You must 
    have two databases running, either both on the same host, or
    on different hosts.

    One the server that will be connecting to the remote 
    host (client), you create your table as such:

    CREATE TABLE test_table ( 
      id     int(20) NOT NULL auto_increment,
      name   varchar(32) NOT NULL default '',
      other  int(20) NOT NULL default '0',
      PRIMARY KEY  (id),
      KEY name (name),
      KEY other_key (other)) 
       ENGINE="FEDERATED" 
       DEFAULT CHARSET=latin1 
       COMMENT='root@127.0.0.1:9306/federated/test_federated';

   Notice the "COMMENT" and "ENGINE" field? This is where you 
   respectively set the engine type, "FEDERATED" and remote
   host information, this being the database your 'client' database 
   will connect to and use as the "data file". Obviously, the remote
   database is running on port 9306, so you want to start up your other 
   database so that it is indeed on port 9306, and your federated 
   database on a port other than that. In my setup, I use port 5554 
   for federated, and port 5555 for the remote.

   Then, on the remote database:

   CREATE TABLE test_table ( 
     id     int(20) NOT NULL auto_increment,
     name   varchar(32) NOT NULL default '',
     other  int(20) NOT NULL default '0',
     PRIMARY KEY  (id),
     KEY name (name),
     KEY other_key (other)) 
     ENGINE="<NAME>" <-- whatever you want, or not specify
     DEFAULT CHARSET=latin1 ;

    This table is exactly the same (and must be exactly the same),
    except that it is not using the federated handler and does 
    not need the URL.
    

    How to see the handler in action
    --------------------------------

    When developing this handler, I compiled the federated database with
    debugging:

    ./configure --with-federated-storage-engine 
    --prefix=/home/mysql/mysql-build/federated/ --with-debug

    Once compiled, I did a 'make install' (not for the purpose of installing
    the binary, but to install all the files the binary expects to see in the
    diretory I specified in the build with --prefix, 
    "/home/mysql/mysql-build/federated". 
    
    Then, I started the remote server:

    /usr/local/mysql/bin/mysqld_safe 
    --user=mysql --log=/tmp/mysqld.5555.log -P 5555

    Then, I went back to the directory containing the newly compiled mysqld,
    <builddir>/sql/, started up gdb:

    gdb ./mysqld

    Then, withn the (gdb) prompt:
    (gdb) run --gdb --port=5554 --socket=/tmp/mysqld.5554 --skip-innodb --debug

    Next, I open several windows for each:

    1. Tail the debug trace: tail -f /tmp/mysqld.trace|grep ha_fed
    2. Tail the SQL calls to the remote database: tail -f /tmp/mysqld.5555.log
    3. A window with a client open to the federated server on port 5554
    4. A window with a client open to the federated server on port 5555

    I would create a table on the client to the remote server on port 
    5555, and then to the federated server on port 5554. At this point,
    I would run whatever queries I wanted to on the federated server,
    just always remembering that whatever changes I wanted to make on 
    the table, or if I created new tables, that I would have to do that
    on the remote server.
    
    Another thing to look for is 'show variables' to show you that you have 
    support for federated handler support:

    show variables like '%federat%'

    and:

    show storage engines;

    Both should display the federated storage handler.


    Testing
    -------

    There is a test for MySQL Federated Storage Handler in ./mysql-test/t, 
    federatedd.test It starts both a slave and master database using
    the same setup that the replication tests use, with the exception that
    it turns off replication, and sets replication to ignore the test tables.
    After ensuring that you actually do have support for the federated storage 
    handler, numerous queries/inserts/updates/deletes are run, many derived 
    from the MyISAM tests, plus som other tests which were meant to reveal
    any issues that would be most likely to affect this handler. All tests
    should work! ;)

    To run these tests, go into ./mysql-test (based in the directory you 
    built the server in)

    ./mysql-test-run federatedd
   
    To run the test, or if you want to run the test and have debug info:

    ./mysql-test-run --debug federated

    This will run the test in debug mode, and you can view the trace and 
    log files in the ./mysql-test/var/log directory

    ls -l mysql-test/var/log/
    -rw-r--r--  1 patg  patg        17  4 Dec 12:27 current_test
    -rw-r--r--  1 patg  patg       692  4 Dec 12:52 manager.log
    -rw-rw----  1 patg  patg     21246  4 Dec 12:51 master-bin.000001
    -rw-rw----  1 patg  patg        68  4 Dec 12:28 master-bin.index
    -rw-r--r--  1 patg  patg      1620  4 Dec 12:51 master.err
    -rw-rw----  1 patg  patg     23179  4 Dec 12:51 master.log
    -rw-rw----  1 patg  patg  16696550  4 Dec 12:51 master.trace
    -rw-r--r--  1 patg  patg         0  4 Dec 12:28 mysqltest-time
    -rw-r--r--  1 patg  patg   2024051  4 Dec 12:51 mysqltest.trace
    -rw-rw----  1 patg  patg     94992  4 Dec 12:51 slave-bin.000001
    -rw-rw----  1 patg  patg        67  4 Dec 12:28 slave-bin.index
    -rw-rw----  1 patg  patg       249  4 Dec 12:52 slave-relay-bin.000003
    -rw-rw----  1 patg  patg        73  4 Dec 12:28 slave-relay-bin.index
    -rw-r--r--  1 patg  patg      1349  4 Dec 12:51 slave.err
    -rw-rw----  1 patg  patg     96206  4 Dec 12:52 slave.log
    -rw-rw----  1 patg  patg  15706355  4 Dec 12:51 slave.trace
    -rw-r--r--  1 patg  patg         0  4 Dec 12:51 warnings

    Of course, again, you can tail the trace log:

    tail -f mysql-test/var/log/master.trace |grep ha_fed  

    As well as the slave query log:
  
    tail -f mysql-test/var/log/slave.log

    Files that comprise the test suit
    ---------------------------------
    mysql-test/t/federated.test
    mysql-test/r/federated.result
    mysql-test/r/have_federated_db.require
    mysql-test/include/have_federated_db.inc


    Other tidbits
    -------------

    These were the files that were modified or created for this 
    Federated handler to work:

    ./configure.in
    ./sql/Makefile.am
    ./config/ac_macros/ha_federated.m4
    ./sql/handler.cc
    ./sql/mysqld.cc
    ./sql/set_var.cc
    ./sql/field.h
    ./sql/sql_string.h
    ./mysql-test/mysql-test-run(.sh)
    ./mysql-test/t/federated.test
    ./mysql-test/r/federated.result
    ./mysql-test/r/have_federated_db.require
    ./mysql-test/include/have_federated_db.inc
    ./sql/ha_federated.cc
    ./sql/ha_federated.h
    
*/ 

#ifdef __GNUC__
#pragma implementation        // gcc: Class implementation
#endif

#include <mysql_priv.h>

#ifdef HAVE_FEDERATED_DB
#include "ha_federated.h"
#define MAX_REMOTE_SIZE IO_SIZE
/* Variables for federated share methods */
static HASH federated_open_tables; // Hash used to track open tables
pthread_mutex_t federated_mutex;   // This is the mutex we use to init the hash
static int federated_init= 0;   // Variable for checking the init state of hash

/*
  Function we use in the creation of our hash to get key.
*/
static byte* federated_get_key(FEDERATED_SHARE *share,uint *length,
                             my_bool not_used __attribute__((unused)))
{
  *length= share->table_name_length;
  return (byte*) share->table_name;
}

/*
  Parse connection info from table->comment

  SYNOPSIS
    parse_url()
      share     pointer to FEDERATED share
      table     pointer to current TABLE class
      
  DESCRIPTION
    populates the share with information about the connection
    to the remote database that will serve as the data source.
    This string must be specified (currently) in the "comment" field,
    listed in the CREATE TABLE statement. 

    This string MUST be in the format of any of these:

    username:password@hostname:port/database/table
    username@hostname/database/table
    username@hostname:port/database/table
    username:password@hostname/database/table

    'password' and 'port' are both optional.

  RETURN VALUE
    0   success
    -1  failure, wrong string format    

*/
int parse_url(FEDERATED_SHARE *share, TABLE *table)
{
  DBUG_ENTER("ha_federated::parse_url");

  share->username= my_strdup(table->comment, MYF(0));
  if (share->hostname= strchr(share->username, '@')) 
  {
    share->username[share->hostname - share->username]= '\0';
    share->hostname++;

    if (share->password= strchr(share->username, ':'))
    {
      share->username[share->password - share->username]= '\0';
      share->password++;
      share->username= share->username;
      // make sure there isn't an extra / or @
      if (strchr(share->password, '/') || strchr(share->hostname, '@'))
      {
        DBUG_PRINT("ha_federated::parse_url",
                   ("this connection string is not in the correct format!!!\n"));
        DBUG_RETURN(-1);
      }
      /* 
        Found that if the string is:
        user:@hostname:port/database/table 
        Then password is a null string, so set to NULL 
      */
      if (share->password[0] == '\0')
        share->password= NULL;
    }
    else
    {
      share->username= share->username;
      share->password= NULL;
    }
    // make sure there isn't an extra / or @
    if (strchr(share->username, '/') || strchr(share->hostname, '@'))
    {
      DBUG_PRINT("ha_federated::parse_url",
        ("this connection string is not in the correct format!!!\n"));
      DBUG_RETURN(-1);
    }

    if (share->database= strchr(share->hostname, '/'))
    {
      share->hostname[share->database - share->hostname]= '\0';
      share->database++;

      if (share->sport= strchr(share->hostname, ':'))
      {
        share->hostname[share->sport - share->hostname]= '\0';
        share->sport++;
        share->port= atoi(share->sport);

      }
      else
      {
        share->sport= NULL;
        share->port= NULL;
      }

      if (share->table_base_name= strchr(share->database, '/'))
      {
        share->database[share->table_base_name - share->database]= '\0';
        share->table_base_name++;
      }
      else
      {
        DBUG_PRINT("ha_federated::parse_url",
          ("this connection string is not in the correct format!!!\n"));
        DBUG_RETURN(-1);
      }
    }
    else
    {
      DBUG_PRINT("ha_federated::parse_url",
        ("this connection string is not in the correct format!!!\n"));
      DBUG_RETURN(-1);
    }
    // make sure there's not an extra /
    if (strchr(share->table_base_name, '/'))
    {
      DBUG_PRINT("ha_federated::parse_url",
        ("this connection string is not in the correct format!!!\n"));
      DBUG_RETURN(-1);
    }
    DBUG_PRINT("ha_federated::parse_url", 
      ("username %s password %s hostname %s port %d database %s tablename %s\n",
      share->username, share->password, share->hostname,
      share->port, share->database, share->table_base_name));
  }
  else
  { 
    DBUG_PRINT("ha_federated::parse_url",
      ("this connection string is not in the correct format!!!\n"));
    DBUG_RETURN(-1);
  }
  DBUG_RETURN(0);
}

/* 
  Convert MySQL result set row to handler internal format

  SYNOPSIS
    convert_row_to_internal_format()
      record    Byte pointer to record 
      row       MySQL result set row from fetchrow()

  DESCRIPTION
    This method simply iterates through a row returned via fetchrow with 
    values from a successful SELECT , and then stores each column's value
    in the field object via the field object pointer (pointing to the table's
    array of field object pointers). This is how the handler needs the data 
    to be stored to then return results back to the user

  RETURN VALUE
    0   After fields have had field values stored from record  
 */
uint ha_federated::convert_row_to_internal_format(byte *record, MYSQL_ROW row)
{
  unsigned long len;
  int x= 0;
  DBUG_ENTER("ha_federated::convert_row_to_internal_format");

  // Question this
  memset(record, 0, table->null_bytes); 

  for (Field **field=table->field; *field ; field++, x++)
  {
    if (!row[x]) 
      (*field)->set_null();
    else
      /*
        changed system_charset_info to default_charset_info because
        testing revealed that german text was not being retrieved properly
      */
      (*field)->store(row[x], strlen(row[x]), default_charset_info);
  }

  DBUG_RETURN(0);
}

/* 
  SYNOPSIS
    quote_data()
     unquoted_string    Pointer pointing to the value of a field
     field              MySQL Field pointer to field being checked for type

  DESCRIPTION
    Simple method that passes the field type to the method "type_quote"
    To get a true/false value as to whether the value in string1 needs 
    to be enclosed with quotes. This ensures that values in the final 
    sql statement to be passed to the remote server will be quoted properly

  RETURN_VALUE
    void      Immediately - if string doesn't need quote
    void      Upon prepending/appending quotes on each side of variable

*/
void ha_federated::quote_data(String *unquoted_string, Field *field )
{
  char quoted_string_buffer[IO_SIZE];
  String quoted_string(quoted_string_buffer, 
                       sizeof(quoted_string_buffer), &my_charset_bin);
  quoted_string.length(0);

  int quote_flag;
  DBUG_ENTER("ha_federated::quote_data");
  DBUG_PRINT("ha_federated::quote_data", ("unquoted string %s", unquoted_string->c_ptr_quick()));

  if (field->is_null())
  {
    DBUG_PRINT("ha_federated::quote_data",
               ("NULL, no quoted needed for unquoted_string %s, returning.",
                unquoted_string->c_ptr_quick()));
    DBUG_VOID_RETURN;
  }

  quote_flag= type_quote(field->type());

  if (quote_flag == 0)
  {
    DBUG_PRINT("ha_federated::quote_data",
               ("quote flag 0 no quoted needed for unquoted_string %s, returning.",
                unquoted_string->c_ptr_quick()));
    DBUG_VOID_RETURN;
  }
  else
  {
    quoted_string.append("'");
    quoted_string.append(*unquoted_string);
    quoted_string.append("'");
    unquoted_string->copy(quoted_string);
  }
  DBUG_PRINT("ha_federated::quote_data",
             ("quote_flag %d unquoted_string %s quoted_string %s", 
              quote_flag, unquoted_string->c_ptr_quick(), quoted_string.c_ptr_quick()));
  DBUG_VOID_RETURN;
}

/*
  Quote a field type if needed

  SYNOPSIS
    ha_federated::type_quote
      int field     Enumerated field type number

  DESCRIPTION 
    Simple method to give true/false whether a field should be quoted. 
    Used when constructing INSERT and UPDATE queries to the remote server
    see write_row and update_row

   RETURN VALUE
      0   if value is of type NOT needing quotes
      1   if value is of type needing quotes
*/
uint ha_federated::type_quote(int type) 
{
  DBUG_ENTER("ha_federated::type_quote");
  DBUG_PRINT("ha_federated::type_quote", ("field type %d", type));

  switch(type) {
    //FIX this is a bug, fix when kernel is fixed
  case MYSQL_TYPE_VARCHAR :
  case FIELD_TYPE_STRING :
  case FIELD_TYPE_VAR_STRING :
  case FIELD_TYPE_YEAR :
  case FIELD_TYPE_NEWDATE :
  case FIELD_TYPE_TIME :
  case FIELD_TYPE_TIMESTAMP :
  case FIELD_TYPE_DATE :
  case FIELD_TYPE_DATETIME :
  case FIELD_TYPE_TINY_BLOB :
  case FIELD_TYPE_BLOB :
  case FIELD_TYPE_MEDIUM_BLOB :
  case FIELD_TYPE_LONG_BLOB :
  case FIELD_TYPE_GEOMETRY :
    DBUG_RETURN(1);

  case FIELD_TYPE_DECIMAL : 
  case FIELD_TYPE_TINY :
  case FIELD_TYPE_SHORT :
  case FIELD_TYPE_INT24 :
  case FIELD_TYPE_LONG :
  case FIELD_TYPE_FLOAT :
  case FIELD_TYPE_DOUBLE :
  case FIELD_TYPE_LONGLONG :
  case FIELD_TYPE_NULL :
  case FIELD_TYPE_SET :
  case FIELD_TYPE_ENUM : 
    DBUG_RETURN(0);

  default: DBUG_RETURN(0);
  }
  DBUG_RETURN(0);
}

int load_conn_info(FEDERATED_SHARE *share, TABLE *table)
{
  DBUG_ENTER("ha_federated::load_conn_info");
  int retcode;
 
  retcode= parse_url(share, table);

  if (retcode < 0)
  {
    DBUG_PRINT("ha_federated::load_conn_info", 
               ("retcode %d, setting defaults", retcode));
    /* sanity checks to make sure all needed pieces are present */
    if (!share->port)
    {
      if (strcmp(share->hostname, "localhost") == 0)
        share->socket= my_strdup("/tmp/mysql.sock",MYF(0));
      else
        share->port= 3306;
    }
  }
  DBUG_PRINT("ha_federated::load_conn_info",
             ("returned from retcode %d", retcode));
    
  DBUG_RETURN(retcode);
}

/*
  Example of simple lock controls. The "share" it creates is structure we will
  pass to each federated handler. Do you have to have one of these? Well, you
  have pieces that are used for locking, and they are needed to function.
*/
static FEDERATED_SHARE *get_share(const char *table_name, TABLE *table)
{
  FEDERATED_SHARE *share;
  // FIX : need to redo 
  //String query;
  char query_buffer[IO_SIZE];
  String query(query_buffer, sizeof(query_buffer), &my_charset_bin);
  query.length(0);

  uint table_name_length, table_base_name_length;
  char *tmp_table_name, *tmp_table_base_name, *table_base_name, *select_query;

  // share->table_name has the file location - we want the actual table's
  // name!
  table_base_name= table->table_name;
  DBUG_PRINT("ha_federated::get_share",("table_name %s", table_base_name));
  /*
    So why does this exist? There is no way currently to init a storage engine.
    Innodb and BDB both have modifications to the server to allow them to
    do this. Since you will not want to do this, this is probably the next
    best method.
  */
  if (!federated_init)
  {
    /* Hijack a mutex for init'ing the storage engine */
    pthread_mutex_lock(&LOCK_mysql_create_db);
    if (!federated_init)
    {
      federated_init++;
      VOID(pthread_mutex_init(&federated_mutex,MY_MUTEX_INIT_FAST));
      (void) hash_init(&federated_open_tables,system_charset_info,32,0,0,
                       (hash_get_key) federated_get_key,0,0);
    }
    pthread_mutex_unlock(&LOCK_mysql_create_db);
  }
  pthread_mutex_lock(&federated_mutex);
  table_name_length= (uint) strlen(table_name);
  table_base_name_length= (uint) strlen(table_base_name);

  if (!(share= (FEDERATED_SHARE*) hash_search(&federated_open_tables,
                                           (byte*) table_name,
                                           table_name_length)))
  {
    query.set_charset(system_charset_info);
    query.append("SELECT * FROM ");
    query.append(table_base_name);
    
    if (!(share= (FEDERATED_SHARE *)
          my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                          &share, sizeof(*share),
                          &tmp_table_name, table_name_length+1,
                          &tmp_table_base_name, table_base_name_length+1, 
                          &select_query, query.length()+1,
                          NullS)))
    {
      pthread_mutex_unlock(&federated_mutex);
      return NULL;
    }

    load_conn_info(share, table);
    share->use_count= 0;
    share->table_name_length= table_name_length;
    share->table_name= tmp_table_name;
    share->table_base_name_length= table_base_name_length;
    share->table_base_name= tmp_table_base_name;
    share->select_query= select_query;
    strmov(share->table_name,table_name);
    strmov(share->table_base_name,table_base_name);
    strmov(share->select_query,query.c_ptr_quick());
    DBUG_PRINT("ha_federated::get_share",("share->select_query %s", share->select_query));
    if (my_hash_insert(&federated_open_tables, (byte*) share))
      goto error;
    thr_lock_init(&share->lock);
    pthread_mutex_init(&share->mutex,MY_MUTEX_INIT_FAST);
  }
  share->use_count++;
  pthread_mutex_unlock(&federated_mutex);

  return share;

error2:
  thr_lock_delete(&share->lock);
  pthread_mutex_destroy(&share->mutex);
error:
  pthread_mutex_unlock(&federated_mutex);
  my_free((gptr) share, MYF(0));

  return NULL;
}


/*
  Free lock controls. We call this whenever we close a table. 
  If the table had the last reference to the share then we
  free memory associated with it.
*/
static int free_share(FEDERATED_SHARE *share)
{
  pthread_mutex_lock(&federated_mutex);
  if (!--share->use_count)
  {
    hash_delete(&federated_open_tables, (byte*) share);
    thr_lock_delete(&share->lock);
    pthread_mutex_destroy(&share->mutex);
    my_free((gptr) share, MYF(0));
  }
  pthread_mutex_unlock(&federated_mutex);

  return 0;
}


/*
  If frm_error() is called then we will use this to to find out
  what file extentions exist for the storage engine. This is 
  also used by the default rename_table and delete_table method 
  in handler.cc.
*/
const char **ha_federated::bas_ext() const
{ static const char *ext[]= { NullS }; return ext; }


/*
  Used for opening tables. The name will be the name of the file.
  A table is opened when it needs to be opened. For instance
  when a request comes in for a select on the table (tables are not
  open and closed for each request, they are cached).

  Called from handler.cc by handler::ha_open(). The server opens
  all tables by calling ha_open() which then calls the handler
  specific open().
*/
int ha_federated::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_federated::open");
  int rc;

  if (!(share= get_share(name, table)))
    DBUG_RETURN(1);
  thr_lock_data_init(&share->lock,&lock,NULL);

  /* Connect to remote database mysql_real_connect() */
  mysql= mysql_init(0); 
  DBUG_PRINT("ha_federated::open",("hostname %s", share->hostname));
  DBUG_PRINT("ha_federated::open",("username %s", share->username));
  DBUG_PRINT("ha_federated::open",("password %s", share->password));
  DBUG_PRINT("ha_federated::open",("database %s", share->database));
  DBUG_PRINT("ha_federated::open",("port %d", share->port));
  if (!mysql_real_connect(mysql,
                     share->hostname,
                     share->username,
                     share->password,
                     share->database,
                     share->port,
                     NULL,
                     0))
  {
    my_error(ER_CONNECT_TO_MASTER, MYF(0), mysql_error(mysql));
    DBUG_RETURN(ER_CONNECT_TO_MASTER);
  }
  DBUG_RETURN(0);
}


/*
  Closes a table. We call the free_share() function to free any resources
  that we have allocated in the "shared" structure.

  Called from sql_base.cc, sql_select.cc, and table.cc.
  In sql_select.cc it is only used to close up temporary tables or during
  the process where a temporary table is converted over to being a
  myisam table.
  For sql_base.cc look at close_data_tables().
*/
int ha_federated::close(void)
{
  DBUG_ENTER("ha_federated::close");
  /* Disconnect from mysql */
  mysql_close(mysql);
  DBUG_RETURN(free_share(share));

}

/*

  Checks if a field in a record is SQL NULL. 

  SYNOPSIS
    field_in_record_is_null()
      table     TABLE pointer, MySQL table object
      field     Field pointer, MySQL field object
      record    char pointer, contains record

    DESCRIPTION
      This method uses the record format information in table to track
      the null bit in record. 

    RETURN VALUE
      1    if NULL
      0    otherwise 
*/
inline uint field_in_record_is_null (
          TABLE* table,	/* in: MySQL table object */
          Field* field,	/* in: MySQL field object */
          char*	record)	/* in: a row in MySQL format */
{
  int null_offset;
  DBUG_ENTER("ha_federated::field_in_record_is_null");

  if (!field->null_ptr)
    DBUG_RETURN(0);

  null_offset= (uint) ((char*) field->null_ptr - (char*) table->record[0]);

  if (record[null_offset] & field->null_bit)
    DBUG_RETURN(1);

  DBUG_RETURN(0);
}

/*
  write_row() inserts a row. No extra() hint is given currently if a bulk load
  is happeneding. buf() is a byte array of data. You can use the field
  information to extract the data from the native byte array type.
  Example of this would be:
  for (Field **field=table->field ; *field ; field++)
  {
    ...
  }

  Called from item_sum.cc, item_sum.cc, sql_acl.cc, sql_insert.cc,
  sql_insert.cc, sql_select.cc, sql_table.cc, sql_udf.cc, and sql_update.cc.
*/
int ha_federated::write_row(byte * buf)
{
  int x= 0, num_fields= 0;
  ulong current_query_id= 1;
  ulong tmp_query_id;
  int all_fields_have_same_query_id= 1;

  char insert_buffer[IO_SIZE];
  char values_buffer[IO_SIZE], insert_field_value_buffer[IO_SIZE];

  // The main insert query string
  String insert_string(insert_buffer, sizeof(insert_buffer), &my_charset_bin);
  insert_string.length(0);
  // The string containing the values to be added to the insert
  String values_string(values_buffer, sizeof(values_buffer), &my_charset_bin);
  values_string.length(0);
  // The actual value of the field, to be added to the values_string 
  String insert_field_value_string(insert_field_value_buffer,
       sizeof(insert_field_value_buffer), &my_charset_bin);
  insert_field_value_string.length(0);

  DBUG_ENTER("ha_federated::write_row");
  /*
    I want to use this and the next line, but the repository needs to be
    updated to do so
  */
  statistic_increment(table->in_use->status_var.ha_write_count,&LOCK_status);
  if (table->timestamp_field_type & TIMESTAMP_AUTO_SET_ON_INSERT)
    table->timestamp_field->set_time();

  /*
   get the current query id - the fields that we add to the insert
   statement to send to the remote will not be appended unless they match
   this query id
  */
  current_query_id= table->in_use->query_id;
  DBUG_PRINT("ha_federated::write_row", ("current query id %d",
                                         current_query_id));

  // start off our string 
  insert_string.append("INSERT INTO "); 
  insert_string.append(share->table_base_name);
  // start both our field and field values strings
  insert_string.append(" (");
  values_string.append(" VALUES (");

  /* 
    Even if one field is different, all_fields_same_query_id can't remain
    0 if it remains 0, then that means no fields were specified in the query
    such as in the case of INSERT INTO table VALUES (val1, val2, valN)
  */
  for (Field **field= table->field; *field ; field++, x++)
  {
    if (x > 0 && tmp_query_id != (*field)->query_id)
      all_fields_have_same_query_id= 0;

    tmp_query_id= (*field)->query_id;
  }
  /*
    loop through the field pointer array, add any fields to both the values
    list and the fields list that match the current query id
  */
  for (Field **field= table->field; *field ; field++, x++)
  {
    DBUG_PRINT("ha_federated::write_row", ("field type %d", (*field)->type()));
    // if there is a query id and if it's equal to the current query id
    if ( ((*field)->query_id && (*field)->query_id == current_query_id )
         || all_fields_have_same_query_id)
    {
      num_fields++;
      
      if ((*field)->is_null())
      {
        DBUG_PRINT("ha_federated::write_row",
                   ("current query id %d field is_null query id %d",
                    current_query_id, (*field)->query_id));
        insert_field_value_string.append("NULL");
      }
      else
      {
        DBUG_PRINT("ha_federated::write_row",
                   ("current query id %d field is not null query ID %d",
                    current_query_id, (*field)->query_id));
        (*field)->val_str(&insert_field_value_string);
      }
      // append the field name
      insert_string.append((*field)->field_name);

      // quote these fields if they require it
      quote_data(&insert_field_value_string, *field);
      // append the value
      values_string.append(insert_field_value_string);
      insert_field_value_string.length(0);

      // append commas between both fields and fieldnames
      insert_string.append(',');
      values_string.append(',');
    DBUG_PRINT("ha_federated::write_row", 
               ("insert_string %s values_string %s insert_field_value_string %s", 
                insert_string.c_ptr_quick(), values_string.c_ptr_quick(), insert_field_value_string.c_ptr_quick()));

    }
  }

  /*
    chop of the trailing comma, or if there were no fields, a '('
    So, "INSERT INTO foo (" becomes "INSERT INTO foo "
    or, with fields, "INSERT INTO foo (field1, field2," becomes
     "INSERT INTO foo (field1, field2"
  */
  insert_string.chop();


  /*
    if there were no fields, we don't want to add a closing paren
    AND, we don't want to chop off the last char '('
    insert will be "INSERT INTO t1 VALUES ();"
  */
  DBUG_PRINT("ha_federated::write_row",("x %d  num fields %d",
                                        x, num_fields));
  if (num_fields > 0)
  {
    // chops off leading commas
    values_string.chop();
    insert_string.append(')');
  }
  // we always want to append this, even if there aren't any fields
  values_string.append(')');
  
  // add the values 
  insert_string.append(values_string);

  DBUG_PRINT("ha_federated::write_row",("insert query %s", 
                                        insert_string.c_ptr_quick()));

  if (mysql_real_query(mysql, insert_string.c_ptr_quick(),
                       insert_string.length()))
  {
    my_error(ER_QUERY_ON_MASTER,MYF(0),mysql_error(mysql));
    DBUG_RETURN(ER_QUERY_ON_MASTER); 
  }

  DBUG_RETURN(0);
}

/*
  Yes, update_row() does what you expect, it updates a row. old_data will have
  the previous row record in it, while new_data will have the newest data in
  it.

  Keep in mind that the server can do updates based on ordering if an ORDER BY
  clause was used. Consecutive ordering is not guarenteed.
  Currently new_data will not have an updated auto_increament record, or
  and updated timestamp field. You can do these for federated by doing these:
  if (table->timestamp_on_update_now)
    update_timestamp(new_row+table->timestamp_on_update_now-1);
  if (table->next_number_field && record == table->record[0])
    update_auto_increment();

  Called from sql_select.cc, sql_acl.cc, sql_update.cc, and sql_insert.cc.
*/
int ha_federated::update_row(
                             const byte * old_data,
                             byte * new_data
                            )
{
  uint x= 0;
  uint has_a_primary_key;
  int  primary_key_field_num; 
  char old_field_value_buffer[IO_SIZE], new_field_value_buffer[IO_SIZE];
  char update_buffer[IO_SIZE], where_buffer[IO_SIZE];

  // stores the value to be replaced of the field were are updating 
  String old_field_value(old_field_value_buffer, sizeof(old_field_value_buffer), &my_charset_bin);
  old_field_value.length(0);
  // stores the new value of the field 
  String new_field_value(new_field_value_buffer, sizeof(new_field_value_buffer), &my_charset_bin);
  new_field_value.length(0);
  // stores the update query
  String update_string(update_buffer, sizeof(update_buffer), &my_charset_bin);
  update_string.length(0);
  // stores the WHERE clause 
  String where_string(where_buffer, sizeof(where_buffer), &my_charset_bin);
  where_string.length(0);

  DBUG_ENTER("ha_federated::update_row");


  has_a_primary_key= table->primary_key == 0 ? 1 : 0;
  primary_key_field_num= has_a_primary_key ? 
    table->key_info[table->primary_key].key_part->fieldnr -1 : -1;
  if (has_a_primary_key)
      DBUG_PRINT("ha_federated::update_row", ("has a primary key"));

  update_string.append("UPDATE ");
  update_string.append(share->table_base_name);
  update_string.append(" SET ");

/*
   In this loop, we want to match column names to values being inserted
   (while building INSERT statement).

   Iterate through table->field (new data) and share->old_filed (old_data) 
   using the same index to created an SQL UPDATE statement, new data is 
   used to create SET field=value and old data is used to create WHERE 
   field=oldvalue
 */
  
  for (Field **field= table->field ; *field ; field++, x++)
  {
    /*
      In all of these tests for 'has_a_primary_key', what I'm trying to
      accomplish is to only use the primary key in the WHERE clause if the
      table has a primary key, as opposed to a table without a primary key
      in which case we have to use all the fields to create a WHERE clause
      using the old/current values, as well as adding a LIMIT statement
    */
    if (has_a_primary_key) 
    {
      if (x == primary_key_field_num) 
        where_string.append((*field)->field_name);
    }
    else
      where_string.append((*field)->field_name);

    update_string.append((*field)->field_name);
    update_string.append('=');

    if ((*field)->is_null())
      new_field_value.append("NULL");
    else
    {
      // otherwise =
      (*field)->val_str(&new_field_value);
      quote_data(&new_field_value, *field);

      if ( has_a_primary_key )
      {
        if (x == primary_key_field_num)
          where_string.append("=");
      }
      else
        if (! field_in_record_is_null(table, *field, (char*) old_data))
          where_string.append("=");
    }

    if ( has_a_primary_key) 
    {
      if (x == primary_key_field_num)
      {
        (*field)->val_str(&old_field_value,
                          (char *)(old_data + (*field)->offset()));
        quote_data(&old_field_value, *field);
        where_string.append(old_field_value);
      }
    }
    else
    {
        if (field_in_record_is_null(table, *field, (char*) old_data))
          where_string.append(" IS NULL ");
        else
        {
          (*field)->val_str(&old_field_value,
                            (char *)(old_data + (*field)->offset()));
          quote_data(&old_field_value, *field);
          where_string.append(old_field_value);
        }
    }
    update_string.append(new_field_value);
    new_field_value.length(0);

    if (x+1 < table->fields)
    {
      update_string.append(", ");
      if (! has_a_primary_key)
        where_string.append(" AND ");
    }
    old_field_value.length(0);
  }
  update_string.append(" WHERE ");
  update_string.append(where_string.c_ptr_quick());
  if (! has_a_primary_key)
    update_string.append(" LIMIT 1");

  DBUG_PRINT("ha_federated::update_row", ("Final update query: %s",
                                          update_string.c_ptr_quick()));
  if (mysql_real_query(mysql, update_string.c_ptr_quick(), 
                       update_string.length()))
  {
    my_error(ER_QUERY_ON_MASTER,MYF(0),mysql_error(mysql));
    DBUG_RETURN(ER_QUERY_ON_MASTER); 
  }


  DBUG_RETURN(0);
}

/*
  This will delete a row. 'buf' will contain a copy of the row to be deleted.
  The server will call this right after the current row has been called (from
  either a previous rnd_nexT() or index call).
  If you keep a pointer to the last row or can access a primary key it will
  make doing the deletion quite a bit easier.
  Keep in mind that the server does no guarentee consecutive deletions.
  ORDER BY clauses can be used.

  Called in sql_acl.cc and sql_udf.cc to manage internal table information.
  Called in sql_delete.cc, sql_insert.cc, and sql_select.cc. In sql_select 
  it is used for removing duplicates while in insert it is used for REPLACE
  calls.
*/
int ha_federated::delete_row(const byte * buf)
{
  int x= 0;
  int quote_flag;
  char delete_buffer[IO_SIZE];
  char data_buffer[IO_SIZE];

  String delete_string(delete_buffer, sizeof(delete_buffer), &my_charset_bin);
  delete_string.length(0);
  String data_string(data_buffer, sizeof(data_buffer), &my_charset_bin);
  data_string.length(0);

  DBUG_ENTER("ha_federated::delete_row");

  delete_string.append("DELETE FROM ");
  delete_string.append(share->table_base_name);
  delete_string.append(" WHERE ");

  for (Field **field= table->field; *field; field++, x++) 
  {
    delete_string.append((*field)->field_name);

    if ((*field)->is_null())
    {
      delete_string.append(" IS ");
      data_string.append("NULL");
      quote_flag= 0;
    }
    else
    {
      delete_string.append("=");
      (*field)->val_str(&data_string);
      quote_flag= type_quote((*field)->type());
    }
  
    if (quote_flag)
      delete_string.append('\'');

    delete_string.append(data_string);
    data_string.copy(NULL);

    if (quote_flag)
      delete_string.append('\'');

    if (x+1 < table->fields)
      delete_string.append(" AND ");
  }

  delete_string.append(" LIMIT 1");
  DBUG_PRINT("ha_federated::delete_row",
             ("Delete sql: %s", delete_string.c_ptr_quick()));
  if ( mysql_real_query(mysql, delete_string.c_ptr_quick(),
                        delete_string.length()))
  {
    my_error(ER_QUERY_ON_MASTER,MYF(0),mysql_error(mysql));
    DBUG_RETURN(ER_QUERY_ON_MASTER); 
  }

  DBUG_RETURN(0);
}


/*
  Positions an index cursor to the index specified in the handle. Fetches the
  row if available. If the key value is null, begin at the first key of the
  index. This method, which is called in the case of an SQL statement having
  a WHERE clause on a non-primary key index, simply calls index_read_idx.
*/
int ha_federated::index_read(byte * buf, const byte * key,
                           uint key_len __attribute__((unused)),
                           enum ha_rkey_function find_flag
                           __attribute__((unused)))
{
  DBUG_ENTER("ha_federated::index_read");
  DBUG_RETURN(index_read_idx(buf, active_index, key, key_len, find_flag));
}


/*
  Positions an index cursor to the index specified in key. Fetches the
  row if any.  This is only used to read whole keys.

  This method is called via index_read in the case of a WHERE clause using
  a regular non-primary key index, OR is called DIRECTLY when the WHERE clause 
  uses a PRIMARY KEY index.
*/
int ha_federated::index_read_idx(byte * buf, uint index, const byte * key,
                               uint key_len __attribute__((unused)),
                               enum ha_rkey_function find_flag
                               __attribute__((unused)))
{
  char index_value[IO_SIZE];
  String index_string(index_value, sizeof(index_value), &my_charset_bin);
  index_string.length(0);

  char sql_query_buffer[IO_SIZE];
  String sql_query(sql_query_buffer, sizeof(sql_query_buffer), &my_charset_bin);
  sql_query.length(0);

  DBUG_ENTER("ha_federated::index_read_idx");
  statistic_increment(table->in_use->status_var.ha_read_key_count,&LOCK_status);

  index_string.length(0);
  sql_query.length(0);

  sql_query.append(share->select_query);
  sql_query.append(" WHERE ");
  sql_query.append(table->key_info[index].key_part->field->field_name);
  sql_query.append(" = ");

  table->key_info[index].key_part->field->val_str(&index_string, (char *)(key));
  quote_data(&index_string, table->key_info[index].key_part->field);
  sql_query.append(index_string);

  DBUG_PRINT("ha_federated::index_read_idx",
             ("sql_query %s", sql_query.c_ptr_quick()));

  if (mysql_real_query(mysql, sql_query.c_ptr_quick(), sql_query.length()))
  {
    my_error(ER_QUERY_ON_MASTER,MYF(0),mysql_error(mysql));
    DBUG_RETURN(ER_QUERY_ON_MASTER); 
  }
  result= mysql_store_result(mysql);

  if (!result)
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  if (mysql_errno(mysql))
  {
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(mysql_errno(mysql)); 
  }

  DBUG_RETURN(rnd_next(buf));
}

/*
  Initialized at each key walk (called multiple times unlike ::rnd_init())
*/
int ha_federated::index_init(uint keynr)
{
  int error;
  DBUG_ENTER("ha_federated::index_init");
  DBUG_PRINT("ha_federated::index_init",
             ("table: '%s'  key: %d", table->real_name, keynr));
  active_index= keynr;
  DBUG_RETURN(0);
}

/*
  Used to read forward through the index.
*/
int ha_federated::index_next(byte * buf)
{
  DBUG_ENTER("ha_federated::index_next");
  DBUG_RETURN(rnd_next(buf));
}


/*
  rnd_init() is called when the system wants the storage engine to do a table
  scan.

  This is the method that gets data for the SELECT calls.

  See the federated in the introduction at the top of this file to see when
  rnd_init() is called.

  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc,
  sql_table.cc, and sql_update.cc.
*/
int ha_federated::rnd_init(bool scan)
{
  DBUG_ENTER("ha_federated::rnd_init");
  int num_fields, rows;

  DBUG_PRINT("ha_federated::rnd_init",
             ("share->select_query %s", share->select_query));
  if (mysql_real_query(mysql, share->select_query, strlen(share->select_query))) 
  {
    my_error(ER_QUERY_ON_MASTER,MYF(0),mysql_error(mysql));
    DBUG_RETURN(ER_QUERY_ON_MASTER); 
  }
  result= mysql_store_result(mysql);

  if (mysql_errno(mysql))
    DBUG_RETURN(mysql_errno(mysql)); 
  DBUG_RETURN(0);
}

int ha_federated::rnd_end()
{
  DBUG_ENTER("ha_federated::rnd_end");
  mysql_free_result(result);
  DBUG_RETURN(index_end());
}

int ha_federated::index_end(void)
{
  DBUG_ENTER("ha_federated::index_end");
  active_index= MAX_KEY;
  DBUG_RETURN(0);
}

/*
  This is called for each row of the table scan. When you run out of records
  you should return HA_ERR_END_OF_FILE. Fill buff up with the row information.
  The Field structure for the table is the key to getting data into buf
  in a manner that will allow the server to understand it.

  Called from filesort.cc, records.cc, sql_handler.cc, sql_select.cc,
  sql_table.cc, and sql_update.cc.
*/
int ha_federated::rnd_next(byte *buf)
{
  MYSQL_ROW row;
  DBUG_ENTER("ha_federated::rnd_next");

  // Fetch a row, insert it back in a row format.
  current_position= result->data_cursor;
  if (! (row= mysql_fetch_row(result)))
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  
  DBUG_RETURN(convert_row_to_internal_format(buf,row));
}


/*
  'position()' is called after each call to rnd_next() if the data needs to be
  ordered. You can do something like the following to store the position:
  ha_store_ptr(ref, ref_length, current_position);

  The server uses ref to store data. ref_length in the above case is the size
  needed to store current_position. ref is just a byte array that the server
  will maintain. If you are using offsets to mark rows, then current_position
  should be the offset. If it is a primary key like in BDB, then it needs to
  be a primary key.

  Called from filesort.cc, sql_select.cc, sql_delete.cc and sql_update.cc.
*/
void ha_federated::position(const byte *record)
{
  DBUG_ENTER("ha_federated::position");
  //ha_store_ptr Add seek storage
  ha_store_ptr(ref, ref_length, current_position);
  DBUG_VOID_RETURN;
}


/*
  This is like rnd_next, but you are given a position to use to determine the
  row. The position will be of the type that you stored in ref. You can use
  ha_get_ptr(pos,ref_length) to retrieve whatever key or position you saved
  when position() was called.

  This method is required for an ORDER BY.

  Called from filesort.cc records.cc sql_insert.cc sql_select.cc sql_update.cc.
*/
int ha_federated::rnd_pos(byte * buf, byte *pos)
{
  DBUG_ENTER("ha_federated::rnd_pos");
  statistic_increment(table->in_use->status_var.ha_read_rnd_count,&LOCK_status);
  current_position= ha_get_ptr(pos,ref_length);
  result->current_row= 0;
  result->data_cursor= current_position;
  DBUG_RETURN(rnd_next(buf));
}


/*
  ::info() is used to return information to the optimizer.
  Currently this table handler doesn't implement most of the fields
  really needed. SHOW also makes use of this data
  Another note, you will probably want to have the following in your
  code:
  if (records < 2)
    records = 2;
  The reason is that the server will optimize for cases of only a single
  record. If in a table scan you don't know the number of records
  it will probably be better to set records to two so you can return
  as many records as you need.
  Along with records a few more variables you may wish to set are:
    records
    deleted
    data_file_length
    index_file_length
    delete_length
    check_time
  Take a look at the public variables in handler.h for more information.

  Called in:
    filesort.cc
    ha_heap.cc
    item_sum.cc
    opt_sum.cc
    sql_delete.cc
    sql_delete.cc
    sql_derived.cc
    sql_select.cc
    sql_select.cc
    sql_select.cc
    sql_select.cc
    sql_select.cc
    sql_show.cc
    sql_show.cc
    sql_show.cc
    sql_show.cc
    sql_table.cc
    sql_union.cc
    sql_update.cc

*/
// FIX: later version provide better information to the optimizer
void ha_federated::info(uint flag)
{
  DBUG_ENTER("ha_federated::info");
  records= 10000; // Fake!
  DBUG_VOID_RETURN;
}


/*
  Used to delete all rows in a table. Both for cases of truncate and
  for cases where the optimizer realizes that all rows will be
  removed as a result of a SQL statement.

  Called from item_sum.cc by Item_func_group_concat::clear(),
  Item_sum_count_distinct::clear(), and Item_func_group_concat::clear().
  Called from sql_delete.cc by mysql_delete().
  Called from sql_select.cc by JOIN::reinit().
  Called from sql_union.cc by st_select_lex_unit::exec().
*/
int ha_federated::delete_all_rows()
{
  DBUG_ENTER("ha_federated::delete_all_rows");

  char query_buffer[IO_SIZE];
  String query(query_buffer, sizeof(query_buffer), &my_charset_bin);
  query.length(0);

  query.set_charset(system_charset_info);
  query.append("TRUNCATE ");
  query.append(share->table_base_name);

  if (mysql_real_query(mysql, query.c_ptr_quick(), query.length())) {
    my_error(ER_QUERY_ON_MASTER,MYF(0),mysql_error(mysql));
    DBUG_RETURN(ER_QUERY_ON_MASTER); 
  }

  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}


/*
  The idea with handler::store_lock() is the following:

  The statement decided which locks we should need for the table
  for updates/deletes/inserts we get WRITE locks, for SELECT... we get
  read locks.

  Before adding the lock into the table lock handler (see thr_lock.c)
  mysqld calls store lock with the requested locks.  Store lock can now
  modify a write lock to a read lock (or some other lock), ignore the
  lock (if we don't want to use MySQL table locks at all) or add locks
  for many tables (like we do when we are using a MERGE handler).

  Berkeley DB for federated  changes all WRITE locks to TL_WRITE_ALLOW_WRITE
  (which signals that we are doing WRITES, but we are still allowing other
  reader's and writer's.

  When releasing locks, store_lock() are also called. In this case one
  usually doesn't have to do anything.

  In some exceptional cases MySQL may send a request for a TL_IGNORE;
  This means that we are requesting the same lock as last time and this
  should also be ignored. (This may happen when someone does a flush
  table when we have opened a part of the tables, in which case mysqld
  closes and reopens the tables and tries to get the same locks at last
  time).  In the future we will probably try to remove this.

  Called from lock.cc by get_lock_data().
*/
THR_LOCK_DATA **ha_federated::store_lock(THD *thd,
                                       THR_LOCK_DATA **to,
                                       enum thr_lock_type lock_type)
{
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK) 
  {
    /* 
      Here is where we get into the guts of a row level lock.
      If TL_UNLOCK is set 
      If we are not doing a LOCK TABLE or DISCARD/IMPORT
      TABLESPACE, then allow multiple writers 
    */

    if ((lock_type >= TL_WRITE_CONCURRENT_INSERT &&
         lock_type <= TL_WRITE) && !thd->in_lock_tables
        && !thd->tablespace_op)
      lock_type= TL_WRITE_ALLOW_WRITE;

    /* 
      In queries of type INSERT INTO t1 SELECT ... FROM t2 ...
      MySQL would use the lock TL_READ_NO_INSERT on t2, and that
      would conflict with TL_WRITE_ALLOW_WRITE, blocking all inserts
      to t2. Convert the lock to a normal read lock to allow
      concurrent inserts to t2. 
    */

    if (lock_type == TL_READ_NO_INSERT && !thd->in_lock_tables) 
      lock_type= TL_READ;

    lock.type= lock_type;
  }

  *to++= &lock;

  return to;
}

/*
  create() does nothing, since we have no local setup of our own.
  FUTURE: We should potentially connect to the remote database and
  create tables if they do not exist.
*/
int ha_federated::create(const char *name, TABLE *table_arg,
                       HA_CREATE_INFO *create_info)
{
  DBUG_ENTER("ha_federated::create");
  DBUG_RETURN(0);
}
#endif /* HAVE_FEDERATED_DB */
