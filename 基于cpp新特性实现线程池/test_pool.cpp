#include <iostream>
#include <functional>
#include <thread>
#include <future>
#include <chrono>
#include "threadpool.h"

using namespace std;

int sum1(int a, int b)
{
	this_thread::sleep_for(chrono::seconds(2));
	return a + b;
}

int sum2(int a, int b, int c)
{
	this_thread::sleep_for(chrono::seconds(2));
	return a + b + c;
}

int main()
{

	ThreadPool pool;
	//pool.setMode(PoolMode::MODE_CACHED);
	pool.start(2);
	future<int> r1 = pool.submitTask(sum1, 10, 20);
	future<int> r2 = pool.submitTask(sum2, 10, 20, 30);
	future<int> r3 = pool.submitTask([](int b, int e)->int {
		int sum = 0;
		for (int i = b; i <= e; i++) sum += i;
		return sum;
		}, 1, 100);

	future<int> r4 = pool.submitTask([](int b, int e)->int {
		int sum = 0;
		for (int i = b; i <= e; i++) sum += i;
		return sum;
		}, 1, 100);

	future<int> r5 = pool.submitTask([](int b, int e)->int {
		int sum = 0;
		for (int i = b; i <= e; i++) sum += i;
		return sum;
		}, 1, 100);
	//future<int> r1 = pool.submitTask(sum1, 10, 20);
	cout << r1.get() << endl;
	cout << r2.get() << endl;
	cout << r3.get() << endl;
	cout << r4.get() << endl;
	cout << r5.get() << endl;
	
	//// �� sum1 ��װ��һ���������
	//packaged_task<int(int, int)> task(sum1); 
	//future<int> res = task.get_future(); // future�൱���ϸ��汾��Result��
	////task(10, 20);
	//// packaged_task�ѽ��ÿ�������Ϳ�����ֵ
	//thread(std::move(task), 10, 20); // �����߳���ִ������
	//cout << res.get() << endl; // ����ûִ���������
	
	return 0;
}