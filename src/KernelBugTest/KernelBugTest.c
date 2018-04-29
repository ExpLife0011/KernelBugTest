#include<ntifs.h>

//������
#define IOCODE  CTL_CODE(FILE_DEVICE_UNKNOWN,0x910,METHOD_NEITHER,FILE_ANY_ACCESS)
#define IOCODE1  CTL_CODE(FILE_DEVICE_UNKNOWN,0x911,METHOD_NEITHER,FILE_ANY_ACCESS)
#define IOCODE2  CTL_CODE(FILE_DEVICE_UNKNOWN,0x912,METHOD_NEITHER,FILE_ANY_ACCESS)
#define IOCODE3  CTL_CODE(FILE_DEVICE_UNKNOWN,0x913,METHOD_NEITHER,FILE_ANY_ACCESS)


//������
ULONG g_IoCode = 0;

//ntģ�����ַ
SIZE_T NtBase;

//��¼���ص�ַ
SIZE_T retAddr;

//�ҹ�IopfCompleteRequest�����ĵ�ַ
SIZE_T hookFunVa;

//�����ҹ���ַ
SIZE_T jmpFunVa;

//IopfCompleteRequest ��������opcode
UCHAR IopfComBuff[5] = { 0x8b,0xff,0x55,0x8b,0xec };

//Ŀ������IRP������rva
ULONG IrpFunRva;

//Ŀ������hook�ĵ�ַ
SIZE_T hookIrpFunVa;

//hook��ַ����opcode
UCHAR readOpcode[5];

//Ŀ����������ַ
SIZE_T BugSysBase;

//Ŀ��ģ����
UNICODE_STRING sysName;

//IRP
PIRP g_pIrp;

//IRPջ
PIO_STACK_LOCATION pStack;

//IRP���������صĵ�ַ
SIZE_T IrpFunRetAddr;

//IRP������
NTSTATUS CommonProc(PDEVICE_OBJECT objDevice, PIRP pIrp);

//�ҹ�Ŀ����������
VOID OnHook(SIZE_T hookAddr, ULONG MyFun);

//MyIrpFun
void MyIrpFun();

typedef struct _LDR_DATA_TABLE_ENTRY {
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;
	PVOID DllBase;
	PVOID EntryPoint;
	ULONG SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
	ULONG Flags;
	USHORT LoadCount;
	USHORT TlsIndex;
	union _UNIONA{
		LIST_ENTRY HashLinks;
		struct _SECTION{
			PVOID SectionPointer;
			ULONG CheckSum;
		}SECTION;
	}UNIONA;
	union _UNIONB{
		struct _TIMEDATE{
			ULONG TimeDateStamp;
		}TIMEDATE;
		struct _LOADEDIMP{
			PVOID LoadedImports;
		}LOADEDIMP;
	}UNIONB;
	struct _ACTIVATION_CONTEXT * EntryPointActivationContext;

	PVOID PatchInformation;

} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

//ж�غ���
VOID DriverUnload(PDRIVER_OBJECT pDriver)
{
	UNREFERENCED_PARAMETER(pDriver);

	KdPrint(("Bye"));
}

//ö������
ULONG EnumDriver(PDRIVER_OBJECT pDriver, UNICODE_STRING changeSys)
{
	if (pDriver == NULL)
	{
		return 0;
	}

	PLDR_DATA_TABLE_ENTRY pLdr = (PLDR_DATA_TABLE_ENTRY)pDriver->DriverSection;
	PLDR_DATA_TABLE_ENTRY firstentry;
	firstentry = pLdr;

	do
	{
		if (pLdr->FullDllName.Buffer != 0)
		{
			pLdr = (PLDR_DATA_TABLE_ENTRY)pLdr->InLoadOrderLinks.Blink;

		}
		//�ҵ�Ŀ������
		if (!RtlCompareUnicodeString(&changeSys, &pLdr->BaseDllName, TRUE))
		{
			return pLdr->DllBase;
		}

	} while (pLdr->InLoadOrderLinks.Blink != firstentry);

	return 0;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriver, PUNICODE_STRING pPath)
{
	UNREFERENCED_PARAMETER(pPath);

	DbgBreakPoint();

	//����һ���豸
	NTSTATUS status = 0;
	//�豸������
	UNICODE_STRING pDeviceName = RTL_CONSTANT_STRING(L"\\Device\\Byck01");

	//��������
	UNICODE_STRING pSysName = RTL_CONSTANT_STRING(L"\\DosDevices\\ck01");

	PDEVICE_OBJECT pDevice = NULL;



	//�����豸
	status = IoCreateDevice(pDriver, 0, &pDeviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pDevice);

	if (NT_SUCCESS(status) == FALSE)
	{
		return status;
	}

	//������������
	IoCreateSymbolicLink(&pSysName, &pDeviceName);

	//��дIRP������
	for (int i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++)
	{
		pDriver->MajorFunction[i] = CommonProc;
	}

	UNICODE_STRING changeSys = RTL_CONSTANT_STRING(L"ntoskrnl.exe");

	//��ȡntģ���ַ
	NtBase = EnumDriver(pDriver, changeSys);

	//ж�غ���
	pDriver->DriverUnload = DriverUnload;

	return STATUS_SUCCESS;
}

//�ر�ҳ����
void OffProtect()
{
	_asm
	{
		push eax;
		mov eax, cr0;
		and eax, ~0x10000;
		mov cr0, eax;
		pop eax;
	}
}

//����ҳ����
void OnProtrect()
{
	_asm
	{
		push eax;
		mov eax, cr0;
		or eax, 0x10000;
		mov cr0, eax;
		pop eax;
	}
}

//������Ϣ
void FilterInformation(int flag)
{

	if (!flag)
	{
		//��ӡ��Ҫ����Ϣ
		KdPrint(("g_IoCode:%x\n", g_IoCode));
		KdPrint(("retAddr:%x\n", retAddr));
		KdPrint(("BugSysBase:%x\n", BugSysBase));
		KdPrint(("retAddr Rva:%x\n", retAddr - BugSysBase));
	}
	else
	{
		KdPrint(("*************Hook Information***************\n"));
		KdPrint(("Hook IopfCompleteRequest Addr:%x\n", hookFunVa));
		KdPrint(("Hook Irp Function Addr:%x\n", hookIrpFunVa));

		//��ӡIRPջ�ϵ���Ϣ
		KdPrint(("*************Send Information***************\n"));
		KdPrint(("IoControlCode:%x\n", pStack->Parameters.DeviceIoControl.IoControlCode));

		//METHOD_IN_DIRECT,METHOD_out_DIRECT,METHOD_BUFFERED��ʽ�����buf����g_pIrp->AssociatedIrp.SystemBuffer
		//��ӡ�µ�ַ
		KdPrint(("Input SystemBuffer:%x\n", g_pIrp->AssociatedIrp.SystemBuffer));
		//METHOD_NEITHER ��ʽ�����buf��pStack->Parameters.DeviceIoControl.Type3InputBuffer
		KdPrint(("Type3InputBuffer:%x\n", pStack->Parameters.DeviceIoControl.Type3InputBuffer));

		//��ӡinputBufferSize
		KdPrint(("InputBufferSize:%x\n", pStack->Parameters.DeviceIoControl.InputBufferLength));

		//METHOD_IN_DIRECT,METHOD_out_DIRECT��ʽ�Ĵ���buf��irp->MdlAddress
		KdPrint(("MdlAddress:%x\n", g_pIrp->MdlAddress));

		//METHOD_BUFFERED ��ʽ����buf: irp->AssociatedIrp.SystemBuffer
		KdPrint(("Output SystemBuffer:%x\n", g_pIrp->AssociatedIrp.SystemBuffer));

		//METHOD_NEITHER  ��ʽ����buf:irp->UserBuffer
		KdPrint(("UserBuffer:%x\n", g_pIrp->UserBuffer));

		//��ӡOutputBufferSize
		KdPrint(("OutputBufferSize:%x\n", pStack->Parameters.DeviceIoControl.InputBufferLength));


		//�ָ�ԭ����ָ��
		OffProtect();
		memcpy(hookIrpFunVa, readOpcode, 0x5);
		OnProtrect();
	}


}

//MyIopfCompleteRequest
_declspec(naked) void MyIopfCompleteRequest()
{
	_asm
	{
		mov eax, dword ptr[esp];//��¼���ص�ַ
		mov retAddr, eax;
		mov eax, ecx;//��ȡIRP
		mov eax, dword ptr[eax + 0x60];//��ȡIRPջ
		mov eax, dword ptr[eax + 0xc];//��ȡIoControlCode
		cmp eax, g_IoCode;//�ж��ǲ���Ŀ��Ŀ�����
		jnz j_End;
		pushad;
		push 0;
		call FilterInformation;
		popad;
	j_End:
		mov edi, edi;
		push ebp;
		mov ebp, esp;
		jmp jmpFunVa; //��תִ��ԭ��ָ��
	}
}

//���¹ҹ�
_declspec(naked)retHookA()
{
	_asm
	{
		int 0x3;
		pushad;
		push MyIrpFun;
		push hookIrpFunVa;
		call OnHook;
		popad;
		jmp IrpFunRetAddr; //����ԭ������ִ�еĵط�
	}
}

//MyIrpFun
_declspec(naked) void MyIrpFun()
{
	_asm
	{
		int 0x3;
		mov eax, dword ptr[esp];//��¼IRP�������ķ��ص�ַ
		mov IrpFunRetAddr, eax;

		mov eax, dword ptr[esp + 0x8];//IRP
		mov g_pIrp, eax;

		mov eax, dword ptr[eax + 0x60];//IRPջ
		mov pStack, eax;//����IRPջ��ַ

		pushad;
		push 0x1;
		call FilterInformation;
		popad;

		mov eax, retHookA;
		mov dword ptr[esp], eax;//�޸ķ��ص�ַ

		jmp hookIrpFunVa;
	}
}


//jmp opcode
UCHAR NewCodeBuf[5] = { 0xE9 };
//�ҹ�Ŀ����������
VOID OnHook(SIZE_T hookAddr, ULONG MyFun)
{

	//�ر�ҳ����
	OffProtect();

	//Ҫ��ת������ִ��
	*(ULONG *)(NewCodeBuf + 1) = (ULONG)MyFun - (ULONG)hookAddr - 5;

	memcpy(hookAddr, NewCodeBuf, 5);

	//����ҳ����
	OnProtrect();
}

//IRP������
NTSTATUS CommonProc(PDEVICE_OBJECT objDevice, PIRP pIrp)
{
	//��������ķ����жϴ���
	//��ȡIRPջ
	PIO_STACK_LOCATION pStack = IoGetCurrentIrpStackLocation(pIrp);
	ANSI_STRING ansiString;

	switch (pStack->MajorFunction)
	{
	case IRP_MJ_READ:
		break;
	case IRP_MJ_DEVICE_CONTROL:
	{
		//����IOCtrl�봦��
		switch (pStack->Parameters.DeviceIoControl.IoControlCode)
		{//�ҹ�IopfCompleteRequest
		case IOCODE:
		{
			if (pStack->Parameters.DeviceIoControl.Type3InputBuffer == NULL)
			{
				KdPrint(("Buffer is NULL\n"));
				break;
			}

			//��ȡĿ�������
			g_IoCode = *((ULONG *)pStack->Parameters.DeviceIoControl.Type3InputBuffer);
			KdPrint(("g_IoCode:%x\n", g_IoCode));

			//�ҹ�IopfCompleteRequest
			if (!NtBase)
			{
				break;
			}

			//�ҹ�������ַ
			hookFunVa = NtBase + 0x78809;

			//��ת��ַ
			jmpFunVa = hookFunVa + 0x5;

			OnHook(hookFunVa, MyIopfCompleteRequest);
			break;
		}//��ȡĿ��ģ�����ַ
		case IOCODE1:
		{
			if (pStack->Parameters.DeviceIoControl.Type3InputBuffer == NULL)
			{
				KdPrint(("Buffer is NULL\n"));
				break;
			}

			//��ȡĿ��ģ����
			ansiString.Buffer = pStack->Parameters.DeviceIoControl.Type3InputBuffer;
			ansiString.Length = ansiString.MaximumLength = pStack->Parameters.DeviceIoControl.InputBufferLength;

			RtlAnsiStringToUnicodeString(&sysName, &ansiString, TRUE);
			DbgBreakPoint();
			//��ȡĿ����������ַ
			BugSysBase = EnumDriver(objDevice->DriverObject, sysName);
			KdPrint(("BugSysBase:%x\n", BugSysBase));
			DbgBreakPoint();
			break;
		}//hookĿ��������IRP������
		case IOCODE2:
		{
			if (pStack->Parameters.DeviceIoControl.Type3InputBuffer == NULL || BugSysBase == 0)
			{
				KdPrint(("Buffer Or BugSysBase is NULL\n"));
				break;
			}

			//��ȡĿ������IRP��������rva
			IrpFunRva = *((ULONG *)pStack->Parameters.DeviceIoControl.Type3InputBuffer);

			//����VA
			hookIrpFunVa = BugSysBase + IrpFunRva;

			//��ȡbuf��
			RtlCopyMemory(readOpcode, (UCHAR *)hookIrpFunVa, 0x5);

			//hook��Ӧ����
			OnHook(hookIrpFunVa, MyIrpFun);
			break;
		}//���hook��
		case IOCODE3:
		{
			if (hookIrpFunVa != 0)
			{
				//�ָ�ԭ����ָ��
				OffProtect();
				memcpy(hookIrpFunVa, readOpcode, 0x5);
				OnProtrect();
			}

			if (hookFunVa != 0)
			{
				//�ָ�ԭ����ָ��
				OffProtect();
				memcpy(hookFunVa, IopfComBuff, 0x5);
				OnProtrect();
			}
			break;
		}
		default:
			break;
		}

	}
	default:
		break;
	}
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}