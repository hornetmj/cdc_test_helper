#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cas_cci.h"
#include "cubrid_log.h"

#define NO_ERROR (0)
#define YES_ERROR (-1)

#define PRINT_ERRMSG_GOTO_ERR(error_code) printf ("[ERROR] error_code: %d at %s():%d\n", error_code, __func__, __LINE__); goto error

enum
{
  INTEGER = 1,
  FLOAT = 2,
  DOUBLE = 3,
  STRING = 4,
  OBJECT = 5,
  SET = 6,
  MULTISET = 7,
  SEQUENCE = 8,
  ELO = 9,
  TIME = 10,
  TIMESTAMP = 11,
  DATE = 12,
  SHORT = 18,
  NUMERIC = 22,
  BIT = 23,
  VARBIT = 24,
  CHAR = 25,
  BIGINT = 31,
  DATETIME = 32,
  BLOB = 33,
  CLOB = 34,
  ENUM = 35,
  TIMESTAMPTZ = 36,
  TIMESTAMPLTZ = 37,
  DATETIMETZ = 38,
  DATETIMELTZ = 39
};

typedef struct src_db_global SRC_DB_GLOBAL;
struct src_db_global
{
  char *host;
  char *port;
  char *user;
  char *pw;

  int connection_timeout;	// -1 ~ 360 (def: 300)
  int extraction_timeout;	// -1 ~ 360 (def: 300)
  int max_log_item;		// 1 ~ 1024 (def: 512)
  int all_in_cond;		// 0 ~ 1 (def: 0)

  uint64_t *extraction_table;	// classoid array
  int extraction_table_size;

  char **extraction_user;
  int extraction_user_size;

  char *trace_path;
  int trace_level;
  int trace_size;

  int conn_handle;
  int req_handle;
};

typedef struct target_db_global TARGET_DB_GLOBAL;
struct target_db_global
{

};

typedef struct class_oid CLASS_OID;
struct class_oid
{
  int pageid;
  short slotid;
  short volid;
};

typedef struct attr_info ATTR_INFO;
struct attr_info
{
  char *attr_name;
  int attr_type;

  int def_order;
  int is_nullable;
  int is_primary_key;
};

typedef struct class_info CLASS_INFO;
struct class_info
{
  char *class_name;
  uint64_t class_oid;

  ATTR_INFO attr_info[50];
  int attr_info_size;
};

typedef struct class_info_global CLASS_INFO_GLOBAL;
struct class_info_global
{
  CLASS_INFO class_info[10000];
  int class_info_size;
};

SRC_DB_GLOBAL src_db_Gl;
TARGET_DB_GLOBAL target_db_Gl;

CLASS_INFO_GLOBAL class_info_Gl;

void
print_usages (void)
{
  // printf ("cdc_test_helper\n");
  // s_host=
  // s_port=
  // s_user=
  // s_pw=
}

int
process_command_line_option (int argc, char *argv[])
{
  if (argc == 1)
    {
      print_usages ();
    }

  return NO_ERROR;
}

/*
 * convert the string type of class_oid returned by cci to the uint64_t type.
 *
 * @pageid|slotid|volid -> uint64_t
 * ex) @195|19|0 -> 81604378819
 */
uint64_t
convert_class_oid_to_uint64 (char *class_oid)
{
  char buf[1024];
  char *cur_pos, *next_pos;

  CLASS_OID class_oid_src;
  uint64_t class_oid_dest;

  strncpy (buf, class_oid + 1, strlen (class_oid));

  cur_pos = buf;
  next_pos = strstr (cur_pos, "|");
  assert (next_pos != NULL);

  *next_pos = '\0';

  class_oid_src.pageid = atoi (cur_pos);

  cur_pos = next_pos + 1;
  next_pos = strstr (cur_pos, "|");
  assert (next_pos != NULL);

  *next_pos = '\0';

  class_oid_src.slotid = atoi (cur_pos);

  cur_pos = next_pos + 1;
  next_pos = strstr (cur_pos, "|");
  assert (next_pos == NULL);

  class_oid_src.volid = atoi (cur_pos);

  memcpy (&class_oid_dest, &class_oid_src, sizeof (class_oid_dest));

#if 1
  printf ("class_oid: %s, class_oid_src: @%d|%hd|%hd, class_oid_dest: %lld\n", class_oid, class_oid_src.pageid,
	  class_oid_src.slotid, class_oid_src.volid, class_oid_dest);
#endif

  return class_oid_dest;
}

int
make_class_info (CLASS_INFO * class_info, char *class_name, uint64_t class_oid)
{
  class_info->class_name = strdup (class_name);
  assert (class_info->class_name != NULL);

  class_info->class_oid = class_oid;

  return NO_ERROR;
}

int
make_attr_info (ATTR_INFO * attr_info, char *attr_name, int attr_type, int def_order, int is_nullable,
		int is_primary_key)
{
  attr_info->attr_name = strdup (attr_name);
  assert (attr_info->attr_name != NULL);

  attr_info->attr_type = attr_type;
  attr_info->def_order = def_order;
  attr_info->is_nullable = is_nullable;
  attr_info->is_primary_key = is_primary_key;

  return NO_ERROR;
}

int
fetch_all_schema_info (void)
{
  int conn_handle, req_handle;
  int exec_retval;
  T_CCI_ERROR err_buf;

  char *class_oid, *class_name;
  int indicator;

  uint64_t class_oid_2;

  int error_code;

  //conn_handle = cci_connect ("localhost", 33000, "demodb", "dba", "");
  conn_handle = cci_connect ("127.0.0.1", 33000, "demodb", "dba", "");
  if (conn_handle < 0)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  req_handle =
    cci_prepare_and_execute (conn_handle, "select class_of, class_name from _db_class where is_system_class != 1", 0,
			     &exec_retval, &err_buf);
  if (req_handle < 0)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  class_info_Gl.class_info_size = 0;

  // class info
  for (int i = 0; i < exec_retval; i++)
    {
      error_code = cci_cursor (req_handle, 1, CCI_CURSOR_CURRENT, &err_buf);
      if (error_code < 0)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = cci_fetch (req_handle, &err_buf);
      if (error_code < 0)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = cci_get_data (req_handle, 1, CCI_A_TYPE_STR, &class_oid, &indicator);
      if (error_code < 0)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = cci_get_data (req_handle, 2, CCI_A_TYPE_STR, &class_name, &indicator);
      if (error_code < 0)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      class_oid_2 = convert_class_oid_to_uint64 (class_oid);

#if 1
      printf ("class_name: %s, class_oid: %s, class_oid_2: %lld\n", class_name, class_oid, class_oid_2);
#endif

      error_code = make_class_info (&class_info_Gl.class_info[i], class_name, class_oid_2);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      class_info_Gl.class_info_size++;
      class_info_Gl.class_info[i].attr_info_size = 0;

      // attr infos for each class info
      {
	int req_handle_2, exec_retval_2;

	int def_order, data_type, is_nullable, is_primary_key;
	char *attr_name;

	req_handle_2 =
	  cci_prepare (conn_handle,
		       "select a.attr_name, a.def_order, a.data_type, (select prec from _db_domain d where d in a.domains) as prec, a.is_nullable, (case when a.attr_name in (select key_attr_name from _db_index_key k where k in b.key_attrs) and b.is_primary_key = 1 then 1 else 0 end) as is_primary_key from _db_attribute a, _db_index b where a.class_of.is_system_class != 1 and a.class_of = b.class_of and a.class_of.class_name = ? order by a.def_order",
		       0, &err_buf);
	if (req_handle_2 < 0)
	  {
	    PRINT_ERRMSG_GOTO_ERR (error_code);
	  }

	error_code = cci_bind_param (req_handle_2, 1, CCI_A_TYPE_STR, class_name, CCI_U_TYPE_STRING, CCI_BIND_PTR);
	if (error_code < 0)
	  {
	    PRINT_ERRMSG_GOTO_ERR (error_code);
	  }

	exec_retval_2 = cci_execute (req_handle_2, 0, 0, &err_buf);
	if (exec_retval_2 < 0)
	  {
	    PRINT_ERRMSG_GOTO_ERR (error_code);
	  }

	for (int j = 0; j < exec_retval_2; j++)
	  {
	    error_code = cci_cursor (req_handle_2, 1, CCI_CURSOR_CURRENT, &err_buf);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    error_code = cci_fetch (req_handle_2, &err_buf);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    // attr_name
	    error_code = cci_get_data (req_handle_2, 1, CCI_A_TYPE_STR, &attr_name, &indicator);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    // def_order
	    error_code = cci_get_data (req_handle_2, 2, CCI_A_TYPE_INT, &def_order, &indicator);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    // data_type 
	    error_code = cci_get_data (req_handle_2, 3, CCI_A_TYPE_INT, &data_type, &indicator);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    // prec

	    // is_nullable
	    error_code = cci_get_data (req_handle_2, 5, CCI_A_TYPE_INT, &is_nullable, &indicator);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    // is_primary_key
	    error_code = cci_get_data (req_handle_2, 6, CCI_A_TYPE_INT, &is_primary_key, &indicator);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    error_code =
	      make_attr_info (&class_info_Gl.class_info[i].attr_info[j], attr_name, data_type, def_order, is_nullable,
			      is_primary_key);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

#if 1
	    printf ("attr_name: %s, def_order: %d, data_type: %d\n", attr_name, def_order, data_type);
#endif

	    class_info_Gl.class_info[i].attr_info_size++;
	  }

	error_code = cci_close_query_result (req_handle_2, &err_buf);
	if (error_code < 0)
	  {
	    PRINT_ERRMSG_GOTO_ERR (error_code);
	  }

	error_code = cci_close_req_handle (req_handle_2);
	if (error_code < 0)
	  {
	    PRINT_ERRMSG_GOTO_ERR (error_code);
	  }
      }
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

char *
convert_data_item_type_to_string (int data_item_type)
{
  switch (data_item_type)
    {
    case 0:
      return "DDL";
    case 1:
      return "DML";
    case 2:
      return "DCL";
    case 3:
      return "TIMER";
    default:
      assert (0);
    }
}

char *
convert_ddl_type_to_string (int ddl_type)
{
  switch (ddl_type)
    {
    case 0:
      return "create";
    case 1:
      return "alter";
    case 2:
      return "drop";
    case 3:
      return "rename";
    case 4:
      return "truncate";
    default:
      assert (0);
    }
}

char *
convert_object_type_to_string (int object_type)
{
  switch (object_type)
    {
    case 0:
      return "table";
    case 1:
      return "index";
    case 2:
      return "serial";
    case 3:
      return "view";
    case 4:
      return "function";
    case 5:
      return "procedure";
    case 6:
      return "trigger";
    default:
      assert (0);
    }
}

int
print_ddl (CUBRID_DATA_ITEM * data_item)
{
  printf ("ddl_type: %d (%s)\n", data_item->ddl.ddl_type, convert_ddl_type_to_string (data_item->ddl.ddl_type));
  printf ("object_type: %d (%s)\n", data_item->ddl.object_type,
	  convert_object_type_to_string (data_item->ddl.object_type));
  printf ("oid: %lld\n", data_item->ddl.oid);
  printf ("classoid: %lld\n", data_item->ddl.classoid);
  printf ("statement: %s\n", data_item->ddl.statement);
  printf ("statement_length: %d\n", data_item->ddl.statement_length);
  return NO_ERROR;
}

char *
convert_dml_type_to_string (int dml_type)
{
  switch (dml_type)
    {
    case 0:
      return "insert";
    case 1:
      return "update";
    case 2:
      return "delete";
    default:
      assert (0);
    }
}

int
print_dml (CUBRID_DATA_ITEM * data_item)
{
  printf ("dml_type: %d (%s)\n", data_item->dml.dml_type, convert_dml_type_to_string (data_item->dml.dml_type));
  printf ("classoid: %lld\n", data_item->dml.classoid);
  printf ("num_changed_column: %d\n", data_item->dml.num_changed_column);
  printf ("changed_column:\n");
  for (int i = 0; i < data_item->dml.num_changed_column; i++)
    {
      printf ("[%d] data (%d)\n", data_item->dml.changed_column_index[i], data_item->dml.changed_column_data_len[i]);
    }

  printf ("num_cond_column: %d\n", data_item->dml.num_cond_column);
  printf ("cond_column:\n");
  for (int i = 0; i < data_item->dml.num_cond_column; i++)
    {
      printf ("[%d] data (%d)\n", data_item->dml.cond_column_index[i], data_item->dml.cond_column_data_len[i]);
    }
}

char *
convert_dcl_type_to_string (int dcl_type)
{
  switch (dcl_type)
    {
    case 0:
      return "commit";
    case 1:
      return "rollback";
    default:
      assert (0);
    }
}

int
print_dcl (CUBRID_DATA_ITEM * data_item)
{
  printf ("dcl_type: %d (%s)\n", data_item->dcl.dcl_type, convert_dcl_type_to_string (data_item->dcl.dcl_type));
  printf ("timestamp: %s", ctime (&data_item->dcl.timestamp));
}

int
print_timer (CUBRID_DATA_ITEM * data_item)
{
  printf ("timestamp: %s", ctime (&data_item->timer.timestamp));
}

int
print_log_item (CUBRID_LOG_ITEM * log_item)
{
  int error_code;
#if 1
  if (log_item->data_item_type == 3)
    {
      return NO_ERROR;
    }
#endif

  printf ("=====================================================================================\n");
  printf ("[LOG_ITEM]\n");
  printf ("transaction_id: %d\n", log_item->transaction_id);
  printf ("user: %s\n", log_item->user);
  printf ("data_item_type: %d (%s)\n\n", log_item->data_item_type,
	  convert_data_item_type_to_string (log_item->data_item_type));
  printf ("[DATA_ITEM]\n");
  switch (log_item->data_item_type)
    {
    case 0:
      print_ddl (&log_item->data_item);
      break;
    case 1:
      print_dml (&log_item->data_item);
      break;
    case 2:
      print_dcl (&log_item->data_item);
      break;
    case 3:
      print_timer (&log_item->data_item);
      break;
    default:
      assert (0);
    }

  printf ("=====================================================================================\n\n");

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
convert_ddl (CUBRID_DATA_ITEM * data_item, char **sql)
{
  switch (data_item->ddl.ddl_type)
    {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
      break;
    default:
      assert (0);
    }

  *sql = strndup (data_item->ddl.statement, data_item->ddl.statement_length);

  return *sql != NULL ? NO_ERROR : YES_ERROR;
}

CLASS_INFO *
find_class_info (uint64_t class_oid)
{
  int i;
  CLASS_INFO *class_info;

  for (i = 0; i < class_info_Gl.class_info_size; i++)
    {
      class_info = &class_info_Gl.class_info[i];

      if (class_info->class_oid == class_oid)
	{
	  return class_info;
	}
    }

  return NULL;
}

ATTR_INFO *
find_attr_info (CLASS_INFO * class_info, int def_order)
{
  int i;
  ATTR_INFO *attr_info;

  for (i = 0; i < class_info->attr_info_size; i++)
    {
      attr_info = &class_info->attr_info[i];

      if (attr_info->def_order == def_order)
	{
	  return attr_info;
	}
    }

  return NULL;
}

int
process_changed_column (CUBRID_DATA_ITEM * data_item, int col_idx, ATTR_INFO * attr_info, char *sql_buf,
			int *cant_make_sql)
{
  int error_code;

  if (data_item->dml.changed_column_data[col_idx] == NULL && data_item->dml.changed_column_data_len[col_idx] == 0)
    {
      if (!attr_info->is_nullable)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      strcat (sql_buf, "NULL");
    }
  else
    {
      switch (attr_info->attr_type)
	{
	case INTEGER:
	  {
	    int value;

	    assert (sizeof (value) == data_item->dml.changed_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.changed_column_data[col_idx],
		    data_item->dml.changed_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%d", value);
	  }

	  break;

	case FLOAT:
	  {
	    float value;

	    assert (sizeof (value) == data_item->dml.changed_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.changed_column_data[col_idx],
		    data_item->dml.changed_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%f", value);
	  }

	  break;

	case DOUBLE:
	  {
	    double value;

	    assert (sizeof (value) == data_item->dml.changed_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.changed_column_data[col_idx],
		    data_item->dml.changed_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%lf", value);
	  }

	  break;

	case STRING:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	case OBJECT:
	case SET:
	case MULTISET:
	case SEQUENCE:
	case ELO:
	  *cant_make_sql = 1;

	  break;

	case TIME:
	case TIMESTAMP:
	case DATE:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	case SHORT:
	  {
	    short value;

	    assert (sizeof (value) == data_item->dml.changed_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.changed_column_data[col_idx],
		    data_item->dml.changed_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%d", value);
	  }

	  break;

	case NUMERIC:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "%s", value);
	  }

	  break;

	case BIT:
	case VARBIT:
	case CHAR:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	case BIGINT:
	  {
	    int64_t value;

	    assert (sizeof (value) == data_item->dml.changed_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.changed_column_data[col_idx],
		    data_item->dml.changed_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%lld", value);
	  }

	  break;

	case DATETIME:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	case BLOB:
	case CLOB:
	  *cant_make_sql = 1;

	  break;

	case ENUM:
	case TIMESTAMPTZ:
	case TIMESTAMPLTZ:
	case DATETIMETZ:
	case DATETIMELTZ:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	default:
	  assert (0);
	}
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
process_cond_column (CUBRID_DATA_ITEM * data_item, int col_idx, ATTR_INFO * attr_info, char *sql_buf,
		     int *cant_make_sql)
{
  int error_code;

  if (data_item->dml.cond_column_data[col_idx] == NULL && data_item->dml.cond_column_data_len[col_idx] == 0)
    {
      if (!attr_info->is_nullable)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      strcat (sql_buf, "NULL");
    }
  else
    {
      switch (attr_info->attr_type)
	{
	case INTEGER:
	  {
	    int value;

	    assert (sizeof (value) == data_item->dml.cond_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.cond_column_data[col_idx], data_item->dml.cond_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%d", value);
	  }

	  break;

	case FLOAT:
	  {
	    float value;

	    assert (sizeof (value) == data_item->dml.cond_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.cond_column_data[col_idx], data_item->dml.cond_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%f", value);
	  }

	  break;

	case DOUBLE:
	  {
	    double value;

	    assert (sizeof (value) == data_item->dml.cond_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.cond_column_data[col_idx], data_item->dml.cond_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%lf", value);
	  }

	  break;

	case STRING:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	case OBJECT:
	case SET:
	case MULTISET:
	case SEQUENCE:
	case ELO:
	  *cant_make_sql = 1;

	  break;

	case TIME:
	case TIMESTAMP:
	case DATE:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	case SHORT:
	  {
	    short value;

	    assert (sizeof (value) == data_item->dml.cond_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.cond_column_data[col_idx], data_item->dml.cond_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%d", value);
	  }

	  break;

	case NUMERIC:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "%s", value);
	  }

	  break;

	case BIT:
	case VARBIT:
	case CHAR:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	case BIGINT:
	  {
	    int64_t value;

	    assert (sizeof (value) == data_item->dml.cond_column_data_len[col_idx]);

	    memcpy (&value, data_item->dml.cond_column_data[col_idx], data_item->dml.cond_column_data_len[col_idx]);

	    sprintf (sql_buf + strlen (sql_buf), "%lld", value);
	  }

	  break;

	case DATETIME:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	case BLOB:
	case CLOB:
	  *cant_make_sql = 1;

	  break;

	case ENUM:
	case TIMESTAMPTZ:
	case TIMESTAMPLTZ:
	case DATETIMETZ:
	case DATETIMELTZ:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    sprintf (sql_buf + strlen (sql_buf), "\'%s\'", value);
	  }

	  break;

	default:
	  assert (0);
	}
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
make_insert_stmt (CUBRID_DATA_ITEM * data_item, char **sql)
{
  int i;
  char sql_buf[10000] = { '\0', };
  CLASS_INFO *class_info;
  ATTR_INFO *attr_info;

  int cant_make_sql = 0;
  int error_code;

  class_info = find_class_info (data_item->dml.classoid);
  if (class_info == NULL)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  assert (class_info->attr_info_size == data_item->dml.num_changed_column);

  sprintf (sql_buf, "insert into %s (", class_info->class_name);

  for (i = 0; i < data_item->dml.num_changed_column; i++)
    {
      attr_info = &class_info->attr_info[i];

      strcat (sql_buf, attr_info->attr_name);

      if (i != class_info->attr_info_size - 1)
	{
	  strcat (sql_buf, ", ");
	}
      else
	{
	  strcat (sql_buf, ") values (");
	}
    }

  for (i = 0; i < data_item->dml.num_changed_column; i++)
    {
      if (cant_make_sql)
	{
	  // Because of data types not supported by cdc API.
	  break;
	}

      attr_info = &class_info->attr_info[i];

      if (attr_info->def_order != data_item->dml.changed_column_index[i])
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = process_changed_column (data_item, i, attr_info, sql_buf, &cant_make_sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      if (i != class_info->attr_info_size - 1)
	{
	  strcat (sql_buf, ", ");
	}
      else
	{
	  strcat (sql_buf, ")");
	}
    }

  if (cant_make_sql)
    {
      *sql = strdup ("NULL");
    }
  else
    {
      *sql = strdup (sql_buf);
    }

  assert (*sql != NULL);

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
make_update_stmt (CUBRID_DATA_ITEM * data_item, char **sql)
{
  int i;
  char sql_buf[10000] = { '\0', };
  CLASS_INFO *class_info;
  ATTR_INFO *attr_info;

  int cant_make_sql = 0;
  int error_code;

  class_info = find_class_info (data_item->dml.classoid);
  if (class_info == NULL)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  assert (class_info->attr_info_size >= data_item->dml.num_changed_column);

  sprintf (sql_buf, "update %s set ", class_info->class_name);

  // set
  for (i = 0; i < data_item->dml.num_changed_column; i++)
    {
      if (cant_make_sql)
	{
	  // Because of data types not supported by cdc API.
	  break;
	}

      attr_info = find_attr_info (class_info, data_item->dml.changed_column_index[i]);
      if (attr_info == NULL)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      sprintf (sql_buf + strlen (sql_buf), "%s = ", attr_info->attr_name);

      error_code = process_changed_column (data_item, i, attr_info, sql_buf, &cant_make_sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      if (i != data_item->dml.num_changed_column - 1)
	{
	  strcat (sql_buf, ", ");
	}
    }

  if (cant_make_sql)
    {
      goto end;
    }
  else
    {
      strcat (sql_buf, " where ");
    }

  // where
  for (i = 0; i < data_item->dml.num_cond_column; i++)
    {
      if (cant_make_sql)
	{
	  // Because of data types not supported by cdc API.
	  break;
	}

      attr_info = find_attr_info (class_info, data_item->dml.cond_column_index[i]);
      if (attr_info == NULL)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      sprintf (sql_buf + strlen (sql_buf), "%s = ", attr_info->attr_name);

      error_code = process_cond_column (data_item, i, attr_info, sql_buf, &cant_make_sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      if (i != data_item->dml.num_cond_column - 1)
	{
	  strcat (sql_buf, "and ");
	}
      else
	{
	  strcat (sql_buf, " limit 1");
	}
    }

end:

  if (cant_make_sql)
    {
      *sql = strdup ("NULL");
    }
  else
    {
      *sql = strdup (sql_buf);
    }

  assert (*sql != NULL);

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
make_delete_stmt (CUBRID_DATA_ITEM * data_item, char **sql)
{
  int i;
  char sql_buf[10000] = { '\0', };
  CLASS_INFO *class_info;
  ATTR_INFO *attr_info;

  int cant_make_sql = 0;
  int error_code;

  class_info = find_class_info (data_item->dml.classoid);
  if (class_info == NULL)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  assert (class_info->attr_info_size >= data_item->dml.num_cond_column);

  sprintf (sql_buf, "delete from %s where ", class_info->class_name);

  for (i = 0; i < data_item->dml.num_cond_column; i++)
    {
      if (cant_make_sql)
	{
	  // Because of data types not supported by cdc API.
	  break;
	}

      attr_info = find_attr_info (class_info, data_item->dml.cond_column_index[i]);
      if (attr_info == NULL)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      sprintf (sql_buf + strlen (sql_buf), "%s = ", attr_info->attr_name);

      error_code = process_cond_column (data_item, i, attr_info, sql_buf, &cant_make_sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      if (i != data_item->dml.num_cond_column - 1)
	{
	  strcat (sql_buf, " and ");
	}
      else
	{
	  strcat (sql_buf, " limit 1");
	}
    }

  if (cant_make_sql)
    {
      *sql = strdup ("NULL");
    }
  else
    {
      *sql = strdup (sql_buf);
    }

  assert (*sql != NULL);

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
convert_dml (CUBRID_DATA_ITEM * data_item, char **sql)
{
  int error_code;

  switch (data_item->dml.dml_type)
    {
    case 0:
      error_code = make_insert_stmt (data_item, sql);

      break;

    case 1:
      error_code = make_update_stmt (data_item, sql);

      break;

    case 2:
      error_code = make_delete_stmt (data_item, sql);

      break;

    default:
      assert (0);
    }

  return error_code == NO_ERROR ? NO_ERROR : YES_ERROR;
}

int
convert_dcl (CUBRID_DATA_ITEM * data_item, char **sql)
{
  switch (data_item->dcl.dcl_type)
    {
    case 0:
      *sql = strdup ("commit");
      break;
    case 1:
      *sql = strdup ("rollback");
      break;
    default:
      assert (0);
    }

  return *sql != NULL ? NO_ERROR : YES_ERROR;
}

int
convert_log_item_to_sql (CUBRID_LOG_ITEM * log_item)
{
  char *sql;
  int error_code;

  switch (log_item->data_item_type)
    {
    case 0:
      error_code = convert_ddl (&log_item->data_item, &sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

    case 1:
      error_code = convert_dml (&log_item->data_item, &sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

    case 2:
      error_code = convert_dcl (&log_item->data_item, &sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

    case 3:
      break;

    default:
      assert (0);
    }

  if (log_item->data_item_type != 3)
    {
      printf ("=====================================================================================\n");
      printf ("[SQL]\n");
      printf ("transaction_id: %d\n", log_item->transaction_id);
      printf ("sql: %s\n", sql);
      printf ("=====================================================================================\n\n");
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
extract_log (void)
{
  time_t start_time;
  uint64_t extract_lsa;

  int error_code;

  // -1 ~ 360 (300)
  error_code = cubrid_log_set_connection_timeout (300);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  // -1 ~ 360 (300)
  error_code = cubrid_log_set_extraction_timeout (300);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  // dir path (".")
  // 0 ~ 2 (0)
  // 10 ~ 512 (8)
  error_code = cubrid_log_set_tracelog ("./tracelog.err", 0, 8);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  // 1 ~ 1024 (512)
  error_code = cubrid_log_set_max_log_item (512);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  // 0 ~ 1 (0)
  error_code = cubrid_log_set_all_in_cond (0);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  error_code = cubrid_log_set_extraction_table (NULL, 0);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  error_code = cubrid_log_set_extraction_user (NULL, 0);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  //error_code = cubrid_log_connect_server ("localhost", 1523, "demodb", "dba", "");
  error_code = cubrid_log_connect_server ("127.0.0.1", 1523, "demodb", "dba", "");
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  start_time = time (NULL);

  error_code = cubrid_log_find_lsa (&start_time, &extract_lsa);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  printf ("[PASS] cubrid_log_find_lsa ()\n");

  {
    CUBRID_LOG_ITEM *log_item_list, *log_item;
    int list_size;

    while (1)
      {
	error_code = cubrid_log_extract (&extract_lsa, &log_item_list, &list_size);
	if (error_code != CUBRID_LOG_SUCCESS && error_code != CUBRID_LOG_SUCCESS_WITH_NO_LOGITEM)
	  {
	    PRINT_ERRMSG_GOTO_ERR (error_code);
	  }

#if 0
	printf ("[PASS] cubrid_log_extract ()\n");
	//printf ("list_size: %d\n", list_size);
#endif

	log_item = log_item_list;

	while (log_item != NULL)
	  {
	    error_code = print_log_item (log_item);
	    if (error_code != NO_ERROR)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    error_code = convert_log_item_to_sql (log_item);
	    if (error_code != NO_ERROR)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    log_item = log_item->next;
	  }
      }
  }

  return NO_ERROR;
error:

  return YES_ERROR;
}

int
main (int argc, char *argv[])
{
  int error_code;

  error_code = process_command_line_option (argc, argv);
  if (error_code != NO_ERROR)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  error_code = fetch_all_schema_info ();
  if (error_code != NO_ERROR)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

#if 0
  printf ("[PASS] fetch_all_schema_info ()\n");
#endif

  error_code = extract_log ();
  if (error_code != NO_ERROR)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

#if 0
1. ? ? ? 령행인자처리--source DB ? ? ? 속정보-- -ip, port, db user,
  pw-- cubrid_log_set_ *
  설정가능한모든정보--target DB ? ? ? 속정보2. ? ? ?
  키마추출--cci ? ? ? 속--_db_class ? ? ? 회->cdc ? ? ? 뉴얼참고--class ? ? ? 보저장->
  class oid ? ? ? 키로사용(vector ? ? ? 는libcubridcs.so ? ? ? 서이용가능한자료구조활용)
     3. cdc ? ? ? 출--LOG_ITEM ? ? ? 력(데이터타입별처리가능해야함)-- - raw format-- -
  sql format-- transaction ? ? ? 그룹핑가능해야함--LOG_ITEM->
  sql ? ? ? 환가능해야함4. target DB ? ? ? 반영--cci program
#endif