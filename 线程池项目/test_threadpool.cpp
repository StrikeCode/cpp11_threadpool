#include <iostream>
#include <chrono>
#include "threadpool.h"

using uLong = unsigned long long;

class MyTask : public Task
{
public:
	MyTask(uLong /*int*/ begin, uLong /*int*/ end)
		: begin_(begin)
		, end_(end)
	{}

	Any run()
	{
		std::cout << "tid:" << std::this_thread::get_id() << " begin!" << std::endl;
		//std::this_thread::sleep_for(std::chrono::seconds(5));
		uLong sum = 0;
		for (uLong i = begin_; i <= end_; i++)
		{
			sum += i;
		}
		std::cout << "tid:" << std::this_thread::get_id() << " end!" << std::endl;
		
		return sum;
	}
private:
	uLong /*int*/ begin_;
	uLong /*int*/ end_;
};
int main()
{
#if 0
	ThreadPool pool;
	
	pool.start(4);

	Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
	uLong sum1 = res1.get().cast_<uLong>();

	std::cout << sum1 << std::endl;
	std::cout << "main over!" << std::endl;
	
#elif 1
	ThreadPool pool;
	// 设置为可伸缩模式
	pool.setMode(PoolMode::MODE_CACHED);
	pool.start(4);
	
	Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 1000000000));
	Result res2 = pool.submitTask(std::make_shared<MyTask>(1000000001, 2000000000));
	Result res3 = pool.submitTask(std::make_shared<MyTask>(2000000001, 3000000000));
	pool.submitTask(std::make_shared<MyTask>(2000000001, 3000000000));
	pool.submitTask(std::make_shared<MyTask>(2000000001, 3000000000));
	pool.submitTask(std::make_shared<MyTask>(2000000001, 3000000000));
	
	uLong sum1 = res1.get().cast_<uLong>();
	uLong sum2 = res2.get().cast_<uLong>();
	uLong sum3 = res3.get().cast_<uLong>();

	// Master - Slave线程模型
	std::cout << (sum1 + sum2 + sum3) << std::endl;
	
	getchar();
#endif
	return 0;
}