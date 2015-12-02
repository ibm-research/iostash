#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define _TEST_MAIN_
#include "sce.h"



void *thread1(void *parg)
{
	printf("Hello, thread\n");
	return NULL;
}

int main()
{
	pthread_t test_thread;

	int ret = 0;

	printf("# SCE simulation test\n");
	pthread_create(&test_thread, NULL, thread1, NULL);


	pthread_join(test_thread, NULL);

	return ret;
}

