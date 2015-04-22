#include "MonitorUtil.h"
#include "ReadDirectoryChanges.h"
#include <fstream>
#include <io.h>
#include <iostream>
#include <string>
#include <assert.h>
#include <locale>
using namespace std;

//streambuf *outstream;

#include "DirectoryMonitor.h"
#include "DirectoryChangeHandler.h"

//@param: act = 0 ɾ����1 �����ļ��� 2 �����ļ��У� 3 �ƶ��� 4 ������ 5 ����
void test_shield(DirectoryMonitor * dm)
{
	DWORD res;
	char ch;
	//�����ļ� 
	string fn = "add_file �ļ�1";
	string fn2 = "�ļ�2";
	string dn = "�����ļ���A";
	string dn2 = "�ļ���B\\�ļ���C";
	//�����ļ�
	cout << "�����ļ�:" << fn << endl;
	res = dm->DoActWithoutNotify(1, fn);
	if(res) goto fail;
	
	cout << "����Ŀ¼:" << dn << endl;
	res = dm->DoActWithoutNotify(2, dn);
	if(res) goto fail;
	cout << "����Ŀ¼:" << dn2 << endl;
	res = dm->DoActWithoutNotify(2, dn2);
	if(res) goto fail;
	//cin >> ch;
	cout << "�ƶ��ļ�:" << fn << " ,��:" << dn+"\\"+fn2 << endl;
	res = dm->DoActWithoutNotify(3, fn, dn+"\\"+fn2);
	if(res) goto fail;
	//return ;
	cout << "����:" << dn+"\\"+fn2 << " ,��:" << dn+"\\"+fn << endl;
	res = dm->DoActWithoutNotify(4, dn+"\\"+fn2, dn+"\\"+fn);
	if(res) goto fail;
	cout << "���� �ļ���:" << dn << " ,��:" << dn2 + "\\" + dn << endl;
	res = dm->DoActWithoutNotify(5, dn, dn2 + "\\" + dn);
	if(res) goto fail;
	cout << "ɾ��Ŀ¼:" << dn << endl;
	res = dm->DoActWithoutNotify(0, dn);
	if(res) goto fail;
	cout << "�ƶ� �ļ���:" << dn2 + "\\" + dn << " ,��:" << dn << endl;
	res = dm->DoActWithoutNotify(3, dn2 + "\\" + dn, dn);
	if(res) goto fail;
	cout << "ɾ��Ŀ¼:" << dn << endl;
	res = dm->DoActWithoutNotify(0, dn);
	if(res) goto fail;
	cout << "ɾ��Ŀ¼:" << dn2 << endl;
	res = dm->DoActWithoutNotify(0, dn2);
	if(res) goto fail;
	cout << "test_shield end" << endl;
	return ;
fail:
	cout << "handle fail:" << res << endl;
}

void test_shield2(DirectoryMonitor * dm)
{
	DWORD res;
	//char ch;
	string fn = "dir1";
	string fn_new = "dir2";
	res = dm->DoActWithoutNotify(4, fn, fn_new);
	cout << "move file:" << fn << " => "<< fn_new << " , res=" << res << endl;
}

void test_create_office(DirectoryMonitor * dm)
{
	DWORD res;
	const char * files[] = {"word�ĵ�.docx", "word�ĵ�2.doc", 
		"excel�ĵ�.xlsx", "excel�ĵ�2.xls", "ppt�ĵ�.pptx", "ppt�ĵ�2.ppt"};
	for(int i=0; i < sizeof(files)/sizeof(char*); i++)
	{
		res = dm->DoActWithoutNotify(1, files[i]);
		cout << "create file:" << files[i] << " , res=" << res << endl;
	}
	/*
	wchar_t *word = L"C:\\������\\��Ŀ\\myword.docx";
	res = CreateOfficeFile(word);
	wcout << L"create file:" << word << L" , res=" << res << endl;
	*/
}

void main2()
{
	char c;
	DirectoryChangeHandler * dc = new DirectoryChangeHandler(3);
	//DirectoryMonitor * proj = new DirectoryMonitor(dc, "C:\\������\\��Ŀ", "proj", print_notify, NULL);
	DirectoryMonitor * proj = new DirectoryMonitor(dc, "C:\\������\\��Ŀ", "proj", print_notify, NULL);
	cout << "���proj" << endl;
	DirectoryMonitor * sync = new DirectoryMonitor(dc, "C:\\������\\ͬ��", "sync", print_notify, NULL);
	cout << "���sync" << endl;
	
	Sleep(2000);
	//test ���ε�api
	//test_shield(proj);
	/*
	string dn = "test_mvdir";
	DWORD res = proj->DoActWithoutNotify(3, "test\\"+dn, dn, FOP_REPLACE);
	cout << "test mv dir overwite:" << res << endl;
	*/
	test_create_office(proj);
	//test_shield2(proj);

	cout << "input to quit:" << flush;
	cin >> c;
	delete proj;
	cout << "ɾ��proj" << endl;
	delete sync;
	cout << "ɾ��sync" << endl;
	cout << "go on input" << endl;
//	cin >> c;
	dc->Terminate();
	cout << "delte dc";
	cout << "go on input" << endl;
//	cin >> c;
	delete dc;
	cout << "go on input" << endl;
	cin >> c;
	return;
}

void test()
{
	wstring dir = L"C:\\������\\��Ŀ";
	DWORD res = ::GetFileAttributes(dir.c_str());
	if(res == INVALID_FILE_ATTRIBUTES)
		cerr << "GetAttribute fail:" << GetLastError() << endl;
	else if(res & FILE_ATTRIBUTE_READONLY)
		cout << "has readonly attr" << endl;
	else if(res & FILE_ATTRIBUTE_SYSTEM)
		cout << "has system attr" << endl;
	else 
		cout << "normal" << endl;
	char c;
	cin >> c;
	return;
}

//������ִ��
int main()
{
	wcout.imbue(locale("chs"));
	//test();
	main2();
	return 0;
}

