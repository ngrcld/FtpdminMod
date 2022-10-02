//------------------------------------------------------------------------------------
// Minimal FTP server program.  Intended for windows to windows file transfers
//
// This program is totally free.  You may re-liscesne it under GPL or BSD if you wish.
//
// Matthias Wandel June 2004
//
// http://www.sentex.net/~mwandel/tech/ftpdmin.html
//------------------------------------------------------------------------------------
#include <process.h>
#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <direct.h>
#include <time.h>
#include <sys/types.h>
#include <winsock2.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "paths.h"

#define FTPDMIN_VER "0.96-mod"
#define FTPDMIN_DATE "Jun 7 2004"

struct in_addr OurAddr;
char OurAddrStr[20];
int addrlen = sizeof(struct sockaddr_in);
BOOL GetOnly = FALSE;
char PortsUsed[256];

// Address of the data port used to transfer files
// Making this global means we can only handle ONE connection.
typedef struct {
    struct sockaddr_in xfer_addr;
    BOOL PassiveMode;
    int PassiveSocket;
    char XferBuffer[262114];
    int CommandSocket;
    int XferPort;
}Inst_t;

//------------------------------------------------------------------------------------
// Create a TCP/IP port, return the socket for the connection
//------------------------------------------------------------------------------------
static int CreateTcpipSocket(int *Port)
{
    struct sockaddr_in socket_addr;
    int sock;
    int socket_addr_size = sizeof(socket_addr);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0) {
        perror("CreateTcpipSocket():socket failed");
        exit(-1);
    }

    // Name a socket using wildcards
    memset(&socket_addr, 0, socket_addr_size);
    socket_addr.sin_family = AF_INET;
    socket_addr.sin_addr.s_addr = INADDR_ANY;
    socket_addr.sin_port = htons((short)*Port);

    // Create socket address
    if(bind(sock,(const struct sockaddr *)&socket_addr, socket_addr_size) < 0){
        printf("Could not open socket port %d\n", *Port);
        return -1;
    }

    // Get the assigned port number
    if (getsockname(sock,(struct sockaddr *)&socket_addr,&socket_addr_size) < 0){
        perror("Error: getsockname() failed");
        exit(-1);
    }

    // Convert network byte order to host byte order
    *Port = ntohs(socket_addr.sin_port);

    return sock;
}

//------------------------------------------------------------------------------------
// Initiate a connection to a tcp/ip port in the given address 
// Return a socket to the connected port.
// Return -1 if an error occurs
//------------------------------------------------------------------------------------
static int ConnectTcpip(struct sockaddr_in *addr, int addrlen) 
{
    int sock;
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock<0){
        perror("socket() failed");
        return -1;
    }
    if(connect(sock,(struct sockaddr *)addr, addrlen) < 0){
        perror("connect() failed");
        return -1;
    }
    return sock;
}

// FTP Command tokens
typedef enum {
    USER, PASS, CWD,  PORT, 
    QUIT, PASV, ABOR, DELE,
    RMD,  XRMD, MKD,  XMKD,
    PWD,  XPWD, LIST, NLST, 
    SYST, TYPE, MODE, RETR, 
    STOR, REST, RNFR, RNTO,
    STAT, NOOP, MDTM, xSIZE, 
    UNKNOWN_COMMAND
}CmdTypes;

//------------------------------------------------------------------------------------
// Table of FTP commands
//------------------------------------------------------------------------------------
typedef struct {
    char *command;
    CmdTypes CmdNum;
}Lookup_t;

static const Lookup_t CommandLookup[] = {
    "USER", USER, "PASS", PASS, "CWD",  CWD,  "PORT", PORT, 
    "QUIT", QUIT, "PASV", PASV, "ABOR", ABOR, "DELE", DELE,
    "RMD",  RMD,  "XRMD", XRMD, "MKD",  MKD,  "XMKD", XMKD,
    "PWD",  PWD,  "XPWD", XPWD, "LIST", LIST, "NLST", NLST,  
    "SYST", SYST, "TYPE", TYPE, "MODE", MODE, "RETR", RETR,
    "STOR", STOR, "REST", REST, "RNFR", RNFR, "RNTO", RNTO,
    "STAT", STAT, "NOOP", NOOP, "MDTM", MDTM, "SIZE", xSIZE,
};

//------------------------------------------------------------------------------------
// Get a ftp command from the command stream, convert to code and an argument string
//------------------------------------------------------------------------------------
static CmdTypes GetCommand(Inst_t *Conn, char *CmdArg) 
{
    char InputString[500+1];
    int  CmdLen;
    char Command[6];
    int  a,b;

    // Read in command string
    CmdLen = 0;
    memset(InputString, 0, sizeof(InputString));
    for (;;){
        int n;
        n = recv(Conn->CommandSocket, InputString+CmdLen, sizeof(InputString)-CmdLen,0);
        CmdLen += n;
        if (strstr(InputString, "\r")) break;
        if (n <= 0) return -1;
    }

    memset(Command, 0, sizeof(Command));
    for(a=0;a<5;a++) {
        if (!isalpha(InputString[a])) break;
        Command[a] = toupper(InputString[a]);
    }

    b = 0;
    if (InputString[a++] == ' '){
        for (b=0;b<500-1;b++){
            if (InputString[a+b] < 32) break;
            CmdArg[b] = InputString[a+b];
        }
    }
    CmdArg[b] = 0;

    printf("%s %s\n", Command, CmdArg);

    // Search through the list of known commands
    for(a=0;a<sizeof(CommandLookup)/sizeof(Lookup_t);a++){
        if(strncmp(Command, CommandLookup[a].command,4)==0){
            return CommandLookup[a].CmdNum;
        }
    }
    fprintf(stderr,"Unknown command '%s'\n",Command);
    return UNKNOWN_COMMAND;
}

//------------------------------------------------------------------------------------
// Wrapper function for more convenient sending of replies.
//------------------------------------------------------------------------------------
static void SendReply(Inst_t *Conn, char *Reply) 
{
    char ReplyStr[MAX_PATH+20];
    printf("    %s\n",Reply);
    sprintf(ReplyStr, "%s\r\n", Reply);
    send(Conn->CommandSocket, ReplyStr, strlen(ReplyStr),0);
}

//------------------------------------------------------------------------------------
// Report general file or I/O error.
//------------------------------------------------------------------------------------
static void Send550Error(Inst_t *Conn)
{
    char ErrString[200];
    sprintf(ErrString, "550 %s",sys_errlist[errno]);
    SendReply(Conn, ErrString);
}

//------------------------------------------------------------------------------------
// Handle the NLST command (directory)
//------------------------------------------------------------------------------------
static void Cmd_NLST(Inst_t *Conn, char *filename, BOOL Long, BOOL UseCtrlConn)
{
    int xfer_sock;
    char repbuf[500];
    int a;
    BOOL ListAll = FALSE;

    if (UseCtrlConn){
        xfer_sock = Conn->CommandSocket;
    }else{
        SendReply(Conn, "150 Opening connection");

        if (Conn->PassiveMode){
            xfer_sock = accept(Conn->PassiveSocket, NULL, NULL);
        }else{
            // Create TCP/IP connection for data link
            xfer_sock = ConnectTcpip(&Conn->xfer_addr,addrlen);
            if(xfer_sock < 0) {
                Send550Error(Conn);
                return;
            }
        }
    }


    {
        struct _finddata_t finddata;
        intptr_t find_handle; // https://stackoverflow.com/questions/41844603/access-violation-continuously-thrown-by-findnext-c

        if (*filename == 0) filename = "*";
        if (*filename == '-'){ 
            for (a=1;filename[a];a++){
                if (filename[a] == 'a' || filename[a] == 'A') ListAll = TRUE;
                if (filename[a] == 'l') Long = TRUE;
            }
            filename = "*";
        }

        find_handle = _findfirst(filename, &finddata);

        for (;;){
            char timestr[20];
            if (find_handle == -1) break;

            if (!strcmp(finddata.name, ".")) goto next_file;
            if (!strcmp(finddata.name, "..")) goto next_file;

            if (Long){
                struct tm tm;
                char DirAttr;
                char WriteAttr;

                // Call mktime to get weekday and such filled in.
                tm = *localtime(&finddata.time_write);
                strftime(timestr, 20, "%b %d  %Y", &tm);
                
                if (finddata.attrib & (_A_HIDDEN | _A_SYSTEM)){
                    if (!ListAll) goto next_file;
                }

                DirAttr = finddata.attrib & _A_SUBDIR ? 'd' : '-';
                WriteAttr = finddata.attrib & _A_RDONLY ? '-' : 'w';

                sprintf(repbuf,"%cr%c-r%c-r%c-   1 root  root    %7u %s %s\r\n", 
                        DirAttr, WriteAttr, WriteAttr, WriteAttr,
                        (unsigned)finddata.size,
                        timestr,
                        finddata.name);
            }else{
                sprintf(repbuf, "%s\r\n",finddata.name);
            }
            send(xfer_sock, repbuf, strlen(repbuf),0);

            next_file:
            if (_findnext(find_handle, &finddata) != 0) break;
        }
        _findclose(find_handle);
    }

    if (!UseCtrlConn){
        closesocket(xfer_sock);
        SendReply(Conn, "226 Transfer Complete");
    }
}

//------------------------------------------------------------------------------------
// Handle the RECV command
//------------------------------------------------------------------------------------
static void Cmd_RETR(Inst_t *Conn, char *filename) 
{
    int file;
    int xfer_sock;
    int size;

    // Check to see if the file can be opened for reading
    if((file = open(filename, _O_RDONLY | _O_BINARY)) < 0) {
        Send550Error(Conn);
        return;
    }

    // File opened succesfully, so make the connection
    SendReply(Conn, "150 Opening BINARY mode data connection");

    if (Conn->PassiveMode){
        xfer_sock = accept(Conn->PassiveSocket, NULL, NULL);
    }else{
        // Create TCP/IP connection for data link
        xfer_sock = ConnectTcpip(&Conn->xfer_addr,addrlen);
        if(xfer_sock < 0) {
            Send550Error(Conn);
            return;
        }
    }

    // Transfer file
    for(size=1;size > 0;){
        size = read(file, Conn->XferBuffer, sizeof(Conn->XferBuffer));

        if(size < 0) {
            break;
        }

        // Write buffer to socket.
        if(send(xfer_sock, Conn->XferBuffer, size, 0) < 0){
            perror("send failed");
            SendReply(Conn, "426 Broken pipe") ;
            size = -1;
        }
    }
    

    if(size < 0){
        Send550Error(Conn);
    }else{
        SendReply(Conn, "226 Transfer Complete");
    }

    closesocket(xfer_sock);
    close(file);
}


//------------------------------------------------------------------------------------
// Handle the RECV command
//------------------------------------------------------------------------------------
static void Cmd_STOR(Inst_t *Conn, char *filename) 
{
    FILE *file;
    int xfer_sock;
    int size;

    // Check to see if the file can be opened for reading
    file = fopen(filename, "wb");
    if(file == NULL){
        Send550Error(Conn);
        return;
    }

    // File opened succesfully, so make the connection
    SendReply(Conn, "150 Opening BINARY mode data connection");

    if (Conn->PassiveMode){
        xfer_sock = accept(Conn->PassiveSocket, NULL, NULL);
    }else{
        // Create TCP/IP connection for data link
        xfer_sock = ConnectTcpip(&Conn->xfer_addr,addrlen);
        if(xfer_sock < 0) {
            Send550Error(Conn);
            return;
        }
    }

    // Transfer file
    for(size=1;size >= 0;){
        // Get from socket.
        size = recv(xfer_sock, Conn->XferBuffer, sizeof(Conn->XferBuffer), 0);
        if(size < 0) {
            perror("read failed");
        }

        if (size <= 0) break;

        // Write to file
        if (fwrite(Conn->XferBuffer, size, 1, file) != 1){
            size=-1;
            break;
        }
    }

    if(size < 0){
        Send550Error(Conn);
    }else{
        SendReply(Conn, "226 Transfer Complete");
    }

    closesocket(xfer_sock);

    // For some reason, the file ends up readonly - should fix that.
    fclose(file);
}

//------------------------------------------------------------------------------------
// Handle MDTM and SIZE command
//------------------------------------------------------------------------------------
static void StatCmds(Inst_t *Conn, char *filename, CmdTypes Cmd)
{
    struct stat FileStat;
    struct tm tm;
    char RepBuf[50];

    if (stat(filename, &FileStat)){
        Send550Error(Conn);
        return;
    }

    if (FileStat.st_mode & _S_IFDIR){
        // Its a directory.
        SendReply(Conn, "550 not a plain file.");
        return;
    }


    switch(Cmd){
        case MDTM:
            tm = *localtime(&FileStat.st_mtime);

            sprintf(RepBuf, "213 %04d%02d%02d%02d%02d%02d",
                    tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, 
                    tm.tm_hour, tm.tm_min, tm.tm_sec);
            break;

        case xSIZE:
            sprintf(RepBuf, "213 %u",FileStat.st_size);
            break;

        default:
            // Internal screwup!
            strcpy(RepBuf, "550");
            break;
    }
    SendReply(Conn, RepBuf);
}

//------------------------------------------------------------------------------------
// Main loop - handle the FTP commands that are implemented.
//------------------------------------------------------------------------------------
static void ProcessCommands(Inst_t *Conn)
{
    // Data buffer for reading FTP command codes
    char buf[MAX_PATH+10];
    char repbuf[MAX_PATH+10];
    char *NewPath;
    int a;

    MySetDir("\\");

    Conn->PassiveSocket = CreateTcpipSocket(&Conn->XferPort);
    if (Conn->PassiveSocket < 0){
        goto EndConnection;
    }

    // Listen for passive mode connections.
    if(listen(Conn->PassiveSocket, 1) == -1){
        perror("passive socket listen failed");
        goto EndConnection;
    }

    // Get the default data address for the transfer
    if(getsockname(Conn->CommandSocket,(struct sockaddr *)&Conn->xfer_addr,&addrlen) < 0){
        perror("getsockname() failed");
        SendReply(Conn, "500 getsockname failed");
        goto EndConnection;
    }

    // Indicate ready to accept commands
    SendReply(Conn, "220 ftpdmin ready");

    for(;;){
        // Get FTP command from input stream
        CmdTypes FtpCommand;

        FtpCommand = GetCommand(Conn, buf);

        switch(FtpCommand){
            case USER:
                SendReply(Conn, "331 pretend login accepted");
                break;

            case PASS:
                SendReply(Conn, "230 fake user logged in");
                break;

            case SYST:
                SendReply(Conn, "215 WIN32 ftpdmin v. "FTPDMIN_VER);
                break;

            case PASV:
                sprintf(repbuf, "227 Entering Passive Mode (%s,%d,%d)",
                    OurAddrStr, Conn->XferPort >>8, Conn->XferPort & 0xff);
                for (a=0;a<50;a++){
                    if (repbuf[a] == 0) break;
                    if (repbuf[a] == '.') repbuf[a] = ',';
                }
                SendReply(Conn, repbuf);
                Conn->PassiveMode = TRUE;
                break;

            case XPWD:
            case PWD: // Print working directory
                sprintf(repbuf,"257 \"%s\"", MyGetDir());
                SendReply(Conn, repbuf);
                break;

            case NLST: // Request directory, names only.
                NewPath = TranslatePath(buf);
                if (NewPath == NULL){
                    SendReply(Conn, "550 Path permission error");
                    break;
                }
                Cmd_NLST(Conn, NewPath, FALSE, FALSE);
                break;

            case LIST: // Request directory, long version.
                NewPath = TranslatePath(buf);
                if (NewPath == NULL){
                    SendReply(Conn, "550 Path permission error");
                }
                Cmd_NLST(Conn, NewPath, TRUE, FALSE);
                break;

            case STAT: // Just like LIST, but use control connection.
                NewPath = TranslatePath(buf);
                if (NewPath == NULL){
                    SendReply(Conn, "550 Path permission error");
                    break;
                }
                Cmd_NLST(Conn, NewPath, TRUE, TRUE);
                break;

            case DELE:
                NewPath = TranslatePath(buf);
                if (NewPath == NULL){
                    SendReply(Conn, "550 Path permission error");
                    break;
                }
                if (unlink(NewPath)){
                    Send550Error(Conn);
                }else{
                    SendReply(Conn, "250 DELE command successful.");
                }
                break;

            case RMD:
            case MKD:
            case XMKD:
            case XRMD:
                if (GetOnly){
                    SendReply(Conn, "550 Permission denied");
                    break;
                }
                NewPath = TranslatePath(buf);
                if (NewPath == NULL){
                    SendReply(Conn, "550 Path permission error");
                    break;
                }
                if (FtpCommand == MKD || FtpCommand == XMKD){
                    if (mkdir(NewPath)){
                        Send550Error(Conn);
                    }else{
                        SendReply(Conn, "257 Directory created");
                    }
                }else{
                    if (rmdir(NewPath)){
                        Send550Error(Conn);
                    }else{
                        SendReply(Conn, "250 RMD command successful");
                    }
                }
                break;

            case RNFR:
                NewPath = TranslatePath(buf);
                if (NewPath){
                    strcpy(repbuf, NewPath);
                    SendReply(Conn, "350 File Exists");
                }else{
                    SendReply(Conn, "550 Path permission error");
                }
                break;
                
            case RNTO:
                // Must be immediately preceeded by RNFR!
                if (GetOnly){
                    SendReply(Conn, "550 Permission denied");
                    break;
                }
                NewPath = TranslatePath(buf);
                if (rename(repbuf, NewPath)){
                    Send550Error(Conn);
                }else{
                    SendReply(Conn, "250 RNTO command successful");
                }
                break;

            case ABOR:
                SendReply(Conn, "226 Aborted");
                break;

            case xSIZE:
            case MDTM:
                NewPath = TranslatePath(buf);
                if (NewPath == NULL){
                    SendReply(Conn, "550 Path permission error");
                    break;
                }
                StatCmds(Conn, NewPath, FtpCommand);
                break;

            case CWD: // Change working directory
                if (!MySetDir(buf)){
                    SendReply(Conn, "550 Could not change directory");
                }else{
                    SendReply(Conn, "250 CWD command successful");
                }
                break;
 
            case TYPE: // Accept file TYPE commands, but ignore.
                SendReply(Conn, "200 Type set to I");
                break;

            case NOOP:
                SendReply(Conn, "200 OK");
                break;

            case PORT: // Set the TCP/IP addres for trasnfers.
                {
                    int h1,h2,h3,h4,p1,p2;
                    char *a, *p;
                    sscanf(buf,"%d,%d,%d,%d,%d,%d",&h1,&h2,&h3,&h4,&p1,&p2);
                    a = (char *) &Conn->xfer_addr.sin_addr;
                    p = (char *) &Conn->xfer_addr.sin_port;
                    a[0] = h1; a[1] = h2; a[2] = h3; a[3] = h4;
                    p[0] = p1; p[1] = p2;
                }
                SendReply(Conn, "200 PORT command successful");
                break;

            case RETR: // Retrieve File and send it
                NewPath = TranslatePath(buf);
                if (NewPath == NULL){
                    SendReply(Conn, "550 Path permission error");
                    break;
                }
                Cmd_RETR(Conn, NewPath);
                break;

            case STOR: // Store the file.
                if (GetOnly){
                    SendReply(Conn, "553 Permission denied");
                    break;
                }
                NewPath = TranslatePath(buf);
                if (NewPath == NULL){
                    SendReply(Conn, "550 Path permission error");
                    break;
                }
                Cmd_STOR(Conn, NewPath);
                break;

            case UNKNOWN_COMMAND:
                SendReply(Conn, "500 command not recognized");
                break;

            case QUIT: 
                SendReply(Conn, "221 goodbye");
                goto EndConnection;


            case -1:
                goto EndConnection;

            
            default: // Any command not implemented, return not recognized response.
                SendReply(Conn, "500 command not implemented");
                break;
        }
    }
EndConnection:
    printf("Closing control connection\n");
    closesocket(Conn->PassiveSocket);
    closesocket(Conn->CommandSocket);
    PortsUsed[Conn->XferPort & 255] = 0;
    free(Conn);
}

//--------------------------------------------------------------------------
static void Usage (void)
{
    printf("\nftpdmin is a minimal FTP server program.  ftpdmin is run directly from\n"
           "a windows command prompt.  ftpdmin is used as a quck, temporary ad-hock\n"
           "FTP server for transferring files to and from windows PCs via FTP.\n"
           "ftpdmin is single threaded, single session only, and provides no\n"
           "authentication or security of any sort.\n\n");

    printf("Usage: ftpdmin [options] [root]\n");
    printf("[options] are:\n"
           "  -p <port>  Specify control port number (default 21)\n"
           "  -tp <port> Specify port or port range to use for passive mode (defaults to\n"
           "             abitrary port the OS assigns).  Useful behind firewalls.\n"
           "  -ha        Specifies thru which address ftpdmin is to be accessed\n"
           "             (for NAT firewalled access.  Use only if you know how to set up\n"
           "             port forwarding on your firewall)\n"
           "  -g         Get only (disallows uploads)\n"
           "  root       Directory to consider root for FTP access\n");

    printf("\nMatthias Wandel, "FTPDMIN_DATE"\n"
           "http://www.sentex.net/~mwandel/tech/ftpdmin.html\n");

    exit(-1);
}

//------------------------------------------------------------------------------------
// Main for ftpdmin program
//------------------------------------------------------------------------------------
int main (int argc, char **argv)
{
    int socket, ControlPort;
    char hostname[256];
    struct hostent *hostinfo;
    WSADATA wsaData;
    int argn;
    char *UseRootDir = NULL;
    int XferPort, XferPortStart, XferPortRange; 
    char *SpecifiedHostAddr = NULL;

    ControlPort = 21;
    XferPortStart = XferPortRange = XferPort = 0;
   

    printf("ftpdmin v. "FTPDMIN_VER"  "FTPDMIN_DATE"\n");

    for (argn=1;argn<argc;argn++){
        char *arg;
        arg = argv[argn];
        if (argv[argn][0] == '-'){
            if (!strcmp(arg,"-p")){
                if (argn+2 > argc || sscanf(argv[argn+1],"%d", &ControlPort) != 1){
                    printf("Error!  -p must be followed by port number\n");
                    Usage();
                }
                argn += 1;
            }else if (!strcmp(arg,"-tp")){
                if (argn+2 > argc || sscanf(argv[argn+1],"%d-%d", &XferPortStart, &XferPortRange) < 1){
                    printf("Error!  -tp must be followed by port number\n");
                    Usage();
                }
                argn += 1;
            }else if (!strcmp(arg,"-ha")){
                int a;
                if (argn+2 > argc || sscanf(argv[argn+1],"%d.%d.%d.%d", &a,&a,&a,&a) != 4){
                    printf("Error!  -ha must be followed by an ip address\n");
                    Usage();
                }
                SpecifiedHostAddr = argv[argn+1];
                argn += 1;
            }else if (!strcmp(arg,"-g")){
                GetOnly = TRUE;
            }else if (!strcmp(arg,"-h")){
                Usage();
            }else{
                printf("Error!  '%s' not understood\n",argv[argn]);
                Usage();
            }
       
        }else{
            if (UseRootDir){
                printf("Error!  Can only specify one directory\n");
                Usage();
            }
            UseRootDir = argv[argn];
        }
    }

    if (UseRootDir != NULL){
        if (!SetRootDir(UseRootDir)){
            printf("Error!  Unable to set root directory '%s'\n",UseRootDir);
            Usage();
        }
    }else{
        SetRootDir(".");
    }

    if (WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 ){
        // Tell the user that we could not find a usable WinSock DLL.
        perror("Winsock 2.0 required for ftpdmin");
        return -1;
    }

    gethostname(hostname,sizeof(hostname));
    hostinfo = gethostbyname(hostname);
    memcpy(&OurAddr, hostinfo->h_addr_list[0], sizeof(struct in_addr));

    strcpy(OurAddrStr, inet_ntoa(OurAddr));
    if (SpecifiedHostAddr) strncpy(OurAddrStr, SpecifiedHostAddr, 19);
  
    // Create a TCP/IP port to accept connections for FTP services
    socket = CreateTcpipSocket(&ControlPort);
    if (socket < 0){
        if (ControlPort == 21){
            // Its not uncommon for port 21 to fail.  May already be
            // in use, or we may not be running with administrator priviledges
            ControlPort = 2100;
            socket = CreateTcpipSocket(&ControlPort);
        }
        if (socket < 0){
            exit(-1);
        }
    }

    // Listen for connections on socket
    if(listen(socket, 1) == -1){
        perror("listen failed");
        return -1;
    }

    XferPort = XferPortStart;

    printf("ftpdmin ready to accept connections on ftp://%s",OurAddrStr);
    if (ControlPort != 21) printf(":%d",ControlPort);
    printf("\n");

    for(;;){
        int CommandSocket;
        Inst_t *Conn;

        // Accept connections on socket
        CommandSocket = accept(socket, NULL, NULL);
        if(CommandSocket < 0){
            perror("accept failed");
            return -1;
        }

        Conn = (Inst_t *)malloc(sizeof(Inst_t));
        if (Conn == 0){
            printf("Out of memory\n");
            exit(-1);
        }

        Conn->CommandSocket = CommandSocket;
        Conn->XferPort = XferPort;
        PortsUsed[XferPort] = 1;

        ProcessCommands(Conn);

        // Cycle through port numbers.
        XferPort = XferPortStart;
        for(;XferPort < XferPortRange;XferPort++){
            // Find an unused port numbeer 
            // This code only relevant if a port range was specified.
            if (!PortsUsed[XferPort & 255]) break;
        }
    }
}
