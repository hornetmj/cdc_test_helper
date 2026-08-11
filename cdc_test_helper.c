#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>

#include "cas_cci.h"
#include "cubrid_log.h"

#define NO_ERROR (0)
#define YES_ERROR (-1)

#define PRINT_ERRMSG_GOTO_ERR(error_code) printf ("[ERROR] error_code: %d at %s ():%d\n", error_code, __func__, __LINE__); goto error

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
  MONETARY = 13,
  SHORT = 18,
  NUMERIC = 22,
  BIT = 23,
  VARBIT = 24,
  CHAR = 25,
  NCHAR = 26,
  VARNCHAR = 27,
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

enum
{
  OUTPUT_LOG_TYPE_ALL = -1,
  OUTPUT_LOG_TYPE_DDL = 0,
  OUTPUT_LOG_TYPE_DML = 1
};

typedef struct helper_global HELPER_GLOBAL;
struct helper_global
{
  char *cdc_server_ip;
  int cdc_server_port;

  char *broker_ip;
  int broker_port;

  char *target_server_ip;
  int target_server_port;
  char *target_database_name;
  int target_set_count;

  char *dba_user;
  char *dba_passwd;
  char *database_name;
  char config_path[PATH_MAX];

  int print_log_item;
  int print_timer;
  int print_transaction;
  int disable_print_sql;

  int ignore_trigger_dml;

  int connection_timeout;
  int extraction_timeout;
  int max_log_item;
  int all_in_cond;

  time_t start_time;
  int start_time_set;
  uint64_t start_lsa;
  int start_lsa_set;

  long long max_log_items;
  int output_log_type;

  char *extraction_table_name[100];
  int extraction_table_name_count;

  uint64_t extraction_class_oid[100];
  int extraction_class_oid_count;

  char *extraction_user_name[100];
  int extraction_user_name_count;

  char *trace_path;
  int trace_level;
  int trace_size;

  int cci_conn_handle;
  int target_conn_handle;
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
  int attr_info_count;

  ATTR_INFO *attr_info_p;
  int is_malloc_attr_info;
};

typedef struct class_info_global CLASS_INFO_GLOBAL;
struct class_info_global
{
  CLASS_INFO class_info[10000];
  int class_info_count;
};

typedef struct tran TRAN;
struct tran
{
  int tran_id;
  char *sql_list[1000];
  int sql_count;
  bool has_serial;
};

typedef struct tran_table_global TRAN_TABLE_GLOBAL;
struct tran_table_global
{
  TRAN tran_table[10000];
  int tran_count;
};

HELPER_GLOBAL helper_Gl;
CLASS_INFO_GLOBAL class_info_Gl;
TRAN_TABLE_GLOBAL tran_table_Gl;

void
init_helper_global (void)
{
  helper_Gl.cdc_server_ip = NULL;
  helper_Gl.cdc_server_port = 1523;
  helper_Gl.connection_timeout = 300;
  helper_Gl.extraction_timeout = 300;
  helper_Gl.max_log_item = 512;
  helper_Gl.all_in_cond = 0;
  helper_Gl.start_time = 0;
  helper_Gl.start_time_set = 0;
  helper_Gl.start_lsa = 0;
  helper_Gl.start_lsa_set = 0;
  helper_Gl.max_log_items = 0;
  helper_Gl.output_log_type = OUTPUT_LOG_TYPE_ALL;

  helper_Gl.extraction_table_name_count = 0;
  helper_Gl.extraction_class_oid_count = 0;
  helper_Gl.extraction_user_name_count = 0;

  helper_Gl.broker_ip = NULL;
  helper_Gl.broker_port = 33000;

  helper_Gl.target_server_ip = NULL;
  helper_Gl.target_server_port = 0;
  helper_Gl.target_database_name = NULL;
  helper_Gl.target_set_count = 0;

  helper_Gl.dba_user = NULL;
  helper_Gl.dba_passwd = NULL;
  helper_Gl.database_name = NULL;
  helper_Gl.config_path[0] = '\0';

  helper_Gl.print_log_item = 0;
  helper_Gl.print_timer = 0;
  helper_Gl.print_transaction = 0;
  helper_Gl.disable_print_sql = 0;

  helper_Gl.ignore_trigger_dml = 1;

  helper_Gl.cci_conn_handle = -1;
  helper_Gl.target_conn_handle = -1;
}

void
init_class_info_global (void)
{
  class_info_Gl.class_info_count = 0;
}

void
init_tran_table_global (void)
{
  tran_table_Gl.tran_count = 0;
}

void
print_usages (void)
{
  printf ("사용법: cdc_test_helper [옵션] [데이터베이스명]\n\n");
  printf ("옵션:\n");
  printf ("  -t, --start-time TIME    시작 시간(YYYY-MM-DD HH:MM:SS)\n");
  printf ("  -l, --start-lsa LSA      시작 LSA(PAGEID|OFFSET)\n");
  printf ("  -n, --max-log-items N    필터를 통과한 로그 N건 출력 후 종료\n");
  printf ("  -m, --log-type TYPE      출력 종류: all, dml, ddl(기본값: all)\n");
  printf ("  -T, --table TABLES       쉼표로 구분한 테이블 목록\n");
  printf ("  -h, --help               도움말 출력\n\n");
  printf ("설정 파일:\n");
  printf ("  기본 설정 파일은 실행 파일 옆의 cdc_test_helper.conf입니다.\n");
  printf ("  CDC_TEST_HELPER_CONF 환경변수로 다른 설정 파일을 지정할 수 있습니다.\n\n");
  printf ("주의:\n");
  printf ("  --start-time과 --start-lsa는 동시에 사용할 수 없습니다.\n");
  printf ("  명령행 옵션은 설정 파일보다 우선합니다.\n");
}

int
parse_start_time (const char *value, time_t * start_time)
{
  struct tm tm_value = { 0, };
  struct tm verify_tm;
  int year, month, day, hour, minute, second;
  char trailing;

  if (sscanf (value, "%d-%d-%d %d:%d:%d%c", &year, &month, &day, &hour, &minute, &second, &trailing) != 6)
    {
      return YES_ERROR;
    }

  tm_value.tm_year = year - 1900;
  tm_value.tm_mon = month - 1;
  tm_value.tm_mday = day;
  tm_value.tm_hour = hour;
  tm_value.tm_min = minute;
  tm_value.tm_sec = second;
  tm_value.tm_isdst = -1;

  *start_time = mktime (&tm_value);
  if (*start_time == (time_t) - 1 || localtime_r (start_time, &verify_tm) == NULL)
    {
      return YES_ERROR;
    }

  if (verify_tm.tm_year != year - 1900 || verify_tm.tm_mon != month - 1 || verify_tm.tm_mday != day
      || verify_tm.tm_hour != hour || verify_tm.tm_min != minute || verify_tm.tm_sec != second)
    {
      return YES_ERROR;
    }

  return NO_ERROR;
}

void
unpack_lsa (uint64_t packed_lsa, int64_t * pageid, short *offset)
{
  uint64_t pageid_bits = packed_lsa & 0x0000FFFFFFFFFFFFULL;

  if ((pageid_bits & 0x0000800000000000ULL) != 0)
    {
      pageid_bits |= 0xFFFF000000000000ULL;
    }

  *pageid = (int64_t) pageid_bits;
  *offset = (short) (packed_lsa >> 48);
}

int
parse_start_lsa (const char *value, uint64_t * packed_lsa)
{
  long long pageid;
  int offset;
  char trailing;

  if (sscanf (value, "%lld|%d%c", &pageid, &offset, &trailing) != 2)
    {
      return YES_ERROR;
    }

  if (pageid < 0 || pageid > 0x00007FFFFFFFFFFFLL || offset < 0 || offset > 0x7FFF)
    {
      return YES_ERROR;
    }

  *packed_lsa = ((uint64_t) (unsigned short) offset << 48) | ((uint64_t) pageid & 0x0000FFFFFFFFFFFFULL);

  return NO_ERROR;
}

int
parse_positive_long_long (const char *value, long long *result)
{
  char *end;
  long long parsed_value;

  if (value[0] == '\0')
    {
      return YES_ERROR;
    }

  errno = 0;
  parsed_value = strtoll (value, &end, 10);
  if (errno == ERANGE || *end != '\0' || parsed_value <= 0)
    {
      return YES_ERROR;
    }

  *result = parsed_value;

  return NO_ERROR;
}

int
make_extraction_table_list (char *table_list)
{
  char *s, *n;

  s = n = table_list;

  for (int i = 0; i < 100; i++)
    {
      while (*n != ',' && *n != '\0')
	{
	  n++;
	}

      if (*n == ',')
	{
	  *n = '\0';
	  n++;

	  helper_Gl.extraction_table_name[i] = strdup (s);
	  helper_Gl.extraction_table_name_count++;

	  s = n;
	}
      else if (*n == '\0')
	{
	  helper_Gl.extraction_table_name[i] = strdup (s);
	  helper_Gl.extraction_table_name_count++;

	  break;
	}
      else
	{
	  assert (0);
	}
    }

  return NO_ERROR;
}

int
make_extraction_user_list (char *user_list)
{
  char *s, *n;

  s = n = user_list;

  for (int i = 0; i < 100; i++)
    {
      while (*n != ',' && *n != '\0')
	{
	  n++;
	}

      if (*n == ',')
	{
	  *n = '\0';
	  n++;

	  helper_Gl.extraction_user_name[i] = strdup (s);
	  helper_Gl.extraction_user_name_count++;

	  s = n;
	}
      else if (*n == '\0')
	{
	  helper_Gl.extraction_user_name[i] = strdup (s);
	  helper_Gl.extraction_user_name_count++;

	  break;
	}
      else
	{
	  assert (0);
	}
    }

  return NO_ERROR;
}

int
is_option_with_value (const char *argument, const char *long_option, const char *short_option)
{
  size_t argument_length = strlen (argument);
  size_t long_option_length = strlen (long_option);
  size_t long_option_name_length =
    long_option[long_option_length - 1] == '=' ? long_option_length - 1 : long_option_length;
  size_t short_option_length = strlen (short_option);

  return strncmp (argument, long_option, long_option_length) == 0
    || (argument_length == long_option_name_length
	&& strncmp (argument, long_option, long_option_name_length) == 0) || strcmp (argument, short_option) == 0
    || (argument_length > short_option_length && strncmp (argument, short_option, short_option_length) == 0
	&& argument[short_option_length] == '=');
}

char *
get_option_value (int argc, char *argv[], int *index, const char *long_option, const char *short_option)
{
  char *argument = argv[*index];
  size_t long_option_length = strlen (long_option);
  size_t long_option_name_length =
    long_option[long_option_length - 1] == '=' ? long_option_length - 1 : long_option_length;

  if (strncmp (argument, long_option, long_option_length) == 0)
    {
      return argument + long_option_length;
    }

  if ((strlen (argument) == long_option_name_length
       && strncmp (argument, long_option, long_option_name_length) == 0) || strcmp (argument, short_option) == 0)
    {
      if (*index + 1 >= argc)
	{
	  return NULL;
	}

      (*index)++;
      return argv[*index];
    }

  return argument + strlen (short_option) + 1;
}

char *
trim_whitespace (char *value)
{
  char *end;

  while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n')
    {
      value++;
    }

  end = value + strlen (value);
  while (end > value && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
    {
      end--;
    }
  *end = '\0';

  return value;
}

int
parse_config_integer (const char *value, int min_value, int max_value, int *result)
{
  char *end;
  long parsed_value;

  errno = 0;
  parsed_value = strtol (value, &end, 10);
  if (value[0] == '\0' || errno == ERANGE || *end != '\0' || parsed_value < min_value || parsed_value > max_value)
    {
      return YES_ERROR;
    }

  *result = (int) parsed_value;
  return NO_ERROR;
}

int
parse_config_boolean (const char *value, int *result)
{
  if (strcmp (value, "true") == 0 || strcmp (value, "1") == 0 || strcmp (value, "yes") == 0)
    {
      *result = 1;
      return NO_ERROR;
    }

  if (strcmp (value, "false") == 0 || strcmp (value, "0") == 0 || strcmp (value, "no") == 0)
    {
      *result = 0;
      return NO_ERROR;
    }

  return YES_ERROR;
}

void
set_config_string (char **target, const char *value)
{
  if (*target != NULL)
    {
      free (*target);
      *target = NULL;
    }

  if (value[0] != '\0')
    {
      *target = strdup (value);
      assert (*target != NULL);
    }
}

int
set_config_value (const char *key, char *value)
{
  int bool_value;

  if (strcmp (key, "database_name") == 0)
    {
      set_config_string (&helper_Gl.database_name, value);
    }
  else if (strcmp (key, "user") == 0)
    {
      set_config_string (&helper_Gl.dba_user, value);
    }
  else if (strcmp (key, "password") == 0)
    {
      set_config_string (&helper_Gl.dba_passwd, value);
    }
  else if (strcmp (key, "cdc_server_ip") == 0)
    {
      set_config_string (&helper_Gl.cdc_server_ip, value);
    }
  else if (strcmp (key, "cdc_server_port") == 0)
    {
      return parse_config_integer (value, 1, 65535, &helper_Gl.cdc_server_port);
    }
  else if (strcmp (key, "broker_ip") == 0)
    {
      set_config_string (&helper_Gl.broker_ip, value);
    }
  else if (strcmp (key, "broker_port") == 0)
    {
      return parse_config_integer (value, 1, 65535, &helper_Gl.broker_port);
    }
  else if (strcmp (key, "connection_timeout") == 0)
    {
      return parse_config_integer (value, -1, 360, &helper_Gl.connection_timeout);
    }
  else if (strcmp (key, "extraction_timeout") == 0)
    {
      return parse_config_integer (value, -1, 360, &helper_Gl.extraction_timeout);
    }
  else if (strcmp (key, "max_log_item") == 0)
    {
      return parse_config_integer (value, 1, 1024, &helper_Gl.max_log_item);
    }
  else if (strcmp (key, "all_in_cond") == 0)
    {
      return parse_config_boolean (value, &helper_Gl.all_in_cond);
    }
  else if (strcmp (key, "extraction_users") == 0)
    {
      if (value[0] != '\0' && make_extraction_user_list (value) != NO_ERROR)
	{
	  return YES_ERROR;
	}
    }
  else if (strcmp (key, "target_server_ip") == 0)
    {
      set_config_string (&helper_Gl.target_server_ip, value);
    }
  else if (strcmp (key, "target_server_port") == 0)
    {
      if (value[0] == '\0')
	{
	  helper_Gl.target_server_port = 0;
	}
      else
	{
	  return parse_config_integer (value, 1, 65535, &helper_Gl.target_server_port);
	}
    }
  else if (strcmp (key, "target_database_name") == 0)
    {
      set_config_string (&helper_Gl.target_database_name, value);
    }
  else if (strcmp (key, "print_sql") == 0)
    {
      if (parse_config_boolean (value, &bool_value) != NO_ERROR)
	{
	  return YES_ERROR;
	}
      helper_Gl.disable_print_sql = !bool_value;
    }
  else if (strcmp (key, "print_log_item") == 0)
    {
      return parse_config_boolean (value, &helper_Gl.print_log_item);
    }
  else if (strcmp (key, "print_timer") == 0)
    {
      return parse_config_boolean (value, &helper_Gl.print_timer);
    }
  else if (strcmp (key, "print_transaction") == 0)
    {
      return parse_config_boolean (value, &helper_Gl.print_transaction);
    }
  else if (strcmp (key, "ignore_trigger_dml") == 0)
    {
      return parse_config_boolean (value, &helper_Gl.ignore_trigger_dml);
    }
  else
    {
      return YES_ERROR;
    }

  return NO_ERROR;
}

int
make_config_path (const char *program_name)
{
  const char *config_from_env = getenv ("CDC_TEST_HELPER_CONF");
  char executable_path[PATH_MAX];
  char *last_separator;
  ssize_t path_length;

  if (config_from_env != NULL && config_from_env[0] != '\0')
    {
      if (strlen (config_from_env) >= sizeof (helper_Gl.config_path))
	{
	  return YES_ERROR;
	}
      strcpy (helper_Gl.config_path, config_from_env);
      return NO_ERROR;
    }

  path_length = readlink ("/proc/self/exe", executable_path, sizeof (executable_path) - 1);
  if (path_length < 0)
    {
      if (realpath (program_name, executable_path) == NULL)
	{
	  return YES_ERROR;
	}
    }
  else
    {
      executable_path[path_length] = '\0';
    }

  last_separator = strrchr (executable_path, '/');
  if (last_separator == NULL)
    {
      return YES_ERROR;
    }
  *last_separator = '\0';

  if (snprintf (helper_Gl.config_path, sizeof (helper_Gl.config_path), "%s/cdc_test_helper.conf", executable_path)
      >= (int) sizeof (helper_Gl.config_path))
    {
      return YES_ERROR;
    }

  return NO_ERROR;
}

int
load_configuration (const char *program_name)
{
  FILE *config_file;
  char line[4096];
  int line_number = 0;

  if (make_config_path (program_name) != NO_ERROR)
    {
      printf ("[ERROR] Failed to resolve the configuration file path.\n");
      return YES_ERROR;
    }

  config_file = fopen (helper_Gl.config_path, "r");
  if (config_file == NULL)
    {
      printf ("[ERROR] Configuration file not found: %s\n", helper_Gl.config_path);
      return YES_ERROR;
    }

  while (fgets (line, sizeof (line), config_file) != NULL)
    {
      char *content;
      char *separator;
      char *key;
      char *value;

      line_number++;
      if (strchr (line, '\n') == NULL && !feof (config_file))
	{
	  printf ("[ERROR] Configuration line %d is too long.\n", line_number);
	  fclose (config_file);
	  return YES_ERROR;
	}

      content = trim_whitespace (line);
      if (content[0] == '\0' || content[0] == '#' || content[0] == ';')
	{
	  continue;
	}

      separator = strchr (content, '=');
      if (separator == NULL)
	{
	  printf ("[ERROR] Invalid configuration at %s:%d\n", helper_Gl.config_path, line_number);
	  fclose (config_file);
	  return YES_ERROR;
	}

      *separator = '\0';
      key = trim_whitespace (content);
      value = trim_whitespace (separator + 1);

      if (set_config_value (key, value) != NO_ERROR)
	{
	  printf ("[ERROR] Invalid configuration '%s' at %s:%d\n", key, helper_Gl.config_path, line_number);
	  fclose (config_file);
	  return YES_ERROR;
	}
    }

  if (ferror (config_file))
    {
      printf ("[ERROR] Failed to read configuration file: %s\n", helper_Gl.config_path);
      fclose (config_file);
      return YES_ERROR;
    }

  fclose (config_file);

  helper_Gl.target_set_count = 0;
  helper_Gl.target_set_count += helper_Gl.target_server_ip != NULL;
  helper_Gl.target_set_count += helper_Gl.target_server_port > 0;
  helper_Gl.target_set_count += helper_Gl.target_database_name != NULL;

  return NO_ERROR;
}

int
process_command_line_option (int argc, char *argv[])
{
  init_helper_global ();
  init_class_info_global ();
  init_tran_table_global ();

  if (argc == 1)
    {
      print_usages ();
      exit (0);
    }

  for (int i = 1; i < argc; i++)
    {
      if (strcmp (argv[i], "--help") == 0 || strcmp (argv[i], "-h") == 0)
	{
	  print_usages ();
	  exit (0);
	}
    }

  if (load_configuration (argv[0]) != NO_ERROR)
    {
      return YES_ERROR;
    }

  for (int i = 1; i < argc; i++)
    {
      if (is_option_with_value (argv[i], "--start-time=", "-t"))
	{
	  char *option_value = get_option_value (argc, argv, &i, "--start-time=", "-t");

	  if (option_value == NULL || parse_start_time (option_value, &helper_Gl.start_time) != NO_ERROR)
	    {
	      goto print_usages;
	    }

	  helper_Gl.start_time_set = 1;
	}
      else if (is_option_with_value (argv[i], "--start-lsa=", "-l"))
	{
	  char *option_value = get_option_value (argc, argv, &i, "--start-lsa=", "-l");

	  if (option_value == NULL || parse_start_lsa (option_value, &helper_Gl.start_lsa) != NO_ERROR)
	    {
	      goto print_usages;
	    }

	  helper_Gl.start_lsa_set = 1;
	}
      else if (is_option_with_value (argv[i], "--max-log-items=", "-n"))
	{
	  char *option_value = get_option_value (argc, argv, &i, "--max-log-items=", "-n");

	  if (option_value == NULL || parse_positive_long_long (option_value, &helper_Gl.max_log_items) != NO_ERROR)
	    {
	      goto print_usages;
	    }
	}
      else if (is_option_with_value (argv[i], "--log-type=", "-m"))
	{
	  char *log_type = get_option_value (argc, argv, &i, "--log-type=", "-m");

	  if (log_type == NULL)
	    {
	      goto print_usages;
	    }
	  else if (strcmp (log_type, "all") == 0)
	    {
	      helper_Gl.output_log_type = OUTPUT_LOG_TYPE_ALL;
	    }
	  else if (strcmp (log_type, "dml") == 0)
	    {
	      helper_Gl.output_log_type = OUTPUT_LOG_TYPE_DML;
	    }
	  else if (strcmp (log_type, "ddl") == 0)
	    {
	      helper_Gl.output_log_type = OUTPUT_LOG_TYPE_DDL;
	    }
	  else
	    {
	      goto print_usages;
	    }
	}
      else if (is_option_with_value (argv[i], "--table=", "-T"))
	{
	  char table_list[2048];
	  char *option_value = get_option_value (argc, argv, &i, "--table=", "-T");

	  if (option_value == NULL || strlen (option_value) >= sizeof (table_list))
	    {
	      goto print_usages;
	    }

	  strcpy (table_list, option_value);

	  if (NO_ERROR != make_extraction_table_list (table_list))
	    {
	      goto print_usages;
	    }
	}
      else
	{
	  if (argv[i][0] != '-' && i == argc - 1)
	    {
	      set_config_string (&helper_Gl.database_name, argv[i]);
	    }
	  else
	    {
	      goto print_usages;
	    }
	}
    }

  if (helper_Gl.cdc_server_ip == NULL)
    {
      helper_Gl.cdc_server_ip = strdup ("127.0.0.1");
    }

  if (helper_Gl.broker_ip == NULL)
    {
      helper_Gl.broker_ip = strdup ("127.0.0.1");
    }

  if (helper_Gl.dba_user == NULL)
    {
      helper_Gl.dba_user = strdup ("dba");
    }

  if (helper_Gl.dba_passwd == NULL)
    {
      helper_Gl.dba_passwd = strdup ("");
    }

#if 0
  printf ("helper_Gl.target_set_count = %d\n", helper_Gl.target_set_count);
#endif

  // all should be set together.
  if (helper_Gl.target_set_count > 0 && helper_Gl.target_set_count < 3)
    {
      printf ("[ERROR] target_server_ip, target_server_port, and target_database_name must be set together.\n");
      return YES_ERROR;
    }

  if (helper_Gl.start_time_set && helper_Gl.start_lsa_set)
    {
      goto print_usages;
    }

  if (helper_Gl.database_name == NULL || helper_Gl.database_name[0] == '\0')
    {
      printf ("[ERROR] database_name must be set in %s or supplied as the last argument.\n", helper_Gl.config_path);
      return YES_ERROR;
    }

  return NO_ERROR;

print_usages:

  print_usages ();
  return YES_ERROR;
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

#if 0
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
fetch_schema_info (char *query)
{
  int conn_handle, req_handle;
  int exec_retval;
  T_CCI_ERROR err_buf;

  char *class_oid, *class_name;
  int indicator;

  uint64_t class_oid_2;

  CLASS_INFO *cur_class_info = NULL;
  ATTR_INFO *cur_attr_info = NULL;

  char cci_url[1024] = { '\0', };

  int error_code;

#if 0
  printf ("helper_Gl.broker_ip = %s\n", helper_Gl.broker_ip);
  printf ("helper_Gl.broker_port = %d\n", helper_Gl.broker_port);
  printf ("helper_Gl.database_name = %s\n", helper_Gl.database_name);
  printf ("helper_Gl.dba_user = %s\n", helper_Gl.dba_user);
  printf ("helper_Gl.dba_passwd = %s\n", helper_Gl.dba_passwd);
#endif

  if (helper_Gl.cci_conn_handle == -1)
    {
#if 1
      snprintf (cci_url, 1024,
		"cci:cubrid:%s:%d:%s:::?logOnException=true&logSlowQueries=true&logTraceApi=true&logTraceNetwork=true&logFile=cci_debug.log&logBaseDir=%s",
		helper_Gl.broker_ip, helper_Gl.broker_port, helper_Gl.database_name, ".");

      conn_handle = cci_connect_with_url_ex (cci_url, helper_Gl.dba_user, helper_Gl.dba_passwd, &err_buf);
#else
      conn_handle =
	cci_connect (helper_Gl.broker_ip, helper_Gl.broker_port, helper_Gl.database_name, helper_Gl.dba_user,
		     helper_Gl.dba_passwd);
#endif
      if (conn_handle < 0)
	{
	  printf ("[ERROR] [cci] conn_handle=%d, err_code=%d, err_msg=%s\n", conn_handle, err_buf.err_code,
		  err_buf.err_msg);

	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      helper_Gl.cci_conn_handle = conn_handle;
    }
  else
    {
      conn_handle = helper_Gl.cci_conn_handle;
    }

  req_handle = cci_prepare_and_execute (conn_handle, query, 0, &exec_retval, &err_buf);
  if (req_handle < 0)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

#if 0
  printf ("query=%s\n", query);
  printf ("exec_retval=%d\n", exec_retval);
#endif

  // class info
  while (1)
    {
      error_code = cci_cursor (req_handle, 1, CCI_CURSOR_CURRENT, &err_buf);
      if (error_code < 0)
	{
	  if (error_code == CCI_ER_NO_MORE_DATA)
	    {
	      break;
	    }

	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = cci_fetch (req_handle, &err_buf);
      if (error_code < 0)
	{
	  printf ("[ERROR] [cci] req_handle=%d, err_code=%d, err_msg=%s\n", req_handle, err_buf.err_code,
		  err_buf.err_msg);

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

      if (class_oid_2 == 0)
	{
	  // Legacy server bug. The class_oid is null value in spite of the class_name is not null value.
	  // This is a problem that should never happen.
	  break;
	}

#if 0
      printf ("class_name: %s, class_oid: %s, class_oid_2: %lld\n", class_name, class_oid, class_oid_2);
#endif

      assert (class_info_Gl.class_info_count < 10000);

      cur_class_info = &class_info_Gl.class_info[class_info_Gl.class_info_count];

      class_info_Gl.class_info_count++;

#if 0
      printf ("[INC: %d] class_name=%s, class_oid=%lld (%s) from _db_class\n", class_info_Gl.class_info_count,
	      class_name, class_oid_2, class_oid);
#endif

      error_code = make_class_info (cur_class_info, class_name, class_oid_2);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

#if 0
      printf ("class_info_Gl.class_info_count = %d\n", class_info_Gl.class_info_count);
#endif

      cur_class_info->attr_info_count = 0;

      // attr infos for each class info
      {
	int req_handle_2, exec_retval_2;

	int def_order, data_type, is_nullable, is_primary_key;
	char *attr_name;

	req_handle_2 =
	  cci_prepare (conn_handle,
		       "select distinct a.attr_name, a.def_order, a.data_type, (select prec from _db_domain d where d in a.domains) as prec, a.is_nullable, max((case when a.attr_name in (select key_attr_name from _db_index_key k where k in b.key_attrs) and b.is_primary_key = 1 then 1 else 0 end)) as is_primary_key from _db_attribute a left outer join _db_index b on a.class_of = b.class_of where a.class_of.is_system_class != 1 and a.class_of.class_name = ? group by a.attr_name order by def_order",
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

#if 0
	printf ("exec_retval_2 = %d\n", exec_retval_2);
#endif

	if (exec_retval_2 <= 50)	// if column count of table is larger than 50, then malloc.
	  {
	    cur_class_info->attr_info_p = cur_class_info->attr_info;
	    cur_class_info->is_malloc_attr_info = 0;
	  }
	else
	  {
	    cur_class_info->attr_info_p = (ATTR_INFO *) malloc (sizeof (ATTR_INFO) * exec_retval_2);
	    if (cur_class_info->attr_info_p == NULL)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    cur_class_info->is_malloc_attr_info = 1;
	  }

	while (1)
	  {
	    error_code = cci_cursor (req_handle_2, 1, CCI_CURSOR_CURRENT, &err_buf);
	    if (error_code < 0)
	      {
		if (error_code == CCI_ER_NO_MORE_DATA)
		  {
		    break;
		  }

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

	    cur_attr_info = &cur_class_info->attr_info_p[cur_class_info->attr_info_count];
	    cur_class_info->attr_info_count++;

	    error_code = make_attr_info (cur_attr_info, attr_name, data_type, def_order, is_nullable, is_primary_key);
	    if (error_code < 0)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

#if 0
	    printf ("attr_name: %s, def_order: %d, data_type: %d\n", attr_name, def_order, data_type);
#endif
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

  error_code = cci_close_query_result (req_handle, &err_buf);
  if (error_code < 0)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  error_code = cci_close_req_handle (req_handle);
  if (error_code < 0)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
make_extraction_class_oid_list (void)
{
  CLASS_INFO *class_info;

  assert (helper_Gl.extraction_table_name_count != 0);
  assert (helper_Gl.extraction_class_oid_count == 0);

#if 0
  printf ("helper_Gl.extraction_table_name_count=%d, class_info_Gl.class_info_count=%d\n",
	  helper_Gl.extraction_table_name_count, class_info_Gl.class_info_count);
#endif

  for (int i = 0; i < class_info_Gl.class_info_count; i++)
    {
      class_info = &class_info_Gl.class_info[i];

      assert (class_info != NULL && class_info->class_name != NULL);

      helper_Gl.extraction_class_oid[i] = class_info->class_oid;

      helper_Gl.extraction_class_oid_count++;
    }

  return NO_ERROR;
}

int
fetch_all_schema_info (void)
{
  char sql_buf[10000] = { '\0', };
  int error_code;

  if (helper_Gl.extraction_table_name_count == 0)
    {
      error_code = fetch_schema_info ("select class_of, class_name from _db_class where is_system_class != 1");
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}
    }
  else
    {
      strcat (sql_buf, "select class_of, class_name from _db_class where (");

      for (int i = 0; i < helper_Gl.extraction_table_name_count; i++)
	{
	  sprintf (sql_buf + strlen (sql_buf), "class_name = \'%s\'", helper_Gl.extraction_table_name[i]);

	  if (i != helper_Gl.extraction_table_name_count - 1)
	    {
	      strcat (sql_buf, " or ");
	    }
	  else
	    {
	      strcat (sql_buf, ") and is_system_class != 1");
	    }
	}

#if 0
      printf ("sql_buf: %s\n", sql_buf);
#endif

      error_code = fetch_schema_info (sql_buf);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = make_extraction_class_oid_list ();
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
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
    case 5:
      return "grant";
    case 6:
      return "revoke";
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
    case 7:
      return "user";
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

    case 3:
      return "insert by trigger";

    case 4:
      return "update by trigger";

    case 5:
      return "delete by trigger";

    default:
      assert (0);
    }
}

int
print_dml (CUBRID_DATA_ITEM * data_item)
{
  printf ("dml_type: %d (%s)\n", data_item->dml.dml_type, convert_dml_type_to_string (data_item->dml.dml_type));
  printf ("classoid: %lld\n", data_item->dml.classoid);
  printf ("\n");
  printf ("num_changed_column: %d\n", data_item->dml.num_changed_column);
  printf ("changed_column:\n");

  for (int i = 0; i < data_item->dml.num_changed_column; i++)
    {
      printf ("\tindex: %d, data_len: %d\n",
	      data_item->dml.changed_column_index[i], data_item->dml.changed_column_data_len[i]);
    }

  printf ("\n");
  printf ("num_cond_column: %d\n", data_item->dml.num_cond_column);
  printf ("cond_column:\n");

  for (int i = 0; i < data_item->dml.num_cond_column; i++)
    {
      printf ("\tindex: %d, data_len: %d\n", data_item->dml.cond_column_index[i],
	      data_item->dml.cond_column_data_len[i]);
    }

  return NO_ERROR;
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
is_trigger_dml (int dml_type)
{
  switch (dml_type)
    {
    case 0:
    case 1:
    case 2:
      return 0;

    case 3:
    case 4:
    case 5:
      return 1;

    default:
      assert (0);
    }
}

int
should_output_log_item (CUBRID_LOG_ITEM * log_item)
{
  if (helper_Gl.output_log_type != OUTPUT_LOG_TYPE_ALL && helper_Gl.output_log_type != log_item->data_item_type)
    {
      return 0;
    }

  if (log_item->data_item_type == 3)
    {
      return helper_Gl.print_log_item && helper_Gl.print_timer;
    }

  if (helper_Gl.ignore_trigger_dml && log_item->data_item_type == 1
      && is_trigger_dml (log_item->data_item.dml.dml_type))
    {
      return 0;
    }

  return 1;
}

int
print_log_item (CUBRID_LOG_ITEM * log_item, int output_enabled)
{
  int error_code;

  if (helper_Gl.print_log_item == 0 || output_enabled == 0)
    {
      goto end;
    }

  if (helper_Gl.print_timer == 0 && log_item->data_item_type == 3)
    {
      goto end;
    }

  if (helper_Gl.ignore_trigger_dml && log_item->data_item_type == 1
      && is_trigger_dml (log_item->data_item.dml.dml_type))
    {
      goto end;
    }

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

end:

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
    case 5:
    case 6:

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
  CLASS_INFO *class_info;

#if 0
  if (class_info_Gl.class_info_count >= 3)
    {
      printf ("class_info_Gl.class_info_count=%d\n", class_info_Gl.class_info_count);

      for (int i = 0; i < class_info_Gl.class_info_count; i++)
	{
	  class_info = &class_info_Gl.class_info[i];

	  printf ("class_info->class_name=%s\n", class_info->class_name);
	  printf ("class_info->class_oid=%lld\n\n", class_info->class_oid);
	}

      printf ("class_oid=%lld\n", class_oid);
    }
#endif

  assert (class_oid != 0);

  for (int i = 0; i < class_info_Gl.class_info_count; i++)
    {
      class_info = &class_info_Gl.class_info[i];

      if (class_info->class_oid == class_oid)
	{
#if 0
	  printf ("i=%d\n", i);
#endif

	  return class_info;
	}
    }

  return NULL;
}

ATTR_INFO *
find_attr_info (CLASS_INFO * class_info, int def_order)
{
  ATTR_INFO *attr_info;

  for (int i = 0; i < class_info->attr_info_count; i++)
    {
      attr_info = &class_info->attr_info_p[i];
      if (attr_info->def_order == def_order)
	{
	  return attr_info;
	}
    }

  return NULL;
}

int
process_changed_column (CUBRID_DATA_ITEM * data_item, int col_idx,
			ATTR_INFO * attr_info, char *sql_buf, int *cant_make_sql)
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

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.changed_column_data_len[col_idx] + 3, "\'%s\'", value);
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

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.changed_column_data_len[col_idx] + 3, "\'%s\'", value);
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
	case BIT:
	case VARBIT:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.changed_column_data_len[col_idx] + 1, "%s", value);
	  }

	  break;

	case CHAR:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.changed_column_data_len[col_idx] + 3, "\'%s\'", value);
	  }

	  break;

	case NCHAR:
	case VARNCHAR:
	case MONETARY:
	  {
	    char *value;

	    value = data_item->dml.changed_column_data[col_idx];

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.changed_column_data_len[col_idx] + 1, "%s", value);

#if 0
	    printf ("value=%s\n", value);
	    printf ("sql_buf=%s\n", sql_buf);
#endif
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

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.changed_column_data_len[col_idx] + 3, "\'%s\'", value);
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

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.changed_column_data_len[col_idx] + 3, "\'%s\'", value);
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
process_cond_column (CUBRID_DATA_ITEM * data_item, int col_idx,
		     ATTR_INFO * attr_info, char *sql_buf, int *cant_make_sql)
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

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.cond_column_data_len[col_idx] + 3, "\'%s\'", value);
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

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.cond_column_data_len[col_idx] + 3, "\'%s\'", value);
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
	case BIT:
	case VARBIT:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.cond_column_data_len[col_idx] + 1, "%s", value);
	  }

	  break;

	case CHAR:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.cond_column_data_len[col_idx] + 3, "\'%s\'", value);
	  }

	  break;

	case NCHAR:
	case VARNCHAR:
	  {
	    char *value;

	    value = data_item->dml.cond_column_data[col_idx];

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.cond_column_data_len[col_idx] + 1, "%s", value);
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

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.cond_column_data_len[col_idx] + 3, "\'%s\'", value);
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

	    snprintf (sql_buf + strlen (sql_buf), data_item->dml.cond_column_data_len[col_idx] + 3, "\'%s\'", value);
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
#if 0
      printf ("data_item->dml.classoid = %lld\n", data_item->dml.classoid);
      printf ("class_info_Gl.class_info_count = %d\n", class_info_Gl.class_info_count);

      for (i = 0; i < class_info_Gl.class_info_count; i++)
	{
	  class_info = &class_info_Gl.class_info[i];
	  printf ("class_info->class_oid = %lld\n", class_info->class_oid);
	}
#endif

      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  assert (class_info->attr_info_count == data_item->dml.num_changed_column);

  sprintf (sql_buf, "insert into %s (", class_info->class_name);

  for (i = 0; i < data_item->dml.num_changed_column; i++)
    {
      attr_info = &class_info->attr_info_p[i];

      strcat (sql_buf, attr_info->attr_name);

      if (i != class_info->attr_info_count - 1)
	{
	  strcat (sql_buf, ", ");
	}
      else
	{
	  strcat (sql_buf, ") values (");
	}
    }

#if 0
  for (i = 0; i < data_item->dml.num_changed_column; i++)
    {
      attr_info = &class_info->attr_info_p[i];

      printf ("attr_info->def_order=%d, data_item->dml.changed_column_index[%d]=%d\n", attr_info->def_order, i,
	      data_item->dml.changed_column_index[i]);
    }
#endif


  for (i = 0; i < data_item->dml.num_changed_column; i++)
    {
      if (cant_make_sql)
	{
	  // Because of data types not supported by cdc API.
	  break;
	}

      attr_info = &class_info->attr_info_p[i];

      if (attr_info->def_order != data_item->dml.changed_column_index[i])
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = process_changed_column (data_item, i, attr_info, sql_buf, &cant_make_sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      if (i != class_info->attr_info_count - 1)
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

  assert (class_info->attr_info_count >= data_item->dml.num_changed_column);

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
	  strcat (sql_buf, " and ");
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

  assert (class_info->attr_info_count >= data_item->dml.num_cond_column);

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
    case 3:			/* insert by trigger */
      error_code = make_insert_stmt (data_item, sql);

      break;

    case 1:
    case 4:			/* update by trigger */
      error_code = make_update_stmt (data_item, sql);

      break;

    case 2:
    case 5:			/* delete by trigger */
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

TRAN *
find_or_alloc_tran (int tran_id)
{
  int i;
  int is_found = 0;
  TRAN *tran;

  if (tran_table_Gl.tran_count == 10000)
    {
      assert (0);
      return NULL;
    }

  // find
  for (i = 0; i < tran_table_Gl.tran_count; i++)
    {
      tran = &tran_table_Gl.tran_table[i];

      if (tran->tran_id == tran_id)
	{
	  is_found = 1;

	  break;
	}
    }

  // alloc
  if (!is_found)
    {
      tran = &tran_table_Gl.tran_table[tran_table_Gl.tran_count];
      tran->tran_id = tran_id;
      tran->sql_count = 0;
      tran->has_serial = false;
      tran_table_Gl.tran_count++;
    }

  return tran;
}

bool
check_if_serial_in_tran (int tran_id, bool * has_serial)
{
  TRAN *tran;

  int error_code;

  tran = find_or_alloc_tran (tran_id);
  if (tran == NULL)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  *has_serial = tran->has_serial;

  return NO_ERROR;

error:

  return YES_ERROR;

}

int
register_serial_to_tran (int tran_id)
{
  TRAN *tran;

  int error_code;

  tran = find_or_alloc_tran (tran_id);
  if (tran == NULL)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  tran->has_serial = true;

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
register_sql_to_tran (int tran_id, char *sql)
{
  TRAN *tran;

  int error_code;

  tran = find_or_alloc_tran (tran_id);
  if (tran == NULL)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  tran->sql_list[tran->sql_count] = sql;
  tran->sql_count++;

  return NO_ERROR;

error:

  return YES_ERROR;
}

TRAN *
find_tran (int tran_id)
{
  int i;
  TRAN *tran;

  for (i = 0; i < tran_table_Gl.tran_count; i++)
    {
      tran = &tran_table_Gl.tran_table[i];

      if (tran->tran_id == tran_id)
	{
	  return tran;
	}
    }

  return NULL;
}

void
print_sql_list_in_tran (int tran_id)
{
  int i;
  TRAN *tran;

  tran = find_tran (tran_id);

  assert (tran != NULL);
  assert (tran->sql_count > 0);

  printf ("=====================================================================================\n");
  printf ("[TRANS-%d]\n\n", tran->tran_id);

  for (i = 0; i < tran->sql_count; i++)
    {
      printf ("[sql-%d] %s\n", i + 1, tran->sql_list[i]);
    }

  printf ("=====================================================================================\n\n");
}

int
is_apply_target_db (void)
{
  return helper_Gl.target_set_count == 3 ? 1 : 0;
}

int
apply_target_db (int tran_id)
{
  int conn_handle, req_handle;
  int exec_retval;
  T_CCI_ERROR err_buf;

  TRAN *tran;

  char cci_url[1024] = { '\0', };

  int error_code;

#if 0
  printf ("helper_Gl.target_server_ip = %s\n", helper_Gl.target_server_ip);
  printf ("helper_Gl.target_server_port = %d\n", helper_Gl.target_server_port);
  printf ("helper_Gl.target_database_name = %s\n", helper_Gl.target_database_name);
  printf ("helper_Gl.dba_user = %s\n", helper_Gl.dba_user);
  printf ("helper_Gl.dba_passwd = %s\n", helper_Gl.dba_passwd);
#endif

  if (helper_Gl.target_conn_handle == -1)
    {
#if 1
      snprintf (cci_url, 1024,
		"cci:cubrid:%s:%d:%s:::?logOnException=true&logSlowQueries=true&logTraceApi=true&logTraceNetwork=true&logFile=cci_debug_target_server.log&logBaseDir=%s",
		helper_Gl.target_server_ip, helper_Gl.target_server_port, helper_Gl.target_database_name, ".");

      conn_handle = cci_connect_with_url_ex (cci_url, helper_Gl.dba_user, helper_Gl.dba_passwd, &err_buf);
#else
      conn_handle =
	cci_connect (helper_Gl.target_server_ip, helper_Gl.target_server_port,
		     helper_Gl.target_database_name, helper_Gl.dba_user, helper_Gl.dba_passwd);
#endif
      if (conn_handle < 0)
	{
	  printf ("[ERROR] [cci] conn_handle=%d, err_code=%d, err_msg=%s\n", conn_handle, err_buf.err_code,
		  err_buf.err_msg);

	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = cci_set_autocommit (conn_handle, CCI_AUTOCOMMIT_FALSE);
      if (error_code < 0)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      helper_Gl.target_conn_handle = conn_handle;
    }
  else
    {
      conn_handle = helper_Gl.target_conn_handle;
    }

  tran = find_tran (tran_id);

  assert (tran != NULL);

  for (int i = 0; i < tran->sql_count; i++)
    {
#if 0
      printf ("tran->sql_list[%d]: %s\n", i, tran->sql_list[i]);
#endif

      req_handle = cci_prepare_and_execute (conn_handle, tran->sql_list[i], 0, &exec_retval, &err_buf);
      if (req_handle < 0)
	{
	  printf ("[ERROR] [cci] req_handle=%d, err_code=%d, err_msg=%s\n", req_handle, err_buf.err_code,
		  err_buf.err_msg);
	}

      if (req_handle != CCI_ER_DBMS)
	{
	  /* CCI_ER_DBMS is transaction error, not a cci internal error (connection, timeout, ...)
	   * SERIAL changes are required to be sent to target even if the transaction is aborted
	   * But, if transaction is aborted by some transaction error (e.g. unique violation)
	   * cci also returns a CCI_ER_DBMS error, and cdc_test_helper can be quitted.
	   * So, cdc_test_helper is allowed to quit only if req_handle is not a CCI_ER_DBMS */

	  error_code = cci_close_req_handle (req_handle);
	  if (error_code < 0 && error_code != CCI_ER_DBMS)
	    {
	      PRINT_ERRMSG_GOTO_ERR (error_code);
	    }
	}
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
unregister_tran (int tran_id)
{
  TRAN *tran;
  int del_idx;

  tran = find_tran (tran_id);

  assert (tran != NULL);

  for (int i = 0; i < tran->sql_count; i++)
    {
      free (tran->sql_list[i]);
    }

  tran->sql_count = 0;

  del_idx = tran - tran_table_Gl.tran_table;

  if (del_idx != tran_table_Gl.tran_count - 1)	// not last
    {
      tran_table_Gl.tran_table[del_idx] = tran_table_Gl.tran_table[tran_table_Gl.tran_count - 1];
    }

  tran_table_Gl.tran_count--;

  assert (tran_table_Gl.tran_count >= 0);

  return NO_ERROR;
}

int
convert_log_item_to_sql (CUBRID_LOG_ITEM * log_item, int output_enabled)
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

      if (log_item->data_item.ddl.object_type == 2)
	{
	  /* register serial in TRAN */
	  register_serial_to_tran (log_item->transaction_id);
	}

      error_code = register_sql_to_tran (log_item->transaction_id, sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

    case 1:
      if (helper_Gl.ignore_trigger_dml && is_trigger_dml (log_item->data_item.dml.dml_type))
	{
	  /* Nothing to do */
	}
      else
	{
	  error_code = convert_dml (&log_item->data_item, &sql);
	  if (error_code != NO_ERROR)
	    {
	      PRINT_ERRMSG_GOTO_ERR (error_code);
	    }

	  error_code = register_sql_to_tran (log_item->transaction_id, sql);
	  if (error_code != NO_ERROR)
	    {
	      PRINT_ERRMSG_GOTO_ERR (error_code);
	    }
	}

      break;

    case 2:
      error_code = convert_dcl (&log_item->data_item, &sql);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = register_sql_to_tran (log_item->transaction_id, sql);
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

  if (output_enabled && log_item->data_item_type != 3 && helper_Gl.disable_print_sql != 1)
    {
      if (log_item->data_item_type == 1 && helper_Gl.ignore_trigger_dml
	  && is_trigger_dml (log_item->data_item.dml.dml_type))
	{
	  /* Nothing to do */
	}
      else
	{
	  printf ("=====================================================================================\n");
	  printf ("[SQL]\n");
	  printf ("transaction_id: %d\n", log_item->transaction_id);
	  printf ("sql: %s\n", sql);
	  printf ("=====================================================================================\n\n");
	}
    }

  if (log_item->data_item_type == 2)
    {
      bool has_serial = false;

      (void) check_if_serial_in_tran (log_item->transaction_id, &has_serial);

      if (helper_Gl.print_transaction)
	{
	  print_sql_list_in_tran (log_item->transaction_id);
	}

      if (is_apply_target_db () && (has_serial || log_item->data_item.dcl.dcl_type == 0))
	{
	  error_code = apply_target_db (log_item->transaction_id);
	  if (error_code != NO_ERROR)
	    {
	      PRINT_ERRMSG_GOTO_ERR (error_code);
	    }
	}

      error_code = unregister_tran (log_item->transaction_id);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
is_ddl_stmt (CUBRID_LOG_ITEM * log_item)
{
  return log_item->data_item_type == 0 ? 1 : 0;
}

int
is_table_object (CUBRID_DATA_ITEM * data_item)
{
  return data_item->ddl.object_type == 0 ? 1 : 0;
}

int
find_table_name (char *sql_buf, char *table_name)
{
  char *s, *e;

  int error_code;

  // 'A' -> 'a'
  for (int i = 0; i < strlen (sql_buf); i++)
    {
      if (sql_buf[i] >= 'A' && sql_buf[i] <= 'Z')
	{
	  sql_buf[i] = sql_buf[i] + 32;
	}
    }

  s = strstr (sql_buf, "table");
  if (s == NULL)
    {
      s = strstr (sql_buf, "class");
    }

  if (s != NULL)
    {
      s = s + 6;		// skipping 'table ' or 'class '
    }
  else
    {
      s = strstr (sql_buf, " ");	// in the case of 'ALTER ddl_0001 drop col8;'
    }

  while (*s == ' ')
    {
      s++;
    }

  e = s;

  while (*e != ' ' && *e != '(' && *e != '\n' && *e != '\0')
    {
      e++;
    }

  *e = '\0';

  strcpy (table_name, s);

#if 0
  printf ("table_name=%s\n", table_name);
#endif

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
find_new_table_name (char *sql_buf, char *old_table_name, char *new_table_name)
{
  char *s, *e;

  int error_code;

  // 'A' -> 'a'
  for (int i = 0; i < strlen (sql_buf); i++)
    {
      if (sql_buf[i] >= 'A' && sql_buf[i] <= 'Z')
	{
	  sql_buf[i] = sql_buf[i] + 32;
	}
    }

  s = strstr (sql_buf, "table");
  if (s == NULL)
    {
      s = strstr (sql_buf, "class");
      if (s == NULL)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}
    }

  s = s + 6;			// skipping 'table ' or 'class '

  while (*s == ' ')
    {
      s++;
    }

  e = s;

  while (*e != ' ')
    {
      e++;
    }

  *e = '\0';

  strcpy (old_table_name, s);

  s = e + 1;
  e = s;

  s = strstr (e, "as");
  if (s == NULL)
    {
      s = strstr (e, "to");
      if (s == NULL)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}
    }

  s = s + 3;			// skipping 'as ' or 'to '

  while (*s == ' ')
    {
      s++;
    }

  e = s;

  while (*e != ' ' && *e != '\0')
    {
      e++;
    }

  *e = '\0';

  strcpy (new_table_name, s);

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
register_class_info (CUBRID_DATA_ITEM * data_item)
{
  char sql_buf[10000] = { '\0', };
  char table_name[100] = { '\0', };

  char *sql_buf_p = NULL;
  int ddl_stmt_len;

  int error_code;

  ddl_stmt_len = strlen (data_item->ddl.statement);

  if (ddl_stmt_len < 10000)
    {
      sql_buf_p = sql_buf;
    }
  else
    {
      sql_buf_p = (char *) malloc (sizeof (char) * (ddl_stmt_len + 1));
      if (sql_buf_p == NULL)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}
    }

  strcpy (sql_buf_p, data_item->ddl.statement);

  error_code = find_table_name (sql_buf_p, table_name);
  if (error_code != NO_ERROR)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

#if 0
  printf ("table_name = %s\n", table_name);
#endif

  sprintf (sql_buf_p,
	   "select class_of, class_name from _db_class where class_name = \'%s\' and is_system_class != 1", table_name);

#if 0
  printf ("sql_buf_p = %s\n", sql_buf_p);
#endif

#if 0
  printf ("[REGISTER] [%s] class_name=%s, class_oid=%lld from data_item\n",
	  convert_ddl_type_to_string (data_item->ddl.ddl_type), table_name, data_item->ddl.classoid);
#endif

  error_code = fetch_schema_info (sql_buf_p);
  if (error_code != NO_ERROR)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  if (sql_buf_p != sql_buf)
    {
      free (sql_buf_p);
    }

#if 0
  if (class_info_Gl.class_info_count >= 4)
    {
      printf ("[%s] [%s] class_name=%s, class_info_Gl.class_info_count=%d\n\n", __func__,
	      convert_ddl_type_to_string (data_item->ddl.ddl_type), table_name, class_info_Gl.class_info_count);

      for (int i = 0; i < class_info_Gl.class_info_count; i++)
	{
	  printf ("class_info_Gl.class_info[%d].class_name=%s, class_info_Gl.class_info[%d].class_oid=%lld\n", i,
		  class_info_Gl.class_info[i].class_name, i, class_info_Gl.class_info[i].class_oid);
	}
    }
#endif

  return NO_ERROR;

error:

  if (sql_buf_p != NULL && sql_buf_p != sql_buf)
    {
      free (sql_buf_p);
    }

  return YES_ERROR;
}

int
unregister_class_info (CUBRID_DATA_ITEM * data_item)
{
  CLASS_INFO *class_info;

  int del_idx;

  int error_code;

  assert (data_item->ddl.classoid != 0);

  class_info = find_class_info (data_item->ddl.classoid);
  if (class_info == NULL)
    {
      goto end;
    }

#if 0
  printf ("[UN-REGISTER] [%s] class_name=%s, class_oid=%lld from class_info, class_oid=%lld from data_item\n",
	  convert_ddl_type_to_string (data_item->ddl.ddl_type), class_info->class_name, class_info->class_oid,
	  data_item->ddl.classoid);

  printf ("[DEC: %d]\n", class_info_Gl.class_info_count - 1);
#endif

  assert (class_info->class_name != NULL);

#if 0
  printf ("class_info->class_name=%s\n", class_info->class_name);
#endif

  free (class_info->class_name);

  class_info->class_name = NULL;

  for (int i = 0; i < class_info->attr_info_count; i++)
    {
      free (class_info->attr_info_p[i].attr_name);

      class_info->attr_info_p[i].attr_name = NULL;
    }

  class_info->attr_info_count = 0;

  if (class_info->is_malloc_attr_info)
    {
      free (class_info->attr_info_p);
    }

  class_info->attr_info_p = NULL;
  class_info->is_malloc_attr_info = 0;

  del_idx = class_info - class_info_Gl.class_info;

  if (del_idx != class_info_Gl.class_info_count - 1)	// not last
    {
      class_info_Gl.class_info[del_idx] = class_info_Gl.class_info[class_info_Gl.class_info_count - 1];
    }

  class_info_Gl.class_info_count--;

  assert (class_info_Gl.class_info_count >= 0);

end:

#if 0
  printf ("[%s] [%s] class_name=%s (class_oid=%lld) (data_item class_oid=%lld), ", __func__,
	  convert_ddl_type_to_string (data_item->ddl.ddl_type),
	  class_info != NULL ? class_info->class_name : "Not found class info",
	  class_info != NULL ? class_info->class_oid : -1, data_item->ddl.classoid);

  if (class_info != NULL)
    {
      free (class_info->class_name);

      class_info->class_name = NULL;
    }
#endif

#if 0
  printf (" class_info_Gl.class_info_count=%d\n", class_info_Gl.class_info_count);
#endif

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
change_table_name_at_class_info (CUBRID_DATA_ITEM * data_item)
{
  CLASS_INFO *class_info;

  char sql_buf[10000] = { '\0', };
  char old_table_name[100] = { '\0', };
  char new_table_name[100] = { '\0', };

  int error_code;

  class_info = find_class_info (data_item->ddl.classoid);
  if (class_info == NULL)
    {
      goto end;
    }

  strcpy (sql_buf, data_item->ddl.statement);

  error_code = find_new_table_name (sql_buf, old_table_name, new_table_name);
  if (error_code != NO_ERROR)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

#if 0
  printf ("old_table_name: %s, new_table_name: %s\n", old_table_name, new_table_name);
#endif

  if (strcmp (class_info->class_name, old_table_name) != 0)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  free (class_info->class_name);

  class_info->class_name = strdup (new_table_name);
  if (class_info->class_name == NULL)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

end:

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
update_class_info (CUBRID_DATA_ITEM * data_item)
{
  int error_code;

  switch (data_item->ddl.ddl_type)
    {
      /* create */
    case 0:
      error_code = register_class_info (data_item);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

      /* alter */
    case 1:
      error_code = unregister_class_info (data_item);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      error_code = register_class_info (data_item);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

      /* drop */
    case 2:
      error_code = unregister_class_info (data_item);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

      /* rename */
    case 3:
      error_code = change_table_name_at_class_info (data_item);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

      /* truncate */
    case 4:
    case 5:
    case 6:

      break;
    default:
      assert (0);
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
is_extraction_class_oid (uint64_t class_oid)
{
  int is_found = 0;

  for (int i = 0; i < helper_Gl.extraction_class_oid_count; i++)
    {
      if (helper_Gl.extraction_class_oid[i] == class_oid)
	{
	  is_found = 1;

	  break;
	}
    }

  return is_found;
}

int
validate_class_oid_for_ddl (CUBRID_DATA_ITEM * data_item)
{
  int error_code;

  switch (data_item->ddl.object_type)
    {
      // table
    case 0:
      if (!is_extraction_class_oid (data_item->ddl.classoid))
	{
	  error_code = YES_ERROR;
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

      // index
    case 1:
      // serial
    case 2:
      // view
    case 3:
      // function
    case 4:
      // procedure
    case 5:
      // trigger
    case 6:
      // user
    case 7:

      break;

    default:
      assert (0);
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
validate_class_oid_for_dml (CUBRID_DATA_ITEM * data_item)
{
  int error_code;

  if (!is_extraction_class_oid (data_item->dml.classoid))
    {
      error_code = YES_ERROR;
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
validate_class_oid_of_log_item (CUBRID_LOG_ITEM * log_item)
{
  CUBRID_DATA_ITEM *data_item;

  int error_code;

  data_item = &log_item->data_item;

  switch (log_item->data_item_type)
    {
      // DDL
    case 0:
      error_code = validate_class_oid_for_ddl (data_item);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

      // DML
    case 1:
      error_code = validate_class_oid_for_dml (data_item);
      if (error_code != NO_ERROR)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

    default:

      break;
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
is_extraction_user_name (char *user_name)
{
  int is_found = 0;

  for (int i = 0; i < helper_Gl.extraction_user_name_count; i++)
    {
      if (!strcmp (helper_Gl.extraction_user_name[i], user_name))
	{
	  is_found = 1;

	  break;
	}
    }

  return is_found;
}

int
validate_user_name_of_log_item (CUBRID_LOG_ITEM * log_item)
{
  int error_code;

  switch (log_item->data_item_type)
    {
      // DDL
    case 0:
      // DML
    case 1:
      // DCL
    case 2:
      if (!is_extraction_user_name (log_item->user))
	{
	  error_code = YES_ERROR;
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      break;

      // TIMER
    case 3:

      break;

    default:
      assert (0);
    }

  return NO_ERROR;

error:

  return YES_ERROR;
}

int
extract_log (void)
{
  time_t start_time;
  time_t requested_start_time;
  uint64_t extract_lsa;

  long long output_log_item_count = 0;
  int extraction_complete = 0;

  int64_t extract_pageid;
  short extract_offset;

  char requested_time_buf[32];
  char resolved_time_buf[32];

  int error_code;

  // -1 ~ 360 (300)
  error_code = cubrid_log_set_connection_timeout (helper_Gl.connection_timeout);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  // -1 ~ 360 (300)
  error_code = cubrid_log_set_extraction_timeout (helper_Gl.extraction_timeout);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  // dir path (".")
  // 0 ~ 2 (0)
  // 10 ~ 512 (8)
  error_code = cubrid_log_set_tracelog ("./tracelog", 0, 8);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  // 1 ~ 1024 (512)
  error_code = cubrid_log_set_max_log_item (helper_Gl.max_log_item);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  // 0 ~ 1 (0)
  error_code = cubrid_log_set_all_in_cond (helper_Gl.all_in_cond);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  if (helper_Gl.extraction_class_oid_count != 0)
    {
      error_code =
	cubrid_log_set_extraction_table (helper_Gl.extraction_class_oid, helper_Gl.extraction_class_oid_count);
      if (error_code != CUBRID_LOG_SUCCESS)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}
    }

  if (helper_Gl.extraction_user_name_count != 0)
    {
#if 0
      for (int i = 0; i < helper_Gl.extraction_user_name_count; i++)
	{
	  printf ("helper_Gl.extraction_user_name[i] = %s\n", helper_Gl.extraction_user_name[i]);
	}
#endif

      error_code =
	cubrid_log_set_extraction_user (helper_Gl.extraction_user_name, helper_Gl.extraction_user_name_count);
      if (error_code != CUBRID_LOG_SUCCESS)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}
    }

  error_code =
    cubrid_log_connect_server (helper_Gl.cdc_server_ip,
			       helper_Gl.cdc_server_port,
			       helper_Gl.database_name, helper_Gl.dba_user, helper_Gl.dba_passwd);
  if (error_code != CUBRID_LOG_SUCCESS)
    {
      PRINT_ERRMSG_GOTO_ERR (error_code);
    }

  if (helper_Gl.start_lsa_set)
    {
      extract_lsa = helper_Gl.start_lsa;
      unpack_lsa (extract_lsa, &extract_pageid, &extract_offset);

      printf ("[CDC_START] requested_lsa=%lld|%hd, raw_lsa=%llu\n", (long long) extract_pageid, extract_offset,
	      (unsigned long long) extract_lsa);
    }
  else
    {
      start_time = helper_Gl.start_time_set ? helper_Gl.start_time : time (NULL);
      requested_start_time = start_time;

      error_code = cubrid_log_find_lsa (&start_time, &extract_lsa);
      if (error_code != CUBRID_LOG_SUCCESS)
	{
	  PRINT_ERRMSG_GOTO_ERR (error_code);
	}

      strftime (requested_time_buf, sizeof (requested_time_buf), "%Y-%m-%d %H:%M:%S",
		localtime (&requested_start_time));
      strftime (resolved_time_buf, sizeof (resolved_time_buf), "%Y-%m-%d %H:%M:%S", localtime (&start_time));

      unpack_lsa (extract_lsa, &extract_pageid, &extract_offset);

      printf ("[CDC_START] requested_time=%s, resolved_time=%s, lsa=%lld|%hd, raw_lsa=%llu\n",
	      requested_time_buf, resolved_time_buf, (long long) extract_pageid, extract_offset,
	      (unsigned long long) extract_lsa);
    }

#if 0
  printf ("[PASS] cubrid_log_find_lsa ()\n");
#endif

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

	printf ("list_size: %d\n", list_size);
#endif

	log_item = log_item_list;

	while (log_item != NULL)
	  {
	    int output_enabled;

	    if (helper_Gl.extraction_class_oid_count != 0)
	      {
		error_code = validate_class_oid_of_log_item (log_item);
		if (error_code != NO_ERROR)
		  {
		    PRINT_ERRMSG_GOTO_ERR (error_code);
		  }
	      }

	    if (helper_Gl.extraction_user_name_count != 0)
	      {
		error_code = validate_user_name_of_log_item (log_item);
		if (error_code != NO_ERROR)
		  {
		    PRINT_ERRMSG_GOTO_ERR (error_code);
		  }
	      }

	    output_enabled = should_output_log_item (log_item);

	    error_code = print_log_item (log_item, output_enabled);
	    if (error_code != NO_ERROR)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    error_code = convert_log_item_to_sql (log_item, output_enabled);
	    if (error_code != NO_ERROR)
	      {
		PRINT_ERRMSG_GOTO_ERR (error_code);
	      }

	    if (is_ddl_stmt (log_item) && is_table_object (&log_item->data_item))
	      {
		error_code = update_class_info (&log_item->data_item);
		if (error_code != NO_ERROR)
		  {
		    PRINT_ERRMSG_GOTO_ERR (error_code);
		  }
	      }

	    if (output_enabled)
	      {
		output_log_item_count++;

		if (helper_Gl.max_log_items > 0 && output_log_item_count >= helper_Gl.max_log_items)
		  {
		    extraction_complete = 1;
		    break;
		  }
	      }

	    log_item = log_item->next;
	  }

	error_code = cubrid_log_clear_log_item (log_item_list);
	if (error_code != CUBRID_LOG_SUCCESS)
	  {
	    PRINT_ERRMSG_GOTO_ERR (error_code);
	  }

	if (extraction_complete)
	  {
	    break;
	  }
      }
  }

  printf ("[CDC_DONE] output_log_items=%lld\n", output_log_item_count);

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
