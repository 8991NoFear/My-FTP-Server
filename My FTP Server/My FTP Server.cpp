#include <stdio.h>
#include <WinSock2.h>
#include <Windows.h>

// DEFINE

#define MAX_CLIENT 1024
#define N 64

// STRUCT

struct subclient {
    SOCKET s;
    SOCKADDR_IN saddr;
};
typedef struct subclient SUBCLIENT;

typedef struct client {
    SUBCLIENT cmd;
    SUBCLIENT actv;
    SUBCLIENT pasv;
    char wd[1024];
    BOOL isActv;
};
typedef struct client CLIENT;

// PROTOTYPE
DWORD WINAPI CommandThread(LPVOID params);
void concat(char** phtml, char* str);
char* scanFiles(char* path);
void clearClient(int idx);
void clearSubClient(SUBCLIENT* lpsubclient);
char* getFilePath(char* buffer, char* wd);

// GLOBAL VARIABLES
CLIENT g_clients[MAX_CLIENT] = {};
WSAEVENT g_events[MAX_CLIENT] = {};
int g_count = 0;
CRITICAL_SECTION lock;

int main() {
    for (int i = 0; i < MAX_CLIENT; i++) {
        clearClient(i);
    }
    InitializeCriticalSection(&lock);

	WSADATA wsaData;
    int startupRes = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (startupRes != 0) {
		int errCode = WSAGetLastError();
        printf("Error occurs: %d", errCode);
		exit(-1);
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        int errCode = WSAGetLastError();
        printf("Error occurs: %d", errCode);
		exit(-1);
    }

    SOCKADDR_IN saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(8888);
    saddr.sin_addr.S_un.S_addr = ADDR_ANY;

    if (bind(s, (sockaddr*)&saddr, sizeof(saddr)) == SOCKET_ERROR) {
        int errCode = WSAGetLastError();
        printf("Error occurs: %d", errCode);
		exit(-1);
    }

    if (listen(s, 10) == SOCKET_ERROR) {
        int errCode = WSAGetLastError();
        printf("Error occurs: %d", errCode);
		exit(-1);
    }
	
    fd_set fdread;
    FD_ZERO(&fdread);

    while (TRUE) {
        FD_SET(s, &fdread);
        select(0, &fdread, NULL, NULL, NULL);
        if (FD_ISSET(s, &fdread)) {
            FD_CLR(s, &fdread);
            SOCKADDR_IN saddr;
            int addrlen = sizeof(saddr);
            SOCKET cs = accept(s, (sockaddr*)&saddr, &addrlen);
            WSAEVENT ce = WSACreateEvent();
            WSAEventSelect(cs, ce, FD_READ | FD_CLOSE);

            EnterCriticalSection(&lock);
            BOOL hasAnEmptySlot = FALSE;
            for (int i = 0; i < g_count; i++) { // Tan dung vi tri cua nhung socket da dong
                if (g_clients[i].cmd.s == INVALID_SOCKET) {
                    hasAnEmptySlot = TRUE;
                    g_clients[i].cmd.s = cs;
                    g_clients[i].cmd.saddr = saddr;
                    g_events[i] = ce;
                    if ((i%N) == 0) {
                        CreateThread(NULL, 0, CommandThread, (LPVOID)i, 0, NULL); // bat lai luong da ket thuc truoc day
                        printf("Create thread: %d\n", i/N);
                    }
                    break;
                }
            }
            if (!hasAnEmptySlot) {
                g_clients[g_count].cmd.s = cs;
                g_clients[g_count].cmd.saddr = saddr;
                g_events[g_count] = ce;
                g_count++;
                if ((g_count % N) == 1) {
                    CreateThread(NULL, 0, CommandThread, (LPVOID)(g_count - 1), 0, NULL); // tao them luong moi
                    printf("Create thread: %d\n", g_count / N);
                }
            }
            LeaveCriticalSection(&lock);

            char* welcome = (char*)"My FTP Server\r\n220 Ok\r\n";
            send(cs, welcome, strlen(welcome), 0);
        }
    }
}

// LUONG XL CAC LENH GUI TU CLIENT
DWORD WINAPI CommandThread(LPVOID params) {
    int startIdx = (int)params;
    int endIdx = startIdx + N - 1;
    while (TRUE) {
        // thuc hien don phan tu trong mang g_clients, g_events
        EnterCriticalSection(&lock);
        int totalClients = 0;
        for (int i = startIdx; i <= endIdx; i++) {
            totalClients++;
            if (g_clients[i].cmd.s == INVALID_SOCKET) {
                totalClients--;
                for (int j = i; j <= endIdx; j++) {
                    if (g_clients[j].cmd.s != INVALID_SOCKET) {
                        totalClients++;
                        memcpy(&g_clients[i], &g_clients[j], sizeof(CLIENT));
                        memset(&g_clients[j], 0, sizeof(CLIENT));
                        g_clients[j].cmd.s = INVALID_SOCKET;
                        memcpy(&g_events[i], &g_events[j], sizeof(WSAEVENT));
                        memset(&g_events[j], 0, sizeof(WSAEVENT));
                        break;
                    }
                }
                break;
            }
        }
        LeaveCriticalSection(&lock);

        if (totalClients == 0) {
            printf("Terminate thread: %d\n", startIdx/N);
            return 0; // Ket thuc luong --> bat lai xem o tren
        }

        // trong luc treo thi rat co the g_events da duoc cap nhat --> can timeout
        // chi can co 1 invalid handle trong tap event la an loi ngay
        int idx = WSAWaitForMultipleEvents(totalClients, g_events + startIdx, FALSE, 1000, FALSE); 
        if (idx == WSA_WAIT_TIMEOUT) {
            continue;
        }
        idx -= WSA_WAIT_EVENT_0;
        for (int i = idx; i < (startIdx + totalClients); i++) {
            WSANETWORKEVENTS networkEvent;
            WSAEnumNetworkEvents(g_clients[i].cmd.s, g_events[i], &networkEvent);
            WSAResetEvent(g_events[i]);
            if (networkEvent.lNetworkEvents & FD_CLOSE) {
                // Giai phong tai nguyen cho client
                closesocket(g_clients[i].cmd.s);
                WSACloseEvent(g_events[i]);
                clearClient(i);
                printf("Disconnected client: %d\n", i);
            }
            else if (networkEvent.lNetworkEvents & FD_READ) {
                if (networkEvent.iErrorCode[FD_READ_BIT] == 0) {
                    char buffer[1024] = {};
                    recv(g_clients[i].cmd.s, buffer, sizeof(buffer), 0);
                    printf("%s", buffer);

                    if (strncmp(buffer, "USER", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "331 Password required\r\n"); // user nao cung duoc
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "PASS", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "230 Logged on\r\n"); // pass nao cung duoc
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                        memset(g_clients[i].wd, 0, sizeof(g_clients[i].wd));
                        sprintf(g_clients[i].wd, "%s", "/");
                    }
                    else if (strncmp(buffer, "SYST", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "215 UNIX\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "FEAT", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "211-Features:\n MDTM\n REST STREAM\n SIZE\n MLST type*;size*;modify*;\n MLSD\n UTF8\n CLNT\n MFMT\n EPSV\n EPRT\n211 End\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "CLNT", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "200 Don't care\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "OPTS", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "202 UTF8 mode always on\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "PWD", 3) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "217 \"%s\" is current working directory\r\n", g_clients[i].wd);
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "TYPE", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "200 Ok\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "PORT", 4) == 0) {
                        for (int i = 0; i < strlen(buffer); i++) {
                            if (buffer[i] == ',') {
                                buffer[i] = ' ';
                            }
                        }
                        char cmd[5] = {};
                        int ip[4] = {};
                        int port[2] = {};
                        sscanf(buffer, "%s %d %d %d %d %d %d", &cmd, &ip[0], &ip[1], &ip[2], &ip[3], &port[0], &port[1]);

                        g_clients[i].isActv = TRUE;
                        SOCKET ds = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                        if (ds == INVALID_SOCKET) {
                            int errCode = WSAGetLastError();
                            printf("Error occurs: %d\n", errCode);
                            memset(buffer, 0, sizeof(buffer));
                            sprintf(buffer, "400 Port command unsuccessful\r\n");
                            break;
                        }
                        g_clients[i].actv.s = ds;
                        g_clients[i].actv.saddr.sin_family = AF_INET;
                        g_clients[i].actv.saddr.sin_addr.S_un.S_un_b.s_b1 = ip[0];
                        g_clients[i].actv.saddr.sin_addr.S_un.S_un_b.s_b2 = ip[1];
                        g_clients[i].actv.saddr.sin_addr.S_un.S_un_b.s_b3 = ip[2];
                        g_clients[i].actv.saddr.sin_addr.S_un.S_un_b.s_b4 = ip[3];
                        g_clients[i].actv.saddr.sin_port = htons(MAKEWORD(port[1], port[0]));

                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "200 Port command successful\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "MLSD", 4) == 0) {
                        // thong bao mo phien truyen dl o kenh lenh
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "150 Opening data channel\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                        if (connect(g_clients[i].actv.s, (sockaddr*)&g_clients[i].actv.saddr, sizeof(g_clients[i].actv.saddr)) == SOCKET_ERROR) {
                            int errCode = WSAGetLastError();
                            printf("Error occurs: %d\n", errCode);
                            memset(buffer, 0, sizeof(buffer));
                            sprintf(buffer, "425 Cannot open data channel\r\n");
                            send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                            break;
                        }

                        // truyen dl o kenh dl
                        char* scanRes = scanFiles(g_clients[i].wd);
                        if (scanRes == NULL) {
                            scanRes = (char*)calloc(3, sizeof(char)); // cap phat dong de phia duoi free()
                            sprintf(scanRes, "\r\n");
                        }
                        send(g_clients[i].actv.s, scanRes, strlen(scanRes), 0);
                        free(scanRes); scanRes = NULL; // nho la khong free duoc "con tro hang"
                        closesocket(g_clients[i].actv.s);
                        clearSubClient(&g_clients[i].actv);

                        // thong bao truyen xong dl o kenh lenh
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "226 Successfully transferred\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "CWD", 3) == 0) {
                        char dir[1024] = {};
                        while (buffer[strlen(buffer) - 1] == '\r' || buffer[strlen(buffer) - 1] == '\n') {
                            buffer[strlen(buffer) - 1] = '\0';
                        }
                        strcpy(dir, buffer + 4);

                        if (dir[0] == '/') { // neu dir la duong dan tuyet doi
                            sprintf(g_clients[i].wd, "%s", dir);
                        }
                        else if (strcmp(g_clients[i].wd, "/") == 0) {
                            sprintf(g_clients[i].wd + strlen(g_clients[i].wd), "%s", dir);
                        }
                        else {
                            sprintf(g_clients[i].wd + strlen(g_clients[i].wd), "/%s", dir);
                        }

                        memset(buffer, 0, 1024);
                        sprintf(buffer, "%s", "250 CWD successful\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "CDUP", 4) == 0) {
                        if (strcmp(g_clients[i].wd, "/") != 0) {
                            for (int j = strlen(g_clients[i].wd); j >= 0; j--) {
                                if (g_clients[i].wd[j] == '/') {
                                    g_clients[i].wd[j] = '\0';
                                    break;
                                }
                            }
                        }

                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "%s", "250 CDUP successful\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "RETR", 4) == 0) {
                        char* fpath = getFilePath(buffer, g_clients[i].wd);

                        // thong bao mo phien truyen dl o kenh lenh
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "150 Opening data channel\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                        if (connect(g_clients[i].actv.s, (sockaddr*)&g_clients[i].actv.saddr, sizeof(g_clients[i].actv.saddr)) == SOCKET_ERROR) {
                            int errCode = WSAGetLastError();
                            printf("Error occurs: %d\n", errCode);
                            memset(buffer, 0, sizeof(buffer));
                            sprintf(buffer, "425 Cannot open data channel\r\n");
                            send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                            break;
                        }

                        // truyen dl o kenh dl
                        memset(buffer, 0, sizeof(buffer));
                        FILE* f = fopen(fpath, "rb");
                        while (!feof(f)) {
                            int r = fread(buffer, sizeof(char), sizeof(buffer), f);
                            send(g_clients[i].actv.s, buffer, r, 0);
                        }
                        fclose(f);
                        free(fpath); fpath = NULL;
                        closesocket(g_clients[i].actv.s);
                        clearSubClient(&g_clients[i].actv);

                        // thong bao truyen xong dl o kenh lenh
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "226 Successfully transferred\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "STOR", 4) == 0) {
                        char* fpath = getFilePath(buffer, g_clients[i].wd);
                        
                        // thong bao mo phien truyen dl o kenh lenh
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "150 Opening data channel\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                        if (connect(g_clients[i].actv.s, (sockaddr*)&g_clients[i].actv.saddr, sizeof(g_clients[i].actv.saddr)) == SOCKET_ERROR) {
                            int errCode = WSAGetLastError();
                            printf("Error occurs: %d\n", errCode);
                            memset(buffer, 0, sizeof(buffer));
                            sprintf(buffer, "425 Cannot open data channel\r\n");
                            send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                            break;
                        }

                        // truyen dl o kenh dl
                        memset(buffer, 0, 1024);
                        FILE* f = fopen(fpath, "wb");
                        while (true) {
                            int r = recv(g_clients[i].actv.s, buffer, sizeof(buffer), 0);
                            if (r > 0) {
                                fwrite(buffer, sizeof(char), r, f);
                            }
                            else {
                                break;
                            }
                        }
                        fclose(f);
                        free(fpath); fpath = NULL;
                        closesocket(g_clients[i].actv.s);

                        // thong bao truyen xong dl o kenh lenh
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "226 Successfully transferred\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "SIZE", 4) == 0) {
                        char* fpath = getFilePath(buffer, g_clients[i].wd);
                        FILE* f = fopen(fpath, "rb");
                        fseek(f, 0, SEEK_END);
                        long size = ftell(f);
                        fclose(f);
                        free(fpath); fpath = NULL;

                        memset(buffer, 0, 1024);
                        sprintf(buffer, "213 %d\r\n", size);
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "DELE", 4) == 0) {
                        char* fpath = getFilePath(buffer, g_clients[i].wd);
                        remove(fpath); // xoa file
                        free(fpath); fpath = NULL;

                        memset(buffer, 0, 1024);
                        sprintf(buffer, "200 Ok\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "NOOP", 4)) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "200 Ok\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                    else {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "%s", "202 Command not implemented\r\n");
                        send(g_clients[i].cmd.s, buffer, strlen(buffer), 0);
                    }
                }
            }
        }
    }
    return 0;
}

void concat(char** phtml, char* str) {
    // realloc la tao ra vung nho moi roi copy vung nho cu chan len
    int oldlen = (*phtml == NULL) ? 0 : strlen(*phtml);
    int tmplen = strlen(str);
    *phtml = (char*)realloc(*phtml, oldlen + tmplen + 1);
    memset(*phtml + oldlen, 0, tmplen + 1);
    sprintf(*phtml + oldlen, "%s", str);
}

char* scanFiles(char* path) {
    char* res = NULL;
    char oneline[1024];
    // sanitize path
    char findPath[1024] = {};
    if (strcmp(path, "/") == 0)
    {
        sprintf(findPath, "C:/*.*"); // if folder == "/" ==> findPath == C:/*.*
    }
    else
    {
        sprintf(findPath, "C:%s/*.*", path); // if folder == "/tmp" ==> findPath == C:/tmp/*.*
    }

    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(findPath, &findData);
    if (hFind != INVALID_HANDLE_VALUE) {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            sprintf(oneline, "type=dir");
        }
        else {
            sprintf(oneline, "type=file");
        }
        FILETIME fileTime = findData.ftLastWriteTime;
        SYSTEMTIME systemTime;
        FileTimeToSystemTime(&fileTime, &systemTime);
        sprintf(oneline + strlen(oneline), ";modify=%d%02d%02d%02d%02d%02d", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond);
        sprintf(oneline + strlen(oneline), "; %s\r\n", findData.cFileName);
        concat(&res, oneline);
        while (FindNextFileA(hFind, &findData)) {
            memset(oneline, 0, sizeof(oneline));
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                sprintf(oneline, "type=dir");
            }
            else {
                sprintf(oneline, "type=file");
            }
            FILETIME fileTime = findData.ftLastWriteTime;
            SYSTEMTIME systemTime;
            FileTimeToSystemTime(&fileTime, &systemTime);
            sprintf(oneline + strlen(oneline), ";modify=%4d%02d%02d%02d%02d%02d", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond);
            sprintf(oneline + strlen(oneline), "; %s\r\n", findData.cFileName);
            concat(&res, oneline);
        }
    }
    return res;
}

void clearClient(int idx) {
    clearSubClient(&g_clients[idx].cmd);
    clearSubClient(&g_clients[idx].actv);
    clearSubClient(&g_clients[idx].pasv);
    g_events[idx] = INVALID_HANDLE_VALUE;
}

void clearSubClient(SUBCLIENT* lpsubclient) {
    memset(lpsubclient, 0, sizeof(SUBCLIENT));
    lpsubclient->s = INVALID_SOCKET;
}

char* getFilePath(char* buffer, char* wd) {
    char* fname = buffer + 5;
    while (fname[strlen(fname) - 1] == '\r' || fname[strlen(fname) - 1] == '\n') {
        fname[strlen(fname) - 1] = '\0';
    }
    char* fpath = (char*)calloc(1024, sizeof(char));
    sprintf(fpath, "C:%s/%s", wd, fname);
    return fpath;
}