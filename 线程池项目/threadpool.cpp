#include "threadpool.h"
#include <functional>
#include <thread>
#include <iostream>

const int TASK_MAX_THRESHHOLD = INT32_MAX;
const int THREAD_MAX_THRESHHOLD = 1024;
const int THREAD_MAX_IDLE_TIME = 60; // �߳�������ʱ�䣬���������
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
	// �ȴ��̳߳��������̷߳��أ�����״̬������ �� ����ִ��������
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all(); // ֪ͨ���������ߣ������Ǽ�⵽����״̬Ϊfalse�����˳���
	// �ȴ��߳�����
	exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}

// �����̳߳ع���ģʽ
void ThreadPool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

// ����task�������������ֵ
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

// ���̳߳��ύ����
// ����һ���Զ��еĲ��������ٽ���Դ
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
	// ��ȡ��
	std::unique_lock<std::mutex> lck(taskQueMtx_);
	// �̵߳�ͨ��, �ȴ������п���
	//while (taskQue_.size() == taskQueMaxThreshHold_)
	//{
	//	notFull_.wait(lck); // �ȴ����в���
	//}

	// �ȼ��������while
	// ����ڶ����pred�����˳�while
	// ��1sû�������˳�����
	if (!notFull_.wait_for(lck, std::chrono::seconds(1), [&]()->bool { return taskQue_.size() < (size_t)taskQueMaxThreshHold_ ;}))
	{
		std::cerr << "task queue is full, submit task fail" << std::endl;
		return Result(sp, false);//
	}

	// ����п��࣬������������������
	taskQue_.emplace(sp);
	taskSize_++; // ???

	// ������в��գ�����notEmpty_��wait,��֪ͨ����������
	notEmpty_.notify_all();

	// cached ������ȽϽ�����������С���������
	// ͬʱ��Ҫ�������������Ϳ����߳��������ж��Ƿ���Ҫ�����µ��߳�
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_
		&& curThreadSize_ < threadSizeThreshHold_)
	{
		std::cout << ">>> create new thread..." << std::endl;

		// �����µ��̶߳���
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		// unique_ptr�Ŀ�������͸�ֵ����delete,����Ҫ��ptrתΪ��ֵ
		threads_.emplace(threadId, std::move(ptr));

		// �����߳�
		threads_[threadId]->start();

		curThreadSize_++;
		idleThreadSize_++;
	}

	// ���������Result����
	return Result(sp);
}

// �����̳߳�
void ThreadPool::start(int initThreadSize)
{
	// �����̳߳�����״̬
	isPoolRunning_ = true;

	// ��¼��ʼ�̸߳���
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;

	// �����̶߳���
	// ���̺߳�������thread�̶߳���
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
		int threadId = ptr->getId();
		// unique_ptr�Ŀ�������͸�ֵ����delete,����Ҫ��ptrתΪ��ֵ
		threads_.emplace(threadId, std::move(ptr));
	}

	// ���������߳�
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start(); // ��Ҫȥִ��һ���̺߳���
		idleThreadSize_++; // �����߳���+1��cachedģʽ��
	}
}

// �̳߳�������������������
void ThreadPool::threadFunc(int threadid)
{
	auto lastTime = std::chrono::high_resolution_clock().now();
	

	// �����������ִ����ɣ��̳߳زſ��Ի��������߳���Դ
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// ��ȡ��
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			std::cout << "tid:" << std::this_thread::get_id() << "���Ի�ȡ����..." << std::endl;
			
			// cachedģʽ�£����ܴ����˺ܶ��̣߳�
			// ���ǿ���ʱ�䳬��60s��Ӧ�ðѶ�����̻߳��յ�(����Ҫ��֤�߳�����>= initThreadSize_)
			// ��ǰʱ�� - ��һ���߳�ִ��ʱ�� > 60s

			// ÿ�����ж�һ�Σ���Ҫ���ֳ�ʱ���� �� �����������
			// ʹ�� �� + ˫���ж�
			
			// �ȴ�notEmpty_����
			// notEmpty_.wait(lck, [&]()->bool { return taskQue_.size() > 0; });
			
			// û������Ĵ���
			while (taskQue_.size() == 0)
			{
				// �̳߳ؽ����������߳���Դ
				if (!isPoolRunning_)
				{
					threads_.erase(threadid);
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
					exitCond_.notify_all();
					return;
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// ��ʱ������������ʵ��ÿ1s���һ�ε�ǰ�߳̿���ʱ���Ƿ�ﵽɾ������
					if(std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds> (now - lastTime);

						if (dur.count() >= THREAD_MAX_IDLE_TIME
							&& curThreadSize_ > initThreadSize_)
						{
							// ���յ�ǰ���߳�
							// ��¼�߳�������ر������޸�
							// ���̶߳�����߳��б�������ɾ��
							// threadid => Thread���� => erase
							threads_.erase(threadid); // ע�ⲻ��ɾ��std::this_thread::get_id()
							curThreadSize_--;
							idleThreadSize_--; // �����߳̿϶��ǿ��еģ���Ϊѹ��ûִ������

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!" << std::endl;
							return;
						}
					}
				}
				else // fixedģʽ
				{
					notEmpty_.wait(lock);
				}
			}


			idleThreadSize_--; // ִ������ʱ�������߳�-1

			std::cout << "tid:" << std::this_thread::get_id() << "��ȡ����ɹ�..." << std::endl;
			// �����������ȡһ���������
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// ����Ȼ��ʣ�����񣬼���֪ͨ�����߳�ִ������
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// ȡ��һ���������֪ͨ �����߿��Խ�����������
			notFull_.notify_all();
		} // ֻ��Ҫ��taskQue_������ٽ���Դ�����ż�����ִ�в���Ҫ����

		// ��ǰ�̸߳���ִ���������
		if (task != nullptr)
		{
			//task->run(); 
			// ִ�����񣬰�����ķ���ֵsetVal��������Result
			task->exec();
		}
		// ִ�����������߳� + 1
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
// �����߳�
void Thread::start()
{
	// ����һ���߳���ִ��һ���̺߳���
	std::thread t(func_, threadId_);
	t.detach(); //  ���÷����̣߳�t�߳��Լ������Լ���������
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

// get�������û��������������ȡtask�ķ���ֵ
Any Result::get()
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait(); // task�������ûִ���꣬����������û��߳�
	return std::move(any_); // Any���ṩ��ֵ��������Ϳ�����ֵ
}

//��ȡ����ִ�к�ķ���ֵ
void Result::setVal(Any any) // Task::execʱ���ø÷���
{
	// �洢task�ķ���ֵ
	this->any_ = std::move(any);  // Any ��ֻ�ṩ��ֵ�������ƶ���ֵ����
	sem_.post(); // ��ȡ����ķ���ֵ�������ź�����Դ
}

