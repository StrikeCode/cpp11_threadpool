#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHOLD = 4;
ThreadPool::ThreadPool()
	: initThreadSize_(4)
	, taskSize_(0)
	, taskQueMaxThreadhold_(TASK_MAX_THRESHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
{

}

ThreadPool::~ThreadPool()
{}

// 设置线程池工作模式
void ThreadPool::setMode(PoolMode mode)
{
	poolMode_ = mode;
}

// 设置task任务队列上限阈值
void ThreadPool::settaskQueMaxThreadhold(int threshhold)
{
	taskQueMaxThreadhold_ = threshhold;
}

// 给线程池提交任务
// 这是一个对队列的操作，有临界资源
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lck(taskQueMtx_);
	// 线程的通信, 等待队列有空余
	//while (taskQue_.size() == taskQueMaxThreadhold_)
	//{
	//	notFull_.wait(lck); // 等待队列不满
	//}

	// 等价于上面的while
	// 满足第二项的pred才能退出while
	// 等1s没出来就退出返回
	if (!notFull_.wait_for(lck, std::chrono::seconds(1), [&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreadhold_ ;}))
	{
		std::cerr << "task queue is full, submit task fail" << std::endl;
		return Result(sp, false);//
	}

	// 如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++; // ???

	// 任务队列不空，唤醒notEmpty_的wait,即通知消费者消费
	notEmpty_.notify_all();

	// 返回任务的Result对象
	return Result(sp);
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
	// 记录初始线程个数
	initThreadSize_ = initThreadSize;

	// 创建线程对象
	// 把线程函数给到thread线程对象
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		// unique_ptr的拷贝构造和赋值都被delete,所以要把ptr转为右值
		threads_.emplace_back(std::move(ptr));
	}

	// 启动所有线程
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // 需要去执行一个线程函数
	}
}

// 线程池里所有任务消费任务
void ThreadPool::threadFunc()
{
	/*std::cout << " begin threadFunc tid:" << std::this_thread::get_id() << std::endl;
	std::cout << " endthreadFunc tid:" << std::this_thread::get_id() << std::endl;*/
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// 获取锁
			std::unique_lock<std::mutex> lck(taskQueMtx_);
			std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
			// 等待notEmpty_条件
			notEmpty_.wait(lck, [&]()->bool { return taskQue_.size() > 0; });

			std::cout << "tid:" << std::this_thread::get_id() << "获取任务成功..." << std::endl;
			// 从任务队列中取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 若依然有剩余任务，继续通知其他线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// 取出一个任务进行通知 生产者可以进行生产任务
			notFull_.notify_all();
		} // 只需要对taskQue_及相关临界资源操作才加锁，执行不需要加锁

		// 当前线程负责执行这个任务
		if (task != nullptr)
		{
			//task->run(); 
			// 执行任务，把任务的返回值setVal方法给到Result
			task->exec();
		}
		
	}	
}

//////////////////////// class Thread implementation

Thread::Thread(ThreadFunc func)
	: func_(func)
{

}
Thread::~Thread()
{

}
// 启动线程
void Thread::start()
{
	// 创建一个线程来执行一个线程函数
	std::thread t(func_);
	t.detach(); //  设置分离线程，t线程自己控制自己生命周期
}
//////////////////////// class Task implementation
Task::Task()
	: result_(nullptr)
{}
void Task::exec()
{
	if (nullptr != result_)
	{
		result_->setVal(run());
	}
	
}

void Task::setResult(Result* res)
{
	result_ = res;
}

//////////////////////// class Result implementation
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: isValid_(isValid)
	, task_(task)
{
	task->setResult(this);
}

//获取任务执行后的返回值
void Result::setVal(Any any)
{
	// 存储task的返回值
	this->any_ = std::move(any);  // Any 类只提供右值拷贝和移动赋值函数
	sem_.post(); // 获取任务的返回值，增加信号量资源
}

// get方法，用户调用这个方法获取task的返回值
Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); // task任务如果没执行完，这里会阻塞用户线程
	return std::move(any_); // Any不提供左值拷贝构造和拷贝赋值
}