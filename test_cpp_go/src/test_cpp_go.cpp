#include <iostream>
#include <map>
#include <thread>
#include "coroutine.h"
using namespace std;

extern int test_coroutine_1();
extern int test_coroutine_2();
extern int test_coroutine_3();
extern int test_timer1();
extern int test_timer2();
extern int test_timer3();
extern int test_mutex1();
extern int test_mutex2();
extern int test_channel_1();
extern int test_channel_2();
extern int test_channel_3();
extern void test_huge_go();
extern void test_condition_variable();
extern void test_pingpong(int argc, char *argv[]);

int main(int argc, char *argv[])
{
	//test_condition_variable();
	test_pingpong(argc, argv);

	return 0; 
}