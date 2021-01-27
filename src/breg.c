

#include "breg.h"

void go(char* args, int arglen){

    REGISTRY_OPERATION registryOperation;
    LPCSTR ComputerName;
    HKEY HiveRoot;
    LPCSTR KeyName;
    LPCSTR ValueName;
    DWORD DataType;
    LPBYTE Data;
    LONGLONG DataNum;
    DWORD DataSize;
    
    if(!ParseArguments(args, arglen, &registryOperation, &ComputerName, &HiveRoot, &KeyName, &ValueName, &DataType, &Data, &DataNum, &DataSize))
        return;

    if(registryOperation == RegistryQueryOperation){
        QueryKey(ComputerName, HiveRoot, KeyName, ValueName);
    }

}

void QueryKey(LPCSTR ComputerName, HKEY HiveRoot, LPCSTR KeyName, LPCSTR ValueName){
    if(ValueName == NULL)
        EnumerateKey(ComputerName, HiveRoot, KeyName);
}

void EnumerateKey(LPCSTR ComputerName, HKEY HiveRoot, LPCSTR KeyName){    

    HANDLE hHeap = KERNEL32$GetProcessHeap();

    const char* hiveRootString = HiveRootKeyToString(HiveRoot);

    if(hHeap == NULL){
        BeaconPrintf(CALLBACK_OUTPUT, "[-] breg: Failed to open process heap [error %d]\n", hiveRootString, KeyName, KERNEL32$GetLastError());
            return;
    }

    LSTATUS lret;
    HKEY hKey = NULL;

    if(ComputerName != NULL){
        //todo - add remote support
        BeaconPrintf(CALLBACK_OUTPUT, "Remote Not Supported\n");
        return;
    }
    else{
        lret = ADVAPI32$RegOpenKeyA(HiveRoot, KeyName, &hKey);
        if(lret != ERROR_SUCCESS){
            BeaconPrintf(CALLBACK_OUTPUT, "[-] breg: Failed to open '%s\\%s' [error %d]\n", hiveRootString, KeyName, lret);
            return;
        }
    }

    DWORD dwNumSubkeys;
    DWORD dwMaxSubkeyNameLength;
    DWORD dwNumValues;
    DWORD dwMaxValueNameLength;
    DWORD dwMaxDataLength;

    lret = ADVAPI32$RegQueryInfoKeyA(hKey, NULL, NULL, NULL, &dwNumSubkeys, &dwMaxSubkeyNameLength, NULL, &dwNumValues, &dwMaxValueNameLength, &dwMaxDataLength, NULL, NULL);

    if(lret != ERROR_SUCCESS){
        BeaconPrintf(CALLBACK_OUTPUT, "[-] breg: Failed to query information of '%s\\%s' [error %d]\n", hiveRootString, KeyName, lret);
            return;
    }

    if(dwMaxValueNameLength < 9)
        dwMaxValueNameLength = 9;   //"(default)" has length 9
    if(dwMaxDataLength < 32)
        dwMaxDataLength = 32;   //to account for display binary data if it exists

    //2 spaces + len(hiveroot) + '\' + keyname + '\' + subkey + '\n;
    DWORD dwFullSubkeyNameMaxSize = 2 + 4 + 1 + strlen(KeyName) + 1 + dwMaxSubkeyNameLength + 1;
    if(dwFullSubkeyNameMaxSize < 24)
        dwFullSubkeyNameMaxSize = 24; //max length of error string (with some padding)

    //2 spaces + len(valuename) + 4 spaces + max(typelen) + 4 spaces + len(data) + '\n'
    DWORD dwFullValueLength = 2 + dwMaxValueNameLength + 4 + 9 + 4 + dwMaxDataLength + 1;
    if(dwFullValueLength < 24)
        dwFullValueLength = 24; //max length of error string (with some padding)

    int outputLength = 512 + (dwFullSubkeyNameMaxSize * dwNumSubkeys) + (dwFullValueLength * (dwNumValues + 2));

    formatp fpOutputAlloc;
    BeaconFormatAlloc(&fpOutputAlloc, outputLength);

    const char* rootSeparator = (strlen(KeyName) == 0) ? "" : "\\";

    BeaconFormatPrintf(&fpOutputAlloc, "\n[%s%s%s]\n\n", hiveRootString, rootSeparator, KeyName);

    //first enumerate the subkeys
    if(dwNumSubkeys == 0){
        BeaconFormatPrintf(&fpOutputAlloc, "[No Subkeys]\n");
    }
    else{

        LPSTR subkeyName = (LPSTR)KERNEL32$HeapAlloc(hHeap, 0, dwMaxSubkeyNameLength + 1);

        if(subkeyName == NULL){
            BeaconPrintf(CALLBACK_OUTPUT, "[-] breg: Failed to allocate %d bytes memory from process heap [error %d]\n", dwMaxSubkeyNameLength + 1, KERNEL32$GetLastError());
                return;
        }

        BeaconFormatPrintf(&fpOutputAlloc, "Subkeys [%d]:\n\n", dwNumSubkeys);

        for(int i = 0; i < dwNumSubkeys; i++){
            lret = ADVAPI32$RegEnumKeyA(hKey, i, subkeyName, dwMaxSubkeyNameLength + 1);
            if(lret != ERROR_SUCCESS){
                BeaconFormatPrintf(&fpOutputAlloc, "  [error %d]\n", lret);
                continue;
            }
            BeaconFormatPrintf(&fpOutputAlloc, "  %s%s%s\\%s\n", hiveRootString, rootSeparator, KeyName, subkeyName);
        }

        KERNEL32$HeapFree(hHeap, 0, (LPVOID)subkeyName);

    }

    //now enumerate values
    if(dwNumValues == 0){
        BeaconFormatPrintf(&fpOutputAlloc, "\n[No Values]\n");
    }
    else{

        LPSTR valueName = (LPSTR)KERNEL32$HeapAlloc(hHeap, 0, dwMaxValueNameLength + 1);
        if(valueName == NULL){
            BeaconPrintf(CALLBACK_OUTPUT, "[-] breg: Failed to allocate %d bytes memory from process heap [error %d]\n", dwMaxValueNameLength + 1, KERNEL32$GetLastError());
                return;
        }

        LPBYTE bdata = (LPBYTE)KERNEL32$HeapAlloc(hHeap, 0, dwMaxDataLength);
        if(bdata == NULL){
            KERNEL32$HeapFree(hHeap, 0, (LPVOID)valueName);
            return;
        }

        BeaconFormatPrintf(&fpOutputAlloc, "\nValues [%d]:\n\n", dwNumValues);

        BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s%s\n", "Name", dwMaxValueNameLength - 4, "", "Type", MAX_DATATYPE_STRING_LENGTH - 4, "", "Data");
        BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s%s\n", "----", dwMaxValueNameLength - 4, "", "----", MAX_DATATYPE_STRING_LENGTH - 4, "", "----");
        for(int i = 0; i < dwNumValues; i++){
            DWORD valueNameLength = dwMaxValueNameLength + 1;
            DWORD dataLength = dwMaxDataLength;
            DWORD dwRegType;
            lret = ADVAPI32$RegEnumValueA(hKey, i, valueName, &valueNameLength, NULL, &dwRegType, bdata, &dataLength);
            if(lret != ERROR_SUCCESS){
                BeaconFormatPrintf(&fpOutputAlloc, "  [error %d]\n", lret);
                continue;
            }
            
            const char* dataTypeString = DataTypeToString(dwRegType);

            if(dwRegType == REG_SZ || dwRegType == REG_EXPAND_SZ){
                bdata[dataLength-1] = 0;
                if(strlen(valueName) != 0)
                    BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s%s\n", valueName, dwMaxValueNameLength - strlen(valueName), "", dataTypeString, MAX_DATATYPE_STRING_LENGTH - strlen(dataTypeString), "", (LPSTR)bdata);
                else
                    BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s%s\n", "(default)", dwMaxValueNameLength - 9, "", dataTypeString, MAX_DATATYPE_STRING_LENGTH - strlen(dataTypeString), "", (LPSTR)bdata);
            }
            else if (dwRegType == REG_NONE)
                BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s\n", "(default)", dwMaxValueNameLength - 9, "", dataTypeString);
            else if(dwRegType == REG_DWORD)
                BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s0x%x\n", valueName, dwMaxValueNameLength - strlen(valueName), "", dataTypeString, MAX_DATATYPE_STRING_LENGTH - strlen(dataTypeString), "", *(PDWORD)bdata);
            else if(dwRegType == REG_QWORD)
                BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s0x%llx\n", valueName, dwMaxValueNameLength - strlen(valueName), "", dataTypeString, MAX_DATATYPE_STRING_LENGTH - strlen(dataTypeString), "", *(PULONGLONG)bdata);
            else if(dwRegType == REG_BINARY){
                BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s", valueName, dwMaxValueNameLength - strlen(valueName), "", dataTypeString, MAX_DATATYPE_STRING_LENGTH - strlen(dataTypeString), "");
                DWORD maxindex = dataLength;
                if(maxindex > 10)
                    maxindex = 10;
                for(int j = 0; j < maxindex; j++)
                    BeaconFormatPrintf(&fpOutputAlloc, "%02X", bdata[j]);
                if(maxindex != dataLength)
                    BeaconFormatPrintf(&fpOutputAlloc, "... [%d total bytes]\n", dataLength);
                else
                    BeaconFormatPrintf(&fpOutputAlloc, " [%d bytes]\n", dataLength);
            }
            else if(dwRegType == REG_MULTI_SZ){

                BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s", valueName, dwMaxValueNameLength - strlen(valueName), "", dataTypeString, MAX_DATATYPE_STRING_LENGTH - strlen(dataTypeString), "");

                bdata[dataLength-1] = 0;
                DWORD offset = 0;
                if(bdata[0] != 0){
                    for(;;){
                        LPSTR curString = (LPSTR)(bdata + offset);
                        BeaconFormatPrintf(&fpOutputAlloc, "%s", curString);
                        offset += strlen(curString) + 1;
                        if(bdata[offset] == 0)
                            break;
                        BeaconFormatPrintf(&fpOutputAlloc, "~");
                    }
                }
                BeaconFormatPrintf(&fpOutputAlloc, "\n");
            }
            else if(dwRegType == REG_LINK){
                bdata[dataLength-1] = 0;
                if(dataLength > 1)
                    bdata[dataLength-2] = 0;
                BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s%ls\n", valueName, dwMaxValueNameLength - strlen(valueName), "", dataTypeString, MAX_DATATYPE_STRING_LENGTH - strlen(dataTypeString), "", (LPWSTR)bdata);
            }
            else{
                BeaconFormatPrintf(&fpOutputAlloc, "  %s    %*s%s    %*s%s\n", valueName, dwMaxValueNameLength - strlen(valueName), "", dataTypeString, MAX_DATATYPE_STRING_LENGTH - strlen(dataTypeString), "", "<not supported>");
            }
            
        }

        KERNEL32$HeapFree(hHeap, 0, (LPVOID)valueName);
        KERNEL32$HeapFree(hHeap, 0, (LPVOID)bdata);

    }

    BeaconFormatPrintf(&fpOutputAlloc, "\nDONE\n");

    //print output!
    int iOutputLength;
    char* beaconOutputString = BeaconFormatToString(&fpOutputAlloc, &iOutputLength);
    BeaconOutput(CALLBACK_OUTPUT, beaconOutputString, iOutputLength + 1);
}

bool ParseArguments(char * args, int arglen, PREGISTRY_OPERATION pRegistryOperation, LPCSTR *lpcRemoteComputerName, HKEY *pHiveRoot, LPCSTR *lpcKeyName, LPCSTR *lpcValueName, LPDWORD pdwDataType, LPBYTE *pbData, PLONGLONG pllDataNum, LPDWORD cbData){
    datap parser;

    char* regCommand;
    char* remoteComputerName;
    char* hiveRootString;
    char* regKey;
    char* value;
    int dataType;
    char* data;

    BeaconDataParse(&parser, args, arglen);

    regCommand = BeaconDataExtract(&parser, NULL);
    remoteComputerName = BeaconDataExtract(&parser, NULL);
    hiveRootString = BeaconDataExtract(&parser, NULL);
    regKey = BeaconDataExtract(&parser, NULL);
    value = BeaconDataExtract(&parser, NULL);
    dataType = BeaconDataInt(&parser);
    data = BeaconDataExtract(&parser, NULL);

    if(regCommand == NULL || hiveRootString == NULL){
        BeaconPrintf(CALLBACK_OUTPUT, "[-] Usage: breg <command> <key> [arguments]\n");
        return false;
    }

    if( MSVCRT$_stricmp("HKLM", hiveRootString) == 0)
        *pHiveRoot = HKEY_LOCAL_MACHINE;
    else if( MSVCRT$_stricmp("HKCU", hiveRootString) == 0)
        *pHiveRoot = HKEY_CURRENT_USER;
    else if( MSVCRT$_stricmp("HKCR", hiveRootString) == 0)
        *pHiveRoot = HKEY_CLASSES_ROOT;
    else if( MSVCRT$_stricmp("HKU", hiveRootString) == 0)
        *pHiveRoot = HKEY_USERS;
    else if( MSVCRT$_stricmp("HKCC", hiveRootString) == 0)
        *pHiveRoot = HKEY_CURRENT_CONFIG;
    else{
        BeaconPrintf(CALLBACK_OUTPUT, "[-] breg: Unknown registry hive '%s'\n", hiveRootString);
        return false;
    }

    if( MSVCRT$_stricmp("query", regCommand) == 0)
        *pRegistryOperation = RegistryQueryOperation;
    else if( MSVCRT$_stricmp("add", regCommand) == 0)
        *pRegistryOperation = RegistryAddOperation;
    else if( MSVCRT$_stricmp("query", regCommand) == 0)
        *pRegistryOperation = RegistryDeleteOperation;
    else{
        BeaconPrintf(CALLBACK_OUTPUT, "[-] breg: Unknown command '%s'\n", regCommand);
        return false;
    }

    if(remoteComputerName == NULL || strlen(remoteComputerName) > 0)
        *lpcRemoteComputerName = remoteComputerName;
    else
        *lpcRemoteComputerName = NULL;

    if(regKey == NULL)
        regKey = "";
    *lpcKeyName = regKey;

    if(value == NULL || strlen(value) > 0)
        *lpcValueName = value;
    else
        *lpcValueName = NULL;

    *pdwDataType = dataType;
    if(dataType == REG_NONE){
        *pbData = NULL;
        *cbData = 0;
    }
    else if (dataType == REG_SZ){
        *pbData = data;
        *cbData = strlen(data) + 1;
    }
    else if (dataType == REG_DWORD){
        int idata = MSVCRT$atoi(data);
        *pllDataNum = (LONGLONG)idata;
        *pbData = (LPBYTE)pllDataNum;
        *cbData = 4;
    }
    else if (dataType == REG_QWORD){
        LONGLONG lldata = MSVCRT$_atoi64(data);
        *pllDataNum = lldata;
        *pbData = (LPBYTE)pllDataNum;
        *cbData = 8;
    }
    else{
        BeaconPrintf(CALLBACK_OUTPUT, "[-] breg: Unknown datatype '%d'\n", dataType);
        return false;
    }

    return true;
}

const char* HiveRootKeyToString(HKEY HiveRoot){
    if(HiveRoot == HKEY_LOCAL_MACHINE)
        return "HKLM";
    if(HiveRoot == HKEY_CURRENT_USER)
        return "HKCU";
    if(HiveRoot == HKEY_CLASSES_ROOT)
        return "HKCR";
    if(HiveRoot == HKEY_USERS)
        return "HKU";
    if(HiveRoot == HKEY_CURRENT_CONFIG)
        return "HKCC";
    return "N/A";
}

const char* DataTypeToString(DWORD regType){
    if(regType == REG_SZ)
        return "REG_SZ";
    if(regType == REG_NONE)
        return "REG_NONE";
    if(regType == REG_DWORD)
        return "REG_DWORD";
    if(regType == REG_QWORD)
        return "REG_QWORD";
    if(regType == REG_BINARY)
        return "REG_BINARY";
    if(regType == REG_EXPAND_SZ)
        return "REG_EXPAND_SZ";
    if(regType == REG_MULTI_SZ)
        return "REG_MULTI_SZ";
    if(regType == REG_LINK)
        return "REG_LINK";
    return "REG_UNKNOWN";
}