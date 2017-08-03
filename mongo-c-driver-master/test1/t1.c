/*
 * t1.c
 *
 *  Created on: 2017年8月1日
 *      Author: root
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <mongo.h>

int main(int argc, char *argv[])
{
	bson b;
	mongo conn;
	mongo_init(&conn);

	mongo_client(&conn, "127.0.0.1", 27017);
//
//      bson out;
//      bson_init(&out);
//      bson_append_string(&out, "name", "yangmeng1");
//      bson_finish(&out);
//
//      while (1)
//      {
//
//              mongo_insert(&conn, "test.test", &out, NULL);
//      }
//
//      bson_destroy(&out);

	mongo_cursor *cursor = mongo_find(&conn, "test.test",
					  bson_empty(&b), bson_empty(&b),
					  0, 0, 0);

	int count = 0;

	while (mongo_cursor_next(cursor) == MONGO_OK) {
		printf("%s\n", b.cur);
		count++;
	}

	printf("count = %d\n", count);
	return 0;
}
