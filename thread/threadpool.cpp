#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdint.h>
#include <unistd.h>
#include "threadpool.h"
#include "threadtask.h"
#include "taskthread.h"

using namespace std;


#if 0
	//����C++�ӿ�
	//���������ļ�
	std::string strIniFile = GetMoudlePath();
	strIniFile += "GB28181.ini";
	cosip soip(strIniFile);

	//��ʼ������ ��jrtp�����
	soip.StartPSStream(0);

	while (true)
	{
		//���ϻ�ȡ��Ƶ��
		//����
		
		//sdl��ʾ
	}
#endif


//�̳߳ع��캯��
CThreadPool::CThreadPool(size_t maxthreadnum)
{
	MaxThreadNum = maxthreadnum;		//	����̸߳���
	bDestroy = false;					//	�Ƿ������̳߳�
	bWaitDestroy = false;               //	�Ƿ�ȴ�����
	WatiTime = 500;						//	�ȴ�ʱ��
	pthread_mutex_init(&m_taskThreadMutex, NULL);	//	��ʼ�����������
	pthread_cond_init(&m_threadCond, NULL);			//	��ʼ����������
	pthread_mutex_init(&m_freeThreadMutex, NULL);	//	��ʼ���̶߳�����
}

//��������󼰳�Ա
CThreadPool::~CThreadPool()
{
	cout << "�̳߳���������" << endl;

	//�ֱ��ͷ��̶߳��������������������������
	pthread_mutex_destroy(&m_taskThreadMutex);
	pthread_cond_destroy(&m_threadCond);
	pthread_mutex_destroy(&m_freeThreadMutex);
}

//��������
void CThreadPool::AddAsynTask(TaskFunc taskfunc, void* pParams)
{
	//��������ʼ��һ���߳�����
	CThreadTask *task = new CThreadTask(taskfunc, pParams);
	LockTaskQueue();			//	��ס�������
	m_queTask.push(task);		//	�������񵽶���
	SignalTaskQueue();			//	֪ͨ����ִ��
	UnLockTaskQueue();			//	�������
}

//�����̳߳�
void CThreadPool::Activate()
{
	for (unsigned i = 0; i < MaxThreadNum; ++i)
	{
		CTaskThread* thread = new CTaskThread(this);
		m_arrThreads.push_back(thread);
		thread->Start();
	}

	//����ɨ���߳�
	pthread_create(&m_taskThread, NULL, &ScanTask, this);
}

//�����̳߳�
void CThreadPool::Destroy()
{
	cout << "Destroy begin" << endl;
	bDestroy = true;
	SignalTaskQueue();	//֪ͨ�߳��˳��ȴ���
	
	//ֹͣɨ���߳�
	pthread_join(m_taskThread, NULL);

	//ֹͣ�����߳�
	size_t size = m_arrThreads.size();
	for (size_t i = 0; i < size; ++i)
	{
		CTaskThread* thread = m_arrThreads[i];
		thread->Destroy();
		delete thread;
	}

	size_t remain = m_queTask.size();
	for (size_t i = 0; i < remain; ++i)
	{
		CThreadTask *task = m_queTask.front();
		m_queTask.pop();
		delete task;
	}

	cout << "Destroy end" << endl;
}

//���������߳��������
void CThreadPool::WaitTaskFinishAndDestroy()
{
	bWaitDestroy = true;
	pthread_join(m_taskThread, NULL);

	//ֹͣ�����߳�
	for (size_t i = 0; i < m_arrThreads.size(); ++i)
	{
		CTaskThread* thread = m_arrThreads[i];
		thread->Destroy();
		cout << "thread->Destroy()" << endl;
		delete thread;
	}
}

//���ӿ����߳̽��̳߳�
void CThreadPool::AddFreeThreadToQueue(CTaskThread *thread)
{
	//�ȴ���һ������
	LockFreeThreadQueue();
	m_queFreeThreads.push(thread);
	UnLockFreeThreadQueue();
}

//�ı�����
void CThreadPool::SignalTaskQueue()
{
	pthread_cond_signal(&m_threadCond);
}

//��ס�������
void CThreadPool::LockTaskQueue()
{
	pthread_mutex_lock(&m_taskThreadMutex);
}

void CThreadPool::UnLockTaskQueue()
{
	pthread_mutex_unlock(&m_taskThreadMutex);
}

//��ס�����̶߳���
void CThreadPool::LockFreeThreadQueue()
{
	pthread_mutex_lock(&m_freeThreadMutex);
}

void CThreadPool::UnLockFreeThreadQueue()
{
	pthread_mutex_unlock(&m_freeThreadMutex);
}
//ɨ������
void* CThreadPool::ScanTask(void* pParams)
{
	CThreadPool *pool = (CThreadPool*)pParams;
	size_t queNum = 0;
	size_t freeQueNum = 0;
	while (true)
	{
		if (pool->bDestroy)
			break;
		pool->Start(&queNum, &freeQueNum);

		//�����������״̬�����߳�������Ϊ���ֱ������
		if (pool->bWaitDestroy && !queNum && freeQueNum == pool->GetMaxThreadNum())
		{
			pool->bDestroy = true;
			break;
		}
		
		//���������Ϊ�����˳�
		if (queNum)
		{
			if (freeQueNum)
				continue;
			else
				sleep(pool->GetWatiTime());		//�޿����߳�
		}
		else//�����������
		{	
			pool->LockTaskQueue();
			cout << pool->GetQueueTaskCount() << endl;
			if (!pool->GetQueueTaskCount())
			{
				cout << "����ȴ�" << endl;
				pool->WaitQueueTaskDignal();
				cout << "�����ȴ�" << endl;
			}
			pool->UnLockTaskQueue();
		}
	}
	cout << "Scantask end" << endl;
	return NULL;
}

int CThreadPool::GetQueueTaskCount()
{
	return m_queTask.size();
}
void CThreadPool::WaitQueueTaskDignal()
{
	pthread_cond_wait(&m_threadCond, &m_taskThreadMutex);
}

/*
�������ܣ�ִ���߳�
�������1��������ʣ����������
�������2��������ʣ������߳�����
*/
void CThreadPool::Start(size_t *queue_remain_num, size_t *free_thread_num)
{
	cout << "��ʼִ������" << endl;
	LockFreeThreadQueue();
	if (!m_queFreeThreads.empty())
	{
		LockTaskQueue();
		if (!m_queTask.empty())
		{
			CThreadTask* task = m_queTask.front();
			m_queTask.pop();
			CTaskThread* freeThread = m_queFreeThreads.front();
			m_queFreeThreads.pop();
			freeThread->m_task = task;
			freeThread->Notify();

			*queue_remain_num = m_queTask.size();
		}
		else
		{
			*queue_remain_num = 0;
		}

		UnLockTaskQueue();

		*free_thread_num = m_queFreeThreads.size();
	}
	else
	{
		*free_thread_num = 0;
	}
	UnLockFreeThreadQueue();
	cout << "����ִ�����" << endl;
}