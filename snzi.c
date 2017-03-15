#include <stdio.h>

#include "cmpxchg.h"
#include "snzi.h"

int main() {
	struct snzi obj;
	snzi_init(&obj);

	printf("query: %d\n", snzi_query(&obj));

	snzi_inc(&obj, 0);
	snzi_inc(&obj, 1);

	printf("query: %d\n", snzi_query(&obj));

	snzi_dec(&obj, 1);

	printf("query: %d\n", snzi_query(&obj));

	snzi_dec(&obj, 0);

	snzi_inc(&obj, 2);
	snzi_dec(&obj, 2);

	printf("query: %d\n", snzi_query(&obj));

	printf("query: %d\n", snzi_query(&obj));

	return 0;
}
