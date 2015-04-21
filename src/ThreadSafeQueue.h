#ifndef THREADSAFEQUEUE_H
#define THREADSAFEQUEUE_H

#include "iostream"
#include <list>
using namespace std;
#include "MonitorUtil.h"

/*
template <typename C>
class CThreadSafeQueue : protected list<C>
{
public:
	CThreadSafeQueue(int nMaxCount)
	{
		m_bOverflow = false;
		m_hSemaphore = ::CreateSemaphore(
			NULL,		//����ȫ������
			0 ,			//��ʼֵ
			nMaxCount,	//���ֵ
			NULL);		//��������
		InitCS(&m_Crit);
	}
	~CThreadSafeQueue()
	{
		DestCS(&m_Crit);
		::CloseHandle(m_hSemaphore);
		m_hSemaphore = NULL;
	}

	void push(C& c)
	{
		CSLock lock(m_Crit);
		push_back(c);
		lock.Unlock();
		//���ڶ�ָ�����ź�������ָ����ֵ
		if (!::ReleaseSemaphore(m_hSemaphore, 1, NULL))
		{
			//����ź�����,ɾ��
			pop_back();
			if (GetLastError() == ERROR_TOO_MANY_POSTS)
			{
				m_bOverflow = true;
			}
		}
	}
	//�����ֹһ�ε���pop(),��¼�ź�������
	//FIXME: û��Ҫ����ʧ��
	bool pop(C& c)
	{
		CSLock lock(m_Crit);
		//����ǿգ��᲻������?
		if (empty()) 
		{
			//������Ϊ������ź�����signal
			while (::WaitForSingleObject(m_hSemaphore, 0) != WAIT_TIMEOUT)
				1;
			return false;
		}
		c = front();
		pop_front();
		return true;
	} 

	//������ʹ������Ķ���
	void clear()
	{
		CSLock lock(m_Crit);

		for (DWORD i=0; i<size(); i++)
			WaitForSingleObject(m_hSemaphore, 0);

		__super::clear();

		m_bOverflow = false;
	}
	//���
	bool overflow()
	{
		return m_bOverflow;
	}
	//��þ��
	HANDLE GetWaitHandle() { return m_hSemaphore; }
protected:
	HANDLE m_hSemaphore;
	CRITICAL_SECTION m_Crit;
	bool m_bOverflow;
};
*/

//֧������������event�����źŵƣ�ȥ��������������
template <typename C>
class CThreadSafeQueuePro : protected list<C>
{
public:
	CThreadSafeQueuePro()
	{
		m_event = ::CreateEvent(NULL, FALSE, FALSE, NULL);
		InitCS(&m_Crit);
	}
	~CThreadSafeQueuePro()
	{
		DestCS(&m_Crit);
		::CloseHandle(m_event);
		m_event = NULL;
	}

	void push(C& c)
	{
		CSLock lock(m_Crit);
		push_back(c);
		::SetEvent(m_event);
	}

	bool pop(C& c)
	{
		CSLock lock(m_Crit);
		//����ǿգ��᲻������?
		if (empty()) 
		{
			return false;
		}
		c = front();
		pop_front();
		if(!empty())
			::SetEvent(m_event);	//���źţ�������һ��
		return true;
	} 

	void push_n(list<C>& li)
	{
		CSLock lock(m_Crit);
		splice(end(),li);
		::SetEvent(m_event);
	}

	void pop_all(list<C>& li)
	{
		CSLock lock(m_Crit);
		li.clear();
		swap(li);
	}

	//�����������Ҫ���ѵȴ����߳�
	void clear()
	{
		CSLock lock(m_Crit);
		ResetEvent(m_event);
		__super::clear();
	}

	template<typename T>
	void clear_invalid(const T & arg, bool (*isvalid)(const C & item, const T &))
	{
		dlog("in clear_invalid,", true);
		CSLock lock(m_Crit);
		//cout << "size:" << this->size() << endl;
		list<C>::iterator it = begin();
		for (; it != end();)
		{
			it = isvalid(*it, arg) ? ++it : erase(it);
		}
	}

	bool overflow(){ return false; }
	//��þ��
	HANDLE GetWaitHandle() { return m_event; }
protected:
	HANDLE m_event;
	CRITICAL_SECTION m_Crit;
};
#endif
