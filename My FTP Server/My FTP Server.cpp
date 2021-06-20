#include <stdio.h>
#include <WinSock2.h>
#include <Windows.h>

// DEFINE

#define MAX_CLIENT 1024
#define N 64

// PROTOTYPE
DWORD WINAPI CommandThread(LPVOID params);
void concat(char** phtml, char* str);
char* scanFiles(char* path);

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

// GLOBAL VARIABLES
CLIENT g_clients[MAX_CLIENT] = {};
WSAEVENT g_events[MAX_CLIENT] = {};
int g_count = 0;
CRITICAL_SECTION lock;

int main() {
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
    InitializeCriticalSection(&lock);

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
                    break;
                }
            }
            if (!hasAnEmptySlot) {
                g_clients[g_count].cmd.s = cs;
                g_clients[g_count].cmd.saddr = saddr;
                g_events[g_count] = ce;
                g_count++;
            }
            LeaveCriticalSection(&lock);

            if ((g_count % N) == 1) {
                CreateThread(NULL, 0, CommandThread, (LPVOID)(g_count - 1), 0, NULL); // moi luong chi xl toi da N clients
            }

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
        // don mang g_clients, g_events
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

        if (totalClients == 0) {
            return 0; // Ket thuc luong
        }

        int idx = WSAWaitForMultipleEvents(totalClients, g_events + startIdx, FALSE, INFINITE, FALSE); // chi can co 1 invalid handle trong tap event la an loi ngay
        printf("Error occur: %d", WSAGetLastError());
        idx -= WSA_WAIT_EVENT_0;
        for (int i = idx; i <= endIdx; i++) {
            CLIENT client = g_clients[i];
            WSANETWORKEVENTS networkEvent;
            WSAEnumNetworkEvents(client.cmd.s, g_events[i], &networkEvent);
            WSAResetEvent(g_events[i]);
            if (networkEvent.lNetworkEvents & FD_CLOSE) {
                if (networkEvent.iErrorCode[FD_CLOSE_BIT] == 0) {
                    // Giai phong tai nguyen cho client
                    closesocket(client.cmd.s);
                    client.cmd.s = INVALID_SOCKET;
                    memset(&client.cmd.saddr, 0, sizeof(client.cmd.saddr));
                    WSACloseEvent(g_events[i]);
                }
            }
            else if (networkEvent.lNetworkEvents & FD_READ) {
                if (networkEvent.iErrorCode[FD_READ_BIT] == 0) {
                    char buffer[1024] = {};
                    recv(client.cmd.s, buffer, sizeof(buffer), 0);
                    printf("%s", buffer);

                    if (strncmp(buffer, "USER", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "331 Password required\r\n"); // user nao cung duoc
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "PASS", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "230 Logged on\r\n"); // pass nao cung duoc
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                        memset(client.wd, 0, sizeof(client.wd));
                        sprintf(client.wd, "%s", "/");
                    }
                    else if (strncmp(buffer, "SYST", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "215 UNIX\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "FEAT", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "211-Features:\n MDTM\n REST STREAM\n SIZE\n MLST type*;size*;modify*;\n MLSD\n UTF8\n CLNT\n MFMT\n EPSV\n EPRT\n211 End\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "CLNT", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "200 Don't care\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "OPTS", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "202 UTF8 mode always on\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "PWD", 3) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "217 \"%s\" is current working directory\r\n", client.wd);
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "TYPE", 4) == 0) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "200 Ok\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
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

                        client.isActv = TRUE;
                        SOCKET ds = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
                        if (ds == INVALID_SOCKET) {
                            int errCode = WSAGetLastError();
                            printf("Error occurs: %d\n", errCode);
                            memset(buffer, 0, sizeof(buffer));
                            sprintf(buffer, "400 Port command unsuccessful\r\n");
                            break;
                        }
                        client.actv.s = ds;
                        client.actv.saddr.sin_family = AF_INET;
                        client.actv.saddr.sin_addr.S_un.S_un_b.s_b1 = ip[0];
                        client.actv.saddr.sin_addr.S_un.S_un_b.s_b2 = ip[1];
                        client.actv.saddr.sin_addr.S_un.S_un_b.s_b3 = ip[2];
                        client.actv.saddr.sin_addr.S_un.S_un_b.s_b4 = ip[3];
                        client.actv.saddr.sin_port = htons(MAKEWORD(port[1], port[0]));

                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "200 Port command successful\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "MLSD", 4) == 0) {
                        // thong bao mo phien truyen dl o kenh lenh
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "150 Opening data channel\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                        if (connect(client.actv.s, (sockaddr*)&client.actv.saddr, sizeof(client.actv.saddr)) == SOCKET_ERROR) {
                            int errCode = WSAGetLastError();
                            printf("Error occurs: %d\n", errCode);
                            memset(buffer, 0, sizeof(buffer));
                            sprintf(buffer, "425 Cannot open data channel\r\n");
                            send(client.cmd.s, buffer, strlen(buffer), 0);
                            break;
                        }

                        // truyen dl o kenh dl
                        char* scanRes = scanFiles(client.wd);
                        if (scanRes == NULL) {
                            scanRes = (char*)calloc(3, sizeof(char)); // cap phat dong de phia duoi free()
                            sprintf(scanRes, "\r\n");
                        }
                        send(client.actv.s, scanRes, strlen(scanRes), 0);
                        free(scanRes); scanRes = NULL; // nho la khong free duoc "con tro hang"
                        closesocket(client.actv.s);
                        client.actv.s = INVALID_SOCKET;
                        memset(&client.actv.saddr, 0, sizeof(client.actv.saddr));

                        // thong bao truyen xong dl o kenh lenh
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "226 Successfully transferred\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "CWD", 3) == 0) {
                        char dir[1024] = {};
                        while (buffer[strlen(buffer) - 1] == '\r' || buffer[strlen(buffer) - 1] == '\n') {
                            buffer[strlen(buffer) - 1] = '\0';
                        }
                        strcpy(dir, buffer + 4);

                        if (strcmp(client.wd, "/") == 0) {
                            sprintf(client.wd + strlen(client.wd), "%s", dir);
                        }
                        else {
                            sprintf(client.wd + strlen(client.wd), "/%s", dir);
                        }

                        memset(buffer, 0, 1024);
                        sprintf(buffer, "%s", "250 CWD successful\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "CDUP", 4) == 0) {
                        if (strcmp(client.wd, "/") != 0) {
                            for (int j = strlen(client.wd); j >= 0; j--) {
                                if (client.wd[j] == '/') {
                                    client.wd[j] = '\0';
                                    break;
                                }
                            }
                        }

                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "%s", "250 CDUP successful\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else if (strncmp(buffer, "NOOP", 4)) {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "200 Ok");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
                    }
                    else {
                        memset(buffer, 0, sizeof(buffer));
                        sprintf(buffer, "%s", "202 Command not implemented\r\n");
                        send(client.cmd.s, buffer, strlen(buffer), 0);
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