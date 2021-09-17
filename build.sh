indent -l120 -lc120 cdc_test_helper.c

gcc -o cdc_test_helper -g -I$CUBRID/include -L$CUBRID/lib -lcubridcs -I./CUBRID-CCI-11.1.0.0385-cf85a8a-Linux.x86_64-debug/include -L./CUBRID-CCI-11.1.0.0385-cf85a8a-Linux.x86_64-debug/lib -lcascci cdc_test_helper.c
