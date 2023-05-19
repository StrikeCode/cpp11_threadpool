#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // 线程最大空闲时间，超过则回收
ThreadPool::ThreadPool()
	: initThreadSize_(4)
	, taskSize_(0)
	, idleThreadSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{

}

ThreadPool::~ThreadPool()
{
	isPoolRunning_ = false;
	// 等待线程池里所有线程返回，两种状态：阻塞 或 正在执行任务中
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all(); // 通知所有消费者，让它们检测到运行状态为false可以退出了
	// 等待线程销毁
	exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}

// 设置线程池工作模式
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

// 设置task任务队列上限阈值
void ThreadPool::setTaskQueMaxThreadhold(int threshhold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

void ThreadPool::setThreadSizeThreadhold(int threshhold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}

// 给线程池提交任务
// 这是一个对队列的操作，有临界资源
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lck(taskQueMtx_);
	// 线程的通信, 等待队列有空余
	//while (taskQue_.size() == taskQueMaxThreshHold_)
	//{
	//	notFull_.wait(lck); // 等待队列不满
	//}

	// 等价于上面的while
	// 满足第二项的pred才能退出while
	// 等1s没出来就退出返回
	if (!notFull_.wait_for(lck, std::chrono::seconds(1), [&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_ ;}))
	{
		std::cerr << "task queue is full, submit task fail" << std::endl;
		return Result(sp, false);//
	}

	// 如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++; // ???

	// 任务队列不空，唤醒notEmpty_的wait,即通知消费者消费
	notEmpty_.notify_all();

	// cached 任务处理比较紧急，场景：小而快的任务
	// 同时需要根据任务数量和空闲线程数量，判断是否需要创建新的线程
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << ">>> create new thread..." << std::endl;

		// 创建新的线程对象
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		// unique_ptr的拷贝构造和赋值都被delete,所以要把ptr转为右值
		threads_.emplace(threadId, std::move(ptr));

		// 启动线程
		threads_[threadId]->start();

		curThreadSize_++;
		idleThreadSize_++;
	}

	// 返回任务的Result对象
	return Result(sp);
}

// 开启线程池
void ThreadPool::start(int initThreadSize)
{
	// 设置线程池运行状态
	isPoolRunning_ = true;

	// 记录初始线程个数
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// 创建线程对象
	// 把线程函数给到thread线程对象
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		// unique_ptr的拷贝构造和赋值都被delete,所以要把ptr转为右值
		threads_.emplace(threadId, std::move(ptr));
	}

	// 启动所有线程
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // 需要去执行一个线程函数
		idleThreadSize_++; // 空闲线程数+1（cached模式）
	}
}

// 线程池里所有任务消费任务
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	

	// 所有任务必须执行完成，线程池才可以回收所有线程资源
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// 获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			std::cout << "tid:" << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
			
			// cached模式下，可能创建了很多线程，
			// 但是空闲时间超过60s，应该把多余的线程回收掉(但是要保证线程数量>= initThreadSize_)
			// 当前时间 - 上一次线程执行时间 > 60s

			// 每秒中判断一次，需要区分超时返回 和 有任务待返回
			// 使用 锁 + 双重判断
			
			// 等待notEmpty_条件
			// notEmpty_.wait(lck, [&]()->bool { return taskQue_.size() > 0; });
			
			// 没有任务的处理
			while (taskQue_.size() == 0)
			{
				// 线程池结束，回收线程资源
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// 超时，这里是用来实现每1s检测一次当前线程空闲时间是否达到删除条件
					if(std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds> (now - lastTime);

						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// 回收当前现线程
							// 记录线程数量相关变量的修改
							// 将线程对象从线程列表容器中删除
							// threadid => Thread对象 => erase
							threads_.erase(threadid); // 注意不是删除std::this_thread::get_id()
							curThreadSize_--;
							idleThreadSize_--; // 这里线程肯定是空闲的，因为压根没执行任务

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
							return;
						}
					}
				}
				else // fixed模式
				{
					notEmpty_.wait(lock);
				}
			}


			idleThreadSize_--; // 执行任务时，空闲线程-1

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
		// 执行任务后空闲线程 + 1
		idleThreadSize_++;
		auto lastTime = std::chrono::high_resolution_clock().now();
	}	
}

bool ThreadPool::checkRunningState() const
{
	return isPoolRunning_;
}

//////////////////////// class Thread implementation

int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func)
	: func_(func)
	,threadId_(generateId_++)
{

}
Thread::~Thread()
{

}
// 启动线程
void Thread::start()
{
	// 创建一个线程来执行一个线程函数
	std::thread t(func_, threadId_);
	t.detach(); //  设置分离线程，t线程自己控制自己生命周期
}

int Thread::getId() const
{
	return threadId_;
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

//获取任务执行后的返回值
void Result::setVal(Any any) // Task::exec时调用该方法
{
	// 存储task的返回值
	this->any_ = std::move(any);  // Any 类只提供右值拷贝和移动赋值函数
	sem_.post(); // 获取任务的返回值，增加信号量资源
}

