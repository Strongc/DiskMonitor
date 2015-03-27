#ifndef READDIRECTORYCHANGESPRIVATE_H 
#define READDIRECTORYCHANGESPRIVATE_H

#include "MonitorUtil.h"
#include <assert.h>
#include <iostream>
#include <vector>
//#include <atlstr.h>
using namespace std;

class CReadDirectoryChanges;
//namespace ReadDirectoryChangesPrivate
//{
class CReadChangesServer;

// All functions in CReadChangesRequest run in the context of the worker thread.
// One instance of this object is created for each call to AddDirectory().
// ���request�������ǵ�һ������һ���Ϳ����ˣ�����û�������ļ��к��ļ�
// ��������ϵͳע�������ص���һ���ļ��е�ѡ�һ���ļ�ѡ����������룻
// *** ���ǣ�������Ȼ���Է����ļ��к��ļ���ѡ����Ǵ������й��ɵ�˳����Ѱ��ģʽ����̫��
// *** �����ҷ��������Ǻ�Ϊһ�����������ϲ�����¼�ļ��еļ�¼����ʵ����ɾ��ʱ��û���������ͣ�
class CReadChangesRequest
{
public:
	CReadChangesRequest(CReadChangesServer* pServer, LPCTSTR sz, int id, BOOL b, DWORD dw, DWORD size, WORD ftype=ALL_TYPE);

	~CReadChangesRequest();

	bool OpenDirectory();

	void BeginRead();

	// The dwSize is the actual number of bytes sent to the APC.
	// TODO: ����һ���߳�����������������swap (new: �����߳�)
	void BackupBuffer(DWORD dwSize)
	{
		//����Ҳ˵�˿��Լ򵥵Ľ����Ϳ�����
		// We could just swap back and forth between the two
		// buffers, but this code is easier to understand and debug.
		//memcpy(&m_BackupBuffer[0], &m_Buffer[0], dwSize);
		m_BackupBuffer.swap(m_Buffer);
	}

	void ProcessNotification();

	//����Ŀ¼�ļ��
	void RequestTermination()
	{
		if(m_hDirectory == NULL)
			return;
		HANDLE h = m_hDirectory;
		m_hDirectory = nullptr;
		dlog("RequestTermination cancelIo");
		::CancelIo(h);	//�ᴥ��APC�ص�
		::CloseHandle(h);
		dlog("RequestTermination cancelIo end");
	}

	bool validate() const
	{
		return m_hDirectory != 0 && m_hDirectory != INVALID_HANDLE_VALUE;
	}
	void get()
	{
		::InterlockedIncrement(&m_ref);
	}
	void put()
	{
		if(::InterlockedDecrement(&m_ref) == 0)
			delete this;
	}
	DWORD ref() const
	{
		return m_ref;
	}

	//��apc�ӵ����req�����������Ѿ��յ�����Ϣ�������ⲿ��Ϣ���ֲ��õ����Ҳ���֪ͨԴ
	void clear_self();

	CReadChangesServer* m_pServer;
	CReadChangesRequest * m_subrequest;

protected:

	static VOID CALLBACK NotificationCompletion(
		DWORD dwErrorCode,							// completion code
		DWORD dwNumberOfBytesTransfered,			// number of bytes transferred
		LPOVERLAPPED lpOverlapped);					// I/O information buffer

	// Parameters from the caller for ReadDirectoryChangesW().
	DWORD		m_dwFlags;
	BOOL		m_bChildren;
	CStringW	m_wstrDirectory;
	int			m_id;

	// Result of calling CreateFile().
	HANDLE		m_hDirectory;

	// Required parameter for ReadDirectoryChangesW().
	OVERLAPPED	m_Overlapped;

	// Data buffer for the request.
	// Since the memory is allocated by malloc, it will always
	// be aligned as required by ReadDirectoryChangesW().
	vector<BYTE> m_Buffer;

	// ˫buffer
	// Double buffer strategy so that we can issue a new read
	// request before we process the current buffer.
	vector<BYTE> m_BackupBuffer;
	
	//���ڶ��̱߳���
	volatile DWORD m_ref;

	WORD m_ftype;	//��ص����͡�0 dir; 1 file; 2 dir/file
};

// All functions in CReadChangesServer run in the context of the worker thread.
// One instance of this object is allocated for each instance of CReadDirectoryChanges.
// This class is responsible for thread startup, orderly thread shutdown, and shimming
// the various C++ member functions with C-style Win32 functions.

/*
 *	����һ��changes�࣬�����ʵ���󶨵�һ���߳���,
 *	server��������������request��������bind��Ŀ¼��
 */
class CReadChangesServer
{
public:
	CReadChangesServer(CReadDirectoryChanges* pParent)
	{
		m_bTerminate=false; m_nOutstandingRequests=0; m_pBase=pParent;
	}

	static unsigned int WINAPI ThreadStartProc(LPVOID arg)
	{
		CReadChangesServer* pServer = (CReadChangesServer*)arg;
		pServer->Run();
		return 0;
	}
	// Called by QueueUserAPC to start orderly shutdown.
	static void CALLBACK TerminateProc(__in  ULONG_PTR arg)
	{
		dlog("in CReadChangesServer::TerminateProc",true);
		CReadChangesServer* pServer = (CReadChangesServer*)arg;
		pServer->RequestTermination();
	}

	// Called by QueueUserAPC to add another directory.
	static void CALLBACK AddDirectoryProc(__in  ULONG_PTR arg)
	{
		dlog("in CReadChangesServer::AddDirectoryProc",true);
		CReadChangesRequest* pRequest = (CReadChangesRequest*)arg;
		pRequest->m_pServer->AddDirectory(pRequest);
	}

	// Called by QueueUserAPC to delete directory.
	static void CALLBACK DelDirectoryProc(__in  ULONG_PTR arg)
	{
		dlog("in CReadChangesServer::DelDirectoryProc",true);
		CReadChangesRequest* pRequest = (CReadChangesRequest*)arg;
		pRequest->m_pServer->DelDirectory(pRequest);
	}

	CReadDirectoryChanges* m_pBase;

	//Ͷ���������������
	volatile DWORD m_nOutstandingRequests;
protected:
	void Run();

	//call by work thread
	void AddDirectory( CReadChangesRequest* pBlock )
	{
		if (pBlock->OpenDirectory())
		{
			::InterlockedIncrement(&pBlock->m_pServer->m_nOutstandingRequests);
			m_pBlocks.push_back(pBlock);
			pBlock->BeginRead();
		}
		else
		{
			pBlock->put();
		}
	}

	void DelDirectory(CReadChangesRequest* pBlock)
	{
		for (DWORD i=0; i<m_pBlocks.size(); ++i)
		{
			if (m_pBlocks[i] == pBlock)
			{
				m_pBlocks[i] = m_pBlocks[m_pBlocks.size()-1];
				m_pBlocks.pop_back();
				break;
			}
		}
		pBlock->RequestTermination();
	}

	void RequestTermination()
	{
		m_bTerminate = true;	//�����߳�
		for (DWORD i=0; i<m_pBlocks.size(); ++i)
		{
			// Each Request object will delete itself.
			m_pBlocks[i]->put();	//�ϲ�û���ͷ�����
			m_pBlocks[i]->RequestTermination();
		}
		m_pBlocks.clear();
	}

	vector<CReadChangesRequest*> m_pBlocks;

	bool m_bTerminate;
};	//CReadChangesServer
//}


#endif
