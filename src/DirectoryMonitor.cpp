#include <assert.h>
#include "DirectoryMonitor.h"
#include "DirectoryChangeHandler.h"
#include <stdio.h>
#include <Shellapi.h>
#include <set>
using namespace std;

#define MAX_CACHE_SIZE 10000
#define BLACKLIST_CLEAR_FREQUENCY 1

class NotificationBlacklist_nouse
{
	typedef vector<notification_t> item_type;
	typedef map<wstring, item_type >::iterator iter_type;
public:
	NotificationBlacklist_nouse()
	{ 
		CSLock lock_init(m_guard, false, true);
		m_total = 0;
		m_nclear = 0;
	}
	~NotificationBlacklist_nouse()
	{
		DestCS(&m_guard);
	}
	void Add(const notification_t & no)
	{
		CSLock lock(m_guard);
		const wstring & pathKey = no.path;
		iter_type it = m_blist.find(pathKey);
		if(it != m_blist.end())
		{//find it, add in vector
			it->second.push_back(no);
			return;
		}
		item_type v;
		v.push_back(no);
		m_blist[pathKey] = v;
		m_total += 1;
	}
	bool Query(const notification_t & no, bool ClearWhenHit=true)
	{
		CSLock lock(m_guard);
		if(m_total == 0)
			return false;
		return QueryList(no, ClearWhenHit, m_blist) || QueryList(no, ClearWhenHit, m_blist_gen2);
	}
	void Clear()
	{
		if(++m_nclear != BLACKLIST_CLEAR_FREQUENCY)
			return;
		m_nclear = 0;
		CSLock lock(m_guard);
		m_blist_gen2.clear();
		m_blist.swap(m_blist_gen2);
		m_total = m_blist_gen2.size();
	}
private:
	//��ȡ˫��Դģʽ, ����������Ч������old ����
	map<wstring, item_type > m_blist;
	map<wstring, item_type > m_blist_gen2;
	int m_total, m_nclear;
	CRITICAL_SECTION m_guard;
private:
	bool QueryList(const notification_t & no, bool ClearWhenHit, map<wstring, item_type > & bl)
	{
		if(bl.empty())
			return false;
		const wstring & pathKey = no.path;
		iter_type it = bl.find(pathKey);
		if(it == bl.end())
			return false;
		item_type & v = it->second;
		for(item_type::iterator it2 = v.begin(); it2 != v.end(); ++it2)	
		{
			if(isMatch(no, *it2))
			{
				if(!ClearWhenHit) return true;
				if(v.size() == 1)
				{
					bl.erase(it);
					m_total--;
					assert(m_total = m_blist.size() + m_blist_gen2.size());
				}
				else
					v.erase(it2);
				return true;
			}
		}
		return false;
	}
	bool isMatch(const notification_t & q, const notification_t & aim)
	{
		return q == aim;	
	}
};

struct BlacklistItem {
	BlacklistItem(DWORD _act, const wstring & p1, const wstring & p2=wstring())
		:act(_act), path(p1), path2(p2) { }
	DWORD act;
	wstring path;
	wstring path2;
};
bool operator == (const struct BlacklistItem & t1, const struct BlacklistItem & t2)
{
	return t1.act == t2.act && t1.path == t2.path && t1.path2 == t2.path2;
}
bool operator < (const struct BlacklistItem & t1, const struct BlacklistItem & t2)
{
	if(t1.act < t2.act) return true;
	if(t1.act > t2.act) return false;
	if(t1.path < t2.path) return true;
	if(t1.path > t2.path) return false;
	if(t1.path2 < t2.path2) return true;
	if(t1.path2 > t2.path2) return false;
	return false;
}

class NotificationBlacklist
{
	typedef struct BlacklistItem item_type;
	typedef set<item_type >::iterator iter_type;
public:
	NotificationBlacklist()
	{ 
		CSLock lock_init(m_guard, false, true);
		m_nclear = 0;
	}
	~NotificationBlacklist()
	{
		DestCS(&m_guard);
	}
	void Add(DWORD act, const wstring & path, const wstring & path2)
	{
		CSLock lock(m_guard);
		m_blist.insert(BlacklistItem(act, path, path2));
	}
	void Add(const BlacklistItem & item)
	{
		CSLock lock(m_guard);
		m_blist.insert(item);
		assert(item.act < FILE_ACTION_END);
		cout << "blacklist add nofity:(act:" << item.act 
			<< ", path:" << WideToMutilByte(item.path)<< ", path2:" << WideToMutilByte(item.path2) << ")" << endl;
	}
	bool Del(const BlacklistItem & item)
	{
		CSLock lock(m_guard);
		return QueryList(item, true, m_blist) || QueryList(item, true, m_blist_gen2);
	}
	bool Query(DWORD act, const wstring & path, const wstring & path2, bool ClearWhenHit=true)
	{
		CSLock lock(m_guard);
		if(m_blist.empty() && m_blist_gen2.empty())
			return false;
		const BlacklistItem & item = BlacklistItem(act, path, path2);
		return QueryList(item, ClearWhenHit, m_blist) || QueryList(item, ClearWhenHit, m_blist_gen2);
	}
	//timeout ʱ����ã����Կ���Ƶ��
	void Clear()
	{
		if(++m_nclear != BLACKLIST_CLEAR_FREQUENCY)
			return;
		m_nclear = 0;
		CSLock lock(m_guard);
		if(!m_blist_gen2.empty())
		{
			int i=1;
			cout << "blacklist not empty when clear!" << endl;
			for(iter_type it = m_blist_gen2.begin(); it != m_blist_gen2.end(); ++it, ++i)
				cout << "items: " << i << ":act = " << it->act << " path= " << WideToMutilByte(it->path)
					<< " path2=" << WideToMutilByte(it->path2) << endl;
		}
		m_blist_gen2.clear();
		m_blist.swap(m_blist_gen2);
	}
private:
	//��ȡ˫��Դģʽ, ����������Ч������old ����, ��һ��ԭ����
	//timeout�����߳�����һ���̣߳���֪�������õ����ڣ����Կ��ܸո�
	//���þ��п��ܱ�Clear(),����������ṩ������һ�����ڵĻ���ʱ��
	set<item_type > m_blist;
	set<item_type > m_blist_gen2;
	CRITICAL_SECTION m_guard;
	int m_nclear;	//clear�������ۼƴ�����������������������
private:
	bool QueryList(const item_type & item, bool ClearWhenHit, set<item_type > & bl)
	{
		if(bl.empty())
			return false;
		iter_type it = bl.find(item);
		if(it == bl.end())
			return false;
		if(ClearWhenHit)
			bl.erase(it);
		return true;
	}
};

class FileSystemHelper
{
public:
	static wstring GetThreadTempdir(const wstring & homew)
	{
		DWORD threadid = ::GetCurrentThreadId();
		wchar_t _buf[MAX_PATH + 1];
		wstring hidden_dir = homew + L'\\' + TEMP_DIRNAMEW;
		swprintf_s(_buf, MAX_PATH, L"%s\\%ld", hidden_dir.c_str(), threadid);
		return _buf;
	}

	static DWORD CreateThreadTempdir(const wstring & thread_dir)
	{
		DWORD attr = ::GetFileAttributes(thread_dir.c_str());
		bool creat_thread_dir = false;
		if( INVALID_FILE_ATTRIBUTES == attr )//������
			creat_thread_dir = true;
		else if(!(FILE_ATTRIBUTE_DIRECTORY & attr))
		{//�ļ���ɾ��
			::DeleteFile(thread_dir.c_str());
			creat_thread_dir = true;
		}
		if(creat_thread_dir)
		{
			if(!(0 != ::CreateDirectory(thread_dir.c_str(), NULL) &&
				 0 != ::CreateDirectory((thread_dir+L"\\tmp").c_str(), NULL)))
			{
				DWORD err = GetLastError();
				cout << "Create hidden dirpath fail:" << err << endl;
				return err;
			}
		}
		return 0;
	}

	static DWORD DoActWithoutNotify_impl(int act, const wstring & homew, DWORD flag, bool isAbsPath, bool isXP,
										 const wstring & wstr_from, const wstring & wstr_to, 
										 const wstring & wstr_from_full, 
										 const wstring & wstr_from_name, const wstring & wstr_to_name)
	{
		DWORD err = 0;
		int step = 0;
		bool isOfficefile = isOffice(wstr_from);
		wstring wstr_to_full;

		//������Щ���������ڶ��̵߳Ĺ�ϵ������Ӧ�÷�ֹ�����ļ��ĳ�ͻ
		//�ǿ��Ը����߳�id�������ļ��У�����̵߳Ĳ�����������ļ�����

		//���ҵ���Ӧ�߳�id��Ŀ¼
		wstring thread_dir = GetThreadTempdir(homew);
		if(err = CreateThreadTempdir(thread_dir))
			return err;

		//�����ƶ�����,������ļ���Ŀ���ַ��������ڣ����м��ļ��У�����ͬ���������ƶ�����Ӧλ��
		//�����Ӧλ�����ļ������ǣ������Ӧ��ַ��ͬ���ļ���, �ƶ������ļ����¡�
		//����Ŀ¼�Ĺ�����ļ�һ����Ŀ���ַ��������ڣ����м��ļ��У�����ͬ���������ƶ�����Ӧλ��
		//����Ŀ���ַ����,������·�����������Ŀ¼���ƶ������Ŀ¼�£����Ŀ¼����ͬ��Ŀ¼���ϲ�����ͬ���ļ�������126��
		//������ڵ����ļ�,���� 126
		//(��ô˵���������TO��ַ������Ǵ��ڵ�Ŀ¼����ô�ƶ�����Ŀ¼�£����Ŀ¼����ͬ���ĳ�ͻ--��ͬ�࣬��ô����, ���ͬ��͸��ǡ�
		//������ڵ����ļ�����ô�ƶ��ļ����Ǹ��ǣ��ƶ�Ŀ¼���Ǳ������TO��ַ�����ڣ���ô�����ƶ���������)
		SHFILEOPSTRUCT FileOp; 
		ZeroMemory((void*)&FileOp,sizeof(SHFILEOPSTRUCT));
		//������ܻᱻ��ס����Ϊ����û�г����UI��Ϣ�����ܻ��ճɻ��ң�����Ҫ�� FOF_NO_UI
		FileOp.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR;	//FOF_NOCONFIRMMKDIR ��������ã�
		FileOp.fFlags = FOF_NO_UI;



		//��һ����Ԥ�ȴ���, һ�������뵽����Ŀ¼��
		//XXX �������ʱ��ֱ�Ӹ���, ����ע���ڲ������
		step = 1;
		bool inner_moverename = false;
		if(!isAbsPath && wstr_to_name != wstr_from_name)
			inner_moverename = true;
		wstring inner_path = thread_dir + L'\\' + (inner_moverename ? wstr_from_name : wstr_to_name) + L'\0';
		wstring inner_path2;
		wstring thread_dir2 = thread_dir + L'\0';
		bool isfile = true;
		DWORD attr = ::GetFileAttributes(wstr_from_full.c_str());
		if(attr != INVALID_FILE_ATTRIBUTES && FILE_ATTRIBUTE_DIRECTORY & attr)
			isfile = false;
		if(file_exists(inner_path.c_str()))
		{//ɾ��
			FileOp.pFrom = inner_path.c_str();
			FileOp.wFunc = FO_DELETE; 
			::SHFileOperation(&FileOp);
		}

		switch(act)
		{
		case 1:	//�����ļ�
			break;
		case 2:	//�����ļ���
			isfile = false;
			if(attr != INVALID_FILE_ATTRIBUTES)
				if(FILE_ATTRIBUTE_DIRECTORY & attr)
					return 0;
				else //Ϊ�ļ�
					return ERROR_FILE_EXISTS;
			break;
		case 0:	//ɾ��
		case 3:	//�ƶ�
		case 4:	//����
		case 5:	//����
			if(attr == INVALID_FILE_ATTRIBUTES)
				return ERROR_FILE_EXISTS;
			FileOp.pFrom = wstr_from_full.c_str(); 
			FileOp.pTo = inner_path.c_str();	//�ƶ���������������
			FileOp.wFunc = act==5?FO_COPY:FO_MOVE; 
			err = ::SHFileOperation(&FileOp);
			if(err) goto fail;
			break;
		}

		step = 2;
		//�ڶ����������� 
		switch(act)
		{
		case 0:	//ɾ��	
			FileOp.pFrom = inner_path.c_str();
			FileOp.wFunc = FO_DELETE; 
			if(flag & FOP_RECYCLE) //����վ
				FileOp.fFlags |= FOF_ALLOWUNDO; 
			err = ::SHFileOperation(&FileOp);
			FileOp.fFlags &= ~FOF_ALLOWUNDO; 
			break;
		case 1:	//�����ļ�
			if(isOfficefile)
			{
				err = CreateOfficeFile(inner_path.c_str()); //����ֱ����inner_path, ��Ϊĩβ���˸�\0
			}
			else
			{
				HANDLE filehd = ::CreateFile(
											 inner_path.c_str(),					// pointer to the file name
											 GENERIC_READ | GENERIC_WRITE,       // access (read/write) mode
											 FILE_SHARE_READ						// share mode
											 | FILE_SHARE_WRITE
											 | FILE_SHARE_DELETE,
											 NULL,                               // security descriptor
											 CREATE_NEW,							// how to create
											 FILE_ATTRIBUTE_NORMAL,				// file attributes
											 NULL);                              // file with attributes to copy
				if(filehd == INVALID_HANDLE_VALUE)
					err = GetLastError();
				else
					CloseHandle(filehd);
			}
			break;
		case 2:	//�����ļ���
			if(!::CreateDirectory(inner_path.c_str(), NULL))
				err = GetLastError();
			break;
		case 3:	//�ƶ�
		case 5:	//����
			if(!inner_moverename)
				break;
		case 4:	//����
			inner_path2 = thread_dir + L'\\' + wstr_to_name + L'\0';
			FileOp.pFrom = inner_path.c_str();
			FileOp.pTo = inner_path2.c_str();
			FileOp.wFunc = FO_RENAME; 
			err = ::SHFileOperation(&FileOp);
			inner_path = inner_path2;
			break;
		}
		if(err) goto fail;

		//�����������������Ƴ�
		step = 3;
		switch(act)
		{
		case 0:	//ɾ��
			break;
		case 1:	//�����ļ�
		case 2:	//�����ļ���
			assert(wstr_to.empty());
		case 3:	//�ƶ�
		case 4:	//����
		case 5:	//����
			wstr_to_full = homew + L'\\' + (wstr_to.empty() ? wstr_from : wstr_to) + L'\0';
			if(!isfile)
			{
				if((flag & (FOP_REPLACE | FOP_IGNORE_EXIST)) && file_exists(wstr_to_full.c_str()))
				{//ɾ��oldĿ¼
					inner_path2 = thread_dir + L"\\tmp\\" + L'\0';
					FileOp.pFrom = wstr_to_full.c_str(); 
					FileOp.pTo = inner_path2.c_str();
					FileOp.wFunc = FO_MOVE; 
					err = ::SHFileOperation(&FileOp);
					if(err) goto fail;
					inner_path2 = thread_dir + L"\\tmp\\" + wstr_to_name + L'\0';
					FileOp.pFrom = inner_path2.c_str();
					FileOp.pTo = NULL;
					FileOp.wFunc = FO_DELETE; 
					err = ::SHFileOperation(&FileOp);
					if(err) goto fail;
				}
				//�ƶ��Ϳ�����Ҫ�Ǹ�·������
				if(file_exists(GetBaseDIR(wstr_to_full)))
					wstr_to_full = GetBaseDIR(wstr_to_full) + L'\0';	
			}
			else
			{//��Ȼ�������� topath�����и�ͬ����Ŀ¼�������ƶ������Ŀ¼����
				//�����������ŵ����߻��ж������ͻ��Ҫ������ô�죿ɾ�����Ŀ¼��
				if(isXP && file_exists(wstr_to_full.c_str()))
				{//xp�¸���ͬ���ļ����ȴ���ɾ���ļ���Ϣ
					//TODO ����Ϣ����ʱ���ǹ��˵�
					inner_path2 = thread_dir + L"\\tmp\\" + wstr_to_name + L'\0';
					FileOp.pFrom = wstr_to_full.c_str(); 
					FileOp.pTo = inner_path2.c_str();
					FileOp.wFunc = FO_MOVE; 
					err = ::SHFileOperation(&FileOp);
					if(err) goto fail;
					FileOp.pFrom = inner_path2.c_str();
					FileOp.pTo = NULL;
					FileOp.wFunc = FO_DELETE; 
					err = ::SHFileOperation(&FileOp);
					if(err) goto fail;
				}
			}
			FileOp.pFrom = inner_path.c_str();
			FileOp.pTo = wstr_to_full.c_str(); 
			FileOp.wFunc = FO_MOVE; 
			err = ::SHFileOperation(&FileOp);
			break;
		}
		if(err == 0) return 0;

fail:	//FIXME û����ȫ�������
		if(!inner_path.empty() && file_exists(inner_path.c_str()))
		{
			FileOp.pFrom = inner_path.c_str();
			FileOp.wFunc = FO_DELETE; 
			::SHFileOperation(&FileOp);
		}
		return err;
	}
};

DirectoryMonitor::DirectoryMonitor(DirectoryChangeHandler * dc, 
								   const string & path, 
								   const string & type, 
								   void (*cbp)(void *arg, LocalNotification *), void * varg):
	m_dc(dc), m_home(path), m_type(type), m_cbp(cbp), m_varg(varg), m_running(0), m_id(-1), m_expert_act(0),
	m_guess_cnt(0), m_showfilt(false)
{
	MutilByteToWide(m_home, m_homew);
	//TODO:��̬��������Ŀ¼
	m_silent_dir = TEMP_DIRNAMEW;
	CreateHiddenDir(m_homew + L'\\' + m_silent_dir);
	m_running = m_dc->AddDirectory(this)?1:-1;
	m_blacklist = new NotificationBlacklist();
	m_isXP = GetWinVersion() == 0;
}

DirectoryMonitor::~DirectoryMonitor() {
	dlog("in ~DirectoryMonitor()");
	Terminate(); 
	delete m_blacklist;
}

int DirectoryMonitor::Terminate()
{
	dlog("in DirectoryMonitor::Terminate()");
	DWORD err = 0;
	if(m_running >= 0)
	{
		m_running = -1;
		m_dc->DelDirectory(this);	

		//XXX clear hidden dir�� �ŵ��첽�߳���ȥ����
		SHFILEOPSTRUCT FileOp; 
		ZeroMemory((void*)&FileOp,sizeof(SHFILEOPSTRUCT));
		wstring hidden_dir = m_homew + L'\\' + m_silent_dir + L'\0';
		FileOp.fFlags = FOF_NO_UI; //FOF_NOCONFIRMATION | FOF_NOERRORUI; 
		FileOp.pFrom = hidden_dir.c_str();
		FileOp.wFunc = FO_DELETE; 
		err = ::SHFileOperation(&FileOp);
		if(err)
			cout << "clear hidden dirpath fail:" << err << endl;
	}
	return (int)err;
}

void DirectoryMonitor::ClearBlacklist()
{
	m_blacklist->Clear();
}


DWORD DirectoryMonitor::DoActWithoutNotify2(int act, const string & from, const string & to, 
											DWORD flag, int * LPtrCancel)
{
	char msgbuf[1024];
	sprintf_s(msgbuf, 1024, "call DoActWithoutNotify: act=%d ,from = %s, to = %s", act, from.c_str(), to.c_str());
	dlog(&msgbuf[0]);

	wstring wstr_from, wstr_to, 
			wstr_from_full,  wstr_from_name, wstr_to_name;
	DWORD err = 0;

	MutilByteToWide(from, wstr_from);
	RegularPath(wstr_from);
	wstr_from_name = GetFileName(wstr_from);

	if(act == 3 || act == 4 || act == 5)
	{
		assert(!to.empty());
		MutilByteToWide(to, wstr_to);
		RegularPath(wstr_to);
		wstr_to_name = GetFileName(wstr_to);
	}
	else
		wstr_to_name = wstr_from_name;

	bool isAbsPath = from.find(':') == string::npos ? false : true;
	if(isAbsPath)
	{
		assert(act == 3 || act == 5);
		wstr_from_full = wstr_from + L'\0';		//XXX ���������\0����������ʱ����
	}
	else
		wstr_from_full = m_homew + L'\\' + wstr_from + L'\0';

	err = FileSystemHelper::DoActWithoutNotify_impl(act, m_homew, flag, isAbsPath, m_isXP, 
													wstr_from, wstr_to,  wstr_from_full, 
													wstr_from_name, wstr_to_name);
	if(err)
	{
		sprintf_s(msgbuf, 1024, "meet error in DoActWithoutNotify: error code:%d, act=%d ,from = %s, to = %s", 
				  err, act, from.c_str(), to.c_str());
		dlog(&msgbuf[0]);
	}
	return err;
}


/* windows API ��������
 *
 * �����ƶ�����,������ļ���Ŀ���ַ��������ڣ����м��ļ��У�����ͬ���������ƶ�����Ӧλ��
 �����Ӧλ�����ļ������ǣ������Ӧ��ַ��ͬ���ļ���, �ƶ������ļ����¡�

 * ����Ŀ¼�Ĺ�����ļ�һ����Ŀ���ַ��������ڣ����м��ļ��У�����ͬ���������ƶ�����Ӧλ��
 ����Ŀ���ַ����,������·������:
 �����Ŀ¼���ƶ������Ŀ¼�£����Ŀ¼����ͬ��Ŀ¼���ϲ�����ͬ���ļ�������126��
 ������ڵ����ļ�,���� 126
 * (��ô˵���������TO��ַ������Ǵ��ڵ�Ŀ¼����ô�ƶ�����Ŀ¼�£����Ŀ¼����ͬ���ĳ�ͻ--��ͬ�࣬��ô����,
 ���ͬ��͸��ǡ�������ڵ����ļ�����ô�ƶ��ļ����Ǹ��ǣ��ƶ�Ŀ¼���Ǳ������TO��ַ�����ڣ���ô�����ƶ���������)
 */

//@retrun: �ɹ�����0�� ʧ�ܷ��ش�����
//@param: act = 0 ɾ����1 �����ļ��� 2 �����ļ��У� 3 �ƶ��� 4 ������ 5 ����
//@param: from �������ļ�/�С�(�����Ϊȫ·��)
//@param: to   Ŀ���ַ�� (��from���ļ���/Ŀ¼���ĵ�ַ)
//@param: flag	== FOP_REPLACE����ʾ�ƶ����߿��������У����Ŀ���ļ��д��ڣ��滻��������Ĭ�ϵĺϲ���
//				== FOP_RECYCLE ��ʾɾ��ʱ��������վ

//�ļ����ƶ��Ϳ�����Ŀ��Ϊ�ļ�·�������ƶ��ǿ��Ըı��ļ���
//TODO: ����վ���������Ϣ���Σ���Ϊ�ӻ���վ��ԭ��Ҫ��ԭ�ػ�ԭ(��Ҫ�����б�֧��)
//TODO: ʧ��rollback��ô�죿
//TODO: ���ɾ��һ��Ŀ¼�Ļ�����ôto��ø�һ����������ʱĿ¼

DWORD DirectoryMonitor::DoActWithoutNotify(int act, const string & from, const string & to, 
										   DWORD flag, int * LPtrCancel)
{
	char msgbuf[1024];
	sprintf_s(msgbuf, 1024, "call DoActWithoutNotify: act=%d ,from = %s, to = %s", act, from.c_str(), to.c_str());
	dlog(&msgbuf[0]);

	wstring wstr_from, wstr_to, 
			wstr_from_full, wstr_to_full, 
			wstr_from_name, wstr_to_name;
	DWORD err = 0;

	MutilByteToWide(from, wstr_from);
	RegularPath(wstr_from);
	wstr_from_name = GetFileName(wstr_from);

	if(act == 3 || act == 4 || act == 5)
	{
		assert(!to.empty());
		MutilByteToWide(to, wstr_to);
		RegularPath(wstr_to);
		wstr_to_name = GetFileName(wstr_to);
	}
	else
		wstr_to_name = wstr_from_name;

	bool isAbsPath = from.find(':') == string::npos ? false : true;
	if(isAbsPath)
	{
		assert(act == 3 || act == 5);
		wstr_from_full = wstr_from + L'\0';		//XXX ���������\0����������ʱ����
	}
	else
		wstr_from_full = m_homew + L'\\' + wstr_from + L'\0';
	wstr_to_full = m_homew + L'\\' + (wstr_to.empty() ? wstr_from : wstr_to) + L'\0';

	bool isOfficefile = isOffice(wstr_from);
	bool dest_is_exist = file_exists(wstr_to_full.c_str());
	bool isfile = true;

	SHFILEOPSTRUCT FileOp; 
	ZeroMemory((void*)&FileOp,sizeof(SHFILEOPSTRUCT));
	//������ܻᱻ��ס����Ϊ����û�г����UI��Ϣ�����ܻ��ճɻ��ң�����Ҫ�� FOF_NO_UI
	FileOp.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_NOCONFIRMMKDIR;	//FOF_NOCONFIRMMKDIR ��������ã�
	FileOp.fFlags = FOF_NO_UI;

	BlacklistItem bl_item(0, wstr_from, wstr_to);
	DWORD attr = ::GetFileAttributes(wstr_from_full.c_str());
	if(attr != INVALID_FILE_ATTRIBUTES && FILE_ATTRIBUTE_DIRECTORY & attr)
		isfile = false;

	DWORD action = 0;
	switch(act)
	{
	case 0:	//ɾ��
		bl_item.act = FILE_ACTION_REMOVED;
		m_blacklist->Add(bl_item);
		if(isfile || flag & FOP_RECYCLE)	//ɾ���ļ�
		{
			FileOp.pFrom = wstr_from_full.c_str();
			FileOp.wFunc = FO_DELETE; 
			if(flag & FOP_RECYCLE) //����վ
				FileOp.fFlags |= FOF_ALLOWUNDO; 
			err = ::SHFileOperation(&FileOp);
			//FileOp.fFlags &= ~FOF_ALLOWUNDO; 
		}
		else	//ɾ��Ŀ¼����Ϊ�к��ӣ����Բ��ߺ�����
		{
			goto do_in_tempdir;
		}
		break;
	case 1:	//�����ļ�
		if(isOfficefile)
		{
			//office ��Ϊ��ʱ�ļ�����������
			//TODO:XP ���µ�?
			if(m_isXP)
			{
				bl_item.act = FILE_REMOVED;
				m_blacklist->Add(bl_item);
			}
			bl_item.act = FILE_ADDED;
			m_blacklist->Add(bl_item);
			err = CreateOfficeFile(wstr_from_full.c_str());		//����ֱ������Ϊĩβ���˸�\0
			if(err && m_isXP)
			{
				bl_item.act = FILE_REMOVED;
				m_blacklist->Del(bl_item);
				bl_item.act = FILE_ADDED;
			}
			if(err == ERROR_ALREADY_EXISTS)
			{
				m_blacklist->Del(bl_item);
				return 0;
			}
		}
		else
		{
			bl_item.act = FILE_ACTION_ADDED;
			m_blacklist->Add(bl_item);
			HANDLE filehd = ::CreateFile(
										 wstr_from_full.c_str(),				// pointer to the file name
										 GENERIC_READ | GENERIC_WRITE,			// access (read/write) mode
										 FILE_SHARE_READ						
										 | FILE_SHARE_WRITE
										 | FILE_SHARE_DELETE,					// share mode
										 NULL,									// security descriptor
										 //CREATE_AWAYLS,	//����?
										 CREATE_NEW,							// how to create
										 FILE_ATTRIBUTE_NORMAL,					// file attributes
										 NULL);									// file with attributes to copy
			if(filehd == INVALID_HANDLE_VALUE)
				err = GetLastError();
			else
				CloseHandle(filehd);
		}
		break;
	case 2:	//�����ļ���
		if(attr != INVALID_FILE_ATTRIBUTES)
			if(FILE_ATTRIBUTE_DIRECTORY & attr)
				return 0;
			else //Ϊ�ļ�
				return ERROR_FILE_EXISTS;
		bl_item.act = FILE_ACTION_ADDED;
		{
			vector<wstring> fathers = GetBaseDIRs(wstr_from);
			int i=0;
			for(; i<(int)fathers.size(); i++)
			{
				const wstring & fa = m_homew + L'\\' + fathers[i];
				attr = ::GetFileAttributes(fa.c_str());
				if(attr != INVALID_FILE_ATTRIBUTES)	//����
					break;
			}
			for(--i; i>=0; i--)
			{
				const wstring & fa = m_homew + L'\\' + fathers[i];
				bl_item.path = fathers[i];
				m_blacklist->Add(bl_item);
				if(!::CreateDirectory(fa.c_str(), NULL))
					return GetLastError();
			}
			bl_item.path = wstr_from;
			m_blacklist->Add(bl_item);
			if(!::CreateDirectory(wstr_from_full.c_str(), NULL))
				return GetLastError();
		}
		break;
	case 3:	//�ƶ�
		if(attr == INVALID_FILE_ATTRIBUTES)
			return ERROR_FILE_EXISTS;
		//����Ŀ����ڲ���
		if(dest_is_exist && !isfile)
		{
			if(flag & FOP_REPLACE)
			{//ɾ��oldĿ¼
				wstring thread_dir = FileSystemHelper::GetThreadTempdir(m_homew);
				if(false == file_exists(thread_dir))
				{
					if(err = FileSystemHelper::CreateThreadTempdir(thread_dir))
						return err;
				}
				m_blacklist->Add(BlacklistItem(FILE_ACTION_REMOVED, wstr_to));
				wstring inner_path2 = thread_dir + L"\\tmp\\" + L'\0';
				FileOp.pFrom = wstr_to_full.c_str(); 
				FileOp.pTo = inner_path2.c_str();
				FileOp.wFunc = FO_MOVE; 
				err = ::SHFileOperation(&FileOp);
				if(err) goto fail;
				//ɾ����
				inner_path2 = thread_dir + L"\\tmp\\" + wstr_to_name + L'\0';
				FileOp.pFrom = inner_path2.c_str();
				FileOp.pTo = NULL;
				FileOp.wFunc = FO_DELETE; 
				err = ::SHFileOperation(&FileOp);
				if(err)
				{//���ǻ�������
					sprintf_s(msgbuf, 1024, "meet error in DoActWithoutNotify delete tempfile: error code:%d, act=%d ,from = %s, to = %s", err, act, from.c_str(), to.c_str());
					dlog(&msgbuf[0]);
				}
			}
			else
			{//����
				return ERROR_FILE_EXISTS;	//FIXME ����Ŀ¼Ŀǰ����ô�����
				//Ŀ¼��Ŀ����ڵĻ�, ���չ���ȡ��·��,��windowsĬ�ϵĺϲ�
				//wstr_to_full = GetBaseDIR(wstr_to_full.c_str()) + L'\0';
			}
		}

		if(!isAbsPath)
		{//������ƶ�
			bl_item.act = FILE_ACTION_REMOVED;
			bl_item.path2 = wstring();
			m_blacklist->Add(bl_item);
		}

		action = FILE_ACTION_ADDED;
		if(dest_is_exist && isfile)
		{
			if(m_isXP)
				m_blacklist->Add(BlacklistItem(FILE_ACTION_REMOVED, wstr_to));	//xp �ĸ��ǻ��ȷ�ɾ����Ϣ
			else 
				action = FILE_ACTION_MODIFIED;
		}
		m_blacklist->Add(BlacklistItem(action, wstr_to));

		FileOp.pFrom = wstr_from_full.c_str(); 
		FileOp.pTo = wstr_to_full.c_str();	//�ƶ���������������
		FileOp.wFunc = FO_MOVE; 
		err = ::SHFileOperation(&FileOp);
		break;
	case 4:	//����
		if(attr == INVALID_FILE_ATTRIBUTES)
			return ERROR_FILE_EXISTS;
		//XXX ��������ڸ�����Ŀ����
		if(dest_is_exist)
			return ERROR_FILE_EXISTS;
		if(!isfile)
		{
			bl_item.act = DIR_RENAMED;
			m_blacklist->Add(bl_item);
		}
		//bl_item.act = isfile ? FILE_RENAMED : DIR_RENAMED;	//��ʵ�����жϹ���
		bl_item.act = FILE_RENAMED;	//��ֹ������һ���ļ�����(��С�ļ���, ���Ǹ�����������ܱ��գ����Զ��һ���ж�����ν)
		m_blacklist->Add(bl_item);
		FileOp.pFrom = wstr_from_full.c_str(); 
		FileOp.pTo = wstr_to_full.c_str();	//�ƶ���������������
		FileOp.wFunc = FO_MOVE; 
		err = ::SHFileOperation(&FileOp);
		break;
	case 5:	//����
		if(attr == INVALID_FILE_ATTRIBUTES)
			return ERROR_FILE_EXISTS;
		bl_item.act = FILE_ACTION_ADDED;
		bl_item.path = wstr_to;
		bl_item.path2 = wstring();
		m_blacklist->Add(bl_item);
		goto do_in_tempdir;
		break;
	}
fail:
	if(err)
		m_blacklist->Del(bl_item);
	return err;
do_in_tempdir:
	err = FileSystemHelper::DoActWithoutNotify_impl(act, m_homew, flag, isAbsPath, m_isXP, 
													wstr_from, wstr_to,  wstr_from_full, 
													wstr_from_name, wstr_to_name);
	if(err)
	{
		sprintf_s(msgbuf, 1024, "meet error in DoActWithoutNotify: error code:%d, act=%d ,from = %s, to = %s", 
				  err, act, from.c_str(), to.c_str());
		dlog(&msgbuf[0]);
	}
	return err;
}

void DirectoryMonitor::UpdateAttributeCache(const wstring & rpathw)
{
	DWORD attr = ::GetFileAttributes((m_homew + L"/" + rpathw).c_str());
	if(attr == INVALID_FILE_ATTRIBUTES)
		attr = FILE_ATTRIBUTE_DELETED;
	map<wstring, DWORD>::iterator it = m_file_attrs.find(rpathw);
	if(it != m_file_attrs.end())
		attr = it->second | FILE_ATTRIBUTE_DELETED;
	m_file_attrs[rpathw] = attr;
}

DWORD DirectoryMonitor::GetAttributeFromCache(const wstring & rpathw, DWORD act, DWORD & self)
{
	bool isdel = ((act == FILE_ACTION_REMOVED)||(act == FILE_ACTION_RENAMED_OLD_NAME));
	//�����new˵������cache����������ǿ϶���ʧЧ�˵�
	bool isnew = (act == FILE_ACTION_ADDED || act == FILE_ACTION_RENAMED_NEW_NAME);
	DWORD ret, f_attr, attr = 0;	//��ʾΪɾ����
	map<wstring, DWORD>::iterator it = m_file_attrs.find(rpathw);
	if(it != m_file_attrs.end())
	{
		if(isdel)
			it->second |= FILE_ATTRIBUTE_DELETED;
		if(!isnew) //�������Ǿ�Ҫ��������·���ϵ����м�¼
			return it->second;
	}

	//û���Ż������µ�
	//map<wstring, DWORD> rec;
	bool meet_special = false;
	if(isdel)
		attr = FILE_ATTRIBUTE_DELETED;
	else{
		attr = ::GetFileAttributes((m_homew + L"/" + rpathw).c_str());
		if(attr == INVALID_FILE_ATTRIBUTES)
			attr = FILE_ATTRIBUTE_DELETED;
	}
	self = ret = attr;

	bool invalid = false;
	vector<wstring> fathers = GetBaseDIRs(rpathw);
	size_t s = fathers.size();
	for(int i = s-1; i >= 0; i--)	//������
	{
		const wstring & fpath = fathers[i];
		it = m_file_attrs.find(fpath);
		if(!invalid)
		{
			if(it == m_file_attrs.end() || (isnew && isDel(it->second)))	//û�ҵ�������������������ɾ�����Ǹ���Ŀ¼
				invalid = true;	//��ʱ���Ҫget attr
		}
		if(invalid) {	
			//û��f_attr_old, ��Ϊ��Ч��
			//f_attr_old = it != m_file_attrs.end() ? it->second : 0;
			f_attr = ::GetFileAttributes((m_homew + L"/" + fpath).c_str());
			if(f_attr == INVALID_FILE_ATTRIBUTES)
				f_attr = FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DELETED;
			if(!meet_special)
				meet_special = isSpecial(f_attr);	//������special�ļ���
			else
				f_attr |= FILE_ATTRIBUTE_HIDDEN;
			m_file_attrs[fpath] = f_attr;
		}else{
			assert(it != m_file_attrs.end());
			if(!meet_special)
				meet_special = isSpecial(it->second);
		}
	}
	if(meet_special)
		ret |= FILE_ATTRIBUTE_HIDDEN;
	m_file_attrs[rpathw] = ret;
	//m_file_attrs.insert(rec.begin(), rec.end()); //insert��������Ѵ��ڵ�item
	return ret;
}

int DirectoryMonitor::GetNotify(struct TDirectoryChangeNotification & notify)
{
	wstring rpathw(notify.rPath.GetBuffer());

	//�����Ƿ���attr
	//bool isdir = notify.type == DIR_TYPE;
	bool isdir = false;
	DWORD self_attr = -1;
	DWORD attr = GetAttributeFromCache(rpathw, notify.dwAct, self_attr);
	if(isDir(attr))
	{
		isdir = true;
		//cout << "path:" << WideToMutilByte(rpathw) << " is dir" << endl;
	}

	notification_t no(notify.dwAct, rpathw, isdir);
	no.attr = self_attr;
	no.exist = !isDel(attr);
	if(isSpecial(attr) && !isSpecial(self_attr))
		no.fspec = true;
	if(isSpecial(attr) || isFiltType(rpathw))
	{
		no.special = true;
		//no.filted = true;	//XXX ��Ҫ���ã���Ϊ����������ж���Ϊ�ģ�����special���й���
	}
	cout << "path:" << WideToMutilByte(no.path) << " is " << \
	(no.special? "special":"normal") <<	(no.fspec? " ,father is special" : " ,father is OK") << \
		", file attr:" << attr << " , exist:" << no.exist << " ,act:" << notify.dwAct<< endl;

	if(m_blacklist->Query(no.act, no.path, no.path2))
	{
		cout << "blacklist filt nofity:(act:" << no.act << ", path:" << WideToMutilByte(no.path) << ")" << endl;
		return 0;
	}
	if(!filt_notify2(no))
		m_notifications.push_back(no);

	release_resource();
	return 0;
}

#define none_op	0
#define maybe_move_dir	1
#define maybe_move_file	2
#define maybe_out_move_dir	3
#define maybe_copy_dir	4


//ʵ����Ҳ���Ǽ����ط�Ҫ����
bool DirectoryMonitor::guess(notification_t & notify)
{
	const wstring & path = notify.path;
	DWORD act = notify.act;
	bool isdir = notify.isdir;
	m_expert_act = none_op;
	m_expert_path = L"";
	m_guess_cnt = 0;
	switch (act)
	{
	case FILE_ACTION_ADDED:
		if(isdir)
		{
			notify.expert_act = m_expert_act = maybe_out_move_dir;
			m_expert_path = notify.path;
			m_guess_cnt = 0;
			return true;
		}
		break;
	case FILE_ACTION_MODIFIED:
		break;
	case FILE_ACTION_REMOVED:
		notify.expert_act = m_expert_act = notify.isdir ? maybe_move_dir : maybe_move_file;
		m_expert_path = notify.path;
		m_guess_cnt = 0;
		return true;
		break;
	case FILE_ACTION_RENAMED_OLD_NAME:
		break;
	case FILE_ACTION_RENAMED_NEW_NAME:
		break;
	}
	return false;
}

//�����Ƿ�������filter 
//���Ҫ�����ж϶��У����� true; ���Ҫ������ֱ��filter.filted = true
bool DirectoryMonitor::filt_old_notify(notification_t & filter, int advance)
{
	const wstring & path = filter.path;
	const wstring & path2 = filter.path2;

	vector<notification_t>::reverse_iterator it = m_notifications.rbegin(), endit = m_notifications.rend();
	while(advance--) it++;

	int change_act2 = 0;
	bool not_cmp = false;
	switch (filter.act)
	{
	case FILE_ACTION_ADDED:
	case DIR_ADDED:
		break;
	case FILE_ACTION_MODIFIED:
		for(; it != endit; ++it)
		{
			if(it->filted || it->path != path) continue;
			if(it->act == FILE_ACTION_MODIFIED || it->act == FILE_ACTION_ADDED)
			{//����������Ǹ�֪ͨ(���Ը���add������rename����)
				filter.filted = true; //debug ����Щ
				return true;
				// return false;	//XXX ������, ��������
			}
		}
		break;
	case FILE_REMOVED:
	case DIR_REMOVED:
	case FILE_ACTION_REMOVED:
		//for file �����޸ĵ�֪ͨ
		if(!filter.isdir)
		{
			for(; it != endit; ++it)
			{
				if(it->filted || it->path != path) continue;
				//�������˼�ǣ��������������ɾ���ˣ���ô��ֱ��ɾ��ԭ����
				if(it->act == FILE_RENAMED || it->act == DIR_RENAMED)
				{
					filter.filted = true;
					it->path = it->path2;
					it->act = FILE_ACTION_REMOVED;
					//��ʱ����Ҫ�������, ����������иı��ˣ������ɾ���������˵�
					vector<notification_t>::iterator it2 = it.base();
					for(; it2 != m_notifications.end(); ++it2)
					{
						if(it2->act == FILE_ACTION_MODIFIED || it2->act == FILE_ACTION_ADDED){
							it->filted = true;
							break;
						}
					}
					//��ԭ��ԭ����ɾ��
					return true;
				}
				if(it->act == FILE_ACTION_MODIFIED || it->act == FILE_ACTION_ADDED)
				{
					it->filted = true;
					if(it->act == FILE_ACTION_ADDED) //ok, all filted����Ӻ�ɾ�������˲�֪ͨ
					{
						filter.filted = true; //debug ����Щ
						return true;
					}
				}
				return true;
			}
		}
		else
		{ //for dir ���˺��ӵ�ɾ��֪ͨ ()
			for(; it != endit; ++it)
			{
				if(it->filted) continue;
				if(!isChildren(path, it->path) && path != it->path) continue;

				//rename �� move ���������ˣ���Ϊ�漰����һ���ļ�
				//Ҫô��ԭ����ɾ��(���Ǹı�������)
				if(it->act == FILE_ACTION_REMOVED	|| 
				   it->act == FILE_ACTION_MODIFIED	|| 
				   it->act == FILE_ACTION_ADDED)
					it->filted = true;

				if(it->handled)
					if(
					   //it->act == FILE_ADDED	||	//������
					   //it->act == FILE_MODIFIED ||
					   it->act == DIR_ADDED		||
					   it->act == DIR_COPY		||
					   it->act == FILE_REMOVED	||
					   it->act == DIR_REMOVED
					  )
						it->filted = true;
			}
		}
		break;
	case FILE_ACTION_RENAMED_OLD_NAME:
		break;
	case FILE_ACTION_RENAMED_NEW_NAME:
		break;
	case FILE_RENAMED:
	case DIR_RENAMED:
		//if(filter.special || filter.spec2)	//���������漰������
		//	break;
	case FILE_MOVED:
	case DIR_MOVED:
		for(; it != endit; ++it)
		{
			//cout << "it path:" << WideToMutilByte(it->path) << "is special:" << it->special << endl;
			if(it->filted) continue; 
			//if(it->path != path && it->path != path2) continue;

			if(it->path == path && !not_cmp)
			{
				//��ǰ�ĸĶ�����Ч����rename������
				if(it->act == FILE_ACTION_MODIFIED || it->act == FILE_ACTION_ADDED)
					it->filted = true;
				if(it->act == FILE_REMOVED || it->act == FILE_ACTION_REMOVED)	//�ļ���ɾ����������
					it->filted = true;
				else
					not_cmp = true;	//�򵥱Ƚ�
			}
			//��һ����·���ǣ�rename��oldname
			if(it->path == path2)
			{
				if(it->act == FILE_ACTION_MODIFIED) {
					it->filted = true;
					change_act2 = FILE_ACTION_MODIFIED;
					//���Ҫ����modify
				} else if(it->act == FILE_ACTION_ADDED) {
					it->filted = true;
					change_act2 = FILE_ACTION_ADDED;
				} else if(it->act == filter.act) {	//������С, ��������
					it->filted = true;
					filter.path2 = it->path2;
					filter.spec2 = it->spec2;
				}
			}
		}
		if(change_act2 == FILE_ACTION_ADDED)
			filter.act = FILE_ACTION_ADDED;
		else if(change_act2 == FILE_ACTION_MODIFIED)
		{//�¼�һ����ʾmodify����Ϣ
			dlog("add a new modify because changed before rename");
			notification_t newone(filter);
			newone.act = FILE_ACTION_MODIFIED;
			m_notifications.push_back(newone);
		}
		break;
	}
	return true;
}

void report_unexpert(const notification_t & unexpert, const wstring & expert_path)
{
	cout << (unexpert.isdir ? "directory:" : "file:") << WideToMutilByte(unexpert.path)  
		<< " act:" << unexpert.act <<" not we experted. we expert: path=" << WideToMutilByte(expert_path) << endl; 
}

bool DirectoryMonitor::filt_notify2(notification_t & notify)
{
	//m_showfilt = file_exists(m_homew + L'\\' + TEMP_DIRNAMEW + L'\\' + L"showfilt.flag") ? true : false;
	//return false;
	if(file_exists(m_homew + L'\\' + TEMP_DIRNAMEW + L'\\' + L"test.flag"))
		return false;	//����Ͳ�������
	const wstring & path = notify.path;
	bool ret = false;
	DWORD act = notify.act;
	//ͨ������Ҫ�ж�ǰһ���Ƿ����й�����notify����������жϺܶ��������
	size_t size = m_notifications.size();
	size_t advance = 0;

	//��������
	bool handle = true;	//XXX û�õ���
	bool no_need_guess = false;
	bool filt_old = true;

	//XXX ???
	//notification_t filter = notify;
	m_guess_cnt++;


	//��Ӧ���룬���������󣬼����µĲ��룬��ȷ��ʵ�ֲ���
	//XXX Ŀǰ�İ汾��˵�����ڲ�������������ģ��������������Ĳ����ᵼ����Щ����ʧ��
	//���ǣ�ֻ�пɴ�ϵĶ�������Ӱ�죬��move����������֪ͨ����Ӱ�졣
	switch(m_expert_act)
	{
	case maybe_out_move_dir: //�ж��Ƿ��Ǵ��ⲿmove��dir, �жϵ��ǽ����ŵ�Ŀ¼֪ͨ�����Ǻ��ӵ�copy
		//XXX �����ʱʱ��ҲҪ�����������
		if(m_guess_cnt == 1) {
			notification_t & last = m_notifications[size-1];
			assert(last.expert_act == m_expert_act);

			if(act == FILE_ACTION_MODIFIED && notify.isdir)
			{
				if(path == GetBaseDIR(m_expert_path)) //��һ���Ǹ�·����ok������Ϣ��������Ϊ�����������ж���һ��ȥ
				{//����, �п�����������ˣ���һ��move dir����
					//��Ҫ�������жϣ���Ϊ���ܺ��ӻ�û��copy������̫����
					goto filt_this;
				}
				if(path == m_expert_path)
				{//�Լ��ı���˵������һ����ӹ���
					//win7������Ĵ���������������жϲ��ᵽ��, ����xp���ᵽ����
					m_expert_act = maybe_copy_dir;
					last.act = DIR_ADDED;
					last.handled = true;
					goto filt_this;
				}
			}

			if(act == FILE_ACTION_ADDED && m_expert_path == GetBaseDIR(path)) { 
				//win7���ᵽ������, ����xp��...
				//dlog("last dir is father, so maybe this is copydir op");
				m_expert_act = maybe_copy_dir;
				//cout << "last act == " << last.act << " , now is DIR_ADD, last path = " << WideToMutilByte(last.path) << endl;
				last.act = DIR_ADDED;
				last.handled = true;
				break;
			}
			//yes, ����, �����Ĳ�������, ���������
		}
		else{
			report_unexpert(notify, m_expert_path);
		}
		m_expert_act = none_op;
		break;
	}

	switch(m_expert_act)
	{
	case maybe_copy_dir:
		if(act == FILE_ACTION_ADDED) {
			if(isChildren(m_expert_path, path)) //������Ŀ¼copy
			{ //XXX ����ֱ���ж����ļ�����Ŀ¼, ��ʱ������ FILE_ACTION_ADDED�������������
				notify.act = notify.isdir == false ? FILE_ACTION_ADDED : DIR_ADDED;
				notify.handled = true;
				return false;
			}
			//�����ڴ���Ŀ¼��add����, ��ô�ϴβ������������
			m_expert_act = none_op;
		}else if(act == FILE_ACTION_MODIFIED){
			if(notify.isdir)
				goto filt_this;
			//�ļ��ı�, ѹ��֪ͨ
		} else{
			//���ܽ������������̣���Ϊ�ļ������Ǹ��������̣�
			//��������������ǿ��Զ����ģ���Ӱ��
			m_expert_act = none_op;
		}
		break;
	case maybe_move_file:
	case maybe_move_dir:
		notification_t & last = m_notifications[size-m_guess_cnt];
		if(act == FILE_ACTION_MODIFIED && notify.isdir)
		{//XXX ע������Ǹ�Ŀ¼�£����ղ��������Ŀ¼�ĸ���֪ͨ
			if(m_guess_cnt == 1) //must be ��Ϊ�Һ������ļ��е��޸�
			{
				if (path == GetBaseDIR(m_expert_path))//���������Ǹ�·�����޸�
				{
					last.act = m_expert_act == maybe_move_dir ? DIR_REMOVED : FILE_REMOVED;
					last.handled = true;
					m_expert_act = none_op;
					//�����һ��remove��������ô�������ļ��Ķ���, �����ã���Ϊ����Ķ���֮����Ѿ�������
					//ɾ���ļ�ûʲô�����	
				}//else, ok, ����һ��move����,����������Ŀ���ַ�ĸ�Ŀ¼
				goto filt_this;
			}
		}
		if(act == FILE_ACTION_ADDED || act == FILE_ACTION_MODIFIED)
		{//modify�Ǽ���ճ��ʱ�򸲸�ͬ���ļ�����֪ͨ
			if((!notify.exist || 
				(notify.isdir && m_expert_act == maybe_move_dir) ||
				m_expert_act == maybe_move_file )
			   && GetFileName(path) == GetFileName(m_expert_path)) //û���жϸ�����
			{
				//cout << "in guess move act" << endl;
				if(path == m_expert_path)
				{//���г�ȥ���л�����office ppt)
					no_need_guess = true;
					m_expert_act = none_op;
					break;
				}
				if(last.expert_act == m_expert_act)
				{//����
					//cout << "we get move act" << endl;
					last.filted = true;
					notify.act = notify.isdir ? DIR_MOVED : FILE_MOVED;
					notify.handled = true;
					notify.path2 = last.path;
					notify.spec2 = last.special;
					no_need_guess = true;
					m_expert_act = none_op;
					//filt_old = false;
					//����û�������moved��Ϣ�����˵ģ�������Ϊxp���ļ����ǻ��Ȳ���һ��ɾ����Ϣ
					//����Ҫ���˵��Ǹ�ɾ����Ϣ
					goto filt_record;
					break;
				}
			}
			else
			{//we see what happened 
				cout << "m_expert_act == maybe_move_dir:" << (m_expert_act == maybe_move_dir) << endl;
				cout << "isdir:" << notify.isdir << " ,exist:" << notify.exist<< " ,path:" << WideToMutilByte(path) << endl;
			}
		}
		else if(act == FILE_ACTION_REMOVED || L"" == GetBaseDIR(m_expert_path))
		{//ֱ�����ľ�����һ��ɾ������ ���� ��·���ǿ�û�յ�
			last.act = m_expert_act == maybe_move_dir ? DIR_REMOVED : FILE_REMOVED;
			last.handled = true;
			if(isChildren(m_expert_path, path))
			{//��ɾ��Ŀ¼�ĺ��ӣ�����.��Ϊ�п��ܳ��ָ�·���ͺ���ɾ��֪ͨ��˳��ߵ��ˣ��������ӵ�ʱ��
				//XXX �޸ĺ���֤�Ƿ�ߵ�, ���񲻻�
				goto filt_this;
			}
		}
		else 
		{ //���ܱ�����������ϣ�����ԭ�������ɾ��/��ӣ�TODO����������ö��session���Ľ�
			report_unexpert(notify, m_expert_path);
		}
		m_expert_act = none_op;
		break;
	}


	if(handle)
	{
		//������������
		switch (act)
		{
		case FILE_ACTION_ADDED:
			/* �����ˣ���Ȼ�ļ������ڣ������֪ͨ��Ҫ���added����֮�ᱻ���˵���
			   if(!notify.exist) goto filt_this; */
			break;
		case FILE_ACTION_MODIFIED:
			if(notify.isdir || notify.fspec || !notify.exist)//�ļ��л������ļ��޸ģ�����
				goto filt_this;
			no_need_guess = true;
			break;
		case FILE_ACTION_REMOVED:
			break;
		case FILE_ACTION_RENAMED_OLD_NAME:
			break;
		case FILE_ACTION_RENAMED_NEW_NAME:
			assert(size > 0);
			notification_t & last = m_notifications[size-1];
			assert(last.act == FILE_ACTION_RENAMED_OLD_NAME);

			//��������û�б仯
			last.filted = true;
			notify.act = notify.isdir ? DIR_RENAMED : FILE_RENAMED;
			notify.handled = true;
			notify.path2 = last.path;
			notify.spec2 = last.special;
			no_need_guess = true;
			m_expert_act = none_op;
			//Ψһһ����Ҫ����·����
			if(m_blacklist->Query(notify.act, notify.path2, notify.path))
				goto filt_this;
			break;
		}
	}

	if(!no_need_guess && m_expert_act == none_op)
	{ // �������
		guess(notify);
	}

filt_record:
	if(filt_old && size >= advance)
	{
		return !filt_old_notify(notify, advance) || ret;
	}
	return ret;

filt_this:
	m_guess_cnt--;
	return true;
}

//���ط��͵�����
int DirectoryMonitor::SendNotify()
{
	m_expert_act = none_op; //�µĿ�ʼ
	size_t size = m_notifications.size();
	LocalNotification ln;
	ln.basedir = m_home;
	ln.t = m_type;

	for(vector<notification_t>::iterator it	= m_notifications.begin(); it != m_notifications.end(); ++it)
	{
		//ExplainAction2(*it, m_id);

		//������ϢĿ¼����Ϣȫ������
		if(isChildren(m_silent_dir, it->path))
			continue;
		if(
		   (it->act == FILE_RENAMED || 
			it->act == DIR_RENAMED || 
			it->act == FILE_MOVED || 
			it->act == DIR_MOVED) && isChildren(m_silent_dir, it->path2)
		  )
			continue;
		if((it->act == FILE_MOVED || it->act == DIR_MOVED)){
			if(it->special && !it->spec2)	//�ƶ�������Ŀ¼����ɾ��
			{
				it->act = it->act==FILE_MOVED ? FILE_REMOVED : DIR_REMOVED;
				it->special = false;
				it->path = it->path2;
			}
			if(!it->special && it->spec2)	//������Ŀ¼�ƶ���ʵ�ʵ�Ŀ¼��������
				it->act = it->act==FILE_MOVED ? FILE_ADDED : 
					(hasChildren(m_homew + L'\\' + it->path) ? DIR_COPY : DIR_ADDED);
		}
		if(it->act == FILE_RENAMED){	//case tmp�ļ����ǹ��˵���
			if(it->special && !it->spec2)	//��ɾ��
			{
				if(isOffice(it->path2))
				{//XXX office �ļ����⴦��
					cout << "office rename filed: from " << WideToMutilByte(it->path2) 
						<< " ,to " << WideToMutilByte(it->path) << endl;
					it->filted = true;
				}
				else
				{
					it->act = FILE_REMOVED;
					it->special = false;
					it->path = it->path2;
				}
			}
			if(!it->special && it->spec2)	//������
			{
				cout << "office rename as NEW: from " << WideToMutilByte(it->path2) 
					<< " ,to " << WideToMutilByte(it->path) << endl;
				it->act = FILE_ADDED;
				if(isOffice(it->path))
					it->act = FILE_MODIFIED;
			}
		}

		if(it->special && ! it->filted) 
		{
			it->filted = true;
			
			if(it->isdir || it->fspec || isFiltType(it->path))
			{
				it->filted = true;
			}
			else if(it->act == FILE_ACTION_ADDED || it->act == FILE_ACTION_MODIFIED)
			{
				//�Լ������ص�, Ŀǰֻ����֤�ļ��޸�û
				if(it->attr != -1 && isSpecial(it->attr))
				{
					DWORD attr = ::GetFileAttributes((m_homew + L"/" + it->path).c_str());
					if(!isSpecial(attr))
					{
						it->filted = false;
						UpdateAttributeCache(it->path);
						cout << "specal flag is changed!" << endl;
					}
				}
			}
		}
		//��ӡlog
		ExplainAction2(*it, m_id);

		if(!m_showfilt && it->filted) //m_showfilt for debug
			continue;

		if(m_cbp)
		{
			LocalOp op;
			WideToMutilByte(it->path, op.to);
			string fpath = GetBaseDIR(op.to);
			op.to = GetFileName(op.to);
			switch(it->act)
			{
			case FILE_ACTION_ADDED:
				if(!file_exists(m_homew + L'\\' + it->path))
					continue;
				it->act = it->isdir == false ? FILE_ADDED : 
					hasChildren(m_homew + L'\\' + it->path) ? DIR_COPY : DIR_ADDED;
				break;
			case FILE_ACTION_REMOVED:
				it->act = FILE_REMOVED;
				break;
			case FILE_ACTION_MODIFIED:
				it->act = FILE_MODIFIED;
				break;
			case FILE_RENAMED:
			case DIR_RENAMED:
				WideToMutilByte(it->path2, op.from);
				op.from = GetFileName(op.from);
				break;
			case FILE_MOVED:
			case DIR_MOVED:
				WideToMutilByte(it->path2, op.from);
				break;
			}
			op.act = it->act;

			//��������ת����������Ϣ���������֪ͨǰ���һ������
			switch(it->act)
			{
			case FILE_RENAMED:
			case DIR_RENAMED:
			case FILE_MOVED:
			case DIR_MOVED:
				if(m_blacklist->Query(it->act, it->path2, it->path))
					continue;
				break;
			default:
				if(m_blacklist->Query(it->act, it->path, wstring()))
					continue;
				break;
			}
			if(fpath != ln.fpath)
			{//��·����ͬ,֪ͨ�ͻ���
				if(!ln.ops.empty())
				{
					//TODO: ֧����������
					m_cbp(m_varg, &ln);
					ln.ops.clear();
				}
				ln.fpath = fpath;
			}
			ln.ops.push_back(op);
		}
	}
	if(m_cbp && !ln.ops.empty())
	{
		m_cbp(m_varg, &ln);
		//for debug
		if(m_cbp != print_notify)
			print_notify(m_varg, &ln);
	}
	m_notifications.clear();
	//cout << "clear" << endl;
	return size;
}

//�ڳ�ʱʱ���ͷ���Դ
void DirectoryMonitor::release_resource()
{
	if(m_file_attrs.size() > MAX_CACHE_SIZE)
		m_file_attrs.clear();
}
